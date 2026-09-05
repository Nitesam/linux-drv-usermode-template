#ifndef SOT_READER_H
#define SOT_READER_H

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

#include "mem_client.h"
#include "logger.h"
#include "sot_offsets.h"
#include "sot_gnames.h"

class SotReader {
public:
    explicit SotReader(MemClient& client) : client_(client) {}

    SotWorldState update(int pid, uint64_t base) {
        SotWorldState state{};
        state.base_address = base;
        pid_ = pid;

        // ── Read GWorld ──────────────────────────────────────────
        uint64_t gworld = read_ptr(base + SOT_GWORLD_OFFSET);
        state.debug.gworld = gworld;
        if (!gworld) {
            state.error = "GWorld null";
            return state;
        }

        // ── Init GNames ──────────────────────────────────────────
        uint64_t gnames = base + SOT_GNAMES_OFFSET;
        gnames_.set_address(gnames);
        state.debug.gnames_ok = gnames_.test_resolve(client_, pid_, gworld);
        log_gnames_status(state.debug.gnames_ok);

        // ── Levels ───────────────────────────────────────────────
        uint64_t persistent_level = read_ptr(gworld + SOT_WORLD_PERSISTENT_LEVEL);
        state.debug.persistent_level = persistent_level;

        SotTArray world_levels{};
        bool has_world_levels = read_mem(gworld + SOT_WORLD_LEVELS, &world_levels, sizeof(world_levels)) &&
                                world_levels.Count > 0 &&
                                world_levels.Count < 256 &&
                                SotIsLikelyPointer(world_levels.Data);
        if (!persistent_level && !has_world_levels) {
            state.error = "No levels available";
            return state;
        }

        // ── GameState ────────────────────────────────────────────
        uint64_t game_state = read_ptr(gworld + SOT_WORLD_GAMESTATE);
        state.debug.game_state = game_state;

        // ── Local player / camera ────────────────────────────────
        uint64_t local_pawn = 0;
        uint64_t camera_manager = 0;
        uint64_t player_controller = 0;

        uint64_t game_instance = read_ptr(gworld + SOT_WORLD_OWNING_INSTANCE);
        if (game_instance) {
            SotTArray local_players{};
            read_mem(game_instance + SOT_GI_LOCAL_PLAYERS, &local_players, sizeof(local_players));
            if (local_players.Count > 0 && SotIsLikelyPointer(local_players.Data)) {
                uint64_t lp0 = read_ptr(local_players.Data);
                if (lp0) {
                    player_controller = read_ptr(lp0 + SOT_LP_PLAYER_CONTROLLER);
                    if (player_controller) {
                        local_pawn = read_ptr(player_controller + SOT_PC_ACK_PAWN);
                        camera_manager = read_ptr(player_controller + SOT_PC_CAMERA_MANAGER);
                    }
                }
            }
        }
        state.debug.local_pawn = local_pawn;
        state.debug.camera_manager = camera_manager;
        if (local_pawn) {
            gnames_.debug_object_class_probe(client_, pid_, local_pawn,
                                             state.debug.local_pawn_probe,
                                             sizeof(state.debug.local_pawn_probe));

            std::vector<std::string> local_chain;
            if (gnames_.resolve_object_class_chain(client_, pid_, local_pawn, local_chain)) {
                std::string chain_text = format_class_chain(local_chain);
                snprintf(state.debug.local_pawn_class, sizeof(state.debug.local_pawn_class),
                         "%s", chain_text.c_str());
            }
        }

        // ── Read camera ──────────────────────────────────────────
        if (read_camera(camera_manager, player_controller, local_pawn,
                        state.camera, state.debug.camera_source, sizeof(state.debug.camera_source)))
        {
            state.has_camera = true;
            last_camera_ = state.camera;
            last_camera_ok_ = true;
        } else if (last_camera_ok_) {
            state.camera = last_camera_;
            state.has_camera = true;
            snprintf(state.debug.camera_source, sizeof(state.debug.camera_source), "%s", "cached");
        }

        // ── Get local player position ────────────────────────────
        SotVector local_pos{};
        if (local_pawn) {
            local_pos = read_actor_position(local_pawn);
        }
        if ((!SotIsFiniteVec(local_pos) ||
             (local_pos.X == 0.0f && local_pos.Y == 0.0f && local_pos.Z == 0.0f)) &&
            state.has_camera && SotIsFiniteVec(state.camera.Location))
        {
            local_pos = state.camera.Location;
        }

        std::unordered_set<uint64_t> seen_actors;
        std::unordered_set<std::string> seen_bp_classes;
        int remaining_budget = 25000;
        bool scanned_levels = false;

        if (has_world_levels) {
            constexpr int LEVEL_BATCH = 64;
            uint64_t level_ptrs[LEVEL_BATCH]{};

            for (uint32_t i = 0; i < world_levels.Count && remaining_budget > 0; i += LEVEL_BATCH) {
                int count = std::min<int>(LEVEL_BATCH, (int)world_levels.Count - (int)i);
                int bytes = count * 8;
                if (!read_mem(world_levels.Data + (uint64_t)i * 8, level_ptrs, bytes))
                    break;

                for (int j = 0; j < count && remaining_budget > 0; j++) {
                    uint64_t level = level_ptrs[j];
                    if (!SotIsLikelyPointer(level))
                        continue;

                    scan_level(state, level, local_pawn, local_pos,
                               seen_actors, seen_bp_classes, remaining_budget);
                    scanned_levels = true;
                }
            }
        }

        if (!scanned_levels && persistent_level) {
            scan_level(state, persistent_level, local_pawn, local_pos,
                       seen_actors, seen_bp_classes, remaining_budget);
        }

        state.player_count = (int)state.players.size();
        state.debug.player_count = state.player_count;
        state.debug.ship_count = (int)state.ships.size();
        state.debug.object_count = (int)state.objects.size();
        state.valid = true;
        return state;
    }

private:
    MemClient& client_;
    SotGNames  gnames_;
    int        pid_{};
    bool       last_gnames_ok_{true};
    std::string last_gnames_error_;
    bool       last_camera_ok_{false};
    SotMinimalViewInfo last_camera_{};
    std::unordered_set<std::string> logged_bp_classes_;
    uint32_t   cached_actor_array_offset_{SOT_LEVEL_ACTORS};
    bool       cached_actor_array_offset_valid_{true};

    // ── Memory helpers ───────────────────────────────────────────

    uint64_t read_ptr(uint64_t addr) {
        uint64_t val = 0;
        unsigned char buf[8]{};
        if (client_.read_mem(pid_, addr, 8, buf))
            memcpy(&val, buf, 8);
        return SotIsLikelyPointer(val) ? val : 0;
    }

    bool read_mem(uint64_t addr, void* out, size_t size) {
        if (size <= 4096) {
            return client_.read_mem(pid_, addr, size, (unsigned char*)out);
        }

        uint8_t* p = (uint8_t*)out;
        while (size > 0) {
            size_t chunk = std::min(size, (size_t)4096);
            if (!client_.read_mem(pid_, addr, chunk, p))
                return false;
            p += chunk;
            addr += chunk;
            size -= chunk;
        }
        return true;
    }

    void log_gnames_status(bool ok) {
        const char* err = gnames_.last_error();
        if (ok) {
            if (!last_gnames_ok_)
                LOG_INFO("GNames OK");
            last_gnames_ok_ = true;
            last_gnames_error_.clear();
            return;
        }

        const std::string reason = (err && err[0] != '\0') ? err : "Unknown reason";
        if (last_gnames_ok_ || reason != last_gnames_error_)
            LOG_ERR("GNames FAILED: %s", reason.c_str());

        last_gnames_ok_ = false;
        last_gnames_error_ = reason;
    }

    float read_float(uint64_t addr) {
        float val = 0;
        unsigned char buf[4]{};
        if (client_.read_mem(pid_, addr, 4, buf))
            memcpy(&val, buf, 4);
        return val;
    }

    bool read_u32(uint64_t addr, uint32_t& out) {
        out = 0;
        unsigned char buf[4]{};
        if (!client_.read_mem(pid_, addr, sizeof(buf), buf))
            return false;
        memcpy(&out, buf, sizeof(out));
        return true;
    }

    bool read_u64_raw(uint64_t addr, uint64_t& out) {
        out = 0;
        unsigned char buf[8]{};
        if (!client_.read_mem(pid_, addr, sizeof(buf), buf))
            return false;
        memcpy(&out, buf, sizeof(out));
        return true;
    }

    bool read_tracked_actor_type(uint64_t actor, uint64_t& out_type) {
        out_type = 0;

        static const uint64_t candidate_offsets[] = {
            SOT_AI_TRACKED_ACTOR_TYPE,
            SOT_STORAGE_TRACKED_ACTOR_TYPE,
            SOT_SHIPWRECK_TRACKED_ACTOR_TYPE,
            SOT_GHOSTSHIP_TRACKED_ACTOR_TYPE,
            SOT_ROWBOAT_TRACKED_ACTOR_TYPE,
        };

        for (uint64_t offset : candidate_offsets) {
            uint64_t raw64 = 0;
            if (read_u64_raw(actor + offset, raw64) &&
                SotClassifyTrackedActorType(raw64) != ESotActorType::Unknown)
            {
                out_type = raw64;
                return true;
            }

            uint32_t raw32 = 0;
            if (read_u32(actor + offset, raw32) &&
                SotClassifyTrackedActorType((uint64_t)raw32) != ESotActorType::Unknown)
            {
                out_type = (uint64_t)raw32;
                return true;
            }
        }

        return false;
    }

    static bool class_chain_contains_any(const std::vector<std::string>& chain,
                                         std::initializer_list<const char*> needles) {
        for (const auto& name : chain) {
            if (SotClassContainsAny(name, needles))
                return true;
        }
        return false;
    }

    static std::string format_class_chain(const std::vector<std::string>& chain) {
        std::string out;
        for (size_t i = 0; i < chain.size(); ++i) {
            if (i > 0)
                out += " <- ";
            out += chain[i];
            if (out.size() > 150) {
                out.resize(150);
                break;
            }
        }
        return out;
    }

    static bool class_chain_is_actor_like(const std::vector<std::string>& chain) {
        for (const auto& name : chain) {
            if (name == "Actor" || name == "AActor" ||
                name == "Pawn" || name == "APawn" ||
                name == "Character" || name == "ACharacter" ||
                SotClassifyActor(name) != ESotActorType::Unknown)
            {
                return true;
            }
        }
        return false;
    }

    static std::string choose_actor_class_name(const std::vector<std::string>& chain) {
        for (const auto& name : chain) {
            if (!SotIsProbablyBrokenClassName(name))
                return name;
        }
        return {};
    }

    ESotActorType resolve_actor_type(uint64_t actor, std::string& class_name,
                                     const std::vector<std::string>& class_chain) {
        ESotActorType type = SotClassifyActor(class_name);

        bool can_use_tracked_type = SotCanUseTrackedActorType(class_name);
        for (const auto& name : class_chain) {
            if (SotCanUseTrackedActorType(name)) {
                can_use_tracked_type = true;
                break;
            }
        }
        if (!can_use_tracked_type)
            return type;

        uint64_t tracked_actor_type = 0;
        if (!read_tracked_actor_type(actor, tracked_actor_type))
            return type;

        ESotActorType tracked_type = SotClassifyTrackedActorType(tracked_actor_type);
        if (tracked_type == ESotActorType::Unknown)
            return type;

        if (class_name.empty())
            class_name = SotTrackedActorTypeName(tracked_actor_type);

        if (type == ESotActorType::Unknown)
            return tracked_type;

        bool generic_ai = SotClassContainsAny(class_name, {
            "AthenaAICharacter",
            "AICharacter",
            "GoalDrivenCharacter",
        }) || class_chain_contains_any(class_chain, {
            "AthenaAICharacter",
            "AICharacter",
            "GoalDrivenCharacter",
        });

        if (generic_ai || type == ESotActorType::Skeleton)
            return tracked_type;

        return type;
    }

    // ── Read actor position with world-space fallbacks ───────────

    bool read_vec3(uint64_t addr, SotVector& out) {
        out = {};
        unsigned char buf[12]{};
        if (!client_.read_mem(pid_, addr, sizeof(buf), buf))
            return false;
        memcpy(&out, buf, sizeof(out));
        return SotIsFiniteVec(out);
    }

    static SotVector add_vec(const SotVector& a, const SotVector& b) {
        return SotVector{a.X + b.X, a.Y + b.Y, a.Z + b.Z};
    }

    static bool is_non_zero_vec(const SotVector& v) {
        return v.X != 0.0f || v.Y != 0.0f || v.Z != 0.0f;
    }

    int score_actor_array_candidate(const SotTArray& actors_arr) {
        if (actors_arr.Count == 0 || actors_arr.Count >= 100000 ||
            !SotIsLikelyPointer(actors_arr.Data))
        {
            return -100000;
        }

        constexpr int SAMPLE_LIMIT = 64;
        uint64_t ptrs[SAMPLE_LIMIT]{};
        int count = std::min<int>((int)actors_arr.Count, SAMPLE_LIMIT);
        if (!read_mem(actors_arr.Data, ptrs, (size_t)count * 8))
            return -100000;

        int score = 0;
        int valid_ptrs = 0;
        for (int i = 0; i < count; ++i) {
            uint64_t actor = ptrs[i];
            if (!SotIsLikelyPointer(actor))
                continue;

            valid_ptrs++;
            std::vector<std::string> chain;
            if (!gnames_.resolve_object_class_chain(client_, pid_, actor, chain)) {
                score -= 1;
                continue;
            }

            if (class_chain_is_actor_like(chain))
                score += 6;
            else
                score -= 1;

            std::string class_name = choose_actor_class_name(chain);
            if (!class_name.empty() &&
                SotClassifyActor(class_name) != ESotActorType::Unknown)
            {
                score += 12;
            }
        }

        if (valid_ptrs == 0)
            return -100000;

        return score;
    }

    bool read_level_actors(SotWorldState& state, uint64_t level, SotTArray& actors_arr) {
        actors_arr = {};
        if (!SotIsLikelyPointer(level))
            return false;

        if (cached_actor_array_offset_valid_) {
            SotTArray cached{};
            if (read_mem(level + cached_actor_array_offset_, &cached, sizeof(cached)) &&
                cached.Count > 0 &&
                cached.Count < 100000 &&
                SotIsLikelyPointer(cached.Data))
            {
                actors_arr = cached;
                state.debug.actor_array_offset = cached_actor_array_offset_;
                state.debug.actor_array_count = cached.Count;
                state.debug.actor_array_score = 0;
                return true;
            }

            cached_actor_array_offset_valid_ = false;
        }

        static const uint32_t candidate_offsets[] = {
            0x98,
            SOT_LEVEL_ACTORS,
            0xA8,
            0xB0,
            0xB8,
            0xC0,
        };

        SotTArray best_arr{};
        uint32_t best_offset = 0;
        int best_score = -100000;

        for (uint32_t offset : candidate_offsets) {
            SotTArray candidate{};
            if (!read_mem(level + offset, &candidate, sizeof(candidate)))
                continue;
            int score = score_actor_array_candidate(candidate);
            if (score > best_score) {
                best_score = score;
                best_arr = candidate;
                best_offset = offset;
            }
        }

        if (best_score <= 0) {
            SotTArray fallback{};
            if (!read_mem(level + SOT_LEVEL_ACTORS, &fallback, sizeof(fallback)) ||
                fallback.Count == 0 ||
                fallback.Count >= 100000 ||
                !SotIsLikelyPointer(fallback.Data))
            {
                return false;
            }

            actors_arr = fallback;
            cached_actor_array_offset_ = SOT_LEVEL_ACTORS;
            cached_actor_array_offset_valid_ = true;
            state.debug.actor_array_offset = SOT_LEVEL_ACTORS;
            state.debug.actor_array_count = fallback.Count;
            state.debug.actor_array_score = best_score;
            return true;
        }

        actors_arr = best_arr;
        cached_actor_array_offset_ = best_offset;
        cached_actor_array_offset_valid_ = true;
        state.debug.actor_array_offset = best_offset;
        state.debug.actor_array_count = best_arr.Count;
        state.debug.actor_array_score = best_score;
        return true;
    }

    void scan_level(SotWorldState& state, uint64_t level,
                    uint64_t local_pawn, const SotVector& local_pos,
                    std::unordered_set<uint64_t>& seen_actors,
                    std::unordered_set<std::string>& seen_bp_classes,
                    int& remaining_budget)
    {
        SotTArray actors_arr{};
        if (!read_level_actors(state, level, actors_arr))
            return;

        constexpr int BATCH = 256;
        uint64_t ptrs[BATCH]{};
        int scan_limit = std::min<int>((int)actors_arr.Count, remaining_budget);

        for (int i = 0; i < scan_limit && remaining_budget > 0; i += BATCH) {
            int count = std::min(BATCH, scan_limit - i);
            int bytes = count * 8;
            if (!read_mem(actors_arr.Data + (uint64_t)i * 8, ptrs, bytes))
                break;

            for (int j = 0; j < count && remaining_budget > 0; j++) {
                uint64_t actor = ptrs[j];
                if (!SotIsLikelyPointer(actor))
                    continue;
                if (!seen_actors.insert(actor).second)
                    continue;

                state.debug.actor_scan_count++;
                process_actor(state, actor, local_pawn, local_pos, seen_bp_classes);
                remaining_budget--;
            }
        }
    }

    SotVector read_component_position(uint64_t component, int depth = 0) {
        SotVector pos{};
        if (!SotIsLikelyPointer(component) || depth > 8)
            return pos;

        // Walk the component attachment chain first. This avoids treating a
        // child component's RelativeLocation as if it were already in world
        // space, which is what makes distances appear static or nonsensical.
        SotVector rel{};
        bool has_rel = read_vec3(component + SOT_SCENE_RELATIVE_LOC, rel);

        uint64_t attach_parent = read_ptr(component + SOT_SCENE_ATTACH_PARENT);
        if (attach_parent && has_rel) {
            SotVector parent_pos = read_component_position(attach_parent, depth + 1);
            if (SotIsFiniteVec(parent_pos) && is_non_zero_vec(parent_pos))
                return add_vec(parent_pos, rel);
        }

        if (has_rel && is_non_zero_vec(rel))
            return rel;

        // Keep the old derived ComponentToWorld probe as a last resort only.
        if (read_vec3(component + SOT_SCENE_COMPONENT_TO_WORLD + SOT_TRANSFORM_TRANSLATION, pos) &&
            is_non_zero_vec(pos))
        {
            return pos;
        }

        return {};
    }

    SotVector read_actor_position(uint64_t actor, int depth = 0) {
        SotVector pos{};
        if (!SotIsLikelyPointer(actor) || depth > 4)
            return pos;

        // For replicated world actors like ships and floating loot, this is
        // usually the most stable absolute position.
        if (read_vec3(actor + SOT_ACTOR_REPLICATED_MOVEMENT + SOT_REPMOV_LOCATION, pos) &&
            is_non_zero_vec(pos))
        {
            return pos;
        }

        // When an actor is attached to something else, like the local pawn on
        // a ship, RelativeLocation is local-space. Use the parent actor plus
        // attachment offset to keep distances moving with the ship.
        uint64_t attach_parent = read_ptr(actor + SOT_ACTOR_ATTACHMENT_REPLICATION + SOT_REPATT_PARENT);
        SotVector attach_offset{};
        if (attach_parent &&
            read_vec3(actor + SOT_ACTOR_ATTACHMENT_REPLICATION + SOT_REPATT_LOC_OFFSET, attach_offset))
        {
            SotVector parent_pos = read_actor_position(attach_parent, depth + 1);
            if (SotIsFiniteVec(parent_pos) &&
                is_non_zero_vec(parent_pos))
            {
                return add_vec(parent_pos, attach_offset);
            }
        }

        uint64_t root = read_ptr(actor + SOT_ACTOR_ROOT_COMPONENT);
        if (root) {
            pos = read_component_position(root);
            if (SotIsFiniteVec(pos) && is_non_zero_vec(pos))
                return pos;
        }

        return {};
    }

    // ── Read camera ──────────────────────────────────────────────

    bool read_rotator(uint64_t addr, SotRotator& out) {
        unsigned char buf[12]{};
        if (!client_.read_mem(pid_, addr, sizeof(buf), buf))
            return false;

        memcpy(&out, buf, sizeof(out));
        return std::isfinite(out.Pitch) && std::isfinite(out.Yaw) && std::isfinite(out.Roll);
    }

    static float normalize_angle(float deg) {
        while (deg > 180.0f) deg -= 360.0f;
        while (deg < -180.0f) deg += 360.0f;
        return deg;
    }

    bool read_camera_from_controller(uint64_t player_controller, uint64_t local_pawn,
                                     SotMinimalViewInfo& out, char* source, size_t source_size) {
        if (!player_controller || !local_pawn)
            return false;

        SotVector pos = read_actor_position(local_pawn);
        if (!SotIsFiniteVec(pos))
            return false;

        static const struct {
            uint64_t offset;
            const char* label;
        } kRotSources[] = {
            { SOT_PC_TARGET_VIEW_ROTATION, "pc.target_view" },
            { SOT_CONTROLLER_CONTROL_ROT,  "pc.control_rot" },
        };

        for (const auto& rot_src : kRotSources) {
            SotRotator rot{};
            if (!read_rotator(player_controller + rot_src.offset, rot))
                continue;

            rot.Pitch = normalize_angle(rot.Pitch);
            rot.Yaw   = normalize_angle(rot.Yaw);
            rot.Roll  = normalize_angle(rot.Roll);

            out.Location = pos;
            out.Location.Z += 64.0f;
            out.Rotation = rot;
            out.FOV = (last_camera_ok_ && std::isfinite(last_camera_.FOV) &&
                       last_camera_.FOV >= 1.0f && last_camera_.FOV <= 170.0f)
                    ? last_camera_.FOV
                    : 90.0f;

            snprintf(source, source_size, "%s", rot_src.label);
            return true;
        }

        return false;
    }

    bool read_camera(uint64_t cam_mgr, uint64_t player_controller, uint64_t local_pawn,
                     SotMinimalViewInfo& out, char* source, size_t source_size) {
        if (source_size > 0)
            source[0] = '\0';

        if (cam_mgr) {
            // The SDK shows FMinimalViewInfo is much larger than the classic 28-byte
            // layout, so probe known POV sources and accept the first plausible FOV.
            static const struct {
                uint64_t offset;
                const char* label;
            } kPovSources[] = {
                { SOT_CAM_CACHE,            "cam.cache" },
                { SOT_CAM_LAST_FRAME_CACHE, "cam.last_frame" },
                { SOT_CAM_VIEWTARGET,       "cam.viewtarget" },
            };
            static const uint32_t fov_offsets[] = {
                0x18, 0x1C, 0x20, 0x24, 0x28, 0x30, 0x38,
            };

            for (const auto& pov_src : kPovSources) {
                uint64_t pov_addr = cam_mgr + pov_src.offset + SOT_CAM_POV_OFFSET;
                unsigned char buf[0x60]{};
                if (!client_.read_mem(pid_, pov_addr, sizeof(buf), buf))
                    continue;

                SotMinimalViewInfo candidate{};
                memcpy(&candidate.Location, buf + 0x00, 12);
                memcpy(&candidate.Rotation, buf + 0x0C, 12);

                if (!SotIsFiniteVec(candidate.Location) ||
                    !std::isfinite(candidate.Rotation.Pitch) ||
                    !std::isfinite(candidate.Rotation.Yaw) ||
                    !std::isfinite(candidate.Rotation.Roll))
                    continue;

                for (uint32_t off : fov_offsets) {
                    float fov = 0.0f;
                    memcpy(&fov, buf + off, 4);
                    if (!std::isfinite(fov) || fov < 1.0f || fov > 170.0f)
                        continue;

                    candidate.FOV = fov;
                    out = candidate;
                    snprintf(source, source_size, "%s+0x%X", pov_src.label, off);
                    return true;
                }
            }
        }

        return read_camera_from_controller(player_controller, local_pawn, out, source, source_size);
    }

    // ── Read player name from PlayerState ────────────────────────

    void read_player_name(uint64_t playerstate, char* out_name, size_t max_len) {
        out_name[0] = '\0';
        if (!playerstate) return;

        uint64_t str_data = 0;
        uint32_t str_count = 0;

        unsigned char sbuf[16]{};
        if (!client_.read_mem(pid_, playerstate + SOT_PS_PLAYER_NAME, 16, sbuf))
            return;

        memcpy(&str_data, sbuf, 8);
        memcpy(&str_count, sbuf + 8, 4);

        if (!SotIsLikelyPointer(str_data) || str_count == 0 || str_count > 256)
            return;

        size_t wchar_bytes = str_count * 2;
        if (wchar_bytes > 512) wchar_bytes = 512;

        unsigned char wbuf[512]{};
        if (!client_.read_mem(pid_, str_data, wchar_bytes, wbuf))
            return;

        size_t out_idx = 0;
        for (uint32_t i = 0; i < str_count && out_idx < max_len - 1; i++) {
            uint16_t wc = 0;
            memcpy(&wc, wbuf + i * 2, 2);
            if (wc == 0) break;
            out_name[out_idx++] = (wc >= 32 && wc <= 126) ? (char)wc : '?';
        }
        out_name[out_idx] = '\0';
    }

    // ── Process a single actor ───────────────────────────────────

    static bool starts_with_bp_prefix(const std::string& class_name) {
        return class_name.rfind("BP_", 0) == 0;
    }

    void record_bp_class(SotWorldState& state,
                         const std::string& class_name,
                         std::unordered_set<std::string>& seen_bp_classes) {
        if (!starts_with_bp_prefix(class_name))
            return;

        state.debug.bp_actor_count++;
        if (!seen_bp_classes.insert(class_name).second)
            return;

        state.debug.bp_classes.push_back(class_name);
        if (logged_bp_classes_.insert(class_name).second)
            LOG_DBG("[SoT] BP_ class found: %s", class_name.c_str());
    }

    void record_bp_classes(SotWorldState& state,
                           const std::vector<std::string>& class_chain,
                           std::unordered_set<std::string>& seen_bp_classes) {
        for (const auto& name : class_chain)
            record_bp_class(state, name, seen_bp_classes);
    }

    void process_actor(SotWorldState& state, uint64_t actor,
                       uint64_t local_pawn, const SotVector& local_pos,
                       std::unordered_set<std::string>& seen_bp_classes)
    {
        std::vector<std::string> class_chain;
        if (!gnames_.resolve_object_class_chain(client_, pid_, actor, class_chain)) {
            state.debug.class_resolve_fail_count++;
            const char* err = gnames_.last_error();
            if (err && err[0] != '\0')
                snprintf(state.debug.class_resolve_error, sizeof(state.debug.class_resolve_error), "%s", err);
            return;
        }

        record_bp_classes(state, class_chain, seen_bp_classes);

        if (!class_chain_is_actor_like(class_chain)) {
            state.debug.unknown_type_count++;
            if (state.debug.unknown_actor_count < 32) {
                std::string chain_text = format_class_chain(class_chain);
                snprintf(state.debug.unknown_actors[state.debug.unknown_actor_count],
                         64, "%s", chain_text.c_str());
                state.debug.unknown_actor_count++;
            }
            return;
        }

        std::string class_name = choose_actor_class_name(class_chain);

        if (!class_name.empty() && SotIsProbablyBrokenClassName(class_name)) {
            state.debug.broken_class_name_count++;
            return;
        }

        ESotActorType type = resolve_actor_type(actor, class_name, class_chain);
        if (type == ESotActorType::Unknown) {
            state.debug.unknown_type_count++;
            if (state.debug.unknown_actor_count < 32) {
                bool interesting =
                    class_name.find("BP_") != std::string::npos ||
                    class_name.find("Athena") != std::string::npos ||
                    class_name.find("Pawn") != std::string::npos ||
                    class_name.find("Character") != std::string::npos ||
                    class_name.find("Tracked:") != std::string::npos;
                if (interesting || state.debug.unknown_actor_count < 8) {
                    snprintf(state.debug.unknown_actors[state.debug.unknown_actor_count],
                             64, "%s", class_name.c_str());
                    state.debug.unknown_actor_count++;
                }
            }
            return;
        }

        state.debug.classified_actor_count++;
        SotVector pos = read_actor_position(actor);
        if (!SotIsFiniteVec(pos) || (pos.X == 0 && pos.Y == 0 && pos.Z == 0)) {
            state.debug.position_fail_count++;
            if (state.debug.position_fail_sample_count < 8 && !class_name.empty()) {
                snprintf(state.debug.position_fail_samples[state.debug.position_fail_sample_count],
                         64, "%s", class_name.c_str());
                state.debug.position_fail_sample_count++;
            }
            return;
        }

        float dist = SotVectorDistance(pos, local_pos);

        if ((type == ESotActorType::Fort || type == ESotActorType::WorldEvent) &&
            dist < 20000.0f &&
            pos.Z < local_pos.Z - 1200.0f)
        {
            return;
        }

        switch (type) {
            case ESotActorType::Player: {
                SotPlayerData p{};
                p.address = actor;
                p.type = ESotActorType::Player;
                p.position = pos;
                p.distance = dist / 100.0f; // UE units -> meters
                p.valid = true;
                p.is_local = (actor == local_pawn);
                snprintf(p.actor_class, sizeof(p.actor_class), "%s", class_name.c_str());

                uint64_t ps = read_ptr(actor + SOT_PAWN_PLAYERSTATE);
                p.playerstate = ps;
                if (ps) {
                    read_player_name(ps, p.name, sizeof(p.name));
                }
                if (p.name[0] == '\0') {
                    snprintf(p.name, sizeof(p.name), "Player_%d", (int)state.players.size());
                }

                state.players.push_back(p);
                break;
            }

            case ESotActorType::Ship: {
                SotShipData s{};
                s.address = actor;
                s.ship_type = SotClassifyShip(class_name);
                s.position = pos;
                s.distance = dist / 100.0f;
                s.valid = true;
                snprintf(s.actor_class, sizeof(s.actor_class), "%s", class_name.c_str());
                state.ships.push_back(s);
                break;
            }

            case ESotActorType::Skeleton: {
                SotPlayerData p{};
                p.address = actor;
                p.type = ESotActorType::Skeleton;
                p.position = pos;
                p.distance = dist / 100.0f;
                p.valid = true;
                snprintf(p.actor_class, sizeof(p.actor_class), "%s", class_name.c_str());
                snprintf(p.name, sizeof(p.name), "Skeleton");
                state.players.push_back(p);
                break;
            }

            default: {
                SotObjectData o{};
                o.address = actor;
                o.type = type;
                o.position = pos;
                o.distance = dist / 100.0f;
                snprintf(o.class_name, sizeof(o.class_name), "%s", class_name.c_str());
                state.objects.push_back(o);
                break;
            }
        }
    }
};

#endif
