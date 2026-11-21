// ModCore — logging (file + in-memory ring for GUI)
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

extern "C" __attribute__((stdcall)) unsigned long GetCurrentThreadId(void);
#define MC_CURTHREAD GetCurrentThreadId()

namespace mc {

struct LogLine {
    long long ts_ms;
    char     level[8];
    char     text[224];
};

class Logger {
public:
    static Logger& inst() { static Logger l; return l; }

    bool open(const char* path) {
        std::lock_guard<std::mutex> g(m_);
        if (f_) fclose(f_), f_ = nullptr;
        f_ = fopen(path, "ab");
        if (f_) {
            fprintf(f_, "\n==== ModCore session start ====\n");
            fflush(f_);
        }
        return f_ != nullptr;
    }

    void logf(const char* level, const char* fmt, ...) __attribute__((format(printf,3,4))) {
        char buf[224];
        va_list ap; va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);

        timespec ts{};
        clock_gettime(CLOCK_REALTIME, &ts);
        long long ms = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;

        {
            std::lock_guard<std::mutex> g(m_);
            ring_.push_back({ms, {}, {}});
            LogLine& l = ring_.back();
            snprintf(l.level, sizeof(l.level), "%s", level);
            snprintf(l.text, sizeof(l.text), "%s", buf);
            if (ring_.size() > 600) ring_.erase(ring_.begin(), ring_.begin() + 100);
            if (f_) {
                struct tm tmv;
                time_t sec = (time_t)(ms / 1000);
                gmtime_s(&tmv, &sec);
                fprintf(f_, "%02d:%02d:%02d.%03d [%s] [t%lu] %s\n",
                        tmv.tm_hour, tmv.tm_min, tmv.tm_sec, (int)(ms % 1000), level,
                        MC_CURTHREAD, buf);
                fflush(f_);
            }
        }
    }

    std::vector<LogLine> snapshot() {
        std::lock_guard<std::mutex> g(m_);
        return ring_;
    }

private:
    FILE* f_ = nullptr;
    std::mutex m_;
    std::vector<LogLine> ring_;
};

#define MC_LOG(...)    mc::Logger::inst().logf("INFO ", __VA_ARGS__)
#define MC_WARN(...)   mc::Logger::inst().logf("WARN ", __VA_ARGS__)
#define MC_ERR(...)    mc::Logger::inst().logf("ERROR", __VA_ARGS__)
#define MC_EVT(...)    mc::Logger::inst().logf("EVT  ", __VA_ARGS__)

} // namespace mc
