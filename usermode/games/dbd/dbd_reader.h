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

        bool verbose = (cycle_ == 1);

        if (verbose)
            LOG_CHAIN("==== DBD CYCLE #1 ====  PID=%d  Base=0x%lX", pid, base_address);

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

        bool object_scan = (cycle_ % 60 == 0) || (cycle_ == 1);
        if (object_scan)
            scan_objects(pid, persistent_level, state, verbose);
        else {
            state.objects = cached_objects_;
            for (auto& obj : state.objects)
                read_object_state(pid, obj.address, obj);
        }

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
            LOG_CHAIN("Players: %d (S:%d K:%d) | Objects: %zu",
                      state.player_count, survivors, killers, state.objects.size());
        }

        return state;
    }

private:
    MemClient& client_;
    uint64_t cycle_;
    DbdGNames gnames_;
    bool gnames_resolved_{};
    std::vector<DbdObjectData> cached_objects_;
    uint32_t bone_array_offset_{};
    int bone_info_stride_{};
    uint32_t c2w_rot_offset_{};
    std::unordered_set<uint64_t> bone_mapped_addrs_;

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

            DbdPlayerData p{};
            p.role = role;
            if (role == EDbdPlayerRole::Role_Camper) {
                p.type = EDbdActorType::Survivor;
                p.name = "Survivor";
            } else {
                p.type = EDbdActorType::Killer;
                p.name = "Killer";
            }

            read_player_name(pid, ps, p.name);

            if (pawn != 0 && pawn != local_pawn) {
                p.address = pawn;
                uint64_t root = 0;
                DbdUEVector pos{};
                if (read_ptr(pid, pawn + DBD_ROOT_COMPONENT, root))
                    read_val(pid, root + DBD_RELATIVE_LOCATION, pos);

                if (DbdIsFiniteVec(pos) && !(pos.X == 0.0 && pos.Y == 0.0 && pos.Z == 0.0)) {
                    p.position = pos;
                    if (state.has_camera)
                        p.distance = DbdVectorDistance(pos, state.camera.Location) / 100.0f;
                }

                read_player_enrichment(pid, ps, pawn, p);
            } else {
                read_player_enrichment_lobby(pid, ps, p);
            }

            p.valid = true;

            if (verbose)
                LOG_CHAIN("[P] %s pawn=0x%lX pos=(%.0f,%.0f,%.0f) hp=%d lv=%d p=%d",
                          p.name.c_str(), pawn,
                          p.position.X, p.position.Y, p.position.Z,
                          p.health_states, p.level, p.prestige);

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



            EDbdObjectType obj_type;
            bool found = classify_object(class_name, obj_type);
            if (!found) continue;



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

            read_object_state(pid, actor, obj);

            state.objects.push_back(obj);
        }

        cached_objects_ = state.objects;

        if (verbose)
            LOG_CHAIN("[OBJ] Scanned %u actors, matched %zu objects", ptrs_to_read, state.objects.size());
    }

    static bool classify_object(const std::string& name, EDbdObjectType& out) {
        if (name.find("Generator") != std::string::npos)       { out = EDbdObjectType::Generator; return true; }
        if (name.find("Totem") != std::string::npos)            { out = EDbdObjectType::Totem; return true; }
        if (name.find("Pallet") != std::string::npos)           { out = EDbdObjectType::Pallet; return true; }
        if (name.find("MeatHook") != std::string::npos ||
            name.find("Hook") != std::string::npos ||
            name.find("Locker") != std::string::npos)           { out = EDbdObjectType::Hook; return true; }
        if (name.find("Hatch") != std::string::npos)            { out = EDbdObjectType::Hatch; return true; }
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

    void read_object_state(int pid, uint64_t actor, DbdObjectData& obj) {
        switch (obj.type) {
            case EDbdObjectType::Generator: {
                float native_pct = 0;
                read_val(pid, actor + 0x7B4, native_pct);
                if (std::isfinite(native_pct) && native_pct >= 0 && native_pct <= 1.01f) {
                    obj.gen_progress = native_pct * 100.0f;
                    obj.gen_max_charge = 100.0f;
                }
                uint8_t repaired = 0;
                read_val(pid, actor + 0x6E8, repaired);
                if (repaired) {
                    obj.gen_progress = 100.0f;
                    obj.gen_max_charge = 100.0f;
                }
                uint8_t blocked = 0;
                read_val(pid, actor + DBD_GEN_IS_BLOCKED, blocked);
                obj.gen_blocked = (blocked != 0);
                break;
            }
            case EDbdObjectType::Pallet: {
                read_val(pid, actor + DBD_PALLET_STATE, obj.pallet_state);
                break;
            }
            case EDbdObjectType::Totem: {
                read_val(pid, actor + DBD_TOTEM_STATE, obj.totem_state);
                break;
            }
            case EDbdObjectType::Hatch: {
                read_val(pid, actor + DBD_HATCH_STATE, obj.hatch_state);
                break;
            }
            case EDbdObjectType::Hook: {
                uint64_t hooked = 0;
                unsigned char hbuf[8]{};
                if (client_.read_mem(pid, actor + DBD_HOOK_SURVIVOR, 8, hbuf)) {
                    memcpy(&hooked, hbuf, 8);
                    obj.hook_occupied = DbdIsLikelyPointer(hooked);
                }
                uint8_t basement = 0;
                read_val(pid, actor + DBD_HOOK_IS_BASEMENT, basement);
                obj.hook_basement = (basement != 0);
                break;
            }
            case EDbdObjectType::Chest: {
                uint8_t opened = 0;
                read_val(pid, actor + DBD_CHEST_IS_OPENED, opened);
                obj.chest_opened = (opened != 0);
                break;
            }
            case EDbdObjectType::EscapeDoor: {
                uint8_t activated = 0;
                read_val(pid, actor + DBD_ESCAPE_ACTIVATED, activated);
                obj.escape_activated = (activated != 0);
                break;
            }
            default: break;
        }
    }

    void read_player_enrichment(int pid, uint64_t ps, uint64_t pawn, DbdPlayerData& p) {
        int32_t level = -1, prestige = -1;
        read_val(pid, ps + DBD_PLAYER_DATA + DBD_PLAYER_DATA_CHAR_LEVEL, level);
        read_val(pid, ps + DBD_PLAYER_DATA + DBD_PLAYER_DATA_PRESTIGE, prestige);
        if (level >= 0 && level <= 100) p.level = level;
        if (prestige >= 0 && prestige <= 100) p.prestige = prestige;

        if (pawn != 0 && p.type == EDbdActorType::Survivor) {
            uint64_t health_comp = 0;
            if (read_ptr(pid, pawn + DBD_SURVIVOR_HEALTH_COMP, health_comp)) {
                int32_t states = 0;
                if (read_val(pid, health_comp + DBD_HEALTH_STATE_COUNT, states)) {
                    if (states >= 0 && states <= 3)
                        p.health_states = states;
                }
            }
        }

        read_player_bones(pid, pawn, p);
    }

    void read_player_enrichment_lobby(int pid, uint64_t ps, DbdPlayerData& p) {
        int32_t level = -1, prestige = -1;
        read_val(pid, ps + DBD_PLAYER_DATA + DBD_PLAYER_DATA_CHAR_LEVEL, level);
        read_val(pid, ps + DBD_PLAYER_DATA + DBD_PLAYER_DATA_PRESTIGE, prestige);
        if (level >= 0 && level <= 100) p.level = level;
        if (prestige >= 0 && prestige <= 100) p.prestige = prestige;
    }

    void read_player_bones(int pid, uint64_t pawn, DbdPlayerData& p) {
        memset(p.bone_map, -1, sizeof(p.bone_map));

        uint64_t mesh_comp = 0;
        if (!read_ptr(pid, pawn + DBD_CHARACTER_MESH, mesh_comp)) return;
        if (!DbdIsLikelyPointer(mesh_comp)) return;
        p.mesh_component = mesh_comp;

        DbdUEVector base_pos = p.position;
        base_pos.Z -= 88.0;

        double qx = 0, qy = 0, qz = 0, qw = 1;
        if (c2w_rot_offset_ == 0) {
            unsigned char scan[0x300];
            if (client_.read_mem(pid, mesh_comp, 0x300, scan)) {
                for (uint32_t off = 0x100; off <= 0x280; off += 8) {
                    double r[4];
                    memcpy(r, scan + off, 32);
                    double len2 = r[0]*r[0] + r[1]*r[1] + r[2]*r[2] + r[3]*r[3];
                    if (len2 > 0.95 && len2 < 1.05 && std::isfinite(r[0])) {
                        double tx, ty, tz;
                        memcpy(&tx, scan + off + 32, 8);
                        memcpy(&ty, scan + off + 40, 8);
                        memcpy(&tz, scan + off + 48, 8);
                        if (std::isfinite(tx) && std::abs(tx) > 100 && std::abs(tx) < 1e7) {
                            c2w_rot_offset_ = off;
                            LOG_CHAIN("[BONE] Found C2W quaternion at meshComp+0x%X (%.3f,%.3f,%.3f,%.3f) pos=(%.0f,%.0f,%.0f)",
                                off, r[0], r[1], r[2], r[3], tx, ty, tz);
                            break;
                        }
                    }
                }
            }
        }

        if (c2w_rot_offset_ > 0) {
            double rot[4]{};
            if (read_val(pid, mesh_comp + c2w_rot_offset_, rot)) {
                double len2 = rot[0]*rot[0] + rot[1]*rot[1] + rot[2]*rot[2] + rot[3]*rot[3];
                if (len2 > 0.9 && len2 < 1.1)
                    { qx = rot[0]; qy = rot[1]; qz = rot[2]; qw = rot[3]; }
            }
            double c2w_pos[3]{};
            if (read_val(pid, mesh_comp + c2w_rot_offset_ + 32, c2w_pos)) {
                if (std::isfinite(c2w_pos[0]) && std::abs(c2w_pos[0]) > 10) {
                    base_pos.X = c2w_pos[0];
                    base_pos.Y = c2w_pos[1];
                    base_pos.Z = c2w_pos[2];
                }
            }
        }

        if (bone_array_offset_ == 0) {
            for (uint32_t off = 0x480; off <= 0x6A0; off += 0x10) {
                DbdTArray arr{};
                if (!read_val(pid, mesh_comp + off, arr)) continue;
                if (!DbdIsLikelyPointer(arr.Data)) continue;
                if (arr.Count < 20 || arr.Count > 300) continue;

                DbdFTransform test_bone{};
                if (!read_val(pid, arr.Data, test_bone)) continue;
                if (!std::isfinite(test_bone.PosX) || !std::isfinite(test_bone.PosY))
                    continue;

                bone_array_offset_ = off;
                LOG_CHAIN("[BONE] Found transform array at meshComp+0x%X count=%u", off, arr.Count);
                break;
            }
            if (bone_array_offset_ == 0) return;
        }

        if (gnames_resolved_ && !p.bones_mapped) {
            resolve_bone_map(pid, mesh_comp, p);
        }

        DbdTArray bone_arr{};
        if (!read_val(pid, mesh_comp + bone_array_offset_, bone_arr)) return;
        if (!DbdIsLikelyPointer(bone_arr.Data)) return;
        uint32_t count = (bone_arr.Count < DBD_MAX_BONES) ? bone_arr.Count : DBD_MAX_BONES;
        if (count < 10) return;

        uint32_t total_bytes = count * sizeof(DbdFTransform);
        std::vector<unsigned char> bulk(total_bytes, 0);
        for (uint32_t off = 0; off < total_bytes; ) {
            uint32_t chunk = total_bytes - off;
            if (chunk > 4096) chunk = 4096;
            if (!client_.read_mem(pid, bone_arr.Data + off, chunk, &bulk[off])) return;
            off += chunk;
        }

        p.bone_positions.resize(count);
        const auto* transforms = reinterpret_cast<const DbdFTransform*>(bulk.data());
        for (uint32_t i = 0; i < count; i++) {
            double bx = transforms[i].PosX;
            double by = transforms[i].PosY;
            double bz = transforms[i].PosZ;

            double t2x = qx*2, t2y = qy*2, t2z = qz*2;
            double wt2x = qw*t2x, wt2y = qw*t2y, wt2z = qw*t2z;
            double xt2x = qx*t2x, xt2y = qx*t2y, xt2z = qx*t2z;
            double yt2y = qy*t2y, yt2z = qy*t2z, zt2z = qz*t2z;
            double rx = (1-(yt2y+zt2z))*bx + (xt2y-wt2z)*by + (xt2z+wt2y)*bz;
            double ry = (xt2y+wt2z)*bx + (1-(xt2x+zt2z))*by + (yt2z-wt2x)*bz;
            double rz = (xt2z-wt2y)*bx + (yt2z+wt2x)*by + (1-(xt2x+yt2y))*bz;

            p.bone_positions[i].X = base_pos.X + rx;
            p.bone_positions[i].Y = base_pos.Y + ry;
            p.bone_positions[i].Z = base_pos.Z + rz;
        }

        static bool bone_debug_logged = false;
        if (!bone_debug_logged && count > 5) {
            bone_debug_logged = true;
            LOG_CHAIN("[BONE-DBG] base_pos=(%.0f,%.0f,%.0f)",
                base_pos.X, base_pos.Y, base_pos.Z);
            LOG_CHAIN("[BONE-DBG] bone[0] raw=(%.1f,%.1f,%.1f) world=(%.0f,%.0f,%.0f)",
                transforms[0].PosX, transforms[0].PosY, transforms[0].PosZ,
                p.bone_positions[0].X, p.bone_positions[0].Y, p.bone_positions[0].Z);
            if (p.bones_mapped && p.bone_map[BONE_HEAD] >= 0) {
                int hi = p.bone_map[BONE_HEAD];
                LOG_CHAIN("[BONE-DBG] head[%d] raw=(%.1f,%.1f,%.1f)",
                    hi, transforms[hi].PosX, transforms[hi].PosY, transforms[hi].PosZ);
            }
            LOG_CHAIN("[BONE-DBG] actor pos=(%.0f,%.0f,%.0f)",
                p.position.X, p.position.Y, p.position.Z);
        }
    }

    void resolve_bone_map(int pid, uint64_t mesh_comp, DbdPlayerData& p) {
        uint64_t skel_mesh = 0;
        if (!read_ptr(pid, mesh_comp + DBD_SKEL_MESH_PTR, skel_mesh)) return;
        if (!DbdIsLikelyPointer(skel_mesh)) return;

        DbdTArray bone_info{};
        if (!read_val(pid, skel_mesh + DBD_BONE_INFO_ARRAY, bone_info)) return;
        if (!DbdIsLikelyPointer(bone_info.Data)) return;
        if (bone_info.Count < 10 || bone_info.Count > 300) return;

        static const int strides[] = {16, 12, 20};
        int stride = bone_info_stride_;

        if (stride == 0) {
            for (int try_stride : strides) {
                int matched = 0;
                for (uint32_t i = 0; i < bone_info.Count && i < 100; i++) {
                    uint32_t fname_idx = 0;
                    if (!read_val(pid, bone_info.Data + i * try_stride, fname_idx)) break;
                    if (fname_idx == 0) continue;
                    std::string name = gnames_.resolve(client_, pid, fname_idx);
                    if (name.find("joint_") != std::string::npos) matched++;
                }
                if (matched >= 5) {
                    stride = try_stride;
                    bone_info_stride_ = try_stride;
                    LOG_CHAIN("[BONE] BoneInfo stride=%d matched=%d bones=%u", try_stride, matched, bone_info.Count);
                    break;
                }
            }
            if (stride == 0) return;
        }

        int found = 0;
        for (uint32_t i = 0; i < bone_info.Count && i < 200; i++) {
            uint32_t fname_idx = 0;
            if (!read_val(pid, bone_info.Data + i * stride, fname_idx)) continue;
            if (fname_idx == 0) continue;
            std::string name = gnames_.resolve(client_, pid, fname_idx);
            if (name.empty()) continue;

            for (int b = 0; b < BONE_COUNT; b++) {
                if (name == DbdBoneNames[b]) {
                    p.bone_map[b] = (int)i;
                    found++;
                    break;
                }
            }
        }

        if (found >= 8) {
            p.bones_mapped = true;
            if (!bone_mapped_addrs_.count(mesh_comp)) {
                bone_mapped_addrs_.insert(mesh_comp);
                LOG_CHAIN("[BONE] Mapped %d/%d bones", found, BONE_COUNT);
            }
        }
    }
};

#endif
