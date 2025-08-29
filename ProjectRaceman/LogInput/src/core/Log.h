#pragma once
#include <iostream>
#include <string>

namespace Core {
    enum class LogLevel { Info, Warn, Error };

    class Log {
    public:
        static void Info(const std::string& msg) { Write("INFO", msg); }
        static void Warn(const std::string& msg) { Write("WARN", msg); }
        static void Error(const std::string& msg) { Write("ERROR", msg); }

    private:
        static void Write(const std::string& level, const std::string& msg) {
            std::cout << "[" << level << "] " << msg << std::endl;
        }
    };
}
