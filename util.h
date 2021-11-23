template<typename... T>
void print_err_fmt(T&&... args)
{
        std::array<char, 256> buf = {};
        const auto res = readlink("/proc/self/fd/2", buf.data(), std::size(buf));
        if(res >= 0)
        {
                buf.back() = '\0';

                const std::string_view sv = buf.data();
                if(sv.find("/dev/pts/") == 0)
                {
                        fmt::print(stderr, STDERR_COLOR std::forward<T>(args)...);
                        return;
                }
        }

        fmt::print(stderr, std::forward<T>(args)...);
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
