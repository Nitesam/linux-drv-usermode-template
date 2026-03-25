#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <X11/keysym.h>
#include <vector>
#include <sys/prctl.h>

#include <GL/gl.h>
#include <GLFW/glfw3.h>

#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xfixes.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "mem_client.h"
#include "game_interface.h"
#include "logger.h"
#include "screen_info.h"

#include "games/hll/hll_module.h"
static std::unique_ptr<GameModule> create_game() { return std::make_unique<HllModule>(); }

static MemClient          g_client;
static int                g_target_pid   = -1;
static uint64_t           g_base_address = 0;
static std::string        g_status_msg   = "Ready";
static std::atomic<bool>  g_running{true};
static std::mutex         g_game_mutex;
static bool               g_ui_visible   = false;
static bool               g_passthrough  = false;
static bool               g_ins_was_down = false;
static bool               g_show_debug   = false;

static std::vector<ScreenInfo> g_screens;
static int                     g_selected_screen = 0;
static int                     g_screen_width  = 1920;
static int                     g_screen_height = 1080;

static Display *g_x11_dpy = nullptr;
static Window   g_x11_win = 0;

static void set_clickthrough(bool enable)
{
    if (!g_x11_dpy || !g_x11_win || g_passthrough == enable) return;
    g_passthrough = enable;
    if (enable) {
        XserverRegion region = XFixesCreateRegion(g_x11_dpy, nullptr, 0);
        XFixesSetWindowShapeRegion(g_x11_dpy, g_x11_win, ShapeInput, 0, 0, region);
        XFixesDestroyRegion(g_x11_dpy, region);
    } else {
        XFixesSetWindowShapeRegion(g_x11_dpy, g_x11_win, ShapeInput, 0, 0, None);
    }
    XFlush(g_x11_dpy);
}

static void poll_global_hotkey()
{
    if (!g_x11_dpy) return;
    char keys[32];
    XQueryKeymap(g_x11_dpy, keys);
    KeyCode ins = XKeysymToKeycode(g_x11_dpy, XK_Insert);
    bool is_down = (keys[ins / 8] & (1 << (ins % 8))) != 0;
    if (is_down && !g_ins_was_down) {
        g_ui_visible = !g_ui_visible;
        set_clickthrough(!g_ui_visible);
    }
    g_ins_was_down = is_down;
}

static std::unique_ptr<GameModule> g_game;

static void reader_thread_func()
{
    auto local_game = create_game();

    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));

        if (g_target_pid <= 0 || g_base_address == 0 || !g_client.is_open())
            continue;

        local_game->update(g_client, g_target_pid, g_base_address);

        {
            std::lock_guard<std::mutex> lock(g_game_mutex);
            g_game.swap(local_game);
        }

        if (!local_game)
            local_game = create_game();
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

    ImVec4 *c = style.Colors;
    ImVec4 ga = ImVec4(0.2f, 0.8f, 0.4f, 1.0f);
    ImVec4 gd = ImVec4(0.15f, 0.6f, 0.3f, 1.0f);
    ImVec4 gb = ImVec4(0.25f, 0.9f, 0.5f, 1.0f);

    c[ImGuiCol_WindowBg]             = ImVec4(0.08f, 0.08f, 0.10f, 0.92f);
    c[ImGuiCol_ChildBg]              = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.10f, 0.10f, 0.12f, 0.95f);
    c[ImGuiCol_Border]               = ImVec4(0.20f, 0.20f, 0.22f, 0.60f);
    c[ImGuiCol_FrameBg]              = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.18f, 0.18f, 0.22f, 1.00f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.15f, 0.50f, 0.30f, 0.60f);
    c[ImGuiCol_TitleBg]              = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);
    c[ImGuiCol_TitleBgActive]        = ImVec4(0.10f, 0.35f, 0.20f, 1.00f);
    c[ImGuiCol_ScrollbarGrab]        = gd;
    c[ImGuiCol_ScrollbarGrabHovered] = ga;
    c[ImGuiCol_ScrollbarGrabActive]  = gb;
    c[ImGuiCol_CheckMark]            = ga;
    c[ImGuiCol_SliderGrab]           = gd;
    c[ImGuiCol_SliderGrabActive]     = ga;
    c[ImGuiCol_Button]               = ImVec4(0.15f, 0.50f, 0.30f, 0.70f);
    c[ImGuiCol_ButtonHovered]        = ga;
    c[ImGuiCol_ButtonActive]         = gb;
    c[ImGuiCol_Header]               = ImVec4(0.15f, 0.50f, 0.30f, 0.50f);
    c[ImGuiCol_HeaderHovered]        = ga;
    c[ImGuiCol_HeaderActive]         = gb;
    c[ImGuiCol_Separator]            = ImVec4(0.20f, 0.55f, 0.35f, 0.40f);
    c[ImGuiCol_Tab]                  = ImVec4(0.10f, 0.35f, 0.20f, 0.80f);
    c[ImGuiCol_TabHovered]           = ga;
    c[ImGuiCol_Text]                 = ImVec4(0.92f, 0.93f, 0.94f, 1.00f);
    c[ImGuiCol_TextDisabled]         = ImVec4(0.45f, 0.45f, 0.48f, 1.00f);
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
    prctl(PR_SET_NAME, "gsd-housekeepin", 0, 0, 0);

    Logger::instance().init();

    g_game = create_game();
    LOG_INFO("Starting application (PID %d) — Game: %s", getpid(), g_game->game_name());

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) { LOG_ERR("glfwInit failed"); return 1; }

    g_screens = get_available_screens();
    if (!g_screens.empty()) {
        GLFWmonitor *primary = glfwGetPrimaryMonitor();
        int primary_idx = 0;
        if (primary) {
            int count = 0;
            GLFWmonitor **mons = glfwGetMonitors(&count);
            for (int i = 0; i < count; ++i) {
                if (mons[i] == primary) { primary_idx = i; break; }
            }
        }
        if (primary_idx >= (int)g_screens.size()) primary_idx = 0;
        g_selected_screen = primary_idx;
        g_screen_width  = g_screens[primary_idx].width;
        g_screen_height = g_screens[primary_idx].height;
        LOG_INFO("Detected %d screen(s). Primary: %s (%dx%d)",
                 (int)g_screens.size(), g_screens[primary_idx].label.c_str(),
                 g_screen_width, g_screen_height);
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);

    GLFWwindow *window = glfwCreateWindow(g_screen_width, g_screen_height, "Settings", nullptr, nullptr);
    if (!window) { LOG_ERR("Failed to create GLFW window"); glfwTerminate(); return 1; }

    {
        const auto& scr = g_screens[g_selected_screen];
        glfwSetWindowPos(window, scr.x, scr.y);
        glfwSetWindowSize(window, scr.width, scr.height);
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);


    {
        g_x11_dpy = glfwGetX11Display();
        if (g_x11_dpy) {
            g_x11_win = glfwGetX11Window(window);
            if (g_x11_win) {
                Atom net_wm_pid = XInternAtom(g_x11_dpy, "_NET_WM_PID", False);
                XDeleteProperty(g_x11_dpy, g_x11_win, net_wm_pid);

                XClassHint *ch = XAllocClassHint();
                ch->res_name  = (char*)"gnome-control-center";
                ch->res_class = (char*)"Gnome-control-center";
                XSetClassHint(g_x11_dpy, g_x11_win, ch);
                XFree(ch);

                Atom wm_type = XInternAtom(g_x11_dpy, "_NET_WM_WINDOW_TYPE", False);
                Atom type_notif = XInternAtom(g_x11_dpy, "_NET_WM_WINDOW_TYPE_NOTIFICATION", False);
                XChangeProperty(g_x11_dpy, g_x11_win, wm_type, XA_ATOM, 32,
                                PropModeReplace, (unsigned char*)&type_notif, 1);

                Atom wm_state = XInternAtom(g_x11_dpy, "_NET_WM_STATE", False);
                Atom states[] = {
                    XInternAtom(g_x11_dpy, "_NET_WM_STATE_ABOVE", False),
                    XInternAtom(g_x11_dpy, "_NET_WM_STATE_SKIP_TASKBAR", False),
                    XInternAtom(g_x11_dpy, "_NET_WM_STATE_SKIP_PAGER", False),
                };
                XChangeProperty(g_x11_dpy, g_x11_win, wm_state, XA_ATOM, 32,
                                PropModeReplace, (unsigned char*)states, 3);

                const auto& scr = g_screens[g_selected_screen];
                XMoveResizeWindow(g_x11_dpy, g_x11_win, scr.x, scr.y,
                                  scr.width, scr.height);

                XFlush(g_x11_dpy);

                set_clickthrough(true);
            }
        }
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    setup_theme();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    if (!g_client.open_driver()) {
        g_status_msg = "Driver unavailable";
        LOG_ERR("Driver open FAILED: %s", g_client.last_error().c_str());
    } else {
        g_status_msg = "Driver active";
        if (g_client.hide_self())
            g_status_msg += " | PID hidden";
    }

    g_target_pid = g_client.find_pid(g_game->process_name());
    if (g_target_pid > 0) {
        g_base_address = g_client.get_base_address(g_target_pid, g_game->module_filter());
        g_status_msg += " | PID: " + std::to_string(g_target_pid);
        if (g_base_address > 0) {
            char buf[32]; snprintf(buf, sizeof(buf), "0x%lX", g_base_address);
            g_status_msg += std::string(" | Base: ") + buf;
        } else {
            g_status_msg += " | Base: NOT FOUND";
        }
    } else {
        g_status_msg += std::string(" | ") + g_game->game_name() + " not found";
    }

    std::thread reader_thread(reader_thread_func);

    int raise_counter = 0;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        poll_global_hotkey();

        if (g_x11_dpy && g_x11_win && ++raise_counter >= 60) {
            raise_counter = 0;
            XRaiseWindow(g_x11_dpy, g_x11_win);
            XFlush(g_x11_dpy);
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        std::unique_ptr<GameModule> render_game;
        {
            std::lock_guard<std::mutex> lock(g_game_mutex);
            render_game.swap(g_game);
        }
        if (!render_game)
            render_game = create_game();

        ImDrawList* fg = ImGui::GetForegroundDrawList();
        render_game->render_esp(fg, g_screen_width, g_screen_height);

        if (g_ui_visible) {
            ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);
            ImGui::Begin("##MainPanel", &g_ui_visible,
                         ImGuiWindowFlags_NoCollapse);

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 0.4f, 1.0f));
            ImGui::Text(u8"\u2588\u2588  %s", render_game->game_name());
            ImGui::PopStyleColor();

            ImGui::SameLine(ImGui::GetWindowWidth() * 0.35f);
            if (g_target_pid > 0)
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f),
                    "PID: %d | Players: %d | Screen: %dx%d",
                    g_target_pid, render_game->player_count(),
                    g_screen_width, g_screen_height);
            else
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "NOT FOUND");

            ImGui::SameLine(ImGui::GetWindowWidth() - 180);
            if (ImGui::Button("Refresh", ImVec2(80, 0))) {
                g_target_pid = g_client.find_pid(render_game->process_name());
                if (g_target_pid > 0) {
                    g_base_address = g_client.get_base_address(g_target_pid, render_game->module_filter());
                    g_status_msg = "PID: " + std::to_string(g_target_pid);
                } else {
                    g_base_address = 0;
                    g_status_msg = std::string(render_game->game_name()) + " not found";
                }
            }

            ImGui::Separator();

            float content_height = ImGui::GetContentRegionAvail().y - 30;

            if (ImGui::BeginTabBar("##MainTabs")) {

                if (ImGui::BeginTabItem("Players")) {
                    ImGui::BeginChild("##players_content", ImVec2(0, content_height - 40), true);

                    render_game->render_controls();
                    ImGui::Separator();

                    std::string st = render_game->status_text();
                    if (!st.empty())
                        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.4f, 1.0f), "%s", st.c_str());

                    render_game->render_table();
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("ESP")) {
                    ImGui::BeginChild("##esp_content", ImVec2(0, content_height - 40), true);
                    render_game->render_esp_controls();
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Debug")) {
                    ImGui::BeginChild("##debug_content", ImVec2(0, content_height - 40), true);
                    Logger::instance().render_widget();
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Settings")) {
                    ImGui::BeginChild("##settings_content", ImVec2(0, content_height - 40), true);

                    ImGui::SeparatorText("Screen Selection");
                    if (g_screens.size() > 1) {
                        if (ImGui::BeginCombo("Monitor", g_screens[g_selected_screen].label.c_str())) {
                            for (int i = 0; i < (int)g_screens.size(); ++i) {
                                bool selected = (i == g_selected_screen);
                                if (ImGui::Selectable(g_screens[i].label.c_str(), selected)) {
                                    g_selected_screen = i;
                                    g_screen_width  = g_screens[i].width;
                                    g_screen_height = g_screens[i].height;
                                    const auto& scr = g_screens[i];
                                    glfwSetWindowPos(window, scr.x, scr.y);
                                    glfwSetWindowSize(window, scr.width, scr.height);
                                    LOG_INFO("Selected screen %d: %s (%dx%d)",
                                             i, g_screens[i].label.c_str(),
                                             g_screen_width, g_screen_height);
                                }
                                if (selected) ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }
                    } else {
                        ImGui::Text("Monitor: %s", g_screens.empty() ? "Default" : g_screens[0].label.c_str());
                    }
                    ImGui::Text("Resolution: %d x %d", g_screen_width, g_screen_height);

                    ImGui::SeparatorText("Status");
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", g_status_msg.c_str());

                    ImGui::SeparatorText("Actions");
                    if (ImGui::Button("Refresh Process", ImVec2(150, 0))) {
                        g_target_pid = g_client.find_pid(render_game->process_name());
                        if (g_target_pid > 0) {
                            g_base_address = g_client.get_base_address(g_target_pid, render_game->module_filter());
                            g_status_msg = "PID: " + std::to_string(g_target_pid);
                        } else {
                            g_base_address = 0;
                            g_status_msg = std::string(render_game->game_name()) + " not found";
                        }
                    }

                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }

            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", g_status_msg.c_str());
            ImGui::End();
        }

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);

        {
            std::lock_guard<std::mutex> lock(g_game_mutex);
            if (!g_game)
                g_game.swap(render_game);
        }
    }

    g_running.store(false);
    reader_thread.join();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    g_client.close_driver();

    return 0;
}
