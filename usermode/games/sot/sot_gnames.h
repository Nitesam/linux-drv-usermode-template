#ifndef SOT_GNAMES_H
#define SOT_GNAMES_H

#include <cstdint>
#include <string>
#include <unordered_map>

#include "mem_client.h"
#include "logger.h"
#include "sot_offsets.h"

class SotGNames {
public:
    SotGNames() = default;

    void set_address(uint64_t gnames_addr) {
        if (gnames_addr != gnames_addr_) {
            gnames_addr_ = gnames_addr;
            cache_.clear();
        }
    }

    uint64_t address() const { return gnames_addr_; }
    const char* last_error() const { return last_error_; }

    std::string resolve(MemClient& client, int pid, uint32_t comparison_index) {
        if (gnames_addr_ == 0) {
            set_error("GNames address is 0");
            return {};
        }
        if (comparison_index == 0) {
            set_error("Comparison index is 0");
            return {};
        }

        auto it = cache_.find(comparison_index);
        if (it != cache_.end()) {
            clear_error();
            return it->second;
        }

        std::string name = read_fname(client, pid, comparison_index);
        if (!name.empty()) {
            cache_[comparison_index] = name;
            clear_error();
        }
        return name;
    }

    std::string resolve_object_class_name(MemClient& client, int pid, uint64_t actor_addr) {
        if (gnames_addr_ == 0) {
            set_error("GNames address is 0");
            return {};
        }

        uint64_t class_ptr = 0;
        unsigned char buf[8]{};
        if (!client.read_mem(pid, actor_addr + SOT_UOBJECT_CLASS, 8, buf)) {
            set_error("Failed to read UObject::Class");
            return {};
        }
        memcpy(&class_ptr, buf, 8);
        if (!SotIsLikelyPointer(class_ptr)) {
            set_error("UObject::Class is not a valid pointer");
            return {};
        }

        static const uint64_t name_offsets[] = {
            SOT_UOBJECT_NAME,
            0x10,
        };

        for (uint64_t name_off : name_offsets) {
            uint32_t fname_index = 0;
            unsigned char ibuf[4]{};
            if (!client.read_mem(pid, class_ptr + name_off, 4, ibuf))
                continue;
            memcpy(&fname_index, ibuf, 4);

            std::string resolved = resolve(client, pid, fname_index);
            if (!resolved.empty())
                return resolved;
        }

        set_error("Failed to resolve UObject::Name");
        return {};
    }

    bool test_resolve(MemClient& client, int pid, uint64_t test_obj) {
        clear_error();
        std::string result = resolve_object_class_name(client, pid, test_obj);
        if (!result.empty()) {
            snprintf(test_result_, sizeof(test_result_), "%s", result.c_str());
            return true;
        }

        // Some probe objects can legitimately expose a zero name index on a
        // given build. Fall back to validating well-known early pool entries.
        static const uint32_t fallback_indices[] = {1, 2, 3, 4, 5, 6, 7, 8};
        for (uint32_t index : fallback_indices) {
            result = resolve(client, pid, index);
            if (!result.empty()) {
                snprintf(test_result_, sizeof(test_result_), "%s", result.c_str());
                return true;
            }
        }

        if (last_error_[0] == '\0')
            set_error("Resolved class name is empty");
        return false;
    }

    const char* get_test_result() const { return test_result_; }

private:
    uint64_t gnames_addr_{};
    char test_result_[64]{};
    char last_error_[160]{};
    std::unordered_map<uint32_t, std::string> cache_;

    void set_error(const char* msg) {
        snprintf(last_error_, sizeof(last_error_), "%s", msg ? msg : "Unknown GNames error");
    }

    void set_error_with_client(const char* msg, const MemClient& client) {
        const std::string& detail = client.last_error();
        if (!detail.empty()) {
            snprintf(last_error_, sizeof(last_error_), "%s (%s)", msg, detail.c_str());
            return;
        }
        set_error(msg);
    }

    void clear_error() {
        last_error_[0] = '\0';
    }

    static bool is_valid_ascii_name(const char* name, size_t max_scan = 32) {
        if (!name || name[0] == '\0')
            return false;

        bool has_alpha = false;
        for (size_t i = 0; i < max_scan; ++i) {
            unsigned char c = static_cast<unsigned char>(name[i]);
            if (c == '\0')
                return has_alpha;
            if (c < 32 || c > 126)
                return false;
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
                has_alpha = true;
        }
        return has_alpha;
    }

    std::string read_fname(MemClient& client, int pid, uint32_t index) {
        uint32_t chunk_idx = index / 16384;
        uint32_t entry_idx = index % 16384;

        uint64_t gnames_ptr = 0;
        if (!client.read_mem(pid, gnames_addr_, 8, (unsigned char*)&gnames_ptr)) {
            set_error_with_client("Failed to read GNames pool pointer", client);
            return {};
        }
        if (!SotIsLikelyPointer(gnames_ptr)) {
            set_error("GNames pool pointer is not valid");
            return {};
        }

        uint64_t chunk = 0;
        if (!client.read_mem(pid, gnames_ptr + chunk_idx * 8, 8, (unsigned char*)&chunk)) {
            set_error_with_client("Failed to read GNames chunk pointer", client);
            return {};
        }
        if (!SotIsLikelyPointer(chunk)) {
            char msg[160];
            snprintf(msg, sizeof(msg), "GNames chunk %u pointer is not valid", chunk_idx);
            set_error(msg);
            return {};
        }

        uint64_t entry = 0;
        if (!client.read_mem(pid, chunk + entry_idx * 8, 8, (unsigned char*)&entry)) {
            set_error_with_client("Failed to read GNames entry pointer", client);
            return {};
        }
        if (!SotIsLikelyPointer(entry)) {
            char msg[160];
            snprintf(msg, sizeof(msg), "GNames entry %u:%u pointer is not valid", chunk_idx, entry_idx);
            set_error(msg);
            return {};
        }

        char name[64]{};
        if (!client.read_mem(pid, entry, sizeof(name), (unsigned char*)name)) {
            set_error_with_client("Failed to read FNameEntry data", client);
            return {};
        }

        name[63] = '\0';

        static const size_t candidate_offsets[] = {0x10, 0x0C, 0x08};
        for (size_t offset : candidate_offsets) {
            if (offset >= sizeof(name))
                continue;

            char* candidate = name + offset;
            if (is_valid_ascii_name(candidate))
                return std::string(candidate);
        }

        set_error("FNameEntry string format is not recognized");
        return {};
    }
};

#endif
