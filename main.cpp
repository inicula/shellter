#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <optional>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <termios.h>
#include <fmt/core.h>
#include <fmt/color.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/regex.hpp>

namespace fs = std::filesystem;

/* config and utils */
#include "config.h"
#include "util.h"

/* global variables */
static bool running = true;
static std::array<char, 256> current_user = {};
static std::array<char, 256> current_host = {};
static fs::path home;
static fs::path old_path;
static bool old_path_set = false;
static std::vector<std::string> line_history;
static std::unordered_map<std::string, std::string> environment_vars;

/* builtin commands */
#include "builtins.h"

/* class declarations */
class BasicCommand;
class LogicSequence;
class PipeSequence;

/* template function declarations */
template<bool>
static void process_line(const std::string_view);

/* function declarations */
static std::pair<bool, boost::smatch> check_special_seq_err(const std::string&);
static void readline_free_history();
static std::optional<std::string> readline_to_string(const char* const);
static bool ends_in_special_seq(const std::string_view);
static void update_current_user();
static std::string get_prompt();
static void loop();
static void interrupt_child(const int);

/* class definitions */
class BasicCommand
{
public:
        static void process(const std::string_view);
        static int process_and_wait(const std::string_view);

private:
        template<bool>
        static auto helper_exec(const std::string_view);
};

class LogicSequence
{
public:
        static void process(const std::string_view);
        static int process_and_wait(const std::string_view);

private:
        static int helper_exec(const std::string_view);
};

class PipeSequence
{
public:
        static void process(const std::vector<std::string_view>&)
        {
        }

        static int process_and_wait(const std::vector<std::string_view>&);
};

/* static member function definitions */
template<bool WAIT>
auto BasicCommand::helper_exec(const std::string_view line_sv)
{
        std::string str(line_sv);
        boost::trim(str);

        /* split words, compress spaces */
        std::vector<std::string> args;
        boost::split(args, str, boost::is_any_of(" "), boost::token_compress_on);

        /* replace environment values */
        for(std::size_t i = 0; i < args.size(); ++i)
        {
                if(i == 1 && args[0] == std::string_view("addenv"))
                {
                        continue;
                }

                const auto it = environment_vars.find(args[i]);
                if(it != environment_vars.end())
                {
                        args[i] = it->second;
                }
        }

        /* check for builtin command */
        const auto builtin_it = builtin_funcs.find(args.front());
        if(builtin_it != builtin_funcs.cend())
        {
                if constexpr(WAIT)
                {
                        return builtin_it->second(args);
                }
                else
                {
                        builtin_it->second(args);
                        return;
                }
        }

        const pid_t child_pid = fork();
        if(child_pid == 0)
        {
                /* construct array of char pointers (null terminated) */
                std::vector<char*> arg_ptrs;
                for(auto& str : args)
                {
                        arg_ptrs.push_back(str.data());
                }
                arg_ptrs.push_back(nullptr);

                execvp(arg_ptrs[0], arg_ptrs.data());
                print_err_fmt("shellter: error calling execvp(): {}: {}\n", arg_ptrs[0],
                              strerror(errno));
                exit(1);
        }

        if constexpr(WAIT)
        {
                int status;
                do
                {
                        waitpid(child_pid, &status, WUNTRACED);
                } while(!WIFEXITED(status) && !WIFSIGNALED(status));

                /* restore stdin if previous command made it invisible
                 * and didn't restore it before returning / being closed */
                struct termios term_status;
                tcgetattr(0, &term_status);

                if(!(term_status.c_lflag & ECHO))
                {
                        term_status.c_lflag |= ECHO;
                        tcsetattr(0, TCSANOW, &term_status);
                }

                return status;
        }
}

void BasicCommand::process(const std::string_view line_sv)
{
        helper_exec<false>(line_sv);
}

int BasicCommand::process_and_wait(const std::string_view line_sv)
{
        return helper_exec<true>(line_sv);
}

void LogicSequence::process(const std::string_view whole_line)
{
        helper_exec(whole_line);
}

int LogicSequence::process_and_wait(const std::string_view whole_line)
{
        return helper_exec(whole_line);
}

int LogicSequence::helper_exec(const std::string_view sub_line)
{
        const auto or_pos = sub_line.find("||");
        const auto and_pos = sub_line.find("&&");

        if(or_pos != sub_line.npos)
        {
                const std::string_view left_line{sub_line.cbegin(), sub_line.cbegin() + or_pos};
                const std::string_view right_line{sub_line.cbegin() + or_pos + 2, sub_line.cend()};

                const int left_ret = helper_exec(left_line);
                if(left_ret != EXIT_SUCCESS)
                {
                        return helper_exec(right_line);
                }

                return left_ret;

        }
        else if(and_pos != sub_line.npos)
        {
                const std::string_view left_line{sub_line.cbegin(), sub_line.cbegin() + and_pos};
                const std::string_view right_line{sub_line.cbegin() + and_pos + 2, sub_line.cend()};

                const int left_ret = helper_exec(left_line);
                if(left_ret == EXIT_SUCCESS)
                {
                        return helper_exec(right_line);
                }

                return left_ret;

        }
        else
        {
                return BasicCommand::process_and_wait(sub_line);
        }
}

int PipeSequence::process_and_wait(const std::vector<std::string_view>& command_components)
{
        const int fd_old_in = dup(0);
        const int fd_old_out = dup(1);

        int fd_command_input = dup(fd_old_in);
        int fd_command_output;

        const std::size_t len = command_components.size();
        for(std::size_t i = 0; i < len; ++i)
        {
                dup2(fd_command_input, 0);
                close(fd_command_input);

                if(i == len - 1)
                {
                        /* for last command, restore original out fd */
                        fd_command_output = dup(fd_old_out);
                }
                else
                {
                        /* set up pipe */
                        int fd_pipe[2];
                        pipe(fd_pipe);

                        fd_command_output = fd_pipe[1];
                        fd_command_input = fd_pipe[0];
                }

                dup2(fd_command_output, 1);
                close(fd_command_output);

                process_line<false>(command_components[i]);
        }

        dup2(fd_old_in, 0);
        dup2(fd_old_out, 1);
        close(fd_old_in);
        close(fd_old_out);

        return EXIT_SUCCESS;
}

/* function definitions */
std::pair<bool, boost::smatch> check_special_seq_err(const std::string& line)
{
        boost::smatch match_array;

        const bool found = boost::regex_search(
            line, match_array, boost::regex("[|]{3,}|[&]{3,}|(&[|]|[|]&)[&\\|]*"));

        return {found, std::move(match_array)};
}

void readline_free_history()
{
        HISTORY_STATE* myhist = history_get_history_state();
        HIST_ENTRY** mylist = history_list();

        for(int i = 0; i < myhist->length; i++)
        {
                free_history_entry(mylist[i]);
        }

        free(myhist);
        free(mylist);
}

std::optional<std::string> readline_to_string(const char* const ptr)
{
        char* buf = readline(ptr);
        if(buf == nullptr)
        {
                return std::nullopt;
        }

        add_history(buf);
        std::string res = buf;
        free(buf);

        return res;
}

template<bool CAN_HAVE_PIPES>
void process_line(const std::string_view line)
{
        if constexpr(CAN_HAVE_PIPES)
        {
                /* split on pipe operator */
                std::vector<std::string_view> pipe_strs;
                boost::split_regex(pipe_strs, line, boost::regex("(?<![|])[|](?![|])"));

                /* line is a pipe command */
                if(pipe_strs.size() > 1)
                {
                        PipeSequence::process_and_wait(pipe_strs);
                        return;
                }
        }

        /* line is a logical sequence */
        if(line.find("&&") != line.npos || line.find("||") != line.npos)
        {
                LogicSequence::process_and_wait(line);
                return;
        }

        /* line is a basic command */
        BasicCommand::process_and_wait(line);
}

bool ends_in_special_seq(const std::string_view line)
{
        const std::size_t len = line.size();

        if(line.back() == '|')
        {
                return true;
        }

        if(len > 1 && std::string_view(line.cend() - 2, line.cend()) == "&&")
        {
                return true;
        }

        return false;
}

void update_current_user()
{
        getlogin_r(current_user.data(), sizeof(current_user));
        gethostname(current_host.data(), sizeof(current_host));
}

std::string get_prompt()
{
        std::string path_str = fs::current_path().c_str();
        const std::string_view home_sv = home.c_str();

        if(path_str.find(home_sv) == 0)
        {
                boost::replace_first(path_str, home_sv, "~");
        }

        return fmt::format("[{}@{}:{}]% ", current_user.data(), current_host.data(), path_str);
}

void loop()
{
        while(running)
        {
                const auto prompt = get_prompt();
                const auto prompt_ptr = prompt.c_str();

                std::optional<std::string> opt_line = readline_to_string(prompt_ptr);
                if(!opt_line.has_value())
                {
                        return;
                }

                std::string& line = *opt_line;

                boost::trim(line);

                if(line.empty())
                {
                        continue;
                }

                while(ends_in_special_seq(line))
                {
                        const auto opt_aux = readline_to_string("> ");
                        if(!opt_aux.has_value())
                        {
                                return;
                        }

                        const auto& aux = *opt_aux;

                        line += ' ' + aux;
                        boost::trim_right(line);
                }

                const auto match_res = check_special_seq_err(line);
                if(match_res.first == true)
                {
                        print_err_fmt("shellter: syntax error: unrecognized sequence of "
                                      "special characters: '{}'\n",
                                      sv_from_match(match_res.second[0]));
                        continue;
                }

                process_line<true>(line);

                update_current_user();
                line_history.push_back(line);
        }
}

void interrupt_child(const int)
{
}

int main()
{
        signal(2, &interrupt_child);

        update_current_user();
        home = std::string("/home/") + current_user.data();

        loop();
        readline_free_history();
}
