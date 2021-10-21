static constexpr std::string_view user = "ionut";
static const fs::path home = "/home/ionut/";

#define STDERR_COLOR_GREY    fg(fmt::terminal_color::bright_black),
#define STDERR_COLOR_BLUE    fg(fmt::terminal_color::bright_blue),
#define STDERR_COLOR_YELLOW  fg(fmt::terminal_color::bright_yellow),
#define STDERR_COLOR_RED     fg(fmt::terminal_color::bright_red),
#define STDERR_COLOR_MAGENTA fg(fmt::terminal_color::bright_magenta),
#define STDERR_COLOR_GREEN   fg(fmt::terminal_color::bright_green),

#ifndef STDERR_COLOR
#define STDERR_COLOR STDERR_COLOR_RED
#endif
