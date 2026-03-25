#ifndef HLL_READER_H
#define HLL_READER_H

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <mutex>

#include "mem_client.h"
#include "logger.h"
#include "hll_offsets.h"

struct PlayerData {
    int index{};
    FVector location{};
    float health{};
    uint8_t team{};
    uint8_t role{};
    int32_t platoon{-1};
    CapsuleData capsule{};
    FTransform component_to_world{};
    std::string weapon_name{};
    float distance{};
    bool valid{};
    bool has_location{};
    bool has_component_to_world{};
    bool visible{};
    std::vector<FTransform> bone_transforms{};
};

struct HllWorldState {
    bool valid{};
    int32_t player_count{};
    FMinimalViewInfo camera{};
    bool has_camera{};
    uint8_t local_team{};
    bool has_local_team{};
    FWeaponAmmoInfo local_ammo{};
    bool has_local_ammo{};
    uint64_t base_address{};
    float world_time{};
    std::vector<PlayerData> players{};
    std::string error{};
};

class HllReader {
public:
    HllReader(MemClient& client) : client_(client), cycle_(0) {}

    uint64_t find_base_address(int pid) {
        LOG_INFO("Searching base address via IOCTL for PID %d", pid);
        uint64_t addr = client_.get_base_address(pid, "HLLEpicGamesStore");
        if (addr > 0)
            return addr;
        addr = client_.get_base_address(pid, ".exe");
        if (addr > 0)
            return addr;
        return 0;
    }

    bool read_ptr(int pid, uint64_t addr, uint64_t& out) {
        unsigned char buf[8]{};
        if (!client_.read_mem(pid, addr, 8, buf))
            return false;
        memcpy(&out, buf, 8);
        return IsLikelyPointer(out);
    }

    template<typename T>
    bool read_val(int pid, uint64_t addr, T& out) {
        unsigned char buf[sizeof(T)]{};
        if (!client_.read_mem(pid, addr, sizeof(T), buf))
            return false;
        memcpy(&out, buf, sizeof(T));
        return true;
    }

    uint64_t resolve_chain(int pid, uint64_t start, const std::vector<int32_t>& offsets) {
        uint64_t addr = start;
        for (size_t i = 0; i < offsets.size(); ++i) {
            if (i < offsets.size() - 1) {
                uint64_t next = 0;
                if (!read_ptr(pid, addr + offsets[i], next))
                    return 0;
                addr = next;
            } else {
                addr = addr + offsets[i];
            }
        }
        return addr;
    }

    uint64_t resolve_chain_all_ptrs(int pid, uint64_t start, const std::vector<int32_t>& offsets) {
        uint64_t addr = start;
        for (size_t i = 0; i < offsets.size(); ++i) {
            uint64_t next = 0;
            if (!read_ptr(pid, addr + offsets[i], next))
                return 0;
            addr = next;
        }
        return addr;
    }

    HllWorldState update(int pid, uint64_t base_address) {
        HllWorldState state{};
        state.base_address = base_address;
        ++cycle_;

        bool verbose = (cycle_ <= 3) || (cycle_ % 60 == 0);

        if (verbose) {
            LOG_CHAIN("════════════════════ CYCLE #%lu ════════════════════", cycle_);
            LOG_CHAIN("PID=%d | Base=0x%lX", pid, base_address);
        }

        uint64_t gworld = 0;
        if (!read_ptr(pid, base_address + HLL_GWORLD, gworld)) {
            LOG_ERR("[Cycle %lu] ✗ GWorld FAILED at 0x%lX + 0x%X = 0x%lX",
                    cycle_, base_address, HLL_GWORLD, base_address + HLL_GWORLD);
            state.error = "Failed to read GWorld";
            return state;
        }
        if (verbose)
            LOG_CHAIN("[1] GWorld       = read_ptr(0x%lX + 0x%X) → 0x%lX ✓",
                      base_address, HLL_GWORLD, gworld);

        float world_time = 0.0f;
        read_val(pid, gworld + HLL_GWORLD_TIME_SECONDS, world_time);
        state.world_time = world_time;

        uint64_t game_state = 0;
        if (!read_ptr(pid, gworld + HLL_GWORLD_GAME_STATE, game_state)) {
            LOG_ERR("[Cycle %lu] ✗ GameState FAILED at GWorld(0x%lX) + 0x%X",
                    cycle_, gworld, HLL_GWORLD_GAME_STATE);
            state.error = "Failed to read GameState";
            return state;
        }
        if (verbose)
            LOG_CHAIN("[2] GameState    = read_ptr(0x%lX + 0x%X) → 0x%lX ✓",
                      gworld, HLL_GWORLD_GAME_STATE, game_state);

        int32_t player_count = 0;
        uint64_t pc_addr = game_state + HLL_GAME_STATE_PLAYERS + 0x8;
        if (!read_val(pid, pc_addr, player_count)) {
            LOG_ERR("[Cycle %lu] ✗ PlayerCount FAILED at 0x%lX", cycle_, pc_addr);
            state.error = "Failed to read player count";
            return state;
        }
        state.player_count = player_count;
        if (verbose)
            LOG_CHAIN("[3] PlayerCount  = read_val(0x%lX) → %d ✓", pc_addr, player_count);

        uint64_t game_instance = 0;
        uint64_t local_players_data = 0;
        uint64_t local_player = 0;
        uint64_t player_controller = 0;

        bool chain_ok = true;
        int chain_step = 4;

        if (!read_ptr(pid, gworld + HLL_GWORLD_OWNING_GAME_INSTANCE, game_instance)) {
            if (verbose) LOG_WARN("[%d] GameInstance FAILED at 0x%lX + 0x%X",
                                  chain_step, gworld, HLL_GWORLD_OWNING_GAME_INSTANCE);
            chain_ok = false;
        } else {
            if (verbose) LOG_CHAIN("[%d] GameInstance = read_ptr(0x%lX + 0x%X) → 0x%lX ✓",
                                    chain_step, gworld, HLL_GWORLD_OWNING_GAME_INSTANCE, game_instance);
        }

        if (chain_ok) {
            chain_step = 5;
            if (!read_ptr(pid, game_instance + HLL_GAME_INSTANCE_LOCAL_PLAYERS, local_players_data)) {
                if (verbose) LOG_WARN("[%d] LocalPlayers FAILED at 0x%lX + 0x%X",
                                      chain_step, game_instance, HLL_GAME_INSTANCE_LOCAL_PLAYERS);
                chain_ok = false;
            } else {
                if (verbose) LOG_CHAIN("[%d] LocalPlayers = read_ptr(0x%lX + 0x%X) → 0x%lX ✓",
                                        chain_step, game_instance, HLL_GAME_INSTANCE_LOCAL_PLAYERS, local_players_data);
            }
        }

        if (chain_ok) {
            chain_step = 6;
            if (!read_ptr(pid, local_players_data, local_player)) {
                if (verbose) LOG_WARN("[%d] LocalPlayer[0] FAILED at 0x%lX",
                                      chain_step, local_players_data);
                chain_ok = false;
            } else {
                if (verbose) LOG_CHAIN("[%d] LocalPlayer[0] = read_ptr(0x%lX) → 0x%lX ✓",
                                        chain_step, local_players_data, local_player);
            }
        }

        if (chain_ok) {
            chain_step = 7;
            if (!read_ptr(pid, local_player + HLL_ULOCAL_PLAYER_PLAYER_CONTROLLER, player_controller)) {
                if (verbose) LOG_WARN("[%d] PlayerController FAILED at 0x%lX + 0x%X",
                                      chain_step, local_player, HLL_ULOCAL_PLAYER_PLAYER_CONTROLLER);
                chain_ok = false;
            } else {
                if (verbose) LOG_CHAIN("[%d] PlayerCtrl = read_ptr(0x%lX + 0x%X) → 0x%lX ✓",
                                        chain_step, local_player, HLL_ULOCAL_PLAYER_PLAYER_CONTROLLER, player_controller);
            }
        }

        if (chain_ok) {
            uint64_t camera_manager = 0;
            if (read_ptr(pid, player_controller + HLL_PLAYER_CONTROLLER_CAMERA_MANAGER, camera_manager)) {
                if (verbose) LOG_CHAIN("[8] CameraManager = read_ptr(0x%lX + 0x%X) → 0x%lX ✓",
                                        player_controller, HLL_PLAYER_CONTROLLER_CAMERA_MANAGER, camera_manager);
                if (read_val(pid, camera_manager + HLL_CAMERA_MANAGER_CAMERA_CACHE, state.camera)) {
                    state.has_camera = true;
                    LOG_DBG("[CAM] FOV=%.1f dFOV=%.1f Pos=(%.0f,%.0f,%.0f)",
                            state.camera.FOV, state.camera.DesiredFOV,
                            state.camera.Location.X,
                            state.camera.Location.Y,
                            state.camera.Location.Z);
                    if (verbose) {
                        LOG_CHAIN("[9] CameraCache = FOV=%.1f Pos=(%.0f,%.0f,%.0f) ✓",
                                  state.camera.FOV,
                                  state.camera.Location.X,
                                  state.camera.Location.Y,
                                  state.camera.Location.Z);
                    }
                } else {
                    if (verbose) LOG_WARN("[9] CameraCache FAILED at 0x%lX + 0x%X",
                                          camera_manager, HLL_CAMERA_MANAGER_CAMERA_CACHE);
                }
            } else {
                if (verbose) LOG_WARN("[8] CameraManager FAILED at 0x%lX + 0x%X",
                                      player_controller, HLL_PLAYER_CONTROLLER_CAMERA_MANAGER);
            }

            uint64_t ack_pawn = 0;
            if (read_ptr(pid, player_controller + HLL_PLAYER_CONTROLLER_ACK_PAWN, ack_pawn)) {
                if (verbose) LOG_CHAIN("[10] AckPawn = read_ptr(0x%lX + 0x%X) → 0x%lX ✓",
                                        player_controller, HLL_PLAYER_CONTROLLER_ACK_PAWN, ack_pawn);

                uint64_t player_state_ptr = 0;
                if (read_ptr(pid, ack_pawn + HLL_PAWN_PLAYER_STATE, player_state_ptr)) {
                    if (read_val(pid, player_state_ptr + HLL_REP_PLAYER_INFO + HLL_PLAYER_INFO_TEAM, state.local_team)) {
                        state.has_local_team = true;
                        if (verbose) LOG_CHAIN("[11] LocalTeam = %d ✓", state.local_team);
                    }
                } else {
                    if (verbose) LOG_WARN("[11] PlayerState FAILED at AckPawn(0x%lX) + 0x%X",
                                          ack_pawn, HLL_PAWN_PLAYER_STATE);
                }

                uint64_t weapon_ptr = 0;
                if (read_ptr(pid, ack_pawn + HLL_PAWN_CURRENT_WEAPON, weapon_ptr)) {
                    if (read_val(pid, weapon_ptr + HLL_WEAPON_AMMO_INFO, state.local_ammo)) {
                        state.has_local_ammo = true;
                        if (verbose) LOG_CHAIN("[12] LocalAmmo = %d ✓", state.local_ammo.CurrentAmmo);
                    }
                } else {
                    if (verbose) LOG_WARN("[12] Weapon FAILED at AckPawn(0x%lX) + 0x%X",
                                          ack_pawn, HLL_PAWN_CURRENT_WEAPON);
                }
            } else {
                if (verbose) LOG_WARN("[10] AckPawn FAILED at PlayerCtrl(0x%lX) + 0x%X",
                                      player_controller, HLL_PLAYER_CONTROLLER_ACK_PAWN);
            }
        }

        if (verbose && !chain_ok)
            LOG_WARN("Local player chain BROKEN at step %d — camera/team/ammo unavailable", chain_step);

        if (player_count <= 1) {
            state.valid = true;
            state.error = "Waiting for match";
            if (verbose) LOG_CHAIN("PlayerCount=%d → waiting for match", player_count);
            return state;
        }

        uint64_t players_array_ptr = 0;
        if (!read_ptr(pid, game_state + HLL_GAME_STATE_PLAYERS, players_array_ptr)) {
            state.valid = true;
            state.error = "Failed to read players array";
            LOG_WARN("[Cycle %lu] Players array pointer FAILED at GameState(0x%lX) + 0x%X",
                     cycle_, game_state, HLL_GAME_STATE_PLAYERS);
            return state;
        }
        if (verbose)
            LOG_CHAIN("[13] PlayersArray = read_ptr(0x%lX + 0x%X) → 0x%lX ✓",
                      game_state, HLL_GAME_STATE_PLAYERS, players_array_ptr);

        state.players.reserve(player_count);

        unsigned char player_ptrs_buf[4096]{};
        int ptrs_to_read = (player_count < 512) ? player_count : 512;
        int ptrs_bytes = ptrs_to_read * 8;
        if (ptrs_bytes > 4096) ptrs_bytes = 4096;

        if (!client_.read_mem(pid, players_array_ptr, ptrs_bytes, player_ptrs_buf)) {
            state.valid = true;
            state.error = "Failed to batch-read player pointers";
            LOG_ERR("[Cycle %lu] Batch read of %d player pointers FAILED", cycle_, ptrs_to_read);
            return state;
        }

        int valid_count = 0, located_count = 0, dead_count = 0;

        for (int i = 1; i < ptrs_to_read; ++i) {
            uint64_t ps_ptr = 0;
            memcpy(&ps_ptr, player_ptrs_buf + i * 8, 8);
            if (!IsLikelyPointer(ps_ptr))
                continue;

            PlayerData p{};
            p.index = i;

            read_val(pid, ps_ptr + HLL_REP_PLAYER_INFO + HLL_PLAYER_INFO_TEAM, p.team);
            read_val(pid, ps_ptr + HLL_REP_PLAYER_INFO + HLL_PLAYER_INFO_ROLE, p.role);
            read_val(pid, ps_ptr + HLL_REP_PLAYER_INFO + HLL_PLAYER_INFO_PLATOON, p.platoon);

            uint64_t pawn = 0;
            if (!read_ptr(pid, ps_ptr + HLL_PAWN_PRIVATE, pawn)) {
                p.valid = true;
                state.players.push_back(p);
                valid_count++;
                continue;
            }

            read_val(pid, pawn + HLL_PAWN_CHARACTER_HEALTH, p.health);
            if (p.health <= 0.0f) dead_count++;

            uint64_t root_comp = 0;
            if (read_ptr(pid, pawn + HLL_ACTOR_ROOT_COMPONENT, root_comp)) {
                if (read_val(pid, root_comp + HLL_ACTOR_ROOT_COMPONENT_REL_LOC, p.location)) {
                    p.has_location = true;
                    located_count++;
                }
            }

            uint64_t capsule_comp = 0;
            if (read_ptr(pid, pawn + HLL_PAWN_CAPSULE_COMPONENT, capsule_comp))
                read_val(pid, capsule_comp + HLL_PAWN_CAPSULE_HALF_HEIGHT, p.capsule);

            uint64_t skel_mesh = 0;
            if (read_ptr(pid, pawn + HLL_PAWN_SKELETAL_MESH, skel_mesh)) {
                read_val(pid, skel_mesh + HLL_MESH_COMPONENT_TO_WORLD, p.component_to_world);
                p.has_component_to_world = true;

                UETArray bone_arr{};
                UETArray bone_arr1{};
                bool has_bones = false;

                if (read_val(pid, skel_mesh + HLL_SKINNED_MESH_SPACE_TRANSFORMS, bone_arr) &&
                    IsLikelyPointer(bone_arr.ArrayPointer) && bone_arr.Length > 0) {
                    has_bones = true;
                } else if (read_val(pid, skel_mesh + HLL_SKINNED_MESH_SPACE_TRANSFORMS_1, bone_arr1) &&
                           IsLikelyPointer(bone_arr1.ArrayPointer) && bone_arr1.Length > 0) {
                    bone_arr = bone_arr1;
                    has_bones = true;
                }

                if (has_bones) {
                    int bone_count = (bone_arr.Length < (int)HLL_MAX_BONES) ? bone_arr.Length : (int)HLL_MAX_BONES;
                    int bone_bytes = bone_count * sizeof(FTransform);

                    if (bone_bytes <= 4096) {
                        unsigned char bone_buf[4096]{};
                        if (client_.read_mem(pid, bone_arr.ArrayPointer, bone_bytes, bone_buf)) {
                            p.bone_transforms.resize(bone_count);
                            memcpy(p.bone_transforms.data(), bone_buf, bone_bytes);
                        }
                    } else {
                        p.bone_transforms.resize(bone_count);
                        int offset = 0;
                        int remaining = bone_bytes;
                        while (remaining > 0) {
                            int chunk = (remaining > 4096) ? 4096 : remaining;
                            unsigned char bone_buf[4096]{};
                            if (!client_.read_mem(pid, bone_arr.ArrayPointer + offset, chunk, bone_buf))
                                break;
                            memcpy(reinterpret_cast<uint8_t*>(p.bone_transforms.data()) + offset, bone_buf, chunk);
                            offset += chunk;
                            remaining -= chunk;
                        }
                        if (remaining > 0)
                            p.bone_transforms.clear();
                    }
                }
            }

            uint64_t weapon = 0;
            if (read_ptr(pid, pawn + HLL_PAWN_CURRENT_WEAPON, weapon)) {
                uint8_t wtype = 0;
                if (read_val(pid, weapon + HLL_WEAPON_TYPE, wtype))
                    p.weapon_name = WeaponName(static_cast<EWeaponType>(wtype));
            }

            if (p.has_location && state.has_camera && IsFiniteVector(p.location) && IsFiniteVector(state.camera.Location)) {
                p.distance = VectorDistance(p.location, state.camera.Location) / 100.0f;
            }

            p.valid = true;
            state.players.push_back(p);
            valid_count++;
        }

        state.valid = true;

        if (verbose) {
            LOG_CHAIN("Players: %d/%d valid | %d located | %d dead",
                      valid_count, player_count, located_count, dead_count);
            if (state.has_camera)
                LOG_CHAIN("Camera: FOV=%.1f Pos=(%.0f,%.0f,%.0f) | Team=%d | Ammo=%d",
                          state.camera.FOV, state.camera.Location.X,
                          state.camera.Location.Y, state.camera.Location.Z,
                          state.has_local_team ? state.local_team : 0,
                          state.has_local_ammo ? state.local_ammo.CurrentAmmo : -1);
            LOG_CHAIN("════════════════════ END CYCLE #%lu ════════════════════", cycle_);
        } else {
            if (cycle_ % 10 == 0) {
                LOG_CHAIN("Cycle #%lu: OK | %d players (%d valid) | cam=%s | team=%d",
                          cycle_, player_count, valid_count,
                          state.has_camera ? "✓" : "✗",
                          state.has_local_team ? state.local_team : 0);
            }
        }

        return state;
    }

private:
    MemClient& client_;
    uint64_t cycle_;
};

#endif
