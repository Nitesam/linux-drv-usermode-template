#ifndef DBD_GNAMES_H
#define DBD_GNAMES_H

#include <cstdint>
#include <string>
#include <unordered_map>

#include "mem_client.h"
#include "logger.h"
#include "dbd_offsets.h"

class DbdGNames {
public:
    DbdGNames() = default;

    void set_address(uint64_t gnames_addr) {
        if (gnames_addr != gnames_addr_) {
            gnames_addr_ = gnames_addr;
            cache_.clear();
        }
    }

    uint64_t address() const { return gnames_addr_; }

    std::string resolve(MemClient& client, int pid, uint32_t comparison_index) {
        if (gnames_addr_ == 0 || comparison_index == 0)
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
        if (gnames_addr_ == 0) return {};

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

    bool test_resolve(MemClient& client, int pid, uint64_t test_obj) {
        std::string result = resolve_object_class_name(client, pid, test_obj);
        if (!result.empty()) {
            LOG_CHAIN("[GN] GNames OK at 0x%lX (test='%s')", gnames_addr_, result.c_str());
            return true;
        }
        return false;
    }

private:
    uint64_t gnames_addr_{};
    std::unordered_map<uint32_t, std::string> cache_;

    std::string read_fname(MemClient& client, int pid, uint32_t index) {
        uint32_t block_idx = index >> 16;
        uint32_t byte_offset = (index & 0xFFFF) * 4;

        uint64_t block_ptr = 0;
        unsigned char pbuf[8]{};
        if (!client.read_mem(pid, gnames_addr_ + DBD_GNAMES_BLOCKS_OFFSET + block_idx * 8, 8, pbuf))
            return {};
        memcpy(&block_ptr, pbuf, 8);
        if (!DbdIsLikelyPointer(block_ptr))
            return {};

        uint16_t header = 0;
        unsigned char hbuf[2]{};
        if (!client.read_mem(pid, block_ptr + byte_offset + 4, 2, hbuf))
            return {};
        memcpy(&header, hbuf, 2);

        bool is_wide = (header & 1) != 0;
        uint32_t len = header >> 1;

        if (len == 0 || len > 256 || is_wide)
            return {};

        std::string name(len, '\0');
        if (!client.read_mem(pid, block_ptr + byte_offset + 6, len, (unsigned char*)name.data()))
            return {};

        for (auto& c : name) {
            if (c < 32 || c > 126) return {};
        }
        return name;
    }
};

#endif
