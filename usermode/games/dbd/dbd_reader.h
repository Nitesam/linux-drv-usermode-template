#ifndef DBD_READER_H
#define DBD_READER_H

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include "mem_client.h"
#include "logger.h"
#include "dbd_offsets.h"
#include "dbd_gnames.h"

struct DbdWorldState {
    bool valid{};
    int32_t player_count{};
    DbdMinimalViewInfo camera{};
    bool has_camera{};
    uint64_t base_address{};
    std::vector<DbdPlayerData> players{};
    std::vector<DbdObjectData> objects{};
    std::string error{};
};

class DbdReader {
public:
    DbdReader(MemClient& client) : client_(client), cycle_(0) {}

    bool read_ptr(int pid, uint64_t addr, uint64_t& out) {
        unsigned char buf[8]{};
        if (!client_.read_mem(pid, addr, 8, buf))
            return false;
        memcpy(&out, buf, 8);
        return DbdIsLikelyPointer(out);
    }

    template<typename T>
    bool read_val(int pid, uint64_t addr, T& out) {
        unsigned char buf[sizeof(T)]{};
        if (!client_.read_mem(pid, addr, sizeof(T), buf))
            return false;
        memcpy(&out, buf, sizeof(T));
        return true;
    }

    DbdWorldState update(int pid, uint64_t base_address) {
        DbdWorldState state{};
        state.base_address = base_address;
        ++cycle_;

        bool verbose = (cycle_ <= 3) || (cycle_ % 60 == 0);

        if (verbose)
            LOG_CHAIN("==== DBD CYCLE #%lu ====  PID=%d  Base=0x%lX", cycle_, pid, base_address);

        uint64_t gworld = 0;
        {
            unsigned char raw[8]{};
            if (client_.read_mem(pid, base_address + DBD_EGS_GWORLD_OFFSET, 8, raw))
                memcpy(&gworld, raw, 8);
        }

        if (!DbdIsLikelyPointer(gworld)) {
            state.error = "GWorld not found";
            return state;
        }

        if (verbose)
            LOG_CHAIN("[1] GWorld = 0x%lX", gworld);

        if (!gnames_resolved_) {
            static const uint64_t gnames_offsets[] = {
                DBD_EGS_GNAMES_OFFSET,
                0x0BCCE680,
            };

            for (auto off : gnames_offsets) {
                uint64_t addr = base_address + off;
                gnames_.set_address(addr);
                if (gnames_.test_resolve(client_, pid, gworld)) {
                    gnames_resolved_ = true;
                    break;
                }
            }
            if (!gnames_resolved_)
                LOG_CHAIN("[GN] GNames resolution FAILED for all offsets");
        }

        uint64_t persistent_level = 0;
        if (!read_ptr(pid, gworld + DBD_PERSISTENT_LEVEL, persistent_level)) {
            state.error = "PersistentLevel failed";
            return state;
        }

        if (verbose)
            LOG_CHAIN("[2] PersistentLevel = 0x%lX", persistent_level);

        uint64_t owning_instance = 0, local_players = 0;
        uint64_t player_controller = 0, camera_manager = 0;
        uint64_t local_pawn = 0;

        bool chain_ok = read_ptr(pid, gworld + DBD_OWNING_GAME_INSTANCE, owning_instance)
                      && read_ptr(pid, owning_instance + DBD_LOCAL_PLAYERS, local_players)
                      && read_ptr(pid, local_players, local_players)
                      && read_ptr(pid, local_players + DBD_PLAYER_CONTROLLER, player_controller);

        if (chain_ok) {
            read_ptr(pid, player_controller + DBD_ACK_PAWN, local_pawn);
            if (verbose && local_pawn)
                LOG_CHAIN("[LOCAL] Pawn=0x%lX", local_pawn);
        }

        if (chain_ok) {
            if (read_ptr(pid, player_controller + DBD_CAMERA_MANAGER, camera_manager)) {
                DbdCameraCacheEntry cache{};
                if (read_val(pid, camera_manager + DBD_CAMERA_CACHE_PRIVATE, cache)) {
                    if (std::isfinite(cache.POV.FOV) && cache.POV.FOV > 1.0f && cache.POV.FOV < 180.0f) {
                        state.camera = cache.POV;
                        state.has_camera = true;
                        if (verbose)
                            LOG_CHAIN("[CAM] FOV=%.1f Pos=(%.0f,%.0f,%.0f)",
                                      state.camera.FOV,
                                      state.camera.Location.X,
                                      state.camera.Location.Y,
                                      state.camera.Location.Z);
                    } else {
                        DbdCameraCacheEntry cache2{};
                        if (read_val(pid, camera_manager + DBD_CAMERA_CACHE_PRIVATE + 0x8, cache2.POV)) {
                            if (std::isfinite(cache2.POV.FOV) && cache2.POV.FOV > 1.0f && cache2.POV.FOV < 180.0f) {
                                state.camera = cache2.POV;
                                state.has_camera = true;
                            }
                        }
                    }
                }
            }
        }

        read_players_from_gamestate(pid, gworld, local_pawn, state, verbose);

        bool object_scan = (cycle_ % 60 == 0) || (cycle_ <= 3);
        if (object_scan)
            scan_objects(pid, persistent_level, state, verbose);
        else
            state.objects = cached_objects_;

        if (state.has_camera) {
            for (auto& obj : state.objects) {
                obj.distance = DbdVectorDistance(obj.position, state.camera.Location) / 100.0f;
            }
        }

        state.player_count = static_cast<int32_t>(state.players.size());
        state.valid = true;

        if (verbose) {
            int survivors = 0, killers = 0;
            for (auto& p : state.players) {
                if (p.type == EDbdActorType::Survivor) survivors++;
                else killers++;
            }
            LOG_CHAIN("Players: %d total (%d survivors, %d killers) | Objects: %zu",
                      state.player_count, survivors, killers, state.objects.size());
            LOG_CHAIN("==== END DBD CYCLE #%lu ====", cycle_);
        } else if (cycle_ % 10 == 0) {
            LOG_CHAIN("Cycle #%lu: %d players | %zu objects | cam=%s",
                      cycle_, state.player_count, state.objects.size(),
                      state.has_camera ? "OK" : "NO");
        }

        return state;
    }

private:
    MemClient& client_;
    uint64_t cycle_;
    DbdGNames gnames_;
    bool gnames_resolved_{};
    std::vector<DbdObjectData> cached_objects_;

    std::string test_gnames_resolve(int pid, uint64_t gworld) {
        return gnames_.resolve_object_class_name(client_, pid, gworld);
    }

    void read_players_from_gamestate(int pid, uint64_t gworld, uint64_t local_pawn,
                                     DbdWorldState& state, bool verbose)
    {
        uint64_t game_state = 0;
        if (!read_ptr(pid, gworld + DBD_GAME_STATE, game_state))
            return;

        DbdTArray player_array{};
        if (!read_val(pid, game_state + DBD_PLAYER_ARRAY, player_array))
            return;
        if (!DbdIsLikelyPointer(player_array.Data) || player_array.Count == 0 || player_array.Count > 20)
            return;

        if (verbose)
            LOG_CHAIN("[PS] GameState=0x%lX PlayerArray count=%u", game_state, player_array.Count);

        for (uint32_t i = 0; i < player_array.Count; ++i) {
            uint64_t ps = 0;
            if (!read_ptr(pid, player_array.Data + i * 8, ps)) continue;

            EDbdPlayerRole role = EDbdPlayerRole::Role_None;
            read_val(pid, ps + DBD_GAME_ROLE, role);
            if (role != EDbdPlayerRole::Role_Camper && role != EDbdPlayerRole::Role_Slasher)
                continue;

            uint64_t pawn = 0;
            read_ptr(pid, ps + DBD_PAWN_PRIVATE, pawn);

            if (pawn == 0)
                continue;

            if (pawn == local_pawn)
                continue;

            uint64_t root = 0;
            DbdUEVector pos{};
            if (read_ptr(pid, pawn + DBD_ROOT_COMPONENT, root)) {
                read_val(pid, root + DBD_RELATIVE_LOCATION, pos);
            }

            if (!DbdIsFiniteVec(pos) || (pos.X == 0.0 && pos.Y == 0.0 && pos.Z == 0.0))
                continue;

            DbdPlayerData p{};
            p.address = pawn;
            p.role = role;
            if (role == EDbdPlayerRole::Role_Camper) {
                p.type = EDbdActorType::Survivor;
                p.name = "Survivor";
            } else {
                p.type = EDbdActorType::Killer;
                p.name = "Killer";
            }

            read_player_name(pid, ps, p.name);

            p.position = pos;
            if (state.has_camera)
                p.distance = DbdVectorDistance(pos, state.camera.Location) / 100.0f;
            p.valid = true;

            if (verbose)
                LOG_CHAIN("[P] %s pawn=0x%lX pos=(%.0f,%.0f,%.0f)",
                          p.name.c_str(), pawn,
                          p.position.X, p.position.Y, p.position.Z);

            state.players.push_back(p);
        }
    }

    void scan_objects(int pid, uint64_t persistent_level,
                      DbdWorldState& state, bool verbose)
    {
        uint64_t actor_array = 0;
        uint32_t actor_count = 0;

        if (read_ptr(pid, persistent_level + DBD_DIRECT_ACTORS_ARRAY, actor_array))
            read_val(pid, persistent_level + DBD_DIRECT_ACTORS_COUNT, actor_count);

        if (actor_array == 0 || actor_count == 0 || actor_count > 3000)
            return;

        uint32_t ptrs_to_read = (actor_count < 3000) ? actor_count : 3000;
        std::vector<uint64_t> actor_ptrs(ptrs_to_read, 0);
        {
            std::vector<unsigned char> bulk(ptrs_to_read * 8, 0);
            uint32_t total_bytes = ptrs_to_read * 8;
            for (uint32_t off = 0; off < total_bytes; ) {
                uint32_t chunk = total_bytes - off;
                if (chunk > 4096) chunk = 4096;
                if (!client_.read_mem(pid, actor_array + off, chunk, &bulk[off])) break;
                off += chunk;
            }
            memcpy(actor_ptrs.data(), bulk.data(), ptrs_to_read * 8);
        }

        std::unordered_set<uint64_t> seen_addrs;
        int name_log_count = 0;

        for (uint32_t i = 0; i < ptrs_to_read; ++i) {
            uint64_t actor = actor_ptrs[i];
            if (!DbdIsLikelyPointer(actor)) continue;

            if (seen_addrs.count(actor)) continue;

            std::string class_name = gnames_.resolve_object_class_name(client_, pid, actor);
            if (class_name.empty()) continue;

            if (verbose && name_log_count < 10) {
                LOG_CHAIN("[OBJ-NAME] actor=0x%lX class='%s'", actor, class_name.c_str());
                name_log_count++;
            }

            EDbdObjectType obj_type;
            bool found = classify_object(class_name, obj_type);
            if (!found) continue;

            if (verbose)
                LOG_CHAIN("[OBJ-MATCH] %s -> %s", class_name.c_str(), DbdObjectTypeName(obj_type));

            seen_addrs.insert(actor);

            uint64_t root = 0;
            if (!read_ptr(pid, actor + DBD_ROOT_COMPONENT, root)) continue;

            DbdUEVector pos{};
            if (!read_val(pid, root + DBD_RELATIVE_LOCATION, pos)) continue;
            if (!DbdIsFiniteVec(pos)) continue;
            if (pos.X == 0.0 && pos.Y == 0.0 && pos.Z == 0.0) continue;

            DbdObjectData obj{};
            obj.address = actor;
            obj.type = obj_type;
            obj.position = pos;
            state.objects.push_back(obj);
        }

        cached_objects_ = state.objects;

        if (verbose)
            LOG_CHAIN("[OBJ] Scanned %u actors, found %zu objects", ptrs_to_read, state.objects.size());
    }

    static bool classify_object(const std::string& name, EDbdObjectType& out) {
        if (name.find("Generator") != std::string::npos)       { out = EDbdObjectType::Generator; return true; }
        if (name.find("Totem") != std::string::npos)            { out = EDbdObjectType::Totem; return true; }
        if (name.find("Pallet") != std::string::npos)           { out = EDbdObjectType::Pallet; return true; }
        if (name.find("MeatHook") != std::string::npos ||
            name.find("Hook") != std::string::npos)             { out = EDbdObjectType::Hook; return true; }
        if (name.find("Hatch") != std::string::npos)            { out = EDbdObjectType::Hatch; return true; }
        if (name.find("Locker") != std::string::npos)           { out = EDbdObjectType::Locker; return true; }
        if (name.find("Chest") != std::string::npos ||
            name.find("Searchable") != std::string::npos)       { out = EDbdObjectType::Chest; return true; }
        if (name.find("Window") != std::string::npos)           { out = EDbdObjectType::Window; return true; }
        if (name.find("Trap") != std::string::npos ||
            name.find("BearTrap") != std::string::npos)         { out = EDbdObjectType::Trap; return true; }
        if (name.find("EscapeDoor") != std::string::npos ||
            name.find("ExitGate") != std::string::npos)         { out = EDbdObjectType::EscapeDoor; return true; }
        if (name.find("BreakableWall") != std::string::npos ||
            name.find("Breakable") != std::string::npos)        { out = EDbdObjectType::BreakableDoor; return true; }
        return false;
    }

    void read_player_name(int pid, uint64_t player_state, std::string& out_name) {
        DbdTArray name_arr{};
        if (!read_val(pid, player_state + DBD_PLAYER_NAME_PRIVATE, name_arr))
            return;
        if (!DbdIsLikelyPointer(name_arr.Data) || name_arr.Count == 0 || name_arr.Count > 128)
            return;

        uint32_t bytes = name_arr.Count * 2;
        if (bytes > 256) bytes = 256;

        std::vector<unsigned char> buf(bytes, 0);
        if (!client_.read_mem(pid, name_arr.Data, bytes, buf.data()))
            return;

        std::string name;
        for (uint32_t j = 0; j < bytes; j += 2) {
            uint16_t ch = buf[j] | (buf[j+1] << 8);
            if (ch == 0) break;
            if (ch < 128)
                name += static_cast<char>(ch);
            else
                name += '?';
        }

        if (!name.empty())
            out_name = name;
    }
};

#endif
