#ifndef MEM_CLIENT_H
#define MEM_CLIENT_H

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <errno.h>

#include "memrw_ioctl.h"

class MemClient {
public:
    MemClient() : fd_(-1) {}
    ~MemClient() { close_driver(); }

    bool open_driver() {
        fd_ = open(MEMRW_HIDDEN_NODE_PATH, O_RDWR);
        if (fd_ >= 0) {
            last_error_.clear();
            return true;
        }

        last_error_ = std::string("Cannot open ") + MEMRW_HIDDEN_NODE_PATH
                    + ": " + strerror(errno);
        return false;
    }

    void close_driver() {
        if (fd_ >= 0) {
            close(fd_);
            fd_ = -1;
        }
    }

    bool is_open() const { return fd_ >= 0; }

    bool read_mem(int pid, unsigned long addr, size_t size, unsigned char *out_buf) {
        if (fd_ < 0) {
            last_error_ = "Driver not open";
            return false;
        }
        if (size == 0 || size > MEMRW_BUF_SIZE) {
            last_error_ = "Invalid size (must be 1-4096)";
            return false;
        }

        struct mem_request req;
        memset(&req, 0, sizeof(req));
        req.pid  = pid;
        req.addr = addr;
        req.size = size;

        int ret = ioctl(fd_, IOCTL_MEM_READ, &req);
        if (ret < 0) {
            last_error_ = std::string("IOCTL_MEM_READ failed: ") + strerror(errno);
            return false;
        }

        memcpy(out_buf, req.buf, size);
        last_error_.clear();
        return true;
    }

    bool write_mem(int pid, unsigned long addr, size_t size, const unsigned char *in_buf) {
        if (fd_ < 0) {
            last_error_ = "Driver not open";
            return false;
        }
        if (size == 0 || size > MEMRW_BUF_SIZE) {
            last_error_ = "Invalid size (must be 1-4096)";
            return false;
        }

        struct mem_request req;
        memset(&req, 0, sizeof(req));
        req.pid  = pid;
        req.addr = addr;
        req.size = size;
        memcpy(req.buf, in_buf, size);

        int ret = ioctl(fd_, IOCTL_MEM_WRITE, &req);
        if (ret < 0) {
            last_error_ = std::string("IOCTL_MEM_WRITE failed: ") + strerror(errno);
            return false;
        }

        last_error_.clear();
        return true;
    }

    bool hide_self() {
        if (fd_ < 0) {
            last_error_ = "Driver not open";
            return false;
        }

        int pid = getpid();
        int ret = ioctl(fd_, IOCTL_HIDE_PID, &pid);
        if (ret < 0) {
            last_error_ = std::string("IOCTL_HIDE_PID failed: ") + strerror(errno);
            return false;
        }

        last_error_.clear();
        return true;
    }

    bool move_mouse(int dx, int dy) {
        if (fd_ < 0) {
            last_error_ = "Driver not open";
            return false;
        }

        struct mouse_request mreq;
        mreq.dx = dx;
        mreq.dy = dy;

        int ret = ioctl(fd_, IOCTL_MOUSE_MOVE, &mreq);
        if (ret < 0) {
            last_error_ = std::string("IOCTL_MOUSE_MOVE failed: ") + strerror(errno);
            return false;
        }

        last_error_.clear();
        return true;
    }

    bool unhide_module() {
        if (fd_ < 0) {
            last_error_ = "Driver not open";
            return false;
        }

        int ret = ioctl(fd_, IOCTL_UNHIDE_MODULE);
        if (ret < 0) {
            last_error_ = std::string("IOCTL_UNHIDE_MODULE failed: ") + strerror(errno);
            return false;
        }

        last_error_.clear();
        return true;
    }

    int find_pid(const char *name) {
        if (fd_ < 0) {
            last_error_ = "Driver not open";
            return -1;
        }

        struct pid_request preq;
        memset(&preq, 0, sizeof(preq));
        strncpy(preq.name, name, sizeof(preq.name) - 1);

        int ret = ioctl(fd_, IOCTL_FIND_PID, &preq);
        if (ret < 0) {
            last_error_ = std::string("IOCTL_FIND_PID failed: ") + strerror(errno);
            return -1;
        }

        last_error_.clear();
        return preq.pid;
    }

    uint64_t get_base_address(int pid, const char *name = "") {
        if (fd_ < 0) {
            last_error_ = "Driver not open";
            return 0;
        }

        struct base_addr_request breq;
        memset(&breq, 0, sizeof(breq));
        breq.pid = pid;
        strncpy(breq.name, name, sizeof(breq.name) - 1);

        int ret = ioctl(fd_, IOCTL_GET_BASE_ADDR, &breq);
        if (ret < 0) {
            last_error_ = std::string("IOCTL_GET_BASE_ADDR failed: ") + strerror(errno);
            return 0;
        }

        last_error_.clear();
        return breq.addr;
    }

    bool sig_scan_patch(int pid, unsigned long start_addr, size_t scan_size,
                        const unsigned char *pattern,
                        const unsigned char *mask,
                        size_t pattern_size,
                        const unsigned char *patch,
                        size_t patch_size,
                        uint64_t *found_addr = nullptr,
                        size_t patch_offset = 0,
                        bool use_avx2 = true,
                        bool *avx2_used = nullptr) {
        if (fd_ < 0) {
            last_error_ = "Driver not open";
            return false;
        }
        if (!pattern || !mask || !patch ||
            pattern_size == 0 || pattern_size > MEMRW_SIG_MAX ||
            patch_size == 0 || patch_size > MEMRW_PATCH_MAX ||
            scan_size < pattern_size) {
            last_error_ = "Invalid sig scan/patch request";
            return false;
        }

        struct sig_patch_request sreq;
        memset(&sreq, 0, sizeof(sreq));
        sreq.pid = pid;
        sreq.start_addr = start_addr;
        sreq.scan_size = scan_size;
        sreq.pattern_size = pattern_size;
        sreq.patch_size = patch_size;
        sreq.patch_offset = patch_offset;
        sreq.flags = use_avx2 ? MEMRW_SIG_FLAG_AVX2 : 0;
        memcpy(sreq.pattern, pattern, pattern_size);
        memcpy(sreq.mask, mask, pattern_size);
        memcpy(sreq.patch, patch, patch_size);

        int ret = ioctl(fd_, IOCTL_SIG_SCAN_PATCH, &sreq);
        if (ret < 0) {
            last_error_ = std::string("IOCTL_SIG_SCAN_PATCH failed: ") + strerror(errno);
            return false;
        }

        if (found_addr)
            *found_addr = sreq.found_addr;
        if (avx2_used)
            *avx2_used = sreq.avx2_used != 0;

        last_error_.clear();
        return true;
    }

    bool sig_scan_patch_ida(int pid, unsigned long start_addr, size_t scan_size,
                            const char *ida_pattern,
                            const unsigned char *patch,
                            size_t patch_size,
                            uint64_t *found_addr = nullptr,
                            size_t patch_offset = 0,
                            bool use_avx2 = true,
                            bool *avx2_used = nullptr) {
        std::vector<unsigned char> pattern;
        std::vector<unsigned char> mask;

        if (!parse_ida_pattern(ida_pattern, pattern, mask)) {
            last_error_ = "Invalid IDA signature pattern";
            return false;
        }

        return sig_scan_patch(pid, start_addr, scan_size,
                              pattern.data(), mask.data(), pattern.size(),
                              patch, patch_size, found_addr, patch_offset,
                              use_avx2, avx2_used);
    }

    const std::string &last_error() const { return last_error_; }

private:
    static int hex_value(char c) {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    }

    static bool parse_ida_pattern(const char *text,
                                  std::vector<unsigned char> &pattern,
                                  std::vector<unsigned char> &mask) {
        const char *p = text;

        if (!p)
            return false;

        while (*p) {
            while (*p && std::isspace(static_cast<unsigned char>(*p)))
                ++p;

            if (!*p)
                break;

            if (*p == '?') {
                pattern.push_back(0);
                mask.push_back(0);
                ++p;
                if (*p == '?')
                    ++p;
            } else {
                int hi = hex_value(p[0]);
                int lo = p[1] ? hex_value(p[1]) : -1;
                if (hi < 0 || lo < 0)
                    return false;

                pattern.push_back(static_cast<unsigned char>((hi << 4) | lo));
                mask.push_back(0xff);
                p += 2;
            }

            if (pattern.size() > MEMRW_SIG_MAX)
                return false;

            if (*p && !std::isspace(static_cast<unsigned char>(*p)) && *p != '?')
                return false;
        }

        return !pattern.empty();
    }

    int         fd_;
    std::string last_error_;
};

#endif
