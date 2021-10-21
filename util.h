template<typename... T>
void print_err_fmt(T&&... args)
{
        fmt::print(stderr, STDERR_COLOR std::forward<T>(args)...);
}
