#define FMT_BOLD fmt::emphasis::bold

#define STDERR_COLOR_GREY    fg(fmt::terminal_color::bright_black),
#define STDERR_COLOR_BLUE    fg(fmt::terminal_color::bright_blue),
#define STDERR_COLOR_YELLOW  fg(fmt::terminal_color::bright_yellow),
#define STDERR_COLOR_RED     fg(fmt::terminal_color::bright_red),
#define STDERR_COLOR_MAGENTA fg(fmt::terminal_color::bright_magenta),
#define STDERR_COLOR_GREEN   fg(fmt::terminal_color::bright_green),

#ifndef STDERR_COLOR
#define STDERR_COLOR STDERR_COLOR_RED
#endif

#ifdef BOOST_NO_EXCEPTIONS
namespace boost
{
void throw_exception(std::exception const&)
{
        exit(EXIT_FAILURE);
}
} // namespace boost
#endif
