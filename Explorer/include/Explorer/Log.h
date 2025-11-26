#ifndef REASSETEXPLORER_LOG_H
#define REASSETEXPLORER_LOG_H

enum class LogLevel {
    Info,
    Warning,
    Error
};

// Messages carry no trailing newline. Without a sink they go to stdout.
using LogSink = void (*)(LogLevel level, const char* message);
void SetLogSink(LogSink sink);
void MuteLogOnThisThread(bool muted);

void Log(LogLevel level, const char* format, ...) __attribute__((format(printf, 2, 3)));
#define LogInfo(...) Log(LogLevel::Info, __VA_ARGS__)
#define LogWarning(...) Log(LogLevel::Warning, __VA_ARGS__)
#define LogError(...) Log(LogLevel::Error, __VA_ARGS__)

#endif
