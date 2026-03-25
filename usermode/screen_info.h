#ifndef SCREEN_INFO_H
#define SCREEN_INFO_H

#include <cstdio>
#include <string>
#include <vector>
#include <GLFW/glfw3.h>

struct ScreenInfo {
    int index;
    int x, y;
    int width, height;
    std::string label;
};

inline std::vector<ScreenInfo> get_available_screens()
{
    std::vector<ScreenInfo> screens;

    int count = 0;
    GLFWmonitor **monitors = glfwGetMonitors(&count);
    if (!monitors || count <= 0) {
        screens.push_back({0, 0, 0, 1920, 1080, "Default (1920x1080)"});
        return screens;
    }

    for (int i = 0; i < count; ++i) {
        int x = 0, y = 0;
        glfwGetMonitorPos(monitors[i], &x, &y);
        const GLFWvidmode *mode = glfwGetVideoMode(monitors[i]);
        int w = mode ? mode->width  : 1920;
        int h = mode ? mode->height : 1080;

        const char *name = glfwGetMonitorName(monitors[i]);
        char buf[128];
        snprintf(buf, sizeof(buf), "%s (%dx%d at %d,%d)",
                 name ? name : "Monitor", w, h, x, y);
        screens.push_back({i, x, y, w, h, std::string(buf)});
    }

    return screens;
}

#endif
