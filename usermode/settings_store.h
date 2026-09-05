#ifndef SETTINGS_STORE_H
#define SETTINGS_STORE_H

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <chrono>
#include <sys/stat.h>

struct SettingsStore {
    std::string path;
    std::chrono::steady_clock::time_point dirty_since{};
    bool dirty{};

    void init() {
        const char* home = std::getenv("HOME");
        if (!home) home = "/tmp";
        std::string dir = std::string(home) + "/.config/gsd-housekeeping";
        mkdir(dir.c_str(), 0700);
        path = dir + "/preferences.json";
    }

    void mark_dirty() {
        if (!dirty) {
            dirty = true;
            dirty_since = std::chrono::steady_clock::now();
        }
    }

    bool should_flush() const {
        if (!dirty) return false;
        auto elapsed = std::chrono::steady_clock::now() - dirty_since;
        return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() >= 3000;
    }

    bool save(const char* json) {
        if (path.empty()) return false;
        FILE* f = fopen(path.c_str(), "w");
        if (!f) return false;
        fputs(json, f);
        fclose(f);
        dirty = false;
        return true;
    }

    std::string load() {
        if (path.empty()) return {};
        FILE* f = fopen(path.c_str(), "r");
        if (!f) return {};
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz <= 0 || sz > 8192) { fclose(f); return {}; }
        std::string buf(sz, '\0');
        fread(&buf[0], 1, sz, f);
        fclose(f);
        return buf;
    }

    static float json_float(const std::string& s, const char* key, float def) {
        auto pos = s.find(std::string("\"") + key + "\"");
        if (pos == std::string::npos) return def;
        pos = s.find(':', pos);
        if (pos == std::string::npos) return def;
        return static_cast<float>(atof(s.c_str() + pos + 1));
    }

    static bool json_bool(const std::string& s, const char* key, bool def) {
        auto pos = s.find(std::string("\"") + key + "\"");
        if (pos == std::string::npos) return def;
        pos = s.find(':', pos);
        if (pos == std::string::npos) return def;
        auto rest = s.substr(pos + 1, 10);
        if (rest.find("true") != std::string::npos) return true;
        if (rest.find("false") != std::string::npos) return false;
        return def;
    }
};

#endif
