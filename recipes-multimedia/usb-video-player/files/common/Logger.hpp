#pragma once

#include <iostream>
#include <string>
#include <sstream>
#include <mutex>
#include <chrono>
#include <iomanip>

namespace automotive {

enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERROR,
    SAFETY,
    FSM
};

class Logger {
public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    void log(LogLevel level, const std::string& module, const std::string& message) {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        auto now = std::chrono::system_clock::now();
        auto now_c = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        
        std::tm tm_buf;
        localtime_r(&now_c, &tm_buf);

        std::cout << "\033[90m[" 
                  << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S") 
                  << "." << std::setfill('0') << std::setw(3) << ms.count()
                  << "]\033[0m ";

        switch (level) {
            case LogLevel::DEBUG:
                std::cout << "\033[36m[DEBUG ]\033[0m";
                break;
            case LogLevel::INFO:
                std::cout << "\033[32m[INFO  ]\033[0m";
                break;
            case LogLevel::WARN:
                std::cout << "\033[33m[WARN  ]\033[0m";
                break;
            case LogLevel::ERROR:
                std::cout << "\033[31;1m[ERROR ]\033[0m";
                break;
            case LogLevel::SAFETY:
                std::cout << "\033[35;1m[SAFETY]\033[0m";
                break;
            case LogLevel::FSM:
                std::cout << "\033[34;1m[FSM   ]\033[0m";
                break;
        }

        std::cout << " \033[1m[" << module << "]\033[0m " << message << std::endl;
    }

private:
    Logger() = default;
    std::mutex m_mutex;
};

#define LOG_D(mod, msg) do { std::ostringstream _oss; _oss << msg; automotive::Logger::getInstance().log(automotive::LogLevel::DEBUG, mod, _oss.str()); } while(0)
#define LOG_I(mod, msg) do { std::ostringstream _oss; _oss << msg; automotive::Logger::getInstance().log(automotive::LogLevel::INFO, mod, _oss.str()); } while(0)
#define LOG_W(mod, msg) do { std::ostringstream _oss; _oss << msg; automotive::Logger::getInstance().log(automotive::LogLevel::WARN, mod, _oss.str()); } while(0)
#define LOG_E(mod, msg) do { std::ostringstream _oss; _oss << msg; automotive::Logger::getInstance().log(automotive::LogLevel::ERROR, mod, _oss.str()); } while(0)
#define LOG_S(mod, msg) do { std::ostringstream _oss; _oss << msg; automotive::Logger::getInstance().log(automotive::LogLevel::SAFETY, mod, _oss.str()); } while(0)
#define LOG_FSM(mod, msg) do { std::ostringstream _oss; _oss << msg; automotive::Logger::getInstance().log(automotive::LogLevel::FSM, mod, _oss.str()); } while(0)

} // namespace automotive
