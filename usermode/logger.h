#ifndef LOGGER_H
#define LOGGER_H

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <mutex>
#include <vector>
#include <string>

class Logger {
public:
    static Logger& instance() {
        static Logger inst;
        return inst;
    }

    bool init() {
        std::lock_guard<std::mutex> lock(mtx_);
        clock_gettime(CLOCK_MONOTONIC, &start_time_);
        ready_ = true;
        push("[SYSTEM] In-memory logger initialized");
        return true;
    }

    void log(const char* level, const char* fmt, ...) {
        if (!ready_) return;
        std::lock_guard<std::mutex> lock(mtx_);

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (now.tv_sec - start_time_.tv_sec)
                       + (now.tv_nsec - start_time_.tv_nsec) / 1e9;

        char header[64];
        snprintf(header, sizeof(header), "[%8.3f] [%-5s] ", elapsed, level);

        char body[512];
        va_list args;
        va_start(args, fmt);
        vsnprintf(body, sizeof(body), fmt, args);
        va_end(args);

        push(std::string(header) + body);
    }

    void render_widget() {
        std::lock_guard<std::mutex> lock(mtx_);

        ImGui::Text("Entries: %d / %d", count_, MAX_ENTRIES);
        ImGui::SameLine();
        if (ImGui::Button("Clear")) {
            count_ = 0;
            head_ = 0;
        }

        ImGui::Separator();
        render_log_inner();
    }

    void render_log_only() {
        std::lock_guard<std::mutex> lock(mtx_);
        render_log_inner();
    }

    std::vector<std::string> entries_snapshot() {
        std::lock_guard<std::mutex> lock(mtx_);
        std::vector<std::string> out;
        out.reserve(count_);
        for (int i = 0; i < count_; ++i) {
            int idx = (count_ < MAX_ENTRIES) ? i : (head_ + i) % MAX_ENTRIES;
            out.push_back(ring_[idx]);
        }
        return out;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mtx_);
        count_ = 0;
        head_ = 0;
    }

    void shutdown() {
        std::lock_guard<std::mutex> lock(mtx_);
        ready_ = false;
    }

    const char* path() const { return "(in-memory)"; }

private:
    static constexpr int MAX_ENTRIES = 2000;

    Logger() : ready_(false), auto_scroll_(true), head_(0), count_(0) {
        memset(&start_time_, 0, sizeof(start_time_));
        ring_.resize(MAX_ENTRIES);
    }
    ~Logger() { shutdown(); }

    void push(const std::string& line) {
        ring_[head_] = line;
        head_ = (head_ + 1) % MAX_ENTRIES;
        if (count_ < MAX_ENTRIES) count_++;
    }

    bool ready_;
    bool auto_scroll_;
    int head_;
    int count_;
    struct timespec start_time_;
    std::mutex mtx_;
    std::vector<std::string> ring_;

    inline int entry_index(int i) const {
        if (count_ < MAX_ENTRIES) return i;
        return (head_ + i) % MAX_ENTRIES;
    }

    void render_log_inner() {
        ImGui::BeginChild("##log_scroll", ImVec2(0, 0), false,
                          ImGuiWindowFlags_HorizontalScrollbar);

        ImGuiListClipper clipper;
        clipper.Begin(count_);
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const auto& e = ring_[entry_index(i)];
                ImVec4 col = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
                if (e.find("[ERROR]") != std::string::npos)
                    col = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
                else if (e.find("[WARN") != std::string::npos)
                    col = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
                else if (e.find("[CHAIN]") != std::string::npos)
                    col = ImVec4(0.4f, 0.7f, 1.0f, 1.0f);
                else if (e.find("[SYSTEM]") != std::string::npos)
                    col = ImVec4(0.2f, 0.8f, 0.4f, 1.0f);

                ImGui::PushStyleColor(ImGuiCol_Text, col);
                ImGui::TextUnformatted(e.c_str());
                ImGui::PopStyleColor();
            }
        }

        if (auto_scroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10)
            ImGui::SetScrollHereY(1.0f);

        ImGui::EndChild();
    }
};

#define LOG_INFO(fmt, ...)  Logger::instance().log("INFO",  fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  Logger::instance().log("WARN",  fmt, ##__VA_ARGS__)
#define LOG_ERR(fmt, ...)   Logger::instance().log("ERROR", fmt, ##__VA_ARGS__)
#define LOG_DBG(fmt, ...)   Logger::instance().log("DBG",   fmt, ##__VA_ARGS__)
#define LOG_CHAIN(fmt, ...) Logger::instance().log("CHAIN", fmt, ##__VA_ARGS__)

#endif
