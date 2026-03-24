#ifndef GAME_INTERFACE_H
#define GAME_INTERFACE_H

#include <string>
#include "mem_client.h"

class GameModule {
public:
    virtual ~GameModule() = default;

    virtual const char* game_name()      = 0;
    virtual const char* process_name()   = 0;
    virtual const char* module_filter()  = 0;

    virtual void update(MemClient& client, int pid, uint64_t base) = 0;
    virtual bool is_valid()       = 0;
    virtual int  player_count()   = 0;
    virtual std::string status_text() = 0;

    virtual void render_controls() {}
    virtual void render_table()    = 0;
};

#endif
