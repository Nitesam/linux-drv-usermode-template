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
#include <sys/prctl.h>

#include <GL/gl.h>
#include <GLFW/glfw3.h>

#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "mem_client.h"
#include "game_interface.h"
#include "logger.h"

#include "games/hll/hll_module.h"
static std::unique_ptr<GameModule> create_game() { return std::make_unique<HllModule>(); }

static MemClient          g_client;
static int                g_target_pid   = -1;
static uint64_t           g_base_address = 0;
static std::string        g_status_msg   = "Ready";
static std::atomic<bool>  g_running{true};
static std::mutex         g_game_mutex;
static bool               g_ui_visible   = true;

static void key_callback(GLFWwindow*, int key, int, int action, int) {
    if (key == GLFW_KEY_INSERT && action == GLFW_PRESS)
        g_ui_visible = !g_ui_visible;
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

    c[ImGuiCol_WindowBg]             = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
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

    if (Logger::instance().init())
        LOG_INFO("Logger initialized → %s", Logger::instance().path());
    else
        fprintf(stderr, "Warning: logger init failed\n");

    g_game = create_game();
    LOG_INFO("Starting application (PID %d) — Game: %s", getpid(), g_game->game_name());

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) { LOG_ERR("glfwInit failed"); return 1; }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    GLFWwindow *window = glfwCreateWindow(1024, 768, "Settings", nullptr, nullptr);
    if (!window) { LOG_ERR("Failed to create GLFW window"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetKeyCallback(window, key_callback);

    {
        Display *x11_dpy = glfwGetX11Display();
        if (x11_dpy) {
            Window x11_win = glfwGetX11Window(window);
            if (x11_win) {
                Atom net_wm_pid = XInternAtom(x11_dpy, "_NET_WM_PID", False);
                XDeleteProperty(x11_dpy, x11_win, net_wm_pid);

                XClassHint *ch = XAllocClassHint();
                ch->res_name  = (char*)"gnome-control-center";
                ch->res_class = (char*)"Gnome-control-center";
                XSetClassHint(x11_dpy, x11_win, ch);
                XFree(ch);

                Atom wm_state = XInternAtom(x11_dpy, "_NET_WM_STATE", False);
                Atom wm_above = XInternAtom(x11_dpy, "_NET_WM_STATE_ABOVE", False);
                XEvent xev = {};
                xev.type = ClientMessage;
                xev.xclient.window = x11_win;
                xev.xclient.message_type = wm_state;
                xev.xclient.format = 32;
                xev.xclient.data.l[0] = 1;
                xev.xclient.data.l[1] = (long)wm_above;
                xev.xclient.data.l[2] = 0;
                XSendEvent(x11_dpy, DefaultRootWindow(x11_dpy), False,
                           SubstructureRedirectMask | SubstructureNotifyMask, &xev);

                XFlush(x11_dpy);
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

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (!g_ui_visible) {
            int dw, dh;
            glfwGetFramebufferSize(window, &dw, &dh);
            glViewport(0, 0, dw, dh);
            glClearColor(0.06f, 0.06f, 0.08f, 1.00f);
            glClear(GL_COLOR_BUFFER_BIT);
            glfwSwapBuffers(window);
            continue;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("##MainPanel", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

        std::unique_ptr<GameModule> render_game;
        {
            std::lock_guard<std::mutex> lock(g_game_mutex);
            render_game.swap(g_game);
        }
        if (!render_game)
            render_game = create_game();

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 0.4f, 1.0f));
        ImGui::Text(u8"\u2588\u2588  %s", render_game->game_name());
        ImGui::PopStyleColor();

        ImGui::SameLine(ImGui::GetWindowWidth() * 0.35f);
        if (g_target_pid > 0)
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f),
                "PID: %d | Players: %d", g_target_pid, render_game->player_count());
        else
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "NOT FOUND");

        ImGui::SameLine(ImGui::GetWindowWidth() - 350);
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

        ImGui::SameLine();
        render_game->render_controls();

        std::string st = render_game->status_text();
        if (!st.empty()) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.4f, 1.0f), "%s", st.c_str());
        }

        ImGui::Separator();

        float table_height = ImGui::GetContentRegionAvail().y - 30;
        ImGui::BeginChild("##content", ImVec2(0, table_height), true);
        render_game->render_table();
        ImGui::EndChild();

        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", g_status_msg.c_str());
        ImGui::End();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.06f, 0.06f, 0.08f, 1.00f);
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
