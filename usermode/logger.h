#ifndef LOGGER_H
#define LOGGER_H

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <mutex>
#include <unistd.h>
#include <sys/types.h>

class Logger {
public:
    static Logger& instance() {
        static Logger inst;
        return inst;
    }

    bool init() {
        snprintf(log_path_, sizeof(log_path_), "/tmp/.gsd_log_%d", getpid());
        fp_ = fopen(log_path_, "w");
        if (!fp_) return false;
        setvbuf(fp_, nullptr, _IOLBF, 0);

        clock_gettime(CLOCK_MONOTONIC, &start_time_);

        fprintf(fp_, "╔══════════════════════════════════════════════════╗\n");
        fprintf(fp_, "║            DEBUG CONSOLE INITIALIZED            ║\n");
        fprintf(fp_, "╠══════════════════════════════════════════════════╣\n");
        fprintf(fp_, "║  Log file: %-38s║\n", log_path_);
        fprintf(fp_, "║  PID:      %-38d║\n", getpid());
        fprintf(fp_, "╚══════════════════════════════════════════════════╝\n\n");
        fflush(fp_);

        fprintf(stderr, "[Logger] Log file: %s\n", log_path_);
        spawn_console();
        return true;
    }

    void log(const char* level, const char* fmt, ...) {
        if (!fp_) return;
        std::lock_guard<std::mutex> lock(mtx_);

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (now.tv_sec - start_time_.tv_sec)
                       + (now.tv_nsec - start_time_.tv_nsec) / 1e9;

        fprintf(fp_, "[%8.3f] [%-5s] ", elapsed, level);

        va_list args;
        va_start(args, fmt);
        vfprintf(fp_, fmt, args);
        va_end(args);

        fputc('\n', fp_);
    }

    void shutdown() {
        if (fp_) {
            log("INFO", "Logger shutting down");
            fclose(fp_);
            fp_ = nullptr;
        }
    }

    const char* path() const { return log_path_; }

private:
    Logger() : fp_(nullptr) {
        memset(log_path_, 0, sizeof(log_path_));
        memset(&start_time_, 0, sizeof(start_time_));
    }
    ~Logger() { shutdown(); }

    void spawn_console() {
        char cmd[512];
        snprintf(cmd, sizeof(cmd),
            "gnome-terminal --title=Log -- tail -n +1 -f '%s' &>/dev/null &",
            log_path_);
        if (system(cmd) != 0) {
            snprintf(cmd, sizeof(cmd),
                "xterm -T Log -geometry 140x35 -e 'tail -n +1 -f %s' &>/dev/null &",
                log_path_);
            if (system(cmd) != 0) {
                fprintf(stderr, "[Logger] No terminal emulator found. Use: tail -f %s\n", log_path_);
            }
        }
    }

    FILE* fp_;
    char log_path_[128];
    struct timespec start_time_;
    std::mutex mtx_;
};

#define LOG_INFO(fmt, ...)  Logger::instance().log("INFO",  fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  Logger::instance().log("WARN",  fmt, ##__VA_ARGS__)
#define LOG_ERR(fmt, ...)   Logger::instance().log("ERROR", fmt, ##__VA_ARGS__)
#define LOG_DBG(fmt, ...)   Logger::instance().log("DBG",   fmt, ##__VA_ARGS__)
#define LOG_CHAIN(fmt, ...) Logger::instance().log("CHAIN", fmt, ##__VA_ARGS__)

#endif
