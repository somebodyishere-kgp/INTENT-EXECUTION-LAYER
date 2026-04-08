#include "Logger.h"

#include <atomic>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

namespace iee {
namespace {

std::mutex g_logMutex;
std::atomic<bool> g_logEnabled{true};

const char* LevelToText(LogLevel level) {
    switch (level) {
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warning:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    }

    return "UNKNOWN";
}

std::string BuildTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);

    std::tm localTime{};
#if defined(_WIN32)
    localtime_s(&localTime, &nowTime);
#else
    localtime_r(&nowTime, &localTime);
#endif

    std::ostringstream stream;
    stream << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    return stream.str();
}

}  // namespace

void Logger::SetEnabled(bool enabled) {
    g_logEnabled.store(enabled);
}

bool Logger::Enabled() {
    return g_logEnabled.load();
}

void Logger::Log(LogLevel level, std::string_view source, std::string_view message) {
    if (!g_logEnabled.load()) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_logMutex);
    std::cout << "[" << BuildTimestamp() << "]"
              << "[" << LevelToText(level) << "]"
              << "[" << source << "] "
              << message << std::endl;
}

void Logger::Info(std::string_view source, std::string_view message) {
    Log(LogLevel::Info, source, message);
}

void Logger::Warning(std::string_view source, std::string_view message) {
    Log(LogLevel::Warning, source, message);
}

void Logger::Error(std::string_view source, std::string_view message) {
    Log(LogLevel::Error, source, message);
}

}  // namespace iee
