#ifndef MEM_CLIENT_H
#define MEM_CLIENT_H

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <fstream>
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

    bool open_driver(const char *dev_name = MEMRW_DEVICE_NAME) {
        int major = find_major_number(dev_name);
        if (major > 0) {
            char tmp_path[64];
            snprintf(tmp_path, sizeof(tmp_path), "/tmp/.%s_%d", dev_name, getpid());

            dev_t dev = makedev(major, 0);
            if (mknod(tmp_path, S_IFCHR | 0600, dev) == 0) {
                fd_ = open(tmp_path, O_RDWR);
                unlink(tmp_path);
                if (fd_ >= 0) {
                    last_error_.clear();
                    return true;
                }
            }
            unlink(tmp_path);
        }

        std::string path = std::string("/dev/") + dev_name;
        fd_ = open(path.c_str(), O_RDWR);
        if (fd_ < 0) {
            last_error_ = std::string("Cannot open device: ") + strerror(errno);
            return false;
        }
        last_error_.clear();
        return true;
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

    const std::string &last_error() const { return last_error_; }

private:
    int         fd_;
    std::string last_error_;

    static int find_major_number(const char *name) {
        std::ifstream f("/proc/devices");
        if (!f.is_open()) return -1;

        std::string line;
        bool in_char_section = false;
        while (std::getline(f, line)) {
            if (line == "Character devices:")
                { in_char_section = true; continue; }
            if (line == "Block devices:")
                break;
            if (!in_char_section) continue;

            int major = 0;
            char dev_name[128] = {};
            if (sscanf(line.c_str(), " %d %127s", &major, dev_name) == 2) {
                if (strcmp(dev_name, name) == 0)
                    return major;
            }
        }
        return -1;
    }
};

#endif