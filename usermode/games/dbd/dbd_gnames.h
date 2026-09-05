#ifndef DBD_GNAMES_H
#define DBD_GNAMES_H

#include <cstdint>
#include <string>
#include <unordered_map>

#include "mem_client.h"
#include "logger.h"
#include "dbd_offsets.h"

/*
 * FNamePool layout (from UE reference):
 *   uintptr_t HeaderPad[2];   // 0x00 - 16 bytes
 *   uintptr_t Blocks[8192];   // 0x10 - block pointers
 *
 * FNameEntry layout:
 *   uint32_t ComparisonId;    // 0x00
 *   uint16_t Header;          // 0x04  (bit0 = wide, bits[1..15] = length)
 *   char     Name[];          // 0x06
 *
 * Entry address = Blocks[chunkIdx] + nameIdx * 4
 *   where chunkIdx = comparisonIndex >> 16
 *         nameIdx  = (uint16_t)comparisonIndex
 */

class DbdGNames {
public:
    DbdGNames() = default;

    void set_address(uint64_t gnames_addr) {
        if (gnames_addr != gnames_addr_) {
            gnames_addr_ = gnames_addr;
            pool_addr_ = 0;
            pool_resolved_ = false;
            cache_.clear();
        }
    }

    uint64_t address() const { return gnames_addr_; }

    std::string resolve(MemClient& client, int pid, uint32_t comparison_index) {
        if (pool_addr_ == 0 || comparison_index == 0)
            return {};

        auto it = cache_.find(comparison_index);
        if (it != cache_.end())
            return it->second;

        std::string name = read_fname(client, pid, comparison_index);
        if (!name.empty())
            cache_[comparison_index] = name;
        return name;
    }

    std::string resolve_object_class_name(MemClient& client, int pid, uint64_t actor_addr) {
        if (pool_addr_ == 0) return {};

        uint64_t class_ptr = 0;
        unsigned char buf[8]{};
        if (!client.read_mem(pid, actor_addr + DBD_OBJECT_CLASS, 8, buf))
            return {};
        memcpy(&class_ptr, buf, 8);
        if (!DbdIsLikelyPointer(class_ptr))
            return {};

        uint32_t fname_index = 0;
        unsigned char ibuf[4]{};
        if (!client.read_mem(pid, class_ptr + DBD_OBJECT_NAME, 4, ibuf))
            return {};
        memcpy(&fname_index, ibuf, 4);

        return resolve(client, pid, fname_index);
    }

    bool test_resolve(MemClient& client, int pid, uint64_t test_obj, uint64_t base_address = 0) {
        if (gnames_addr_ == 0) return false;
        base_address_ = base_address ? base_address : 0x140000000ULL;

        pool_addr_ = gnames_addr_;
        blocks_offset_ = DBD_GNAMES_BLOCKS_OFFSET;
        entry_stride_ = 4;
        diag_strategy_ = 1;
        std::string result = resolve_object_class_name(client, pid, test_obj);
        if (!result.empty() && is_valid_name(result)) {
            snprintf(test_result_, sizeof(test_result_), "%s", result.c_str());
            pool_resolved_ = true;
            LOG_INFO("GNames: strategy 1 (direct) OK -> %s", result.c_str());
            return true;
        }
        cache_.clear();

        // Fallback: brute-force scan around the known offset.
        {
            uint64_t class_ptr = 0;
            unsigned char cbuf[8]{};
            uint32_t fname_idx = 0;
            if (client.read_mem(pid, test_obj + DBD_OBJECT_CLASS, 8, cbuf)) {
                memcpy(&class_ptr, cbuf, 8);
                if (DbdIsLikelyPointer(class_ptr)) {
                    unsigned char ibuf[4]{};
                    if (client.read_mem(pid, class_ptr + DBD_OBJECT_NAME, 4, ibuf))
                        memcpy(&fname_idx, ibuf, 4);
                }
            }

            if (fname_idx > 0 && fname_idx < 0x100000) {
                uint32_t chunk_idx = fname_idx >> 16;
                uint16_t name_idx = static_cast<uint16_t>(fname_idx);

                uint64_t scan_base = gnames_addr_ - 0x2000000;
                uint64_t scan_end  = gnames_addr_ + 0x2000000;
                if (scan_base < base_address_) scan_base = base_address_;

                LOG_INFO("GNames scan: searching 0x%lX - 0x%lX for idx=%u", scan_base, scan_end, fname_idx);

                for (uint64_t try_addr = scan_base; try_addr < scan_end; try_addr += 0x8) {
                    uint64_t block_ptr = 0;
                    unsigned char bb[8]{};
                    if (!client.read_mem(pid, try_addr + blocks_offset_ + chunk_idx * 8, 8, bb))
                        continue;
                    memcpy(&block_ptr, bb, 8);
                    if (!DbdIsLikelyPointer(block_ptr))
                        continue;

                    uint64_t entry_addr = block_ptr + static_cast<uint64_t>(name_idx) * 4;
                    uint16_t header = 0;
                    unsigned char hbuf[2]{};
                    if (!client.read_mem(pid, entry_addr + 4, 2, hbuf))
                        continue;
                    memcpy(&header, hbuf, 2);

                    bool is_wide = (header & 1) != 0;
                    uint32_t len = header >> 1;
                    if (len < 3 || len > 64 || is_wide)
                        continue;

                    char nbuf[64]{};
                    if (!client.read_mem(pid, entry_addr + 6, len, (unsigned char*)nbuf))
                        continue;

                    bool valid = true;
                    for (uint32_t ci = 0; ci < len; ci++) {
                        if (nbuf[ci] < 32 || nbuf[ci] > 126) { valid = false; break; }
                    }
                    if (!valid) continue;

                    std::string found_name(nbuf, len);
                    if (found_name == "World" || found_name == "Level" || found_name == "Engine" ||
                        found_name == "GameInstance" || found_name == "Package") {
                        pool_addr_ = try_addr;
                        diag_strategy_ = 2;
                        cache_.clear();
                        std::string verify = resolve_object_class_name(client, pid, test_obj);
                        if (!verify.empty() && is_valid_name(verify)) {
                            snprintf(test_result_, sizeof(test_result_), "%s", verify.c_str());
                            pool_resolved_ = true;
                            uint64_t found_offset = try_addr - base_address_;
                            LOG_INFO("GNames: strategy 2 (scan) FOUND at 0x%lX (offset=0x%lX) -> %s",
                                try_addr, found_offset, verify.c_str());
                            return true;
                        }
                    }
                }
                LOG_INFO("GNames scan: no match found in range");
            }
        }

        pool_addr_ = 0;
        pool_resolved_ = false;
        diag_strategy_ = 0;
        collect_diagnostics(client, pid, test_obj);
        return false;
    }

    const char* get_test_result() const { return test_result_; }
    const char* get_diag() const { return diag_buf_; }
    int get_strategy() const { return diag_strategy_; }

private:
    uint64_t gnames_addr_{};
    uint64_t pool_addr_{};
    uint64_t base_address_{};
    bool pool_resolved_{};
    uint32_t blocks_offset_{DBD_GNAMES_BLOCKS_OFFSET};
    uint32_t entry_stride_{4};
    char test_result_[64]{};
    char diag_buf_[512]{};
    int diag_strategy_{};
    std::unordered_map<uint32_t, std::string> cache_;

    static bool is_valid_name(const std::string& s) {
        if (s.empty() || s.size() > 200) return false;
        for (char c : s) {
            if (c < 32 || c > 126) return false;
        }
        return (s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z');
    }

    std::string read_fname(MemClient& client, int pid, uint32_t index) {
        uint32_t chunk_idx = index >> 16;
        uint16_t name_idx = static_cast<uint16_t>(index);

        if (chunk_idx >= 8192)
            return {};

        uint64_t block_ptr = 0;
        unsigned char pbuf[8]{};
        if (!client.read_mem(pid, pool_addr_ + blocks_offset_ + chunk_idx * 8, 8, pbuf))
            return {};
        memcpy(&block_ptr, pbuf, 8);
        if (!DbdIsLikelyPointer(block_ptr))
            return {};

        uint64_t entry_addr = block_ptr + static_cast<uint64_t>(name_idx) * entry_stride_;

        uint16_t header = 0;
        unsigned char hbuf[2]{};
        if (!client.read_mem(pid, entry_addr + 4, 2, hbuf))
            return {};
        memcpy(&header, hbuf, 2);

        bool is_wide = (header & 1) != 0;
        uint32_t len = header >> 1;

        if (len == 0 || len > 256 || is_wide)
            return {};

        std::string name(len, '\0');
        if (!client.read_mem(pid, entry_addr + 6, len, (unsigned char*)name.data()))
            return {};

        for (auto& c : name) {
            if (c < 32 || c > 126) return {};
        }
        return name;
    }

    void collect_diagnostics(MemClient& client, int pid, uint64_t test_obj) {
        int n = 0;

        unsigned char raw[32]{};
        client.read_mem(pid, gnames_addr_, 32, raw);
        n += snprintf(diag_buf_ + n, sizeof(diag_buf_) - n,
            "GNames@0x%lX raw: %02X%02X%02X%02X %02X%02X%02X%02X | %02X%02X%02X%02X %02X%02X%02X%02X\n",
            gnames_addr_,
            raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7],
            raw[8], raw[9], raw[10], raw[11], raw[12], raw[13], raw[14], raw[15]);

        uint64_t p1 = 0, p2 = 0;
        memcpy(&p1, raw, 8);
        memcpy(&p2, raw + 8, 8);
        n += snprintf(diag_buf_ + n, sizeof(diag_buf_) - n,
            "  [0]=0x%lX ptr?%d  [8]=0x%lX ptr?%d\n",
            p1, DbdIsLikelyPointer(p1) ? 1 : 0,
            p2, DbdIsLikelyPointer(p2) ? 1 : 0);

        uint64_t first_block = 0;
        unsigned char bb[8]{};
        if (client.read_mem(pid, gnames_addr_ + blocks_offset_, 8, bb)) {
            memcpy(&first_block, bb, 8);
            n += snprintf(diag_buf_ + n, sizeof(diag_buf_) - n,
                "  Block[0]@+0x%X=0x%lX ptr?%d\n",
                blocks_offset_, first_block, DbdIsLikelyPointer(first_block) ? 1 : 0);
        }

        uint64_t class_ptr = 0;
        unsigned char cbuf[8]{};
        if (client.read_mem(pid, test_obj + DBD_OBJECT_CLASS, 8, cbuf)) {
            memcpy(&class_ptr, cbuf, 8);
            n += snprintf(diag_buf_ + n, sizeof(diag_buf_) - n,
                "  TestObj class=0x%lX ptr?%d\n",
                class_ptr, DbdIsLikelyPointer(class_ptr) ? 1 : 0);

            if (DbdIsLikelyPointer(class_ptr)) {
                uint32_t fname_idx = 0;
                unsigned char ibuf[4]{};
                if (client.read_mem(pid, class_ptr + DBD_OBJECT_NAME, 4, ibuf)) {
                    memcpy(&fname_idx, ibuf, 4);
                    n += snprintf(diag_buf_ + n, sizeof(diag_buf_) - n,
                        "  FName idx=%u (chunk=%u name=%u)\n",
                        fname_idx, fname_idx >> 16, fname_idx & 0xFFFF);
                }
            }
        }

        LOG_ERR("GNames diagnostics:\n%s", diag_buf_);
    }
};

#endif
