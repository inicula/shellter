#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
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
class Command;
class BasicCommand;
class LogicSequence;
class PipeSequence;

/* function declarations */
static std::unique_ptr<Command> process_line(const std::string_view);
static bool ends_in_special_seq(const std::string_view);
static void update_current_user();
static void prompt();
static void loop();
static void interrupt_child(const int);

/* class definitions */
class Command
{
public:
        virtual ~Command() = default;

        virtual void exec() const = 0;
        virtual int exec_and_wait() const = 0;
};

class BasicCommand : public Command
{
public:
        BasicCommand(const std::string_view line_sv)
            : line_sv(line_sv)
        {
        }

        void exec() const override;
        int exec_and_wait() const override;

private:
        template<bool>
        auto helper_exec() const;

private:
        std::string_view line_sv;
};

class LogicSequence : public Command
{
private:
        using operator_t = signed char;

        struct Node
        {
                Node(const std::string_view);

                bool is_operator_node() const;

                operator_t op;
                std::unique_ptr<BasicCommand> command;
                std::unique_ptr<Node> left;
                std::unique_ptr<Node> right;
        };

public:
        LogicSequence(const std::string_view line)
            : root(new Node(line))
        {
        }

        void exec() const override;
        int exec_and_wait() const override;

private:
        int exec(const std::unique_ptr<Node>&) const;

private:
        std::unique_ptr<Node> root = nullptr;
};

class PipeSequence : public Command
{
public:
        PipeSequence(const std::vector<std::string_view>& split_on_pipe)
        {
                for(auto subline : split_on_pipe)
                {
                        commands.push_back(process_line(subline));
                }
        }

        void exec() const override
        {
        }

        int exec_and_wait() const override;

private:
        std::vector<std::unique_ptr<Command>> commands;
};

/* member function definitions */
template<bool WAIT>
auto BasicCommand::helper_exec() const
{
        std::string str(line_sv);
        boost::trim(str);

        /* split words, compress spaces */
        std::vector<std::string> args;
        boost::split(args, str, boost::is_any_of(" "), boost::token_compress_on);

        /* replace environment values */
        for(auto& word : args)
        {
                const auto it = environment_vars.find(word);
                if(it != environment_vars.end())
                {
                        word = it->second;
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

                return status;
        }
}

void BasicCommand::exec() const
{
        helper_exec<false>();
}

int BasicCommand::exec_and_wait() const
{
        return helper_exec<true>();
}

LogicSequence::Node::Node(const std::string_view line)
{
        const auto or_pos = line.find("||");
        const auto and_pos = line.find("&&");

        if(or_pos != line.npos)
        {
                const std::string_view left_line{line.cbegin(), line.cbegin() + or_pos};
                const std::string_view right_line{line.cbegin() + or_pos + 2, line.cend()};

                op = 1;
                command = nullptr;
                left = std::unique_ptr<Node>(new Node(left_line));
                right = std::unique_ptr<Node>(new Node(right_line));
        }
        else if(and_pos != line.npos)
        {
                const std::string_view left_line{line.cbegin(), line.cbegin() + and_pos};
                const std::string_view right_line{line.cbegin() + and_pos + 2, line.cend()};

                op = 0;
                command = nullptr;
                left = std::unique_ptr<Node>(new Node(left_line));
                right = std::unique_ptr<Node>(new Node(right_line));
        }
        else
        {
                command = std::unique_ptr<BasicCommand>(new BasicCommand(line));
                op = -1;
                left = nullptr;
                right = nullptr;
        }
}

bool LogicSequence::Node::is_operator_node() const
{
        return op >= 0;
}

void LogicSequence::exec() const
{
        exec(root);
}

int LogicSequence::exec_and_wait() const
{
        return exec(root);
}

int LogicSequence::exec(const std::unique_ptr<Node>& node) const
{
        if(node->is_operator_node())
        {
                switch(node->op)
                {
                case 0:
                {
                        const int ret = exec(node->left);
                        if(ret == EXIT_SUCCESS)
                        {
                                return exec(node->right);
                        }

                        return ret;
                }
                case 1:
                {
                        const int ret = exec(node->left);
                        if(ret != EXIT_SUCCESS)
                        {
                                return exec(node->right);
                        }

                        return ret;
                }
                default:
                {
                        assert(false);
                }
                }
        }
        else
        {
                return node->command->exec_and_wait();
        }
}

int PipeSequence::exec_and_wait() const
{
        const int fd_old_in = dup(0);
        const int fd_old_out = dup(1);

        int fd_command_input = dup(fd_old_in);
        int fd_command_output;

        const std::size_t len = commands.size();
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

                commands[i]->exec_and_wait();
        }

        dup2(fd_old_in, 0);
        dup2(fd_old_out, 1);
        close(fd_old_in);
        close(fd_old_out);

        return EXIT_SUCCESS;
}

/* function definitions */
std::unique_ptr<Command> process_line(const std::string_view line)
{
        /* split on pipe operator */
        std::vector<std::string_view> pipe_strs;
        boost::split_regex(pipe_strs, line, boost::regex("(?<![|])[|](?![|])"));

        /* line is a pipe command */
        if(pipe_strs.size() > 1)
        {
                return std::unique_ptr<Command>(new PipeSequence(pipe_strs));
        }

        /* line is a logical sequence */
        if(line.find("&&") != line.npos || line.find("||") != line.npos)
        {
                return std::unique_ptr<Command>(new LogicSequence(line));
        }

        /* line is a basic command */
        return std::unique_ptr<Command>(new BasicCommand(line));
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

void prompt()
{
        fmt::print(FMT_BOLD | fg(fmt::terminal_color::bright_black), "[");
        fmt::print(FMT_BOLD | fg(fmt::terminal_color::yellow), "{}", current_user.data());
        fmt::print(FMT_BOLD | fg(fmt::terminal_color::bright_black), "@");
        fmt::print(FMT_BOLD | fg(fmt::terminal_color::blue), "{}", current_host.data());
        fmt::print(FMT_BOLD | fg(fmt::terminal_color::bright_black), ":");
        fmt::print(FMT_BOLD | fg(fmt::terminal_color::red), "{}", fs::current_path().c_str());
        fmt::print(FMT_BOLD | fg(fmt::terminal_color::bright_black), "]");
        fmt::print(FMT_BOLD | fg(fmt::terminal_color::bright_white), "% ");
}

void loop()
{
        std::string line;
        std::string aux;
        while(running)
        {
                prompt();

                std::getline(std::cin, line);
                boost::trim(line);

                if(line.empty())
                {
                        continue;
                }

                while(ends_in_special_seq(line))
                {
                        fmt::print("> ");
                        std::getline(std::cin, aux);
                        line += ' ' + aux;

                        boost::trim_right(line);
                }

                const std::unique_ptr<Command> command = process_line(line);
                assert(command);
                command->exec_and_wait();

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
}
