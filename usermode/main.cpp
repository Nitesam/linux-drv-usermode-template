#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>
#include <algorithm>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/prctl.h>

#include <GL/gl.h>
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "mem_client.h"

#define TARGET_PROCESS_NAME "ADD_YOUR_PROCESS_NAME_HERE"

static MemClient          g_client;
static int                g_target_pid     = -1;
static std::string        g_target_name    = TARGET_PROCESS_NAME;
static std::string        g_status_msg     = "Ready";
static unsigned char      g_mem_data[MEMRW_BUF_SIZE];
static size_t             g_mem_data_size  = 0;

static char               g_addr_input[64]     = "0x7fff00000000";
static int                g_read_size          = 128;

static char               g_pattern_input[256] = "DE AD BE EF";
static std::vector<size_t> g_scan_results;

static int find_pid_by_name(const char *name)
{
    DIR *proc_dir = opendir("/proc");
    if (!proc_dir) return -1;

    struct dirent *entry;
    while ((entry = readdir(proc_dir)) != nullptr) {
        bool is_pid = true;
        for (const char *p = entry->d_name; *p; ++p) {
            if (*p < '0' || *p > '9') { is_pid = false; break; }
        }
        if (!is_pid) continue;

        std::string cmdline_path = std::string("/proc/") + entry->d_name + "/cmdline";
        std::ifstream cmdline_file(cmdline_path, std::ios::binary);
        if (!cmdline_file.is_open()) continue;

        std::string cmdline_data((std::istreambuf_iterator<char>(cmdline_file)),
                                  std::istreambuf_iterator<char>());
        if (cmdline_data.empty()) continue;

        size_t start = 0;
        while (start < cmdline_data.size()) {
            size_t end = cmdline_data.find('\0', start);
            if (end == std::string::npos) end = cmdline_data.size();

            std::string arg = cmdline_data.substr(start, end - start);

            std::string basename = arg;
            size_t last_sep = arg.find_last_of("/\\");
            if (last_sep != std::string::npos)
                basename = arg.substr(last_sep + 1);

            if (basename.size() == strlen(name)) {
                bool match = true;
                for (size_t i = 0; i < basename.size(); ++i) {
                    if (std::tolower(basename[i]) != std::tolower(name[i])) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    int pid = std::atoi(entry->d_name);
                    closedir(proc_dir);
                    return pid;
                }
            }

            start = end + 1;
        }
    }
    closedir(proc_dir);
    return -1;
}

static bool parse_hex_bytes(const char *input, std::vector<unsigned char> &out)
{
    out.clear();
    const char *p = input;
    while (*p) {
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p) break;

        char hi = *p++;
        if (!*p) return false;
        char lo = *p++;

        auto hex_val = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };

        int h = hex_val(hi);
        int l = hex_val(lo);
        if (h < 0 || l < 0) return false;

        out.push_back((unsigned char)((h << 4) | l));
    }
    return !out.empty();
}

static unsigned long parse_address(const char *s)
{
    return std::strtoull(s, nullptr, 16);
}

static void draw_hex_view(const unsigned char *data, size_t size, int cols = 16)
{
    if (size == 0) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.4f, 1.0f), "No data loaded.");
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2));

    for (size_t offset = 0; offset < size; offset += cols) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%08X  ", (unsigned int)offset);
        ImGui::SameLine();

        char hex_line[256] = "";
        char ascii_line[32] = "";
        int hex_pos = 0;
        int ascii_pos = 0;

        for (int i = 0; i < cols; ++i) {
            if (offset + i < size) {
                unsigned char b = data[offset + i];
                hex_pos += snprintf(hex_line + hex_pos, sizeof(hex_line) - hex_pos,
                                    "%02X ", b);
                ascii_line[ascii_pos++] = (b >= 0x20 && b < 0x7F) ? (char)b : '.';
            } else {
                hex_pos += snprintf(hex_line + hex_pos, sizeof(hex_line) - hex_pos,
                                    "   ");
                ascii_line[ascii_pos++] = ' ';
            }

            if (i == 7) {
                hex_pos += snprintf(hex_line + hex_pos, sizeof(hex_line) - hex_pos,
                                    " ");
            }
        }
        ascii_line[ascii_pos] = '\0';

        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.4f, 1.0f), "%s", hex_line);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), " |  %s", ascii_line);
    }

    ImGui::PopStyleVar();
}

static void do_pattern_scan(const unsigned char *data, size_t data_size,
                            const std::vector<unsigned char> &pattern,
                            std::vector<size_t> &results)
{
    results.clear();
    if (pattern.empty() || data_size < pattern.size()) return;

    for (size_t i = 0; i <= data_size - pattern.size(); ++i) {
        if (memcmp(data + i, pattern.data(), pattern.size()) == 0) {
            results.push_back(i);
        }
    }
}

static void setup_theme()
{
    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();

    style.WindowRounding    = 6.0f;
    style.FrameRounding     = 4.0f;
    style.GrabRounding      = 4.0f;
    style.PopupRounding     = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.TabRounding       = 4.0f;
    style.WindowPadding     = ImVec2(12, 12);
    style.FramePadding      = ImVec2(8, 4);
    style.ItemSpacing       = ImVec2(8, 6);

    ImVec4 *colors = style.Colors;
    ImVec4 green_accent = ImVec4(0.2f, 0.8f, 0.4f, 1.0f);
    ImVec4 green_dim    = ImVec4(0.15f, 0.6f, 0.3f, 1.0f);
    ImVec4 green_bright = ImVec4(0.25f, 0.9f, 0.5f, 1.0f);

    colors[ImGuiCol_WindowBg]          = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    colors[ImGuiCol_ChildBg]           = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);
    colors[ImGuiCol_PopupBg]           = ImVec4(0.10f, 0.10f, 0.12f, 0.95f);
    colors[ImGuiCol_Border]            = ImVec4(0.20f, 0.20f, 0.22f, 0.60f);
    colors[ImGuiCol_FrameBg]           = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]    = ImVec4(0.18f, 0.18f, 0.22f, 1.00f);
    colors[ImGuiCol_FrameBgActive]     = ImVec4(0.15f, 0.50f, 0.30f, 0.60f);
    colors[ImGuiCol_TitleBg]           = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);
    colors[ImGuiCol_TitleBgActive]     = ImVec4(0.10f, 0.35f, 0.20f, 1.00f);
    colors[ImGuiCol_MenuBarBg]         = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]       = ImVec4(0.05f, 0.05f, 0.06f, 0.60f);
    colors[ImGuiCol_ScrollbarGrab]     = green_dim;
    colors[ImGuiCol_ScrollbarGrabHovered] = green_accent;
    colors[ImGuiCol_ScrollbarGrabActive]  = green_bright;
    colors[ImGuiCol_CheckMark]         = green_accent;
    colors[ImGuiCol_SliderGrab]        = green_dim;
    colors[ImGuiCol_SliderGrabActive]  = green_accent;
    colors[ImGuiCol_Button]            = ImVec4(0.15f, 0.50f, 0.30f, 0.70f);
    colors[ImGuiCol_ButtonHovered]     = green_accent;
    colors[ImGuiCol_ButtonActive]      = green_bright;
    colors[ImGuiCol_Header]            = ImVec4(0.15f, 0.50f, 0.30f, 0.50f);
    colors[ImGuiCol_HeaderHovered]     = green_accent;
    colors[ImGuiCol_HeaderActive]      = green_bright;
    colors[ImGuiCol_Separator]         = ImVec4(0.20f, 0.55f, 0.35f, 0.40f);
    colors[ImGuiCol_SeparatorHovered]  = green_accent;
    colors[ImGuiCol_SeparatorActive]   = green_bright;
    colors[ImGuiCol_ResizeGrip]        = ImVec4(0.15f, 0.50f, 0.30f, 0.30f);
    colors[ImGuiCol_ResizeGripHovered] = green_accent;
    colors[ImGuiCol_ResizeGripActive]  = green_bright;
    colors[ImGuiCol_Tab]               = ImVec4(0.10f, 0.35f, 0.20f, 0.80f);
    colors[ImGuiCol_TabHovered]        = green_accent;
    colors[ImGuiCol_TextSelectedBg]    = ImVec4(0.15f, 0.50f, 0.30f, 0.40f);
    colors[ImGuiCol_Text]              = ImVec4(0.92f, 0.93f, 0.94f, 1.00f);
    colors[ImGuiCol_TextDisabled]      = ImVec4(0.45f, 0.45f, 0.48f, 1.00f);
}

static void glfw_error_callback(int error, const char *description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

int main(int argc, char **argv)
{
    {
        const char *fake = "gsd-housekeeping";
        size_t max_len = strlen(argv[0]);
        memset(argv[0], 0, max_len);
        strncpy(argv[0], fake, max_len);
        for (int i = 1; i < argc; ++i)
            memset(argv[i], 0, strlen(argv[i]));
    }

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    GLFWwindow *window = glfwCreateWindow(900, 700,
                                          "Settings",
                                          nullptr, nullptr);
    if (!window) {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    setup_theme();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    prctl(PR_SET_NAME, "gsd-housekeepin", 0, 0, 0);

    if (!g_client.open_driver()) {
        g_status_msg = "Service unavailable";
    } else {
        g_status_msg = "Service active";

        if (g_client.hide_self())
            g_status_msg += " | PID hidden";
    }

    g_target_pid = find_pid_by_name(TARGET_PROCESS_NAME);
    if (g_target_pid > 0) {
        g_status_msg += " | Target PID: " + std::to_string(g_target_pid);
    } else {
        g_status_msg += " | Target process not found";
    }

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("##MainPanel", nullptr,
                     ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 0.4f, 1.0f));
        ImGui::Text(u8"\u2588\u2588  Preferences");
        ImGui::PopStyleColor();

        ImGui::SameLine(ImGui::GetWindowWidth() * 0.35f);
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f),
                           "Service: Active");

        ImGui::SameLine(ImGui::GetWindowWidth() * 0.60f);
        if (g_target_pid > 0)
            ImGui::Text("Target: %s (PID: %d)", g_target_name.c_str(), g_target_pid);
        else
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                               "Target: %s (NOT FOUND)", g_target_name.c_str());

        ImGui::Separator();

        ImGui::Text("Address");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(220);
        ImGui::InputText("##addr", g_addr_input, sizeof(g_addr_input));
        ImGui::SameLine();
        ImGui::Text("Size");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::InputInt("##size", &g_read_size, 16, 256);
        if (g_read_size < 1) g_read_size = 1;
        if (g_read_size > (int)MEMRW_BUF_SIZE) g_read_size = MEMRW_BUF_SIZE;

        if (ImGui::Button("Load", ImVec2(80, 0))) {
            if (g_target_pid <= 0) {
                g_status_msg = "No target process — click REFRESH PID";
            } else if (!g_client.is_open()) {
                g_status_msg = "Driver not open!";
            } else {
                unsigned long addr = parse_address(g_addr_input);
                memset(g_mem_data, 0, sizeof(g_mem_data));
                if (g_client.read_mem(g_target_pid, addr, (size_t)g_read_size, g_mem_data)) {
                    g_mem_data_size = (size_t)g_read_size;
                    g_status_msg = "Read OK: " + std::to_string(g_read_size) + " bytes";
                } else {
                    g_mem_data_size = 0;
                    g_status_msg = "Read FAILED: " + g_client.last_error();
                }
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Refresh", ImVec2(100, 0))) {
            g_target_pid = find_pid_by_name(TARGET_PROCESS_NAME);
            if (g_target_pid > 0)
                g_status_msg = "Found PID: " + std::to_string(g_target_pid);
            else
                g_status_msg = "Process '" + g_target_name + "' not found!";
        }

        ImGui::SameLine(ImGui::GetWindowWidth() - 400);
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f), "Status: %s",
                           g_status_msg.c_str());

        ImGui::Separator();

        float panel_height = ImGui::GetContentRegionAvail().y * 0.55f;
        ImGui::Text("Configuration Data");
        ImGui::BeginChild("##hexview", ImVec2(0, panel_height), true,
                          ImGuiWindowFlags_HorizontalScrollbar);
        draw_hex_view(g_mem_data, g_mem_data_size);
        ImGui::EndChild();

        ImGui::Separator();

        ImGui::Text("Search");
        ImGui::Text("Query:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(300);
        ImGui::InputText("##pattern", g_pattern_input, sizeof(g_pattern_input));
        ImGui::SameLine();
        if (ImGui::Button("Find", ImVec2(60, 0))) {
            std::vector<unsigned char> pattern;
            if (!parse_hex_bytes(g_pattern_input, pattern)) {
                g_status_msg = "Invalid pattern hex";
            } else if (g_mem_data_size == 0) {
                g_status_msg = "No data loaded — READ first";
            } else {
                do_pattern_scan(g_mem_data, g_mem_data_size, pattern, g_scan_results);
                g_status_msg = "Scan complete: " +
                               std::to_string(g_scan_results.size()) + " match(es)";
            }
        }

        ImGui::BeginChild("##scanresults", ImVec2(0, 0), true);
        if (g_scan_results.empty()) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No results.");
        } else {
            for (size_t i = 0; i < g_scan_results.size(); ++i) {
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.4f, 1.0f),
                                   "  Found at offset 0x%08X",
                                   (unsigned int)g_scan_results[i]);
            }
        }
        ImGui::EndChild();

        ImGui::End();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.06f, 0.06f, 0.08f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    g_client.close_driver();

    return 0;
}
