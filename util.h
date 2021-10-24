template<typename... T>
void print_err_fmt(T&&... args)
{
        fmt::print(stderr, STDERR_COLOR std::forward<T>(args)...);
}

auto sv_from_match(const auto& boost_rmatch_result)
{
        return std::string_view(boost_rmatch_result.first, boost_rmatch_result.second);
}

void restore_standard_fds(const std::array<int, 3> old_fds)
{
        for(int i = 0; i < 3; ++i)
        {
                dup2(old_fds[i], i);
                close(old_fds[i]);
        }
}
