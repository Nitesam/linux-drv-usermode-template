#ifndef SOT_GNAMES_H
#define SOT_GNAMES_H

#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

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
            class_chain_cache_.clear();
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
        if (name.empty()) {
            // Fallback to offline dump when pool layout drifts between builds.
            name = resolve_from_dump(comparison_index);
        }
        if (!name.empty()) {
            cache_[comparison_index] = name;
            clear_error();
        }
        return name;
    }

    std::string resolve_object_class_name(MemClient& client, int pid, uint64_t actor_addr) {
        std::vector<std::string> chain;
        if (!resolve_object_class_chain(client, pid, actor_addr, chain))
            return {};

        for (const std::string& name : chain) {
            if (SotClassifyActor(name) != ESotActorType::Unknown)
                return name;
        }

        for (const std::string& name : chain) {
            if (!SotIsProbablyBrokenClassName(name))
                return name;
        }

        set_error("Resolved class chain has no usable names");
        return {};
    }

    std::string resolve_object_name(MemClient& client, int pid, uint64_t object_addr) {
        if (gnames_addr_ == 0) {
            set_error("GNames address is 0");
            return {};
        }
        if (!SotIsLikelyPointer(object_addr)) {
            set_error("Object pointer is not valid");
            return {};
        }

        std::string best_name;
        int best_score = -100000;

        static const uint32_t name_offsets[] = {
            SOT_UOBJECT_NAME,
            0x18,
            0x10,
            0x28,
        };

        for (uint32_t name_off : name_offsets) {
            unsigned char ibuf[8]{};
            if (!client.read_mem(pid, object_addr + name_off, sizeof(ibuf), ibuf))
                continue;

            uint32_t words[2]{};
            memcpy(&words[0], ibuf, 4);
            memcpy(&words[1], ibuf + 4, 4);

            for (int word = 0; word < 2; ++word) {
                std::string resolved = resolve(client, pid, words[word]);
                if (resolved.empty())
                    continue;

                int score = score_fname_decoding(resolved);
                if (resolved == "None")
                    score -= 48;
                if (name_off == SOT_UOBJECT_NAME && word == 0)
                    score += 16;
                else if (name_off == SOT_UOBJECT_NAME && word == 1)
                    score -= 12;
                else
                    score -= 10;

                if (score > best_score) {
                    best_score = score;
                    best_name = resolved;
                }
            }
        }

        if (!best_name.empty() && best_score > 20)
            return best_name;

        set_error("Failed to resolve UObject::Name");
        return {};
    }

    void debug_object_class_probe(MemClient& client, int pid, uint64_t object_addr,
                                  char* out, size_t out_size) {
        if (!out || out_size == 0)
            return;
        out[0] = '\0';

        if (!SotIsLikelyPointer(object_addr)) {
            snprintf(out, out_size, "object=invalid");
            return;
        }

        uint64_t class_ptr = 0;
        if (!client.read_mem(pid, object_addr + SOT_UOBJECT_CLASS, 8, (unsigned char*)&class_ptr)) {
            snprintf(out, out_size, "class=read-fail");
            return;
        }
        if (!SotIsLikelyPointer(class_ptr)) {
            snprintf(out, out_size, "class=0x%lX invalid", class_ptr);
            return;
        }

        struct ProbeOffset {
            uint32_t offset;
            const char* label;
        };

        static const ProbeOffset offsets[] = {
            {SOT_UOBJECT_NAME, "n20"},
            {0x18, "n18"},
            {0x10, "n10"},
            {0x28, "n28"},
        };

        size_t used = (size_t)snprintf(out, out_size, "class=0x%lX", class_ptr);
        for (const ProbeOffset& probe : offsets) {
            if (used + 16 >= out_size)
                break;

            unsigned char ibuf[8]{};
            if (!client.read_mem(pid, class_ptr + probe.offset, sizeof(ibuf), ibuf))
                continue;

            uint32_t w0 = 0;
            uint32_t w1 = 0;
            memcpy(&w0, ibuf, 4);
            memcpy(&w1, ibuf + 4, 4);

            std::string s0 = resolve(client, pid, w0);
            std::string s1 = resolve(client, pid, w1);
            if (s0.size() > 24)
                s0 = s0.substr(0, 24);
            if (s1.size() > 24)
                s1 = s1.substr(0, 24);

            int written = snprintf(out + used, out_size - used,
                                   " %s=%u/%u:%s|%s",
                                   probe.label,
                                   w0,
                                   w1,
                                   s0.empty() ? "-" : s0.c_str(),
                                   s1.empty() ? "-" : s1.c_str());
            if (written < 0)
                break;
            used += (size_t)written;
            if (used >= out_size)
                break;
        }
    }

    bool resolve_object_class_chain(MemClient& client, int pid, uint64_t object_addr,
                                    std::vector<std::string>& out_chain) {
        out_chain.clear();

        if (gnames_addr_ == 0) {
            set_error("GNames address is 0");
            return false;
        }
        if (!SotIsLikelyPointer(object_addr)) {
            set_error("Object pointer is not valid");
            return false;
        }

        uint64_t class_ptr = 0;
        unsigned char buf[8]{};
        if (!client.read_mem(pid, object_addr + SOT_UOBJECT_CLASS, 8, buf)) {
            set_error("Failed to read UObject::Class");
            return false;
        }
        memcpy(&class_ptr, buf, 8);
        if (!SotIsLikelyPointer(class_ptr)) {
            set_error("UObject::Class is not a valid pointer");
            return false;
        }

        auto cached = class_chain_cache_.find(class_ptr);
        if (cached != class_chain_cache_.end()) {
            out_chain = cached->second;
            if (!out_chain.empty()) {
                clear_error();
                return true;
            }
        }

        std::vector<uint64_t> visited;
        uint64_t current = class_ptr;
        for (int depth = 0; depth < 16 && SotIsLikelyPointer(current); ++depth) {
            bool seen = false;
            for (uint64_t prev : visited) {
                if (prev == current) {
                    seen = true;
                    break;
                }
            }
            if (seen)
                break;
            visited.push_back(current);

            std::string name = resolve_object_name(client, pid, current);
            if (!name.empty())
                out_chain.push_back(name);

            uint64_t super_ptr = 0;
            unsigned char sbuf[8]{};
            if (!client.read_mem(pid, current + SOT_USTRUCT_SUPER_STRUCT, 8, sbuf))
                break;
            memcpy(&super_ptr, sbuf, 8);
            if (!SotIsLikelyPointer(super_ptr))
                break;
            current = super_ptr;
        }

        if (!out_chain.empty()) {
            class_chain_cache_[class_ptr] = out_chain;
            clear_error();
            return true;
        }

        set_error("Class chain is empty");
        return false;
    }

    bool test_resolve(MemClient& client, int pid, uint64_t test_obj) {
        clear_error();
        std::string result = resolve_object_class_name(client, pid, test_obj);
        if (!result.empty() && !SotIsProbablyBrokenClassName(result)) {
            snprintf(test_result_, sizeof(test_result_), "%s", result.c_str());
            return true;
        }

        if (!result.empty())
            set_error("Resolved class name looks invalid");

        if (last_error_[0] == '\0')
            set_error("Resolved class name is empty");
        return false;
    }

    const char* get_test_result() const { return test_result_; }

private:
    uint64_t gnames_addr_{};
    char test_result_[64]{};
    char last_error_[160]{};
    bool dump_loaded_{};
    bool dump_available_{};
    std::unordered_map<uint32_t, std::string> cache_;
    std::unordered_map<uint64_t, std::vector<std::string>> class_chain_cache_;
    std::unordered_map<uint32_t, std::string> dump_names_;

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

    static bool is_space_char(char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    }

    static std::string trim_copy(const std::string& s) {
        size_t start = 0;
        while (start < s.size() && is_space_char(s[start]))
            ++start;

        size_t end = s.size();
        while (end > start && is_space_char(s[end - 1]))
            --end;

        return s.substr(start, end - start);
    }

    static bool parse_dump_line(const std::string& line, uint32_t& out_index, std::string& out_name) {
        size_t lbr = line.find('[');
        size_t rbr = line.find(']', lbr == std::string::npos ? 0 : lbr + 1);
        if (lbr == std::string::npos || rbr == std::string::npos || rbr <= lbr + 1)
            return false;

        uint32_t value = 0;
        bool has_digit = false;

        for (size_t i = lbr + 1; i < rbr; ++i) {
            char c = line[i];
            if (c == '-')
                return false;
            if (c < '0' || c > '9')
                return false;
            has_digit = true;
            value = value * 10u + static_cast<uint32_t>(c - '0');
        }
        if (!has_digit)
            return false;

        std::string name = trim_copy(line.substr(rbr + 1));
        if (name.empty())
            return false;

        out_index = value;
        out_name = name;
        return true;
    }

    void ensure_dump_loaded() {
        if (dump_loaded_)
            return;

        dump_loaded_ = true;

        static const char* candidate_paths[] = {
            "games/sot/FNames.txt",
            "./games/sot/FNames.txt",
            "../games/sot/FNames.txt",
            "../../usermode/games/sot/FNames.txt",
            "/home/nitesam/Desktop/Work/FEDORE_KRN_DRV/usermode/games/sot/FNames.txt",
        };

        for (const char* path : candidate_paths) {
            if (!path)
                continue;

            std::ifstream in(path);
            if (!in.good())
                continue;

            std::unordered_map<uint32_t, std::string> parsed;
            parsed.reserve(32768);

            std::string line;
            while (std::getline(in, line)) {
                uint32_t index = 0;
                std::string name;
                if (!parse_dump_line(line, index, name))
                    continue;
                parsed[index] = std::move(name);
            }

            if (parsed.size() < 100)
                continue;

            dump_names_.swap(parsed);
            dump_available_ = true;
            break;
        }
    }

    std::string resolve_from_dump(uint32_t raw_index) {
        ensure_dump_loaded();
        if (!dump_available_)
            return {};

        uint32_t index_candidates[4]{};
        size_t index_count = build_fname_index_candidates(raw_index, index_candidates);
        for (size_t i = 0; i < index_count; ++i) {
            auto it = dump_names_.find(index_candidates[i]);
            if (it != dump_names_.end() && !it->second.empty())
                return it->second;
        }
        return {};
    }

    static void append_unique_u32(uint32_t value, uint32_t* out, size_t& count, size_t max_count) {
        if (value == 0 || count >= max_count)
            return;
        for (size_t i = 0; i < count; ++i) {
            if (out[i] == value)
                return;
        }
        out[count++] = value;
    }

    static size_t build_fname_index_candidates(uint32_t raw, uint32_t out[4]) {
        size_t count = 0;
        append_unique_u32(raw, out, count, 4);
        append_unique_u32(raw & 0x1FFFFFFF, out, count, 4); // 13 block bits + 16 offset bits
        append_unique_u32(raw & 0x3FFFFFFF, out, count, 4);
        append_unique_u32(raw & 0x00FFFFFF, out, count, 4);
        return count;
    }

    static bool is_identifier_name_char(char c) {
        return (c >= 'A' && c <= 'Z') ||
               (c >= 'a' && c <= 'z') ||
               (c >= '0' && c <= '9') ||
               c == '_' || c == '.' || c == '-' || c == ':';
    }

    static bool is_unreal_identifier_like(const std::string& name) {
        if (name.empty() || name.size() > 128)
            return false;

        bool has_alpha = false;
        for (char c : name) {
            if (!is_identifier_name_char(c))
                return false;
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
                has_alpha = true;
        }
        return has_alpha;
    }

    static int score_class_name_candidate(const std::string& name) {
        if (name.empty())
            return -1000;

        int score = 0;

        if (!SotIsProbablyBrokenClassName(name))
            score += 4;
        else
            score -= 8;

        if (SotClassifyActor(name) != ESotActorType::Unknown)
            score += 32;

        if (SotClassContainsAny(name, {
                "BP_",
                "_C",
                "Athena",
                "Actor",
                "Character",
                "Pawn",
                "Ship",
                "Skeleton",
                "Mermaid",
                "Rowboat",
            }))
            score += 6;

        if (SotClassContainsAny(name, {
                "Property",
                "Function",
                "Struct",
                "Enum",
                "Delegate",
                "Package",
                "Default__",
            }))
            score -= 10;

        if (name == "None")
            score -= 16;

        return score;
    }

    static int score_fname_decoding(const std::string& name) {
        if (name.empty())
            return -1000;

        int score = score_class_name_candidate(name);

        if (is_unreal_identifier_like(name))
            score += 24;
        else
            score -= 80;

        if (SotClassContainsAny(name, {
                ",",
                "=",
                "/",
                "\\",
                " ",
                "\t",
                "MinLODSize",
                "MaxLODSize",
                "MinMagFilter",
                "TextureLODGroups",
            }))
            score -= 160;

        if (SotClassContainsAny(name, {
                "Actor",
                "Pawn",
                "Character",
                "Athena",
                "BP_",
                "_C",
                "Ship",
                "Chest",
                "Rowboat",
                "Skeleton",
            }))
            score += 24;

        if (name == "None")
            score += 8;

        return score;
    }

    std::string decode_name_entry_layout(MemClient& client,
                                         int pid,
                                         uint64_t entry_base,
                                         uint32_t header_offset,
                                         uint32_t text_offset) {
        uint16_t header = 0;
        unsigned char hbuf[2]{};
        if (!client.read_mem(pid, entry_base + header_offset, 2, hbuf))
            return {};
        memcpy(&header, hbuf, 2);

        const bool is_wide = (header & 1) != 0;
        const uint32_t len = header >> 1;
        if (len == 0 || len > 256)
            return {};

        if (!is_wide) {
            std::string name(len, '\0');
            if (!client.read_mem(pid, entry_base + text_offset, len,
                                 (unsigned char*)name.data()))
                return {};

            if (!is_valid_ascii_name(name.c_str(), len))
                return {};
            return name;
        }

        std::vector<uint16_t> wide(len);
        if (!client.read_mem(pid, entry_base + text_offset, len * 2,
                             (unsigned char*)wide.data()))
            return {};

        std::string name;
        name.reserve(len);
        for (uint32_t i = 0; i < len; ++i) {
            uint16_t wc = wide[i];
            if (wc == 0)
                break;
            if (wc < 32 || wc > 126)
                return {};
            name.push_back(static_cast<char>(wc));
        }

        if (!is_valid_ascii_name(name.c_str(), name.size()))
            return {};
        return name;
    }

    std::string decode_legacy_name_entry(MemClient& client,
                                         int pid,
                                         uint64_t chunk_ptr,
                                         uint32_t entry_idx,
                                         uint32_t chunk_entry_stride) {
        uint64_t entry_ptr = 0;
        if (!client.read_mem(pid,
                             chunk_ptr + static_cast<uint64_t>(entry_idx) * chunk_entry_stride,
                             8,
                             (unsigned char*)&entry_ptr))
            return {};
        if (!SotIsLikelyPointer(entry_ptr))
            return {};

        char name[96]{};
        if (!client.read_mem(pid, entry_ptr, sizeof(name), (unsigned char*)name))
            return {};

        static const size_t candidate_offsets[] = {0x10, 0x0C, 0x08, 0x00};
        for (size_t offset : candidate_offsets) {
            if (offset >= sizeof(name))
                continue;
            if (is_valid_ascii_name(name + offset, sizeof(name) - offset))
                return std::string(name + offset);
        }

        return {};
    }

    std::string read_fname(MemClient& client, int pid, uint32_t index) {
        uint64_t pool_candidates[2] = {gnames_addr_, 0};
        client.read_mem(pid, gnames_addr_, 8, (unsigned char*)&pool_candidates[1]);

        bool any_block_read = false;
        bool any_block_valid = false;
        std::string best_name;
        int best_score = -100000;

        struct EntryLayout {
            uint32_t stride;
            uint32_t header_offset;
            uint32_t text_offset;
        };

        // Try both common UE name-pool entry encodings.
        static const EntryLayout layouts[] = {
            {2, 0, 2},
            {2, 2, 4},
            {4, 0, 2},
            {4, 4, 6},
        };

        struct IndexSplit {
            uint32_t block_shift;
            uint32_t entry_mask;
            uint32_t legacy_chunk_entry_stride;
        };

        // Handle modern (16-bit offset) and old chunked (14-bit entry index) encodings.
        static const IndexSplit split_modes[] = {
            {16, 0xFFFF, 8},
            {14, 0x3FFF, 8},
        };

        static const uint32_t blocks_offsets[] = {
            SOT_GNAMES_BLOCKS_OFFSET,
            0,
        };

        uint32_t index_candidates[4]{};
        size_t index_candidate_count = build_fname_index_candidates(index, index_candidates);

        for (size_t idx_i = 0; idx_i < index_candidate_count; ++idx_i) {
            uint32_t candidate_index = index_candidates[idx_i];

            for (const IndexSplit& split : split_modes) {
                uint32_t block_idx = candidate_index >> split.block_shift;
                uint32_t entry_idx = candidate_index & split.entry_mask;

                for (uint64_t pool_base : pool_candidates) {
                    if (pool_base == 0)
                        continue;

                    for (uint32_t blocks_off : blocks_offsets) {
                        uint64_t block_ptr = 0;
                        if (!client.read_mem(pid,
                                             pool_base + blocks_off + static_cast<uint64_t>(block_idx) * 8,
                                             8,
                                             (unsigned char*)&block_ptr))
                        {
                            continue;
                        }
                        any_block_read = true;

                        if (!SotIsLikelyPointer(block_ptr))
                            continue;
                        any_block_valid = true;

                        for (const EntryLayout& layout : layouts) {
                            uint64_t entry_base = block_ptr + static_cast<uint64_t>(entry_idx) * layout.stride;
                            std::string decoded = decode_name_entry_layout(client,
                                                                           pid,
                                                                           entry_base,
                                                                           layout.header_offset,
                                                                           layout.text_offset);
                            if (!decoded.empty()) {
                                int score = score_fname_decoding(decoded);
                                if (score > best_score) {
                                    best_score = score;
                                    best_name = decoded;
                                }
                            }
                        }

                        std::string legacy = decode_legacy_name_entry(client,
                                                                      pid,
                                                                      block_ptr,
                                                                      entry_idx,
                                                                      split.legacy_chunk_entry_stride);
                        if (!legacy.empty()) {
                            int score = score_fname_decoding(legacy);
                            if (score > best_score) {
                                best_score = score;
                                best_name = legacy;
                            }
                        }
                    }
                }
            }
        }

        if (!best_name.empty() && best_score > -40)
            return best_name;

        if (!any_block_read) {
            set_error_with_client("Failed to read GNames block pointer", client);
            return {};
        }
        if (!any_block_valid) {
            char msg[160];
            snprintf(msg, sizeof(msg), "GNames block pointer is not valid for index %u", index);
            set_error(msg);
            return {};
        }

        set_error("FNameEntry string format is not recognized");
        return {};
    }
};

#endif
