namespace builtins
{
using args_t = std::vector<std::string>;

int cd(const args_t& args)
{
        const std::size_t len = args.size();
        if(len > 2)
        {
                print_err_fmt("shellter: cd: too many arguments\n");
        }

        fs::path next_path;
        if(len == 1)
        {
                next_path = home;
        }
        else
        {
                if(args[1] == "..")
                {
                        next_path = fs::current_path().parent_path();
                }
                else if(args[1] == "-")
                {
                        if(!old_path_set)
                        {
                                print_err_fmt("shellter: OLDPATH not set\n");
                                return EXIT_FAILURE;
                        }
                        next_path = old_path;
                }
                else
                {
                        next_path = args[1];
                }
        }

        if(!fs::is_directory(next_path))
        {
                print_err_fmt("shellter: cd: {}: No such file or directory\n",
                              next_path.c_str());
                return EXIT_FAILURE;
        }

        old_path = fs::current_path();
        old_path_set = true;
        fs::current_path(next_path);

        return EXIT_SUCCESS;
}

int echo(const args_t& args)
{
        const std::size_t len = args.size();
        for(std::size_t i = 1; i < len - 1; ++i)
        {
                fmt::print("{} ", args[i]);
        }

        if(len > 1)
        {
                fmt::print("{}\n", args.back());
        }

        return EXIT_SUCCESS;
}

int exit(const args_t& args)
{
        const std::size_t len = args.size();

        if(len == 1)
        {
                ::exit(EXIT_SUCCESS);
        }

        if(len == 2)
        {
                ::exit(std::stoi(args[1]));
        }

        print_err_fmt("shellter: exit: too many arguments\n");
        return EXIT_FAILURE;
}

int pwd(const args_t& args)
{
        const std::size_t len = args.size();
        if(len > 1)
        {
                print_err_fmt("shellter: pwd: too many arguments\n");
                return EXIT_FAILURE;
        }

        fmt::print("{}\n", fs::current_path().c_str());
        return EXIT_SUCCESS;
}

int history(const args_t& args)
{
        const std::size_t len = args.size();
        if(len > 1)
        {
                print_err_fmt("shellter: history: too many arguments\n");
                return EXIT_FAILURE;
        }

        for(std::size_t i = 0; i < line_history.size(); ++i)
        {
                fmt::print(" {}  {}\n", i + 1, line_history[i]);
        }

        return EXIT_SUCCESS;
}

int addenv(const args_t& args)
{
        const std::size_t len = args.size();
        if(len != 3 || args[1].front() != '$')
        {
                print_err_fmt("shellter: addenv usage: addenv $VARNAME VALUE\n");
                return EXIT_FAILURE;
        }

        environment_vars[args[1]] = args[2];

        return EXIT_SUCCESS;
}

int quit(const args_t& args)
{
        const std::size_t len = args.size();
        if(len > 1)
        {
                print_err_fmt("shellter: cd: too many arguments\n");
                return EXIT_FAILURE;
        }

        running = false;

        return EXIT_SUCCESS;
}

} // namespace builtins

using builtin_func_t = int (*)(const builtins::args_t&);

static const std::unordered_map<std::string_view, builtin_func_t> builtin_funcs = {
    { "cd",      &builtins::cd      },
    { "echo",    &builtins::echo    },
    { "exit",    &builtins::exit    },
    { "pwd",     &builtins::pwd     },
    { "history", &builtins::history },
    { "addenv",  &builtins::addenv  },
    { "quit",    &builtins::quit    }
};
