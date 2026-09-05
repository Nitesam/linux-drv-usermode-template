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
#include <shared_mutex>
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


#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "mem_client.h"
#include "game_interface.h"
#include "logger.h"
#include "screen_info.h"
#include "settings_store.h"

#include "games/dbd/dbd_module.h"
#include "web_radar.h"
#include "web_radar_page.h"
static std::unique_ptr<GameModule> create_game() { return std::make_unique<DbdModule>(); }

static MemClient          g_client;
static int                g_target_pid   = -1;
static uint64_t           g_base_address = 0;
static std::string        g_status_msg   = "Ready";
static std::atomic<bool>  g_running{true};
static bool               g_ui_visible   = false;
static bool               g_passthrough  = false;
static bool               g_ins_was_down = false;
static bool               g_show_debug   = false;
static int                g_deferred_passthrough = -1;
static std::chrono::steady_clock::time_point g_last_toggle_time{};
static constexpr double   kToggleCooldownSec = 0.2;
static bool               g_settings_dirty = false;

static std::shared_ptr<GameModule> g_shared_state;
static std::shared_mutex           g_state_rwlock;

static SettingsStore       g_settings_store;
static std::string         g_settings_snapshot;

static WebRadarServer      g_web_radar;
static bool                g_web_radar_enabled = true;
static std::chrono::steady_clock::time_point g_last_sse_push{};
constexpr double           kSsePushIntervalSec = 0.10;
static std::string         g_cached_json;
static std::mutex          g_json_mtx;

static std::vector<ScreenInfo> g_screens;
static int                     g_selected_screen = 0;
static int                     g_screen_width  = 1920;
static int                     g_screen_height = 1080;
static int                     g_render_width  = 1920;
static int                     g_render_height = 1080;

static Display *g_x11_dpy = nullptr;
static Window   g_x11_win = 0;

static GLFWwindow *g_glfw_window = nullptr;

static void set_clickthrough(bool enable)
{
    if (!g_glfw_window || g_passthrough == enable) return;

    Window prev_focus = 0;
    int revert_to = 0;
    if (!enable && g_x11_dpy)
        XGetInputFocus(g_x11_dpy, &prev_focus, &revert_to);

    g_passthrough = enable;
    glfwSetWindowAttrib(g_glfw_window, GLFW_MOUSE_PASSTHROUGH, enable ? GLFW_TRUE : GLFW_FALSE);

    if (!enable && g_x11_dpy && prev_focus && prev_focus != g_x11_win) {
        XSetInputFocus(g_x11_dpy, prev_focus, revert_to, CurrentTime);
        XFlush(g_x11_dpy);
    }
}

static void poll_global_hotkey()
{
    if (!g_x11_dpy) return;
    char keys[32];
    XQueryKeymap(g_x11_dpy, keys);
    KeyCode ins = XKeysymToKeycode(g_x11_dpy, XK_Insert);
    bool is_down = (keys[ins / 8] & (1 << (ins % 8))) != 0;
    if (is_down && !g_ins_was_down) {
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - g_last_toggle_time).count();
        if (dt >= kToggleCooldownSec) {
            g_last_toggle_time = now;
            g_ui_visible = !g_ui_visible;
            g_deferred_passthrough = g_ui_visible ? 0 : 1;
        }
    }
    g_ins_was_down = is_down;
}


static int try_find_pid(MemClient& client, GameModule* game)
{
    int pid = client.find_pid(game->process_name());
    if (pid > 0) {
        LOG_INFO("Found %s with driver name '%s': PID %d",
                 game->game_name(), game->process_name(), pid);
        return pid;
    }
    LOG_DBG("Driver PID lookup missed '%s'", game->process_name());

    for (auto name : game->alt_process_names()) {
        pid = client.find_pid(name);
        if (pid > 0) {
            LOG_INFO("Found %s with driver alias '%s': PID %d",
                     game->game_name(), name, pid);
            return pid;
        }
        LOG_DBG("Driver PID lookup missed '%s'", name);
    }

    return game->find_pid_fallback();
}

static void reader_thread_func()
{
    auto local_game = create_game();

    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));

        if (g_target_pid <= 0 || g_base_address == 0 || !g_client.is_open())
            continue;

        local_game->update(g_client, g_target_pid, g_base_address);

        std::string json = local_game->to_json();
        auto render_snapshot = local_game->clone_for_render();

        {
            std::unique_lock<std::shared_mutex> wlock(g_state_rwlock);
            g_shared_state = std::move(render_snapshot);
        }
        {
            std::lock_guard<std::mutex> jlk(g_json_mtx);
            g_cached_json = std::move(json);
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

    auto g_init_game = create_game();
    LOG_INFO("Starting application (PID %d) — Game: %s", getpid(), g_init_game->game_name());

    g_settings_store.init();
    {
        std::string saved = g_settings_store.load();
        if (!saved.empty()) {
            g_init_game->load_settings(saved);
            g_settings_snapshot = saved;
            LOG_INFO("Settings loaded from %s", g_settings_store.path.c_str());
        }
    }

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
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    glfwWindowHint(GLFW_MOUSE_PASSTHROUGH, GLFW_TRUE);

    GLFWwindow *window = glfwCreateWindow(g_screen_width, g_screen_height, "Settings", nullptr, nullptr);
    if (!window) { LOG_ERR("Failed to create GLFW window"); glfwTerminate(); return 1; }
    g_glfw_window = window;
    g_passthrough = true;

    {
        const auto& scr = g_screens[g_selected_screen];
        glfwSetWindowPos(window, scr.x, scr.y);
        glfwSetWindowSize(window, scr.width, scr.height);
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);


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

                XWMHints *wmh = XAllocWMHints();
                wmh->flags = InputHint;
                wmh->input = False;
                XSetWMHints(g_x11_dpy, g_x11_win, wmh);
                XFree(wmh);

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
    glfwSwapInterval(0);

    if (!g_client.open_driver()) {
        g_status_msg = "Driver unavailable";
        LOG_ERR("Driver open FAILED: %s", g_client.last_error().c_str());
    } else {
        g_status_msg = "Driver active";
    }

    g_target_pid = try_find_pid(g_client, g_init_game.get());
    if (g_target_pid > 0) {
        g_base_address = g_client.get_base_address(g_target_pid, g_init_game->module_filter());
        g_status_msg += " | PID: " + std::to_string(g_target_pid);
        if (g_base_address > 0) {
            char buf[32]; snprintf(buf, sizeof(buf), "0x%lX", g_base_address);
            g_status_msg += std::string(" | Base: ") + buf;
        } else {
            g_status_msg += " | Base: NOT FOUND";
        }
    } else {
        g_status_msg += std::string(" | ") + g_init_game->game_name() + " not found";
    }

    g_shared_state = std::move(g_init_game);

    std::thread reader_thread(reader_thread_func);

    if (g_web_radar_enabled) {
        g_web_radar.set_page(WEB_RADAR_HTML);
        g_web_radar.set_state_provider([&]() -> std::string {
            std::lock_guard<std::mutex> jlk(g_json_mtx);
            return g_cached_json;
        });
        if (g_web_radar.start(30120))
            LOG_INFO("Web Radar started on port 30120");
        else
            LOG_ERR("Web Radar failed to start");
    }

    static float g_fps = 0;
    static int g_frame_count = 0;
    static auto g_fps_timer = std::chrono::steady_clock::now();
    static float g_ui_scale = 1.0f;
    constexpr double kTargetFrameTime = 1.0 / 120.0;

    while (!glfwWindowShouldClose(window)) {
        auto frame_start = std::chrono::steady_clock::now();

        glfwPollEvents();
        poll_global_hotkey();

        if (g_deferred_passthrough >= 0) {
            set_clickthrough(g_deferred_passthrough != 0);
            g_deferred_passthrough = -1;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        auto& io = ImGui::GetIO();
        if (io.KeyCtrl && io.MouseWheel != 0.0f) {
            g_ui_scale += io.MouseWheel * 0.1f;
            if (g_ui_scale < 0.5f) g_ui_scale = 0.5f;
            if (g_ui_scale > 4.0f) g_ui_scale = 4.0f;
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Equal))  { g_ui_scale += 0.1f; if (g_ui_scale > 4.0f) g_ui_scale = 4.0f; }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Minus))   { g_ui_scale -= 0.1f; if (g_ui_scale < 0.5f) g_ui_scale = 0.5f; }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_0))       { g_ui_scale = 1.0f; }
        io.FontGlobalScale = g_ui_scale;

        int draw_w = static_cast<int>(io.DisplaySize.x);
        int draw_h = static_cast<int>(io.DisplaySize.y);
        if (draw_w <= 0 || draw_h <= 0)
            glfwGetWindowSize(window, &draw_w, &draw_h);
        if (draw_w <= 0 || draw_h <= 0) {
            draw_w = g_screen_width;
            draw_h = g_screen_height;
        }
        g_render_width = draw_w;
        g_render_height = draw_h;

        std::shared_ptr<GameModule> render_game;
        {
            std::shared_lock<std::shared_mutex> rlock(g_state_rwlock);
            render_game = g_shared_state;
        }
        if (!render_game)
            render_game = create_game();

        ImDrawList* fg = ImGui::GetForegroundDrawList();
        render_game->render_esp(fg, g_render_width, g_render_height);

        g_frame_count++;
        auto now_fps = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now_fps - g_fps_timer).count();
        if (elapsed >= 0.5) {
            g_fps = static_cast<float>(g_frame_count / elapsed);
            g_frame_count = 0;
            g_fps_timer = now_fps;
        }
        char fps_buf[32];
        snprintf(fps_buf, sizeof(fps_buf), "%.0f FPS", g_fps);
        fg->AddText(ImVec2(g_render_width - 80.0f, 5.0f), IM_COL32(180, 180, 180, 180), fps_buf);

        if (g_ui_visible) {
            ImVec2 win_size(1100 * g_ui_scale, 800 * g_ui_scale);
            ImGui::SetNextWindowPos(ImVec2((g_render_width - win_size.x) * 0.5f,
                                           (g_render_height - win_size.y) * 0.5f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(win_size, ImGuiCond_Always);
            ImGui::Begin("##MainPanel", &g_ui_visible,
                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollWithMouse);

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 0.4f, 1.0f));
            ImGui::Text(u8"\u2588\u2588  %s", render_game->game_name());
            ImGui::PopStyleColor();

            ImGui::SameLine(ImGui::GetWindowWidth() * 0.35f);
            if (g_target_pid > 0)
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f),
                    "PID: %d | Players: %d | Screen: %dx%d",
                    g_target_pid, render_game->player_count(),
                    g_render_width, g_render_height);
            else
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "NOT FOUND");

            ImGui::SameLine(ImGui::GetWindowWidth() - 180);
            if (ImGui::Button("Refresh", ImVec2(80, 0))) {
                g_target_pid = try_find_pid(g_client, render_game.get());
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

                    std::string pre_ctrl = render_game->save_settings();
                    render_game->render_controls();
                    if (render_game->save_settings() != pre_ctrl)
                        g_settings_dirty = true;
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
                    std::string pre_esp = render_game->save_settings();
                    render_game->render_esp_controls();
                    if (render_game->save_settings() != pre_esp)
                        g_settings_dirty = true;
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Debug")) {
                    ImGui::BeginChild("##debug_content", ImVec2(0, content_height - 40), true);

                    ImGui::SeparatorText("Status");
                    if (render_game)
                        render_game->render_debug_panel();

                    ImGui::SeparatorText("Log");
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Copy Log")) {
                        std::string all;
                        auto entries = Logger::instance().entries_snapshot();
                        for (auto& e : entries) { all += e; all += '\n'; }
                        if (!all.empty())
                            ImGui::SetClipboardText(all.c_str());
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Clear"))
                        Logger::instance().clear();

                    Logger::instance().render_log_only();

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
                                    if (g_x11_dpy && g_x11_win) {
                                        XMoveResizeWindow(g_x11_dpy, g_x11_win, scr.x, scr.y,
                                                          scr.width, scr.height);
                                        XFlush(g_x11_dpy);
                                    }
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
                    ImGui::Text("Monitor mode: %d x %d", g_screen_width, g_screen_height);
                    ImGui::Text("Render surface: %d x %d", g_render_width, g_render_height);

                    ImGui::SeparatorText("Status");
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", g_status_msg.c_str());

                    ImGui::SeparatorText("Actions");
                    if (ImGui::Button("Refresh Process", ImVec2(150, 0))) {
                        g_target_pid = try_find_pid(g_client, render_game.get());
                        if (g_target_pid > 0) {
                            g_base_address = g_client.get_base_address(g_target_pid, render_game->module_filter());
                            g_status_msg = "PID: " + std::to_string(g_target_pid);
                        } else {
                            g_base_address = 0;
                            g_status_msg = std::string(render_game->game_name()) + " not found";
                        }
                    }

                    ImGui::SeparatorText("Web Radar");
                    ImGui::Checkbox("Enable Web Radar", &g_web_radar_enabled);
                    if (g_web_radar.is_running()) {
                        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f),
                            "Running on port %d  |  %d client(s)",
                            g_web_radar.port(), g_web_radar.client_count());
                    } else if (g_web_radar_enabled) {
                        if (ImGui::Button("Start", ImVec2(80, 0))) {
                            g_web_radar.set_page(WEB_RADAR_HTML);
                            g_web_radar.set_state_provider([&]() -> std::string {
                                std::lock_guard<std::mutex> jlk(g_json_mtx);
                                return g_cached_json;
                            });
                            g_web_radar.start(30120);
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

        auto frame_end = std::chrono::steady_clock::now();
        double frame_elapsed = std::chrono::duration<double>(frame_end - frame_start).count();
        if (frame_elapsed < kTargetFrameTime) {
            double sleep_us = (kTargetFrameTime - frame_elapsed) * 1e6;
            std::this_thread::sleep_for(std::chrono::microseconds(static_cast<long long>(sleep_us)));
        }

        if (g_settings_dirty) {
            g_settings_dirty = false;
            std::string cur = render_game->save_settings();
            if (cur != g_settings_snapshot) {
                g_settings_snapshot = cur;
                g_settings_store.mark_dirty();
            }
        }
        if (g_settings_store.should_flush()) {
            g_settings_store.save(g_settings_snapshot.c_str());
        }

        if (g_web_radar.is_running()) {
            auto now_sse = std::chrono::steady_clock::now();
            double dt_sse = std::chrono::duration<double>(now_sse - g_last_sse_push).count();
            if (dt_sse >= kSsePushIntervalSec) {
                g_web_radar.broadcast_tick();
                g_last_sse_push = now_sse;
            }
        }
    }

    g_running.store(false);
    reader_thread.join();
    g_web_radar.stop();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    g_client.close_driver();

    return 0;
}
