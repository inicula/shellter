void print_err_fmt(auto&& str, auto&&... args)
{
        if(isatty(2))
        {
                fmt::print(stderr, STDERR_COLOR str, std::forward<decltype(args)>(args)...);
        }
        else
        {
                fmt::print(stderr, fmt::runtime(str), std::forward<decltype(args)>(args)...);
        }
}

auto sv_from_match(const auto& boost_rmatch_result)
{
        return std::string_view(boost_rmatch_result.first, boost_rmatch_result.second);
}
