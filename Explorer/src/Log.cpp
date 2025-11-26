#include "Explorer/Log.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <vector>

namespace {

std::atomic<LogSink> g_sink{ nullptr };

}

void SetLogSink(LogSink sink) {
    g_sink.store(sink);
}

namespace {
thread_local bool t_muted = false;
}

void MuteLogOnThisThread(bool muted) {
    t_muted = muted;
}

void Log(LogLevel level, const char* format, ...) {
    if (t_muted) return;
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    int length = std::vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    std::vector<char> text(length > 0 ? static_cast<std::size_t>(length) + 1 : 1, '\0');
    if (length > 0) std::vsnprintf(text.data(), text.size(), format, args);
    va_end(args);

    if (LogSink sink = g_sink.load()) {
        sink(level, text.data());
        return;
    }
    static const char* const PREFIX[] = { "", "[warning] ", "[error] " };
    std::printf("%s%s\n", PREFIX[static_cast<int>(level)], text.data());
}
