#ifndef DBD_READER_H
#define DBD_READER_H

#include <cstdint>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include "dbd_perks.h"
#include <unordered_set>

#include "mem_client.h"
#include "logger.h"
#include "dbd_offsets.h"
#include "dbd_gnames.h"

struct DbdDebugState {
    uint64_t gworld{};
    uint64_t persistent_level{};
    uint64_t game_state{};
    uint64_t local_pawn{};
    bool gnames_ok{};
    char gnames_test[32]{};
    uint32_t actor_scan_count{};
    uint32_t object_match_count{};
    uint32_t aura_cache_size{};
    uint32_t player_array_count{};
    uint32_t c2w_offset{};
    uint32_t bone_array_offset{};
    uint32_t bone_info_stride{};
    uint32_t bone_count_first{};
    int bones_mapped_count{};
    char weapon_id[64]{};

    static constexpr int MAX_EVENTS = 16;
    char events[MAX_EVENTS][128]{};
    int event_count{};

    void add_event(const char* fmt, ...) {
        if (event_count >= MAX_EVENTS) return;
        va_list args;
        va_start(args, fmt);
        vsnprintf(events[event_count], sizeof(events[0]), fmt, args);
        va_end(args);
        event_count++;
    }

    static constexpr int MAX_UNKNOWN_ACTORS = 32;
    char unknown_actors[MAX_UNKNOWN_ACTORS][64]{};
    int unknown_actor_count{};

    void add_unknown_actor(const char* name) {
        for (int i = 0; i < unknown_actor_count; i++)
            if (strcmp(unknown_actors[i], name) == 0) return;
        if (unknown_actor_count >= MAX_UNKNOWN_ACTORS) return;
        snprintf(unknown_actors[unknown_actor_count], sizeof(unknown_actors[0]), "%s", name);
        unknown_actor_count++;
    }
};

struct DbdWorldState {
    bool valid{};
    int32_t player_count{};
    DbdMinimalViewInfo camera{};
    bool has_camera{};
    uint64_t base_address{};
    std::vector<DbdPlayerData> players{};
    std::vector<DbdObjectData> objects{};
    std::string error{};
    DbdDebugState debug{};
    DbdSkillCheckState skillcheck{};
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

    DbdWorldState update(int pid, uint64_t base_address, const DbdAuraConfig* aura_cfg = nullptr,
                         const DbdSkillCheckConfig* sc_cfg = nullptr) {
        DbdWorldState state{};
        state.base_address = base_address;
        ++cycle_;

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
        state.debug.gworld = gworld;

        if (gworld != last_gworld_) {
            actor_class_cache_.clear();
            aura_cache_.clear();
            aura_written_.clear();
            player_cache_.clear();
            last_gworld_ = gworld;
        }

        if (!gnames_resolved_) {
            gnames_.set_address(base_address + DBD_EGS_GNAMES_OFFSET);
            if (gnames_.test_resolve(client_, pid, gworld, base_address)) {
                gnames_resolved_ = true;
                state.debug.add_event("GNames: OK (strategy %d) -> %s",
                    gnames_.get_strategy(), gnames_.get_test_result());
            } else {
                state.debug.add_event("GNames: FAILED all strategies");
                const char* diag = gnames_.get_diag();
                if (diag[0])
                    state.debug.add_event("%.120s", diag);
            }
        }
        state.debug.gnames_ok = gnames_resolved_;
        if (gnames_resolved_) {
            snprintf(state.debug.gnames_test, sizeof(state.debug.gnames_test),
                "%s", gnames_.get_test_result());
        }

        uint64_t persistent_level = 0;
        if (!read_ptr(pid, gworld + DBD_PERSISTENT_LEVEL, persistent_level)) {
            state.error = "PersistentLevel failed";
            return state;
        }
        state.debug.persistent_level = persistent_level;

        uint64_t owning_instance = 0, local_players = 0;
        uint64_t player_controller = 0, camera_manager = 0;
        uint64_t local_pawn = 0;

        bool chain_ok = read_ptr(pid, gworld + DBD_OWNING_GAME_INSTANCE, owning_instance)
                      && read_ptr(pid, owning_instance + DBD_LOCAL_PLAYERS, local_players)
                      && read_ptr(pid, local_players, local_players)
                      && read_ptr(pid, local_players + DBD_PLAYER_CONTROLLER, player_controller);

        if (chain_ok) {
            read_ptr(pid, player_controller + DBD_ACK_PAWN, local_pawn);
            state.debug.local_pawn = local_pawn;
        }

        if (chain_ok) {
            if (read_ptr(pid, player_controller + DBD_CAMERA_MANAGER, camera_manager)) {
                for (uint32_t cam_off = 0; cam_off <= 0x20; cam_off += 0x8) {
                    DbdMinimalViewInfo pov{};
                    if (read_val(pid, camera_manager + DBD_CAMERA_CACHE_PRIVATE + 0x10 + cam_off, pov)) {
                        if (std::isfinite(pov.FOV) && pov.FOV > 1.0f && pov.FOV < 180.0f &&
                            std::isfinite(pov.Location.X) && std::abs(pov.Location.X) > 100 &&
                            std::isfinite(pov.Rotation.Pitch) && std::abs(pov.Rotation.Pitch) < 89.0) {
                            state.camera = pov;
                            state.has_camera = true;
                            break;
                        }
                    }
                }
            }
        }

        read_players_from_gamestate(pid, gworld, local_pawn, state);

        bool object_scan = (cycle_ % 60 == 0) || (cycle_ == 1);
        if (object_scan)
            scan_objects(pid, persistent_level, state);
        else {
            state.objects = cached_objects_;
            ++obj_update_counter_;
            for (auto& obj : state.objects) {
                int ti = static_cast<int>(obj.type);
                bool do_update = false;
                switch (obj.type) {
                    case EDbdObjectType::Generator:
                    case EDbdObjectType::EscapeDoor:
                        do_update = true;
                        break;
                    case EDbdObjectType::Pallet:
                    case EDbdObjectType::Hook:
                    case EDbdObjectType::Hatch:
                        do_update = (obj_update_counter_ % 5 == 0);
                        break;
                    default:
                        do_update = (obj_update_counter_ % 30 == 0);
                        break;
                }
                if (do_update) {
                    read_object_state(pid, obj.address, obj);
                    if (DbdObjectSupportsBox(obj.type) && (obj.type == EDbdObjectType::Pallet || !obj.has_obb))
                        read_object_obb(pid, obj.address, obj);
                }
            }
        }

        if (aura_cfg && aura_cfg->enabled && gnames_resolved_) {
            for (auto& p : state.players) {
                if (!p.valid || p.is_local || p.address == 0) continue;
                bool want = (p.type == EDbdActorType::Survivor && aura_cfg->survivor_aura)
                         || (p.type == EDbdActorType::Killer && aura_cfg->killer_aura);
                if (!want) continue;
                uint64_t ac = find_aura_component(pid, p.address);
                p.aura_component = ac;
                if (ac) {
                    const auto& col = (p.type == EDbdActorType::Survivor)
                        ? aura_cfg->survivor_color : aura_cfg->killer_color;
                    write_aura(pid, ac, col);
                }
            }
            for (auto& obj : state.objects) {
                int ti = static_cast<int>(obj.type);
                if (ti < 0 || ti >= static_cast<int>(EDbdObjectType::OBJ_COUNT)) continue;
                if (!aura_cfg->obj_aura[ti]) continue;
                if (obj.type == EDbdObjectType::Pallet && obj.pallet_state >= 3) continue;
                if (obj.type == EDbdObjectType::Chest && obj.chest_opened) continue;
                uint64_t ac = find_aura_component(pid, obj.address);
                obj.aura_component = ac;
                if (ac)
                    write_aura(pid, ac, aura_cfg->obj_color[ti]);
            }
        }

        if (state.has_camera) {
            for (auto& obj : state.objects) {
                obj.distance = DbdVectorDistance(obj.position, state.camera.Location) / 100.0f;
            }
        }

        if (local_pawn != 0)
            read_and_handle_skillcheck(pid, local_pawn, state);

        state.player_count = static_cast<int32_t>(state.players.size());
        state.valid = true;
        state.debug.aura_cache_size = static_cast<uint32_t>(aura_cache_.size());
        if (state_debug_weapon_[0])
            memcpy(state.debug.weapon_id, state_debug_weapon_, sizeof(state.debug.weapon_id));

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
    int perk_stride_{};
    uint32_t c2w_rot_offset_{};
    std::unordered_set<uint64_t> bone_mapped_addrs_;
    std::unordered_map<uint64_t, int> actor_class_cache_;
    uint64_t last_gworld_{};
    uint32_t obj_update_counter_{};
    std::vector<uint64_t> actor_ptrs_buf_;
    char state_debug_weapon_[64]{};

    struct PlayerEnrichCache {
        char character_name[32]{};
        char perk_names[DBD_MAX_PERKS][48]{};
        int32_t perk_ids[DBD_MAX_PERKS]{};
        int32_t perk_levels[DBD_MAX_PERKS]{};
        char player_name[64]{};
        bool perks_valid{};
        int32_t character_index{-1};
        uint64_t resolve_cycle{};
    };
    std::unordered_map<uint64_t, PlayerEnrichCache> player_cache_;
    static constexpr uint64_t PLAYER_CACHE_TTL = 120;

    struct AuraCacheEntry {
        uint64_t aura_addr{};
        uint64_t resolve_cycle{};
    };
    std::unordered_map<uint64_t, AuraCacheEntry> aura_cache_;
    std::unordered_set<uint64_t> aura_written_;
    static constexpr uint64_t AURA_CACHE_TTL = 30;

    uint64_t find_aura_component(int pid, uint64_t actor_addr) {
        auto it = aura_cache_.find(actor_addr);
        if (it != aura_cache_.end()) {
            if ((cycle_ - it->second.resolve_cycle) < AURA_CACHE_TTL) {
                if (it->second.aura_addr && validate_aura_ptr(pid, it->second.aura_addr))
                    return it->second.aura_addr;
                if (it->second.aura_addr == 0)
                    return 0;
            }
            aura_cache_.erase(it);
        }

        static const uint32_t component_arrays[] = {
            DBD_ACTOR_BLUEPRINT_COMPONENTS,
            DBD_ACTOR_COMPONENTS,
        };

        for (uint32_t array_offset : component_arrays) {
            uint64_t aura = find_aura_component_in_array(pid, actor_addr, array_offset);
            if (aura) {
                aura_cache_[actor_addr] = {aura, cycle_};
                return aura;
            }
        }

        aura_cache_[actor_addr] = {0, cycle_};
        return 0;
    }

    uint64_t find_aura_component_in_array(int pid, uint64_t actor_addr, uint32_t array_offset) {
        DbdTArray comp_array{};
        if (!read_val(pid, actor_addr + array_offset, comp_array))
            return 0;
        if (!DbdIsLikelyPointer(comp_array.Data) || comp_array.Count == 0 || comp_array.Count > 256)
            return 0;

        for (uint32_t i = 0; i < comp_array.Count; i++) {
            uint64_t comp = 0;
            if (!read_ptr(pid, comp_array.Data + i * 8, comp)) continue;
            if (!DbdIsLikelyPointer(comp)) continue;

            std::string class_name = gnames_.resolve_object_class_name(client_, pid, comp);
            std::string object_name = resolve_object_name(pid, comp);

            if ((is_aura_component_name(class_name) || is_aura_component_name(object_name)) &&
                validate_aura_ptr(pid, comp)) {
                return comp;
            }

            if (is_aura_strategy_name(class_name) || is_aura_strategy_name(object_name)) {
                uint64_t strategy_aura = 0;
                if (read_ptr(pid, comp + DBD_AURA_STRATEGY_COMPONENT, strategy_aura) &&
                    validate_aura_ptr(pid, strategy_aura)) {
                    return strategy_aura;
                }
            }
        }

        return 0;
    }

    std::string resolve_object_name(int pid, uint64_t object_addr) {
        uint32_t fname_index = 0;
        if (!read_val(pid, object_addr + DBD_OBJECT_NAME, fname_index))
            return {};
        return gnames_.resolve(client_, pid, fname_index);
    }

    static bool contains_ascii_ci(const std::string& text, const char* needle) {
        if (!needle || !*needle) return true;
        size_t needle_len = strlen(needle);
        if (needle_len > text.size()) return false;
        for (size_t i = 0; i + needle_len <= text.size(); i++) {
            bool matched = true;
            for (size_t j = 0; j < needle_len; j++) {
                unsigned char a = static_cast<unsigned char>(text[i + j]);
                unsigned char b = static_cast<unsigned char>(needle[j]);
                if (std::tolower(a) != std::tolower(b)) {
                    matched = false;
                    break;
                }
            }
            if (matched) return true;
        }
        return false;
    }

    static bool is_aura_component_name(const std::string& name) {
        return contains_ascii_ci(name, "DBDAura") ||
               contains_ascii_ci(name, "AuraComponent") ||
               contains_ascii_ci(name, "DBDOutline") ||
               contains_ascii_ci(name, "OutlineComponent");
    }

    static bool is_aura_strategy_name(const std::string& name) {
        return contains_ascii_ci(name, "AuraUpdateStrategy") ||
               contains_ascii_ci(name, "OutlineUpdateStrategy");
    }

    bool validate_aura_ptr(int pid, uint64_t aura_addr) {
        if (!DbdIsLikelyPointer(aura_addr)) return false;

        uint64_t vtable = 0;
        if (!read_ptr(pid, aura_addr, vtable)) return false;
        if (!DbdIsLikelyPointer(vtable)) return false;

        uint64_t class_ptr = 0;
        if (!read_ptr(pid, aura_addr + 0x10, class_ptr)) return false;
        if (!DbdIsLikelyPointer(class_ptr)) return false;

        float min_dist = 0.0f;
        float min_dist_always = 0.0f;
        if (!read_val(pid, aura_addr + DBD_AURA_MIN_DIST, min_dist)) return false;
        if (!read_val(pid, aura_addr + DBD_AURA_MIN_DIST_ALWAYS_VISIBLE, min_dist_always)) return false;
        if (!std::isfinite(min_dist) || !std::isfinite(min_dist_always)) return false;
        if (min_dist < -1.0f || min_dist > 100000.0f) return false;
        if (min_dist_always < -1.0f || min_dist_always > 100000.0f) return false;

        return true;
    }

    bool is_aura_render_ready(int pid, uint64_t aura_addr) {
        uint64_t batch_commands = 0;
        uint64_t rendering_strategy = 0;
        read_ptr(pid, aura_addr + DBD_AURA_BATCH_MESH_COMMANDS, batch_commands);
        read_ptr(pid, aura_addr + DBD_AURA_RENDERING_STRATEGY, rendering_strategy);
        return DbdIsLikelyPointer(batch_commands) || DbdIsLikelyPointer(rendering_strategy);
    }

    void write_aura(int pid, uint64_t aura_addr, const DbdAuraColor& color) {
        (void)color;
        bool render_ready = is_aura_render_ready(pid, aura_addr);
        if (render_ready && aura_written_.find(aura_addr) != aura_written_.end())
            return;

        if (!validate_aura_ptr(pid, aura_addr)) {
            for (auto it = aura_cache_.begin(); it != aura_cache_.end(); ++it) {
                if (it->second.aura_addr == aura_addr) {
                    aura_cache_.erase(it);
                    break;
                }
            }
            return;
        }
        uint8_t backup_valid = 0;
        read_val(pid, aura_addr + DBD_AURA_OVERRIDE_BACKUP_VALID, backup_valid);
        if (!backup_valid) {
            struct AuraVisibilityBackup {
                uint8_t always_visible;
                uint8_t pad[3];
                float min_dist_always;
                float min_dist;
            } backup{};
            read_val(pid, aura_addr + DBD_AURA_IS_ALWAYS_VISIBLE, backup.always_visible);
            read_val(pid, aura_addr + DBD_AURA_MIN_DIST_ALWAYS_VISIBLE, backup.min_dist_always);
            read_val(pid, aura_addr + DBD_AURA_MIN_DIST, backup.min_dist);
            client_.write_mem(pid, aura_addr + DBD_AURA_OVERRIDE_BACKUP,
                              sizeof(backup), reinterpret_cast<const unsigned char*>(&backup));
            backup_valid = 1;
            client_.write_mem(pid, aura_addr + DBD_AURA_OVERRIDE_BACKUP_VALID,
                              sizeof(backup_valid), &backup_valid);
        }

        uint8_t always_visible = 1;
        uint8_t off = 0;
        float interpolation_speed = 4096.0f;
        float zero_dist = 0.0f;
        client_.write_mem(pid, aura_addr + DBD_AURA_INTERPOLATION_SPEED,
                          sizeof(interpolation_speed), reinterpret_cast<const unsigned char*>(&interpolation_speed));
        client_.write_mem(pid, aura_addr + DBD_AURA_SHOULD_BE_ABOVE,
                          sizeof(always_visible), &always_visible);
        client_.write_mem(pid, aura_addr + DBD_AURA_FORCE_FAR_AWAY,
                          sizeof(off), &off);
        client_.write_mem(pid, aura_addr + DBD_AURA_LIMIT_CUSTOM_DEPTH,
                          sizeof(off), &off);
        client_.write_mem(pid, aura_addr + DBD_AURA_FADE_OUT_CLOSING_IN,
                          sizeof(off), &off);
        client_.write_mem(pid, aura_addr + DBD_AURA_IS_ALWAYS_VISIBLE,
                          sizeof(always_visible), &always_visible);
        client_.write_mem(pid, aura_addr + DBD_AURA_MIN_DIST_ALWAYS_VISIBLE,
                          sizeof(zero_dist), reinterpret_cast<const unsigned char*>(&zero_dist));
        client_.write_mem(pid, aura_addr + DBD_AURA_MIN_DIST,
                          sizeof(zero_dist), reinterpret_cast<const unsigned char*>(&zero_dist));
        if (render_ready)
            aura_written_.insert(aura_addr);
    }

    std::string test_gnames_resolve(int pid, uint64_t gworld) {
        return gnames_.resolve_object_class_name(client_, pid, gworld);
    }

    void read_players_from_gamestate(int pid, uint64_t gworld, uint64_t local_pawn,
                                     DbdWorldState& state)
    {
        uint64_t game_state = 0;
        if (!read_ptr(pid, gworld + DBD_GAME_STATE, game_state))
            return;
        state.debug.game_state = game_state;

        DbdTArray player_array{};
        if (!read_val(pid, game_state + DBD_PLAYER_ARRAY, player_array))
            return;
        if (!DbdIsLikelyPointer(player_array.Data) || player_array.Count == 0 || player_array.Count > 20)
            return;
        state.debug.player_array_count = player_array.Count;

        for (uint32_t i = 0; i < player_array.Count; ++i) {
            uint64_t ps = 0;
            if (!read_ptr(pid, player_array.Data + i * 8, ps)) continue;

            EDbdPlayerRole role = EDbdPlayerRole::Role_None;
            read_val(pid, ps + DBD_GAME_ROLE, role);

            DbdPlayerData p{};
            p.role = role;
            if (role == EDbdPlayerRole::Role_Camper) {
                p.type = EDbdActorType::Survivor;
                snprintf(p.name, sizeof(p.name), "Survivor");
            } else if (role == EDbdPlayerRole::Role_Slasher) {
                p.type = EDbdActorType::Killer;
                snprintf(p.name, sizeof(p.name), "Killer");
            } else {
                p.type = EDbdActorType::Unknown;
                snprintf(p.name, sizeof(p.name), "Lobby");
            }

            read_player_name(pid, ps, p.name, sizeof(p.name));

            uint64_t pawn = 0;
            read_ptr(pid, ps + DBD_PAWN_PRIVATE, pawn);

            bool is_local = (pawn != 0 && pawn == local_pawn);
            p.is_local = is_local;
            p.playerstate = ps;

            if (pawn != 0) {
                p.address = pawn;
                uint64_t root = 0;
                DbdUEVector pos{};
                if (read_ptr(pid, pawn + DBD_ROOT_COMPONENT, root))
                    read_val(pid, root + DBD_RELATIVE_LOCATION, pos);

                if (DbdIsFiniteVec(pos) && !(pos.X == 0.0 && pos.Y == 0.0 && pos.Z == 0.0)) {
                    p.position = pos;
                    if (state.has_camera && !is_local)
                        p.distance = DbdVectorDistance(pos, state.camera.Location) / 100.0f;
                }

                if (is_local && state.has_camera && p.position.X == 0.0 && p.position.Y == 0.0)
                    p.position = state.camera.Location;

                if (is_local) {
                    read_character_and_perks(pid, ps, pawn, p, &state.debug);
                } else {
                    read_player_enrichment(pid, ps, pawn, p, &state.debug);
                }
            } else {
                read_player_enrichment_lobby(pid, ps, p, &state.debug);
            }

            p.valid = true;

            state.players.push_back(p);
        }
    }

    void scan_objects(int pid, uint64_t persistent_level,
                      DbdWorldState& state)
    {
        uint64_t actor_array = 0;
        uint32_t actor_count = 0;

        if (read_ptr(pid, persistent_level + DBD_DIRECT_ACTORS_ARRAY, actor_array))
            read_val(pid, persistent_level + DBD_DIRECT_ACTORS_COUNT, actor_count);

        if (actor_array == 0 || actor_count == 0 || actor_count > 3000)
            return;

        uint32_t ptrs_to_read = (actor_count < 3000) ? actor_count : 3000;
        actor_ptrs_buf_.resize(ptrs_to_read);
        memset(actor_ptrs_buf_.data(), 0, ptrs_to_read * 8);
        {
            auto* raw = reinterpret_cast<unsigned char*>(actor_ptrs_buf_.data());
            uint32_t total_bytes = ptrs_to_read * 8;
            for (uint32_t off = 0; off < total_bytes; ) {
                uint32_t chunk = total_bytes - off;
                if (chunk > 4096) chunk = 4096;
                if (!client_.read_mem(pid, actor_array + off, chunk, raw + off)) break;
                off += chunk;
            }
        }
        auto& actor_ptrs = actor_ptrs_buf_;

        std::unordered_set<uint64_t> seen_addrs;
        int name_log_count = 0;

        for (uint32_t i = 0; i < ptrs_to_read; ++i) {
            uint64_t actor = actor_ptrs[i];
            if (!DbdIsLikelyPointer(actor)) continue;

            if (seen_addrs.count(actor)) continue;

            auto cache_it = actor_class_cache_.find(actor);
            EDbdObjectType obj_type;
            bool found;
            if (cache_it != actor_class_cache_.end()) {
                if (cache_it->second < 0) continue;
                obj_type = static_cast<EDbdObjectType>(cache_it->second);
                found = true;
            } else {
                std::string class_name = gnames_.resolve_object_class_name(client_, pid, actor);
                if (class_name.empty()) {
                    actor_class_cache_[actor] = -1;
                    continue;
                }
                found = classify_object(class_name, obj_type);
                actor_class_cache_[actor] = found ? static_cast<int>(obj_type) : -1;
                if (!found) {
                    if (class_name.find("Interactable") != std::string::npos ||
                        class_name.find("Event") != std::string::npos ||
                        class_name.find("Door") != std::string::npos ||
                        class_name.find("Gate") != std::string::npos ||
                        class_name.find("Escape") != std::string::npos ||
                        class_name.find("Blood") != std::string::npos ||
                        class_name.find("Pump") != std::string::npos ||
                        class_name.find("Dispenser") != std::string::npos ||
                        class_name.find("Prop_") != std::string::npos ||
                        class_name.find("Item") != std::string::npos) {
                        state.debug.add_unknown_actor(class_name.c_str());
                    }
                    continue;
                }
            }



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
            if (DbdObjectSupportsBox(obj.type))
                read_object_obb(pid, actor, obj);
            if (is_duplicate_object(obj, state.objects))
                continue;

            state.objects.push_back(obj);
        }

        cached_objects_ = state.objects;
        state.debug.actor_scan_count = ptrs_to_read;
        state.debug.object_match_count = static_cast<uint32_t>(state.objects.size());
    }

    static bool is_pallet_actor_name(const std::string& name) {
        const bool looks_like_pallet =
            name == "Pallet" ||
            name.find("BP_Pallet") != std::string::npos ||
            name.find("Pallet") != std::string::npos;
        if (!looks_like_pallet)
            return false;

        static const char* non_pallet_tokens[] = {
            "Tracker", "Cosmetic", "Component", "Interaction", "Definition",
            "Ability", "Addon", "Anim", "State", "Evaluator", "Action",
            "Skill", "Placement", "Achievement", "Selection", "Targeting",
            "Spawner", "Visibility", "Collider", "Collision", "Blocker",
            "Helper", "Montage", "Settings", "Data", "Behaviour", "Vault",
            "Repair", "Break", "Lift", "Pull", "Drop", "Stun",
        };
        for (const char* token : non_pallet_tokens) {
            if (name.find(token) != std::string::npos)
                return false;
        }
        return true;
    }

    static bool is_escape_door_actor_name(const std::string& name) {
        const bool looks_like_escape =
            name == "EscapeDoor" ||
            name.find("EscapeDoor") != std::string::npos ||
            name.find("BP_Escape") != std::string::npos ||
            name.find("_Escape") != std::string::npos;
        if (!looks_like_escape)
            return false;

        static const char* non_door_tokens[] = {
            "Anim", "Interaction", "Definition", "Achievement", "SubAnim",
            "Component", "Effect", "Skill", "Addon", "Ability", "Incentive",
            "Blocker", "Zone", "Area", "Trigger", "Volume", "Target",
            "Manager", "Evaluator", "Data", "Montage", "Status",
        };
        for (const char* token : non_door_tokens) {
            if (name.find(token) != std::string::npos)
                return false;
        }
        return true;
    }

    static bool is_generator_actor_name(const std::string& name) {
        const bool looks_like_generator =
            name == "Generator" ||
            name.find("BP_Generator") != std::string::npos ||
            name.find("Generator") != std::string::npos;
        if (!looks_like_generator)
            return false;

        static const char* non_generator_tokens[] = {
            "Driven", "Linker", "Cosmetic", "Helper", "Entity", "Anim",
            "Component", "Interaction", "Definition", "Aura", "Strategy",
            "Damage", "Trap", "Progress", "Surrender", "Blockable",
            "Snowball", "Skill", "Effect", "Achievement", "Evaluator",
            "Action", "QEEvaluator", "Data", "Spawner", "Montage",
            "Charge", "CatchUp", "Placer", "StaticMesh", "VFX",
        };
        for (const char* token : non_generator_tokens) {
            if (name.find(token) != std::string::npos)
                return false;
        }
        return true;
    }

    static bool is_duplicate_object(const DbdObjectData& obj, const std::vector<DbdObjectData>& objects) {
        if (obj.type != EDbdObjectType::EscapeDoor &&
            obj.type != EDbdObjectType::Pallet &&
            obj.type != EDbdObjectType::Generator)
            return false;

        float min_dist = 250.0f;
        if (obj.type == EDbdObjectType::EscapeDoor)
            min_dist = 800.0f;
        else if (obj.type == EDbdObjectType::Generator)
            min_dist = 350.0f;
        for (const auto& other : objects) {
            if (other.type != obj.type)
                continue;
            if (DbdVectorDistance(obj.position, other.position) < min_dist)
                return true;
        }
        return false;
    }

    static bool classify_object(const std::string& name, EDbdObjectType& out) {
        if (is_generator_actor_name(name))                     { out = EDbdObjectType::Generator; return true; }
        if (name.find("Totem") != std::string::npos)            { out = EDbdObjectType::Totem; return true; }
        if (is_pallet_actor_name(name))                         { out = EDbdObjectType::Pallet; return true; }
        if (name.find("MeatHook") != std::string::npos ||
            name.find("Hook") != std::string::npos ||
            name.find("Locker") != std::string::npos)           { out = EDbdObjectType::Hook; return true; }
        if (name.find("Hatch") != std::string::npos &&
            name.find("Hatchet") == std::string::npos)   { out = EDbdObjectType::Hatch; return true; }
        if (name.find("Chest") != std::string::npos ||
            name.find("Searchable") != std::string::npos)       { out = EDbdObjectType::Chest; return true; }
        if (name.find("Window") != std::string::npos)           { out = EDbdObjectType::Window; return true; }
        if (name.find("Trap") != std::string::npos ||
            name.find("BearTrap") != std::string::npos)         { out = EDbdObjectType::Trap; return true; }
        if (is_escape_door_actor_name(name))                    { out = EDbdObjectType::EscapeDoor; return true; }
        if (name.find("BreakableWall") != std::string::npos ||
            name.find("Breakable") != std::string::npos)        { out = EDbdObjectType::BreakableDoor; return true; }
        if (name.find("FuelPump") != std::string::npos ||
            name.find("EventInteract") != std::string::npos)    { out = EDbdObjectType::BloodPump; return true; }
        return false;
    }

    void read_player_name(int pid, uint64_t player_state, char* out_name, size_t out_size) {
        auto pc_it = player_cache_.find(player_state);
        if (pc_it != player_cache_.end() && pc_it->second.player_name[0]) {
            snprintf(out_name, out_size, "%s", pc_it->second.player_name);
            return;
        }

        DbdTArray name_arr{};
        if (!read_val(pid, player_state + DBD_PLAYER_NAME_PRIVATE, name_arr))
            return;
        if (!DbdIsLikelyPointer(name_arr.Data) || name_arr.Count == 0 || name_arr.Count > 128)
            return;

        uint32_t bytes = name_arr.Count * 2;
        if (bytes > 256) bytes = 256;

        unsigned char buf[256]{};
        if (!client_.read_mem(pid, name_arr.Data, bytes, buf))
            return;

        char tmp[64]{};
        int pos = 0;
        for (uint32_t j = 0; j < bytes && pos < 63; j += 2) {
            uint16_t ch = buf[j] | (buf[j+1] << 8);
            if (ch == 0) break;
            if (ch < 128)
                tmp[pos++] = static_cast<char>(ch);
            else
                tmp[pos++] = '?';
        }
        tmp[pos] = 0;

        if (pos > 0) {
            snprintf(out_name, out_size, "%s", tmp);
            player_cache_[player_state].resolve_cycle = cycle_;
            snprintf(player_cache_[player_state].player_name, 64, "%s", tmp);
        }
    }

    void read_object_state(int pid, uint64_t actor, DbdObjectData& obj) {
        switch (obj.type) {
            case EDbdObjectType::Generator: {
                float native_pct = 0;
                read_val(pid, actor + DBD_GEN_NATIVE_PROGRESS, native_pct);
                if (std::isfinite(native_pct) && native_pct >= 0 && native_pct <= 1.01f) {
                    obj.gen_progress = native_pct * 100.0f;
                    obj.gen_max_charge = 100.0f;
                }
                uint8_t repaired = 0;
                read_val(pid, actor + DBD_GEN_REPAIRED, repaired);
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

    bool read_component_obb(int pid, uint64_t component, DbdObjectData& obj,
                            const DbdUEVector& fallback_extent,
                            bool read_box_extent = true,
                            double vertical_center_offset = 0.0,
                            bool update_object_position = true) {
        if (!DbdIsLikelyPointer(component))
            return false;

        DbdFTransform transform{};
        if (!read_val(pid, component + DBD_COMPONENT_TO_WORLD, transform))
            return false;
        if (!std::isfinite(transform.PosX) || !std::isfinite(transform.PosY) || !std::isfinite(transform.PosZ))
            return false;

        DbdUEVector extent = fallback_extent;
        DbdUEVector read_extent{};
        if (read_box_extent &&
            read_val(pid, component + DBD_BOX_COMPONENT_EXTENT, read_extent) &&
            DbdIsFiniteVec(read_extent) &&
            read_extent.X > 1.0 && read_extent.Y > 1.0 && read_extent.Z > 1.0 &&
            read_extent.X < 500.0 && read_extent.Y < 500.0 && read_extent.Z < 500.0) {
            extent = read_extent;
        }
        transform.PosZ += vertical_center_offset;

        obj.obb_transform = transform;
        obj.obb_extent = extent;
        obj.has_obb = true;
        if (update_object_position)
            obj.position = {transform.PosX, transform.PosY, transform.PosZ};
        return true;
    }

    DbdUEVector object_obb_fallback_extent(const DbdObjectData& obj) const {
        switch (obj.type) {
            case EDbdObjectType::Generator:
                return {58.0, 38.0, 82.0};
            case EDbdObjectType::Window:
                return {18.0, 95.0, 65.0};
            case EDbdObjectType::Pallet:
                return (obj.pallet_state == 2)
                    ? DbdUEVector{105.0, 38.0, 18.0}
                    : DbdUEVector{80.0, 16.0, 110.0};
            default:
                return {};
        }
    }

    void read_object_obb(int pid, uint64_t actor, DbdObjectData& obj) {
        obj.has_obb = false;
        DbdUEVector fallback_extent = object_obb_fallback_extent(obj);
        if (!DbdIsFiniteVec(fallback_extent) || fallback_extent.X <= 0.0)
            return;

        if (obj.type == EDbdObjectType::Pallet) {
            uint64_t component = 0;
            const bool down = (obj.pallet_state == 2);
            const uint32_t primary = down ? DBD_PALLET_DOWNED_COLLIDER : DBD_PALLET_UP_COLLIDER;
            const uint32_t secondary = down ? DBD_PALLET_UP_COLLIDER : DBD_PALLET_DOWNED_COLLIDER;
            if (read_ptr(pid, actor + primary, component) && read_component_obb(pid, component, obj, fallback_extent))
                return;
            if (read_ptr(pid, actor + secondary, component) && read_component_obb(pid, component, obj, fallback_extent))
                return;
            return;
        }

        uint64_t root = 0;
        if (read_ptr(pid, actor + DBD_ROOT_COMPONENT, root)) {
            double vertical_center_offset = 0.0;
            if (obj.type == EDbdObjectType::Generator)
                vertical_center_offset = 82.0;
            else if (obj.type == EDbdObjectType::Window)
                vertical_center_offset = fallback_extent.Z;
            const bool update_object_position = (obj.type == EDbdObjectType::Pallet);
            read_component_obb(pid, root, obj, fallback_extent, false, vertical_center_offset, update_object_position);
        }
    }

    void read_character_and_perks(int pid, uint64_t ps, uint64_t pawn, DbdPlayerData& p,
                                  DbdDebugState* dbg = nullptr) {
        int32_t level = -1, prestige = -1;
        read_val(pid, ps + DBD_PLAYER_DATA + DBD_PLAYER_DATA_CHAR_LEVEL, level);
        read_val(pid, ps + DBD_PLAYER_DATA + DBD_PLAYER_DATA_PRESTIGE, prestige);
        if (level >= 0 && level <= 100) p.level = level;
        if (prestige >= 0 && prestige <= 100) p.prestige = prestige;

        auto pc_it = player_cache_.find(ps);
        if (pc_it != player_cache_.end() && (cycle_ - pc_it->second.resolve_cycle) < PLAYER_CACHE_TTL) {
            auto& c = pc_it->second;
            if (c.character_name[0] || c.perks_valid) {
                if (c.character_name[0])
                    snprintf(p.character_name, sizeof(p.character_name), "%s", c.character_name);
                p.character_index = c.character_index;
                memcpy(p.perk_ids, c.perk_ids, sizeof(p.perk_ids));
                memcpy(p.perk_levels, c.perk_levels, sizeof(p.perk_levels));
                memcpy(p.perk_names, c.perk_names, sizeof(p.perk_names));
                p.perks_valid = c.perks_valid;
                return;
            }
        }

        if (pawn != 0 && gnames_resolved_) {
            std::string cls = gnames_.resolve_object_class_name(client_, pid, pawn);
            if (!cls.empty()) {
                const char* mapped = DbdMapClassToCharacter(cls);
                if (mapped) {
                    snprintf(p.character_name, sizeof(p.character_name), "%s", mapped);
                } else {
                    size_t bp = cls.find("BP_");
                    size_t ce = cls.find("_Character");
                    if (bp != std::string::npos && ce != std::string::npos && ce > bp + 3) {
                        std::string sub = cls.substr(bp + 3, ce - bp - 3);
                        snprintf(p.character_name, sizeof(p.character_name), "%s", sub.c_str());
                    }
                }
            }
        }

        if (p.character_name[0] == 0) {
            int32_t surv_idx = -1, kill_idx = -1;
            read_val(pid, ps + DBD_SELECTED_SURVIVOR_INDEX, surv_idx);
            read_val(pid, ps + DBD_SELECTED_KILLER_INDEX, kill_idx);
            p.debug_surv_idx = surv_idx;
            p.debug_kill_idx = kill_idx;

            if (gnames_resolved_) {
                uint32_t cc_fname = 0;
                unsigned char ccbuf[4]{};
                if (client_.read_mem(pid, ps + DBD_EQUIPPED_CHAR_CLASS, 4, ccbuf)) {
                    memcpy(&cc_fname, ccbuf, 4);
                    if (cc_fname > 0) {
                        std::string ccname = gnames_.resolve(client_, pid, cc_fname);
                        if (!ccname.empty())
                            snprintf(p.debug_char_class, sizeof(p.debug_char_class), "%s", ccname.c_str());
                    }
                }
            }

            if (p.type == EDbdActorType::Survivor && surv_idx >= 0 && surv_idx < DBD_SURVIVOR_NAME_COUNT) {
                p.character_index = surv_idx;
                snprintf(p.character_name, sizeof(p.character_name), "%s", DbdSurvivorNames[surv_idx]);
            } else if (p.type == EDbdActorType::Killer && kill_idx >= 0 && kill_idx < DBD_KILLER_NAME_COUNT) {
                p.character_index = kill_idx;
                snprintf(p.character_name, sizeof(p.character_name), "%s", DbdKillerNames[kill_idx]);
            }
        }

        DbdTArray perk_id_arr{};
        if (read_val(pid, ps + DBD_PLAYER_DATA + DBD_PLAYER_DATA_PERK_IDS, perk_id_arr)) {
            p.debug_perk_arr_count = perk_id_arr.Count;
            p.debug_perk_arr_data = perk_id_arr.Data;
        }
        if (DbdIsLikelyPointer(perk_id_arr.Data) &&
            perk_id_arr.Count > 0 && perk_id_arr.Count <= DBD_MAX_PERKS)
        {
            uint32_t count = (perk_id_arr.Count < DBD_MAX_PERKS) ? perk_id_arr.Count : DBD_MAX_PERKS;
            static constexpr int PERK_BUF_SIZE = 128;
            unsigned char pbuf[PERK_BUF_SIZE]{};
            if (client_.read_mem(pid, perk_id_arr.Data, PERK_BUF_SIZE, pbuf)) {
                p.perks_valid = true;

                int best_stride = perk_stride_;
                if (best_stride == 0) {
                    int best_score = -1;
                    for (int try_stride : {8, 12, 16, 24}) {
                        if (try_stride * (int)count > PERK_BUF_SIZE) continue;
                        int hits = 0;
                        int unique_count = 0;
                        uint32_t seen_ids[DBD_MAX_PERKS]{};
                        for (uint32_t i = 0; i < count; i++) {
                            uint32_t idx = 0;
                            memcpy(&idx, pbuf + i * try_stride, 4);
                            if (idx > 0 && idx < 0x200000) {
                                std::string test = gnames_.resolve(client_, pid, idx);
                                if (!test.empty() && test != "None" && test[0] != '_') {
                                    hits++;
                                    bool dup = false;
                                    for (int k = 0; k < (int)i; k++) {
                                        if (seen_ids[k] == idx) { dup = true; break; }
                                    }
                                    if (!dup) unique_count++;
                                }
                            }
                            seen_ids[i] = idx;
                        }
                        int score = unique_count * 10 + hits;
                        if (score > best_score || (score == best_score && try_stride > best_stride)) {
                            best_score = score;
                            best_stride = try_stride;
                        }
                    }
                    if (best_stride == 0) best_stride = 16;
                    perk_stride_ = best_stride;
                }

                for (uint32_t i = 0; i < count; i++) {
                    uint32_t comp_idx = 0;
                    memcpy(&comp_idx, pbuf + i * best_stride, 4);
                    p.perk_ids[i] = static_cast<int32_t>(comp_idx);

                    if (best_stride >= 16) {
                        int32_t lv = 0;
                        memcpy(&lv, pbuf + i * best_stride + 0x0C, 4);
                        if (lv >= 0 && lv <= 3) p.perk_levels[i] = lv;
                    }

                    if (gnames_resolved_ && comp_idx > 0) {
                        std::string raw_name = gnames_.resolve(client_, pid, comp_idx);
                        if (!raw_name.empty()) {
                            const char* display = DbdResolvePerkDisplayName(raw_name);
                            if (display)
                                snprintf(p.perk_names[i], sizeof(p.perk_names[i]), "%s", display);
                            else
                                snprintf(p.perk_names[i], sizeof(p.perk_names[i]), "%s", raw_name.c_str());
                        }
                    }
                }
            }
        }

        DbdTArray perk_lv_arr{};
        if (read_val(pid, ps + DBD_PLAYER_DATA + DBD_PLAYER_DATA_PERK_LEVELS, perk_lv_arr) &&
            DbdIsLikelyPointer(perk_lv_arr.Data) &&
            perk_lv_arr.Count > 0 && perk_lv_arr.Count <= DBD_MAX_PERKS)
        {
            uint32_t count = (perk_lv_arr.Count < DBD_MAX_PERKS) ? perk_lv_arr.Count : DBD_MAX_PERKS;
            unsigned char pbuf[DBD_MAX_PERKS * 4]{};
            if (client_.read_mem(pid, perk_lv_arr.Data, count * 4, pbuf))
                memcpy(p.perk_levels, pbuf, count * 4);
        }

        auto& c = player_cache_[ps];
        c.resolve_cycle = cycle_;
        memcpy(c.character_name, p.character_name, sizeof(c.character_name));
        c.character_index = p.character_index;
        memcpy(c.perk_ids, p.perk_ids, sizeof(c.perk_ids));
        memcpy(c.perk_levels, p.perk_levels, sizeof(c.perk_levels));
        memcpy(c.perk_names, p.perk_names, sizeof(c.perk_names));
        c.perks_valid = p.perks_valid;
    }

    void read_player_enrichment(int pid, uint64_t ps, uint64_t pawn, DbdPlayerData& p,
                                DbdDebugState* dbg = nullptr) {
        read_character_and_perks(pid, ps, pawn, p, dbg);

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

    void read_player_enrichment_lobby(int pid, uint64_t ps, DbdPlayerData& p,
                                      DbdDebugState* dbg = nullptr) {
        read_character_and_perks(pid, ps, 0, p, dbg);

        if (p.character_name[0] == 0 && p.type == EDbdActorType::Killer && gnames_resolved_) {
            try_identify_killer_by_weapon(pid, ps, p);
        }
    }

    void try_identify_killer_by_weapon(int pid, uint64_t ps, DbdPlayerData& p) {

        {
            uint32_t power_fname = 0;
            unsigned char fbuf[4]{};
            if (client_.read_mem(pid, ps + DBD_POWER_OR_ITEM_ID, 4, fbuf)) {
                memcpy(&power_fname, fbuf, 4);
                if (power_fname > 0) {
                    std::string power_name = gnames_.resolve(client_, pid, power_fname);
                    if (!power_name.empty()) {
                        const char* killer = DbdMapWeaponToKiller(power_name);
                        if (killer) {
                            snprintf(p.character_name, sizeof(p.character_name), "%s", killer);
                            snprintf(state_debug_weapon_, sizeof(state_debug_weapon_), "%s (power: %s)", killer, power_name.c_str());
                            return;
                        }
                        snprintf(state_debug_weapon_, sizeof(state_debug_weapon_), "power: %s", power_name.c_str());
                    }
                }
            }
        }

        {
            DbdTArray parts{};
            if (read_val(pid, ps + DBD_CUSTOMIZATION_PARTS, parts) &&
                DbdIsLikelyPointer(parts.Data) && parts.Count > 0 && parts.Count <= 20)
            {
                uint32_t count = (parts.Count < 10) ? parts.Count : 10;
                for (uint32_t i = 0; i < count; i++) {
                    uint64_t item_ptr = 0;
                    if (!read_ptr(pid, parts.Data + i * 8, item_ptr)) continue;

                    uint32_t fname_idx = 0;
                    unsigned char ibuf[4]{};
                    if (client_.read_mem(pid, item_ptr + DBD_OBJECT_NAME, 4, ibuf)) {
                        memcpy(&fname_idx, ibuf, 4);
                        if (fname_idx > 0) {
                            std::string name = gnames_.resolve(client_, pid, fname_idx);
                            if (!name.empty()) {
                                const char* killer = DbdMapWeaponToKiller(name);
                                if (killer) {
                                    snprintf(p.character_name, sizeof(p.character_name), "%s", killer);
                                    snprintf(state_debug_weapon_, sizeof(state_debug_weapon_), "%s (custom: %s)", killer, name.c_str());
                                    return;
                                }
                            }
                        }
                    }

                    std::string cls = gnames_.resolve_object_class_name(client_, pid, item_ptr);
                    if (!cls.empty()) {
                        const char* killer = DbdMapWeaponToKiller(cls);
                        if (killer) {
                            snprintf(p.character_name, sizeof(p.character_name), "%s", killer);
                            snprintf(state_debug_weapon_, sizeof(state_debug_weapon_), "%s (class: %s)", killer, cls.c_str());
                            return;
                        }
                    }
                }
            }
        }

        {
            DbdTArray comp_array{};
            if (!read_val(pid, ps + DBD_ACTOR_COMPONENTS, comp_array)) return;
            if (!DbdIsLikelyPointer(comp_array.Data) || comp_array.Count == 0 || comp_array.Count > 100) return;

            uint32_t count = (comp_array.Count < 30) ? comp_array.Count : 30;
            for (uint32_t i = 0; i < count; i++) {
                uint64_t comp = 0;
                if (!read_ptr(pid, comp_array.Data + i * 8, comp)) continue;

                uint32_t fname_idx = 0;
                unsigned char ibuf[4]{};
                if (!client_.read_mem(pid, comp + DBD_OBJECT_NAME, 4, ibuf)) continue;
                memcpy(&fname_idx, ibuf, 4);
                if (fname_idx == 0) continue;

                std::string name = gnames_.resolve(client_, pid, fname_idx);
                if (name.empty()) continue;

                const char* killer = DbdMapWeaponToKiller(name);
                if (killer) {
                    snprintf(p.character_name, sizeof(p.character_name), "%s", killer);
                    snprintf(state_debug_weapon_, sizeof(state_debug_weapon_), "%s (comp: %s)", killer, name.c_str());
                    return;
                }
            }
        }
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
            constexpr uint32_t scan_start = 0x480;
            constexpr uint32_t scan_end   = 0x6B0;
            constexpr uint32_t scan_size  = scan_end - scan_start;
            unsigned char scan_buf[scan_size];
            if (!client_.read_mem(pid, mesh_comp + scan_start, scan_size, scan_buf)) return;

            for (uint32_t off = 0; off <= scan_size - 0x10; off += 0x10) {
                DbdTArray arr{};
                memcpy(&arr, scan_buf + off, sizeof(arr));
                if (!DbdIsLikelyPointer(arr.Data)) continue;
                if (arr.Count < 20 || arr.Count > 300) continue;

                DbdFTransform test_bone{};
                if (!read_val(pid, arr.Data, test_bone)) continue;
                if (!std::isfinite(test_bone.PosX) || !std::isfinite(test_bone.PosY))
                    continue;

                bone_array_offset_ = scan_start + off;
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
        if (!client_.read_mem(pid, bone_arr.Data, total_bytes, bulk.data())) return;

        p.bone_count = count;
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
                    break;
                }
            }
            if (stride == 0) return;
        }

        uint32_t read_count = (bone_info.Count < 200) ? bone_info.Count : 200;
        uint32_t bulk_bytes = read_count * stride;
        if (bulk_bytes > 4096) bulk_bytes = 4096;
        read_count = bulk_bytes / stride;

        std::vector<unsigned char> bulk_data(bulk_bytes, 0);
        for (uint32_t off = 0; off < bulk_bytes; ) {
            uint32_t chunk = bulk_bytes - off;
            if (chunk > 4096) chunk = 4096;
            if (!client_.read_mem(pid, bone_info.Data + off, chunk, &bulk_data[off])) return;
            off += chunk;
        }

        int found = 0;
        for (uint32_t i = 0; i < read_count; i++) {
            uint32_t fname_idx = 0;
            memcpy(&fname_idx, &bulk_data[i * stride], 4);
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
            bone_mapped_addrs_.insert(mesh_comp);
        }
    }

    void read_and_handle_skillcheck(int pid, uint64_t local_pawn, DbdWorldState& state) {
        auto& sc = state.skillcheck;
        sc = {};

        uint64_t handler = 0;

        unsigned char hbuf[8]{};
        client_.read_mem(pid, local_pawn + DBD_INTERACTION_HANDLER, 8, hbuf);
        memcpy(&handler, hbuf, 8);
        sc.debug_handler = handler;

        if (!DbdIsLikelyPointer(handler)) {
            snprintf(sc.debug_fail, sizeof(sc.debug_fail), "handler=0x%lX", handler);
            return;
        }

        uint64_t skill_check = 0;
        unsigned char sbuf[8]{};
        client_.read_mem(pid, handler + DBD_SKILL_CHECK, 8, sbuf);
        memcpy(&skill_check, sbuf, 8);
        sc.debug_skillcheck = skill_check;

        if (!DbdIsLikelyPointer(skill_check)) {
            snprintf(sc.debug_fail, sizeof(sc.debug_fail), "sc=0x%lX", skill_check);
            return;
        }

        uint8_t displayed = 0;
        read_val(pid, skill_check + DBD_SC_IS_DISPLAYED, displayed);
        sc.debug_displayed_raw = displayed;

        if (!displayed) {
            snprintf(sc.debug_fail, sizeof(sc.debug_fail), "displayed=%d", displayed);
            return;
        }

        sc.active = true;
        read_val(pid, skill_check + DBD_SC_CURRENT_PROGRESS, sc.progress);
        read_val(pid, skill_check + DBD_SC_CURRENT_TYPE, sc.type);

        // Dump raw floats from sc+0x190 for debug
        for (int i = 0; i < DbdSkillCheckState::DEBUG_FLOAT_COUNT; i++)
            read_val(pid, skill_check + 0x190 + i * 4, sc.debug_floats[i]);

        // Definition is inline at sc+0x200
        // Read floats from definition for debug
        uint64_t def = skill_check + DBD_SC_DEFINITION;
        for (int i = 0; i < 8; i++)
            read_val(pid, def + i * 4, sc.debug_def_floats[i]);

        // Try standard layout: +0x00=successStart, +0x04=successEnd, +0x08=bonusLen, +0x0C=bonusStart
        sc.success_start = sc.debug_def_floats[0];
        sc.success_end   = sc.debug_def_floats[1];
        sc.bonus_start   = sc.debug_def_floats[3]; // +0x0C = bonusStart (absolute position)
        sc.bonus_end     = sc.debug_def_floats[2]; // +0x08 = bonusLength

        // Check: is currentProgress inside the bonus (great) zone?
        float b_start = sc.bonus_start;
        float b_end   = sc.bonus_start + sc.bonus_end; // start + length

        if (sc.progress >= b_start && sc.progress <= b_end) {
            sc.hit_this_frame = true;
        }
    }
};

#endif
