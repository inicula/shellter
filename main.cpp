#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <optional>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <termios.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/regex.hpp>

#define FMT_HEADER_ONLY
#include "third-party/fmt-8.1.0/include/fmt/core.h"
#include "third-party/fmt-8.1.0/include/fmt/color.h"

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
struct StdioFds;
struct SyntaxErrorRegex;
class BasicCommand;
class LogicSequence;
class PipeSequence;

/* using declarations */
using regsearch_result_t = std::pair<bool, boost::smatch>;

/* template function declarations */
template<bool>
static int process_line(const std::string_view);

static bool check_syntax_errors(const std::string&, const auto&);

/* function declarations */
static regsearch_result_t get_regsearch_result(const std::string&, const boost::regex&);
static void readline_free_history();
static std::optional<std::string> readline_to_string(const char* const);
static bool ends_in_special_seq(const std::string_view);
static void set_user_and_host();
static std::string get_prompt();
static void loop();
static void interrupt_child(const int);

/* class definitions */
struct StdioFds
{
public:
        StdioFds()
        {
                for(int i = 0; i < 3; ++i)
                {
                        old[i] = dup(i);
                }
        }

        ~StdioFds()
        {
                for(int i = 0; i < 3; ++i)
                {
                        dup2(old[i], i);
                        close(old[i]);
                }
        }

private:
        std::array<int, 3> old;
};

struct SyntaxErrorRegex
{
        SyntaxErrorRegex(const char* fmt_str, const char* reg_str)
            : fmt_diag(fmt_str)
            , reg(reg_str)
        {
        }

        std::string_view fmt_diag;
        boost::regex reg;
};

class BasicCommand
{
public:
        static std::optional<std::vector<std::string>> handle_redirections(const std::vector<std::string>&);
        static int process(const std::string_view);
};

class LogicSequence
{
public:
        static int process(const std::string_view);
};

class PipeSequence
{
public:
        static int process(const std::vector<std::string_view>&);
};

/* static member function definitions */
std::optional<std::vector<std::string>> BasicCommand::handle_redirections(const std::vector<std::string>& args)
{
        static constexpr std::array<std::string_view, 8> redir_symbols = {
            "1>>", "2>>", ">>", "1>", "2>", ">", "0<", "<"
        };
        static constexpr std::size_t appending_fds_limit = 2;
        static constexpr std::array<int, 8> symbol_fds = {1, 2, 1, 1, 2, 1, 0, 0};
        static constexpr int OUTFILE_PERMS = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
        static constexpr std::array<int, 8> open_modes = {
            O_WRONLY | O_CREAT | O_APPEND,
            O_WRONLY | O_CREAT | O_APPEND,
            O_WRONLY | O_CREAT | O_APPEND,
            O_WRONLY | O_CREAT | O_TRUNC,
            O_WRONLY | O_CREAT | O_TRUNC,
            O_WRONLY | O_CREAT | O_TRUNC,
            O_RDONLY,
            O_RDONLY
        };

        std::vector<std::string> args_after_redir;
        for(std::size_t i = 0; i < args.size(); ++i)
        {
                const std::string_view current_arg = args[i];

                /* check if at least one symbol was found */
                const auto it = std::find_if(redir_symbols.begin(), redir_symbols.end(),
                                             [&](const auto& symbol)
                                             {
                                                     return current_arg.find(symbol) == 0;
                                             });

                /* none were found */
                if(it == redir_symbols.end())
                {
                        args_after_redir.push_back(args[i]);
                        continue;
                }

                /* at least one was found, process it */
                const auto symbol_pos = static_cast<std::size_t>(it - redir_symbols.begin());
                const auto symbol_found = redir_symbols[symbol_pos];

                const char* filename = nullptr;

                /* check for filename in current arg */
                if(current_arg.size() > symbol_found.size())
                {
                        filename = current_arg.data() + symbol_found.size();
                }

                /* check for filename in next arg */
                if(filename == nullptr && (i < args.size() - 1))
                {
                        filename = args[i + 1].c_str();
                        ++i;
                }

                /* check if filename was found; if so, redirect that stream */
                if(filename != nullptr)
                {
                        /* check if filename refers to valid standard fd */
                        const std::string_view filename_sv = filename;
                        if(filename_sv == "&2" || filename_sv == "&1" || filename_sv == "&0")
                        {
                                if(symbol_pos <= appending_fds_limit)
                                {
                                        print_err_fmt("shellter: can't redirect standard fd "
                                                      "to another standard fd in appending mode\n");
                                        return std::nullopt;
                                }

                                const int new_fd = std::atoi(filename_sv.data() + 1);
                                dup2(new_fd, symbol_fds[symbol_pos]);
                                continue;
                        }
                        else if(filename_sv.find("&") == 0)
                        {
                                print_err_fmt("shellter: looked for valid file descriptor, found: {}\n",
                                              filename_sv);
                                return std::nullopt;
                        }

                        /* filename refers to an actual file */
                        const int new_fd =
                            (symbol_pos < redir_symbols.size() - 2)
                                ? open(filename, open_modes[symbol_pos], OUTFILE_PERMS)
                                : open(filename, open_modes[symbol_pos]);

                        if(new_fd < 0)
                        {
                                print_err_fmt("shellter: error opening {}: {}\n", filename,
                                              strerror(errno));

                                return std::nullopt;
                        }

                        dup2(new_fd, symbol_fds[symbol_pos]);
                        close(new_fd);

                        continue;
                }

                /* filename is missing, report error and return */
                print_err_fmt(
                    "shellter: error in redirection symbol '{}': filename is missing\n",
                    symbol_found);

                return std::nullopt;
        }

        return std::optional{std::move(args_after_redir)};
}

int BasicCommand::process(const std::string_view line_sv)
{
        std::string str(line_sv);
        boost::trim(str);

        /* split words, compress spaces */
        std::vector<std::string> args;
        boost::split(args, std::as_const(str), boost::is_any_of(" "), boost::token_compress_on);

        /* replace environment values */
        for(std::size_t i = 0; i < args.size(); ++i)
        {
                if(i == 1 && (args[0] == std::string_view("addenv") ||
                              args[0] == std::string_view("eaddenv")))
                {
                        continue;
                }

                const auto it = environment_vars.find(args[i]);
                if(it != environment_vars.end())
                {
                        args[i] = it->second;
                }
        }

        /* check for redirection */
        StdioFds old_fds{};
        auto args_after_redir_opt = handle_redirections(args);

        if(!args_after_redir_opt.has_value())
        {
                return EXIT_FAILURE;
        }

        auto& args_after_redir = *args_after_redir_opt;
        if(args_after_redir.empty())
        {
                return EXIT_SUCCESS;
        }

        /* check for builtin command */
        const auto builtin_it = builtin_funcs.find(args_after_redir.front());
        if(builtin_it != builtin_funcs.cend())
        {
                const auto r = builtin_it->second(args_after_redir);

                return r;
        }

        const pid_t child_pid = fork();
        if(child_pid == 0)
        {
                /* construct array of char pointers (null terminated) */
                std::vector<char*> arg_ptrs;
                for(auto& str : args_after_redir)
                {
                        arg_ptrs.push_back(str.data());
                }
                arg_ptrs.push_back(nullptr);

                execvp(arg_ptrs[0], arg_ptrs.data());
                print_err_fmt("shellter: error calling execvp(): {}: {}\n", arg_ptrs[0],
                              strerror(errno));
                exit(1);
        }

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

int LogicSequence::process(const std::string_view sub_line)
{
        const auto or_pos = sub_line.find("||");
        const auto and_pos = sub_line.find("&&");

        if(or_pos != sub_line.npos)
        {
                const std::string_view left_line{sub_line.cbegin(), sub_line.cbegin() + or_pos};
                const std::string_view right_line{sub_line.cbegin() + or_pos + 2, sub_line.cend()};

                const int left_ret = process(left_line);
                if(left_ret != EXIT_SUCCESS)
                {
                        return process(right_line);
                }

                return left_ret;
        }
        else if(and_pos != sub_line.npos)
        {
                const std::string_view left_line{sub_line.cbegin(), sub_line.cbegin() + and_pos};
                const std::string_view right_line{sub_line.cbegin() + and_pos + 2, sub_line.cend()};

                const int left_ret = process(left_line);
                if(left_ret == EXIT_SUCCESS)
                {
                        return process(right_line);
                }

                return left_ret;
        }
        else
        {
                return process_line<false>(sub_line);
        }
}

int PipeSequence::process(const std::vector<std::string_view>& command_components)
{
        const int fd_old_in = dup(0);
        const int fd_old_out = dup(1);

        int fd_command_input = dup(fd_old_in);
        int fd_command_output;

        const std::size_t len = command_components.size();
        int ret = EXIT_FAILURE;
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

                ret = BasicCommand::process(command_components[i]);
        }

        dup2(fd_old_in, 0);
        dup2(fd_old_out, 1);
        close(fd_old_in);
        close(fd_old_out);

        return ret;
}

/* function definitions */
regsearch_result_t get_regsearch_result(const std::string& line, const boost::regex& reg_expr)
{
        boost::smatch match_array;
        const bool found = boost::regex_search(line, match_array, reg_expr);
        return {found, std::move(match_array)};
}

bool check_syntax_errors(const std::string& line, const auto& possible_syntax_errs)
{
        for(auto& possible_err : possible_syntax_errs)
        {
                const auto match_res = get_regsearch_result(line, possible_err.reg);
                if(match_res.first)
                {
                        print_err_fmt(possible_err.fmt_diag, sv_from_match(match_res.second[0]));
                        return true;
                }
        }

        return false;
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

std::optional<std::string> readline_to_string(const char* const prompt)
{
        char* buf = readline(prompt);
        if(buf == nullptr)
        {
                return std::nullopt;
        }

        const std::string res = buf;
        free(buf);

        return res;
}

template<bool CAN_HAVE_LOGICAL_OPERATORS>
int process_line(const std::string_view line)
{
        if constexpr(CAN_HAVE_LOGICAL_OPERATORS)
        {
                /* line is a logical sequence */
                if(line.find("||") != line.npos || line.find("&&") != line.npos)
                {
                        return LogicSequence::process(line);
                }
        }

        /* split on pipe operator */
        std::vector<std::string_view> pipe_strs;
        boost::split_regex(pipe_strs, line, boost::regex("(?<![|])[|](?![|])"));

        /* line is a pipe command */
        if(pipe_strs.size() > 1)
        {
                return PipeSequence::process(pipe_strs);
        }

        /* line is a basic command */
        return BasicCommand::process(line);
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

void set_user_and_host()
{
        cuserid(current_user.data());
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
        /* regular expressions for syntax errors */
        const std::array<SyntaxErrorRegex, 3> possible_syntax_errs =
        {{
             {
                 "shellter: syntax error: unrecognized sequence of special characters: '{}'\n",
                 ">{3,}|<{2,}|[|]{3,}|[&]{3,}|(&[|]|[|]&)[&\\|]*|[|][|][&\\|]+"
             },
             {
                 "shellter: syntax error: empty command between tokens: '{}'\n",
                 "((?<![|])[|](?![|])|&&|[|][|])\\s+((?<![|])[|](?![|])|&&|[|][|])"
             },
             {
                 "shellter: syntax error: bad file descriptor (only standard fds accepted): '{}'\n",
                 "><|[><]\\s+[><]|([^0\\s>]|[\\S]{2,})<|([^12\\s>]|[\\S]{2,})(?<!>)>{1,}"
             }
        }};

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


                /* add line to history */
                add_history(line.c_str());
                line_history.push_back(line);

                std::vector<std::string> separate_commands;
                boost::split(separate_commands, std::as_const(line), boost::is_any_of(";"));

                for(auto& sep_line : separate_commands)
                {
                        boost::trim(sep_line);

                        if(sep_line.empty())
                        {
                                continue;
                        }

                        /* check for syntax errors */
                        if(check_syntax_errors(sep_line, possible_syntax_errs))
                        {
                                continue;
                        }

                        /* line is valid, process it */
                        process_line<true>(sep_line);
                }
        }
}

void interrupt_child(const int)
{
        /* putc() is not signal-safe */
        write(1, "\n", 1);

        /* neither are those, probably */
        rl_on_new_line();
        rl_replace_line("", 0);
        rl_redisplay();
}

int main()
{
        /* misc inits */
        rl_outstream = stderr;
        if(!isatty(0) || !isatty(2))
        {
                FILE* devnull = std::fopen("/dev/null", "w");
                rl_outstream = devnull;
        }

        signal(SIGINT, &interrupt_child);

        set_user_and_host();
        if(geteuid() != 0)
        {
                home = std::string("/home/") + current_user.data();
        }
        else
        {
                home = std::string("/") + current_user.data();
        }

        loop();
        readline_free_history();
}
