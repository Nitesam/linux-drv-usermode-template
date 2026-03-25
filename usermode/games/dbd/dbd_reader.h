#ifndef DBD_READER_H
#define DBD_READER_H

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

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

        uint64_t gworld_addr = base_address + DBD_STEAM_GWORLD_OFFSET;
        uint64_t gworld = 0;
        if (!read_ptr(pid, gworld_addr, gworld)) {
            gworld_addr = base_address + DBD_EGS_GWORLD_OFFSET;
            if (!read_ptr(pid, gworld_addr, gworld)) {
                state.error = "Failed to read GWorld";
                LOG_ERR("GWorld FAILED at both Steam/EGS offsets");
                return state;
            }
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

        uint32_t ptrs_to_read = (actor_count < 500) ? actor_count : 500;
        std::vector<uint64_t> actor_ptrs(ptrs_to_read, 0);
        uint32_t bytes_per_batch = 4096 / 8;

        for (uint32_t offset = 0; offset < ptrs_to_read; ) {
            uint32_t batch = ptrs_to_read - offset;
            if (batch > bytes_per_batch) batch = bytes_per_batch;
            uint32_t batch_bytes = batch * 8;

            unsigned char buf[4096]{};
            if (batch_bytes > 4096) batch_bytes = 4096;
            if (!client_.read_mem(pid, actor_array + offset * 8, batch_bytes, buf))
                break;
            memcpy(&actor_ptrs[offset], buf, batch_bytes);
            offset += batch;
        }

        struct ActorRaw {
            uint64_t addr;
            uint64_t player_state;
            uint64_t ack_pawn;
            uint64_t root_component;
        };

        std::vector<ActorRaw> candidates;
        for (uint32_t i = 0; i < ptrs_to_read; ++i) {
            uint64_t actor = actor_ptrs[i];
            if (!DbdIsLikelyPointer(actor))
                continue;

            ActorRaw raw{};
            raw.addr = actor;

            unsigned char batch[32]{};
            if (!client_.read_mem(pid, actor + DBD_PLAYER_STATE, 8, batch))
                continue;
            memcpy(&raw.player_state, batch, 8);

            if (!client_.read_mem(pid, actor + DBD_ACK_PAWN, 8, batch))
                continue;
            memcpy(&raw.ack_pawn, batch, 8);

            if (!client_.read_mem(pid, actor + DBD_ROOT_COMPONENT, 8, batch))
                continue;
            memcpy(&raw.root_component, batch, 8);

            if (!DbdIsLikelyPointer(raw.root_component))
                continue;
            if (raw.ack_pawn != 0)
                continue;
            if (!DbdIsLikelyPointer(raw.player_state))
                continue;

            candidates.push_back(raw);
        }

        for (auto& raw : candidates) {
            EDbdPlayerRole game_role = EDbdPlayerRole::Role_None;
            read_val(pid, raw.player_state + DBD_GAME_ROLE, game_role);

            if (game_role != EDbdPlayerRole::Role_Camper && game_role != EDbdPlayerRole::Role_Slasher)
                continue;

            DbdPlayerData p{};
            p.address = raw.addr;
            p.role = game_role;

            if (game_role == EDbdPlayerRole::Role_Camper) {
                p.type = EDbdActorType::Survivor;
                p.name = "Survivor";
            } else {
                p.type = EDbdActorType::Killer;
                p.name = "Killer";
            }

            DbdUEVector pos{};
            if (read_val(pid, raw.root_component + DBD_RELATIVE_LOCATION, pos)) {
                if (DbdIsFiniteVec(pos) && (pos.X != 0.0 || pos.Y != 0.0 || pos.Z != 0.0)) {
                    p.position = pos;
                    if (state.has_camera) {
                        p.distance = DbdVectorDistance(pos, state.camera.Location) / 100.0f;
                    }
                    p.valid = true;
                }
            }

            if (p.valid)
                state.players.push_back(p);
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

private:
    MemClient& client_;
    uint64_t cycle_;
};

#endif
