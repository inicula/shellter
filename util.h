template<typename... T>
void print_err_fmt(T&&... args)
{
        fmt::print(stderr, STDERR_COLOR std::forward<T>(args)...);
}

auto sv_from_match(const auto& boost_rmatch_result)
{
        return std::string_view(boost_rmatch_result.first, boost_rmatch_result.second);
}
