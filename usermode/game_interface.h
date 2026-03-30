#ifndef GAME_INTERFACE_H
#define GAME_INTERFACE_H

#include <string>
#include "mem_client.h"

struct ImDrawList;

class GameModule {
public:
    virtual ~GameModule() = default;

    virtual const char* game_name()      = 0;
    virtual const char* process_name()   = 0;
    virtual std::vector<const char*> alt_process_names() { return {}; }
    virtual const char* module_filter()  = 0;

    virtual void update(MemClient& client, int pid, uint64_t base) = 0;
    virtual bool is_valid()       = 0;
    virtual int  player_count()   = 0;
    virtual std::string status_text() = 0;

    virtual void render_controls() {}
    virtual void render_table()    = 0;
    virtual void render_esp(ImDrawList* draw_list, int screen_w, int screen_h) { (void)draw_list; (void)screen_w; (void)screen_h; }
    virtual void render_esp_controls() {}
    virtual void render_debug_panel() {}

    virtual std::string save_settings() { return {}; }
    virtual void load_settings(const std::string&) {}

    virtual std::string to_json() { return "{}"; }

    virtual std::shared_ptr<GameModule> clone_for_render() {
        return std::shared_ptr<GameModule>(nullptr);
    }
};

#endif
