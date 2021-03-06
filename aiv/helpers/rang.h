// From http://codereview.stackexchange.com/questions/120777/colorful-output-on-terminal

#ifndef RANG_H
#define RANG_H

#include <iostream>
#include <cstdlib>
#include <cstring>
extern "C" {
#include <unistd.h>
}

namespace rang {

    enum class style : unsigned char {
        Reset     = 0,
        bold      = 1,
        dim       = 2,
        italic    = 3,
        underline = 4,
        blink     = 5,
        reversed  = 6,
        conceal   = 7,
        crossed   = 8
    };
    enum class fg : unsigned char {
        def     = 39,
        black   = 30,
        red     = 31,
        green   = 32,
        yellow  = 33,
        blue    = 34,
        magenta = 35,
        cyan    = 36,
        gray    = 37
    };
    enum class bg : unsigned char {
        def     = 49,
        black   = 40,
        red     = 41,
        green   = 42,
        yellow  = 43,
        blue    = 44,
        magenta = 45,
        cyan    = 46,
        gray    = 47
    };
}


namespace {
    bool isAllowed = false;
    bool isTerminal()
    {
        return isatty(STDOUT_FILENO);
    }
    bool supportsColor()
    {
        if(const char *env_p = std::getenv("TERM")) {
            const char *const term[8] = {
                "xterm", "xterm-256", "xterm-256color", "vt100",
                "color", "ansi",      "cygwin",         "linux"};
            for(unsigned int i = 0; i < 8; ++i) {
                if(std::strcmp(env_p, term[i]) == 0) return true;
            }
        }
        return false;
    }
    std::ostream &operator<<(std::ostream &os, rang::style v)
    {
        return isAllowed ? os << "\e[" << static_cast<int>(v) << "m" : os;
    }
    std::ostream &operator<<(std::ostream &os, rang::fg v)
    {
        return isAllowed ? os << "\e[" << static_cast<int>(v) << "m" : os;
    }
    std::ostream &operator<<(std::ostream &os, rang::bg v)
    {
        return isAllowed ? os << "\e[" << static_cast<int>(v) << "m" : os;
    }
    namespace init {
        void rang()
        {
            isAllowed = isTerminal() && supportsColor() ? true : false;
        }
    }
}
#endif /* ifndef RANG_H*/

// Change 1
bool supportsColor()
{
    if(const char *env_p = std::getenv("TERM")) {
        const char *const terms[] = {
            "xterm", "xterm-256", "xterm-256color", "vt100",
            "color", "ansi",      "cygwin",         "linux"};
        // Change 1
        for(auto const term: terms) {
            if(std::strcmp(env_p, term) == 0) return true;
        }
    }
    return false;
}

// Change 2
template<typename T>
using enable = typename std::enable_if
<
    std::is_same<T, rang::style>::value ||
    std::is_same<T, rang::fg>::value ||
    std::is_same<T, rang::bg>::value,
    std::ostream&
>::type;

template<typename T>
enable<T> operator<<(std::ostream& os, T const value)
{
    ...
}

// Change 3
if(isAllowed) {
    os << "\e[" << static_cast< int >(value) << "m";
}
return os;