#pragma once
#include <format>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

namespace perlin {
    // "[tag] message" on stdout; the line is formatted first and written in one call, so lines from worker
    // threads never interleave
    template <class... Args> void Log(std::string_view tag, std::format_string<Args...> fmt, Args&&... args) {
        std::string line;
        line.reserve(96);
        line += '[';
        line += tag;
        line += "] ";
        std::format_to(std::back_inserter(line), fmt, std::forward<Args>(args)...);
        line += '\n';
        std::cout.write(line.data(), static_cast<std::streamsize>(line.size()));
        std::cout.flush();
    }
}
