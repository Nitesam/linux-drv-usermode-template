#ifndef WEB_RADAR_H
#define WEB_RADAR_H

#include <cstdio>
#include <cstring>
#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <vector>
#include <mutex>
#include <chrono>
#include <algorithm>

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h>

class WebRadarServer {
public:
    using StateProvider = std::function<std::string()>;

    WebRadarServer() = default;
    ~WebRadarServer() { stop(); }

    void set_state_provider(StateProvider fn) { state_fn_ = std::move(fn); }
    void set_page(const char* html, size_t len) { page_html_ = std::string(html, len); }
    void set_page(const std::string& html) { page_html_ = html; }

    bool start(int port = 30120) {
        if (running_.load()) return true;
        port_ = port;

        server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd_ < 0) return false;

        int opt = 1;
        setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(server_fd_); server_fd_ = -1;
            return false;
        }
        if (listen(server_fd_, 8) < 0) {
            close(server_fd_); server_fd_ = -1;
            return false;
        }

        running_.store(true);
        accept_thread_ = std::thread([this]() { accept_loop(); });
        return true;
    }

    void stop() {
        running_.store(false);
        if (server_fd_ >= 0) {
            shutdown(server_fd_, SHUT_RDWR);
            close(server_fd_);
            server_fd_ = -1;
        }
        if (accept_thread_.joinable())
            accept_thread_.join();

        std::lock_guard<std::mutex> lk(clients_mtx_);
        for (int fd : sse_clients_) {
            shutdown(fd, SHUT_RDWR);
            close(fd);
        }
        sse_clients_.clear();
    }

    bool is_running() const { return running_.load(); }
    int port() const { return port_; }
    int client_count() {
        std::lock_guard<std::mutex> lk(clients_mtx_);
        return (int)sse_clients_.size();
    }

    void broadcast_tick() {
        if (!running_.load() || !state_fn_) return;
        std::string json = state_fn_();
        if (json.empty()) return;

        std::string sse = "data: " + json + "\n\n";

        std::lock_guard<std::mutex> lk(clients_mtx_);
        std::vector<int> dead;
        for (int fd : sse_clients_) {
            ssize_t sent = send(fd, sse.c_str(), sse.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
            if (sent <= 0)
                dead.push_back(fd);
        }
        for (int fd : dead) {
            close(fd);
            sse_clients_.erase(
                std::remove(sse_clients_.begin(), sse_clients_.end(), fd),
                sse_clients_.end());
        }
    }

private:
    StateProvider state_fn_;
    std::string page_html_;
    std::atomic<bool> running_{false};
    int server_fd_ = -1;
    int port_ = 30120;
    std::thread accept_thread_;
    std::mutex clients_mtx_;
    std::vector<int> sse_clients_;

    void accept_loop() {
        signal(SIGPIPE, SIG_IGN);
        while (running_.load()) {
            struct sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);

            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(server_fd_, &fds);
            struct timeval tv{1, 0};
            int sel = select(server_fd_ + 1, &fds, nullptr, nullptr, &tv);
            if (sel <= 0) continue;

            int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
            if (client_fd < 0) continue;

            std::thread([this, client_fd]() { handle_client(client_fd); }).detach();
        }
    }

    void handle_client(int fd) {
        char req[4096]{};
        ssize_t n = recv(fd, req, sizeof(req) - 1, 0);
        if (n <= 0) { close(fd); return; }
        req[n] = 0;

        std::string request(req);

        if (request.find("GET /events") != std::string::npos) {
            const char* sse_headers =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/event-stream\r\n"
                "Cache-Control: no-cache\r\n"
                "Connection: keep-alive\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "\r\n";
            send(fd, sse_headers, strlen(sse_headers), MSG_NOSIGNAL);

            std::lock_guard<std::mutex> lk(clients_mtx_);
            sse_clients_.push_back(fd);
            return;
        }

        if (request.find("GET / ") != std::string::npos ||
            request.find("GET /index") != std::string::npos) {
            std::string resp =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/html; charset=utf-8\r\n"
                "Content-Length: " + std::to_string(page_html_.size()) + "\r\n"
                "Connection: close\r\n"
                "\r\n" + page_html_;
            send(fd, resp.c_str(), resp.size(), MSG_NOSIGNAL);
            close(fd);
            return;
        }

        const char* not_found =
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Length: 9\r\n"
            "Connection: close\r\n"
            "\r\nNot Found";
        send(fd, not_found, strlen(not_found), MSG_NOSIGNAL);
        close(fd);
    }
};

#endif
