#ifndef DBD_READER_H
#define DBD_READER_H

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>

#include "mem_client.h"
#include "logger.h"
#include "dbd_offsets.h"

struct DbdWorldState {
    bool valid{};
    int32_t player_count{};
    DbdMinimalViewInfo camera{};
    bool has_camera{};
    uint64_t base_address{};
    std::vector<DbdPlayerData> players{};
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
        uint64_t gworld_raw_steam = 0, gworld_raw_egs = 0;
        bool steam_read_ok = false, egs_read_ok = false;

        {
            unsigned char raw[8]{};
            uint64_t addr_s = base_address + DBD_STEAM_GWORLD_OFFSET;
            if (client_.read_mem(pid, addr_s, 8, raw)) {
                memcpy(&gworld_raw_steam, raw, 8);
                steam_read_ok = true;
            }
            uint64_t addr_e = base_address + DBD_EGS_GWORLD_OFFSET;
            if (client_.read_mem(pid, addr_e, 8, raw)) {
                memcpy(&gworld_raw_egs, raw, 8);
                egs_read_ok = true;
            }
        }

        if (verbose) {
            LOG_CHAIN("[GW] Steam  addr=0x%lX  read=%s  raw=0x%lX  ptr=%s",
                base_address + DBD_STEAM_GWORLD_OFFSET,
                steam_read_ok ? "OK" : "FAIL",
                gworld_raw_steam,
                DbdIsLikelyPointer(gworld_raw_steam) ? "YES" : "NO");
            LOG_CHAIN("[GW] EGS    addr=0x%lX  read=%s  raw=0x%lX  ptr=%s",
                base_address + DBD_EGS_GWORLD_OFFSET,
                egs_read_ok ? "OK" : "FAIL",
                gworld_raw_egs,
                DbdIsLikelyPointer(gworld_raw_egs) ? "YES" : "NO");
        }

        if (steam_read_ok && DbdIsLikelyPointer(gworld_raw_steam))
            gworld = gworld_raw_steam;
        else if (egs_read_ok && DbdIsLikelyPointer(gworld_raw_egs))
            gworld = gworld_raw_egs;

        if (gworld == 0) {
            state.error = "Failed to read GWorld";
            LOG_ERR("GWorld FAILED at both Steam/EGS offsets");
            return state;
        }
        if (verbose)
            LOG_CHAIN("[1] GWorld = 0x%lX", gworld);

        uint64_t persistent_level = 0;
        if (!read_ptr(pid, gworld + DBD_PERSISTENT_LEVEL, persistent_level)) {
            state.error = "Failed to read PersistentLevel";
            return state;
        }
        if (verbose)
            LOG_CHAIN("[2] PersistentLevel = 0x%lX", persistent_level);

        uint64_t game_instance = 0;
        uint64_t local_player = 0;
        uint64_t player_controller = 0;
        uint64_t camera_manager = 0;
        bool chain_ok = true;

        if (!read_ptr(pid, gworld + DBD_OWNING_GAME_INSTANCE, game_instance)) {
            if (verbose) LOG_WARN("GameInstance FAILED");
            chain_ok = false;
        }

        if (chain_ok) {
            uint64_t local_players_arr = 0;
            if (!read_ptr(pid, game_instance + DBD_LOCAL_PLAYERS, local_players_arr)) {
                if (verbose) LOG_WARN("LocalPlayers FAILED");
                chain_ok = false;
            } else {
                if (!read_ptr(pid, local_players_arr, local_player)) {
                    if (verbose) LOG_WARN("LocalPlayer[0] FAILED");
                    chain_ok = false;
                }
            }
        }

        if (chain_ok) {
            if (!read_ptr(pid, local_player + DBD_PLAYER_CONTROLLER, player_controller)) {
                if (verbose) LOG_WARN("PlayerController FAILED");
                chain_ok = false;
            }
        }

        uint64_t local_pawn = 0;
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
            } else {
                if (verbose) LOG_WARN("CameraManager FAILED");
            }
        }

        uint64_t direct_actors = 0;
        uint32_t direct_count = 0;
        uint64_t actor_array = 0;
        uint32_t actor_count = 0;

        if (read_ptr(pid, persistent_level + DBD_DIRECT_ACTORS_ARRAY, direct_actors)) {
            read_val(pid, persistent_level + DBD_DIRECT_ACTORS_COUNT, direct_count);
            if (direct_actors && direct_count > 0 && direct_count <= 2000) {
                actor_array = direct_actors;
                actor_count = direct_count;
            }
        }

        if (actor_array == 0 || actor_count == 0) {
            uint64_t actor_cluster = 0;
            if (read_ptr(pid, persistent_level + DBD_ACTOR_CLUSTER, actor_cluster)) {
                DbdTArray arr{};
                if (read_val(pid, actor_cluster + DBD_ACTOR_ARRAY, arr)) {
                    if (arr.Data && arr.Count > 0 && arr.Count <= 2000) {
                        actor_array = arr.Data;
                        actor_count = arr.Count;
                    }
                }
            }
        }

        if (actor_array == 0 || actor_count == 0) {
            DbdTArray model_comps{};
            if (read_val(pid, persistent_level + DBD_MODEL_COMPONENTS, model_comps)) {
                if (model_comps.Data && model_comps.Count > 0 && model_comps.Count <= 128) {
                    for (uint32_t ci = 0; ci < model_comps.Count && actor_array == 0; ++ci) {
                        uint64_t container = 0;
                        if (!read_ptr(pid, model_comps.Data + ci * 8, container))
                            continue;
                        DbdTArray arr{};
                        if (read_val(pid, container + DBD_ACTOR_ARRAY, arr)) {
                            if (arr.Data && arr.Count > 0 && arr.Count <= 2000) {
                                actor_array = arr.Data;
                                actor_count = arr.Count;
                            }
                        }
                    }
                }
            }
        }

        if (actor_array == 0 || actor_count == 0) {
            state.valid = true;
            state.error = "No actors found";
            if (verbose) LOG_WARN("No actor array found");
            return state;
        }

        if (verbose)
            LOG_CHAIN("[3] ActorArray=0x%lX Count=%u", actor_array, actor_count);
        bool full_scan = (cycle_ % 30 == 0) || (cycle_ <= 3);

        if (!full_scan) {
            for (auto& [ps_addr, entry] : stable_players_) {
                if (entry.data.address != 0) {
                    uint64_t root = 0;
                    if (read_ptr(pid, entry.data.address + DBD_ROOT_COMPONENT, root)) {
                        DbdUEVector pos{};
                        if (read_val(pid, root + DBD_RELATIVE_LOCATION, pos)) {
                            if (DbdIsFiniteVec(pos) && (pos.X != 0.0 || pos.Y != 0.0 || pos.Z != 0.0)) {
                                entry.data.position = pos;
                                if (state.has_camera)
                                    entry.data.distance = DbdVectorDistance(pos, state.camera.Location) / 100.0f;
                            }
                        }
                    }
                }
                state.players.push_back(entry.data);
            }
            state.player_count = static_cast<int32_t>(state.players.size());
            state.valid = true;
            return state;
        }

        uint32_t ptrs_to_read = (actor_count < 2000) ? actor_count : 2000;
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

        std::unordered_map<uint64_t, DbdPlayerData> frame_players;

        for (uint32_t i = 0; i < ptrs_to_read; ++i) {
            uint64_t actor = actor_ptrs[i];
            if (!DbdIsLikelyPointer(actor)) continue;

            uint64_t actor_ps = 0, actor_root = 0;

            if (!read_ptr(pid, actor + DBD_PLAYER_STATE, actor_ps)) continue;

            uint64_t ps_class = 0;
            if (!read_ptr(pid, actor_ps + 0x10, ps_class)) continue;

            if (!read_ptr(pid, actor + DBD_ROOT_COMPONENT, actor_root)) continue;
            if (!DbdIsLikelyPointer(actor_root)) continue;

            EDbdPlayerRole role = EDbdPlayerRole::Role_None;
            read_val(pid, actor_ps + DBD_GAME_ROLE, role);
            if (role != EDbdPlayerRole::Role_Camper && role != EDbdPlayerRole::Role_Slasher)
                continue;

            if (local_pawn != 0 && actor == local_pawn) continue;

            if (frame_players.count(actor_ps)) {
                auto& existing = frame_players[actor_ps];
                if (existing.position.X == 0.0 && existing.position.Y == 0.0) {
                } else {
                    continue;
                }
            }

            DbdPlayerData p{};
            p.address = actor;
            p.role = role;
            if (role == EDbdPlayerRole::Role_Camper) {
                p.type = EDbdActorType::Survivor;
                p.name = "Survivor";
            } else {
                p.type = EDbdActorType::Killer;
                p.name = "Killer";
            }

            read_player_name(pid, actor_ps, p.name);

            DbdUEVector pos{};
            if (read_val(pid, actor_root + DBD_RELATIVE_LOCATION, pos)) {
                if (DbdIsFiniteVec(pos) && (pos.X != 0.0 || pos.Y != 0.0 || pos.Z != 0.0)) {
                    p.position = pos;
                    if (state.has_camera)
                        p.distance = DbdVectorDistance(pos, state.camera.Location) / 100.0f;
                }
            }
            p.valid = true;

            if (verbose)
                LOG_CHAIN("[P] %s ps=0x%lX pos=(%.0f,%.0f,%.0f)",
                          p.name.c_str(), actor_ps,
                          p.position.X, p.position.Y, p.position.Z);

            frame_players[actor_ps] = p;
        }

        for (auto it = stable_players_.begin(); it != stable_players_.end(); ) {
            if (frame_players.count(it->first)) {
                it->second.missing_frames = 0;
                it->second.data = frame_players[it->first];
                ++it;
            } else {
                it->second.missing_frames++;
                if (it->second.missing_frames > 10)
                    it = stable_players_.erase(it);
                else
                    ++it;
            }
        }
        for (auto& [ps_addr, pdata] : frame_players) {
            if (!stable_players_.count(ps_addr)) {
                StableEntry e;
                e.data = pdata;
                e.missing_frames = 0;
                stable_players_[ps_addr] = e;
            }
        }

        for (auto& [ps_addr, entry] : stable_players_) {
            state.players.push_back(entry.data);
        }

        state.player_count = static_cast<int32_t>(state.players.size());
        state.valid = true;

        if (verbose) {
            int survivors = 0, killers = 0;
            for (auto& p : state.players) {
                if (p.type == EDbdActorType::Survivor) survivors++;
                else killers++;
            }
            LOG_CHAIN("Players: %d total (%d survivors, %d killers)",
                      state.player_count, survivors, killers);
            LOG_CHAIN("==== END DBD CYCLE #%lu ====", cycle_);
        } else if (cycle_ % 10 == 0) {
            LOG_CHAIN("Cycle #%lu: %d players | cam=%s",
                      cycle_, state.player_count,
                      state.has_camera ? "OK" : "NO");
        }

        return state;
    }

    void read_player_name(int pid, uint64_t player_state, std::string& out_name) {
        uint64_t fstring_data = 0;
        int32_t  fstring_len = 0;

        unsigned char buf[16]{};
        if (!client_.read_mem(pid, player_state + DBD_PLAYER_NAME_PRIVATE, 16, buf))
            return;
        memcpy(&fstring_data, buf, 8);
        memcpy(&fstring_len, buf + 8, 4);

        if (!DbdIsLikelyPointer(fstring_data) || fstring_len <= 0 || fstring_len > 128)
            return;

        uint32_t byte_len = fstring_len * 2;
        std::vector<unsigned char> wbuf(byte_len + 2, 0);
        if (!client_.read_mem(pid, fstring_data, byte_len, wbuf.data()))
            return;

        std::string name;
        name.reserve(fstring_len);
        for (int32_t i = 0; i < fstring_len; ++i) {
            uint16_t ch = wbuf[i * 2] | (wbuf[i * 2 + 1] << 8);
            if (ch == 0) break;
            if (ch < 128)
                name += static_cast<char>(ch);
            else
                name += '?';
        }

        if (!name.empty())
            out_name = name;
    }

private:
    MemClient& client_;
    uint64_t cycle_;

    struct StableEntry {
        DbdPlayerData data;
        int missing_frames{};
    };
    std::unordered_map<uint64_t, StableEntry> stable_players_;
};

#endif
