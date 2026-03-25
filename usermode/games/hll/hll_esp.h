#ifndef HLL_ESP_H
#define HLL_ESP_H

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "imgui.h"
#include "hll_offsets.h"
#include "hll_reader.h"

namespace w2s {

constexpr float kPi = 3.1415926f;
constexpr float kRotToRad = kPi / 180.0f;

struct ScreenPoint { float X{}, Y{}; };
struct ScreenSize  { int Width{}, Height{}; };

struct CameraAxes {
    FVector AxisX{}, AxisY{}, AxisZ{}, CameraLocation{};
    float FovFactor{};
};

inline FVector RotationToVector(const FRotator& r)
{
    float yaw   = r.Yaw   * kRotToRad;
    float pitch = r.Pitch * kRotToRad;
    float cp    = std::cos(pitch);
    return {std::cos(yaw) * cp, std::sin(yaw) * cp, std::sin(pitch)};
}

inline float VecSize(const FVector& v)
{
    return std::sqrt(v.X*v.X + v.Y*v.Y + v.Z*v.Z);
}

inline void Normalize(FVector& v)
{
    float m = VecSize(v);
    if (m == 0.0f) { v = {1,1,1}; return; }
    v.X /= m; v.Y /= m; v.Z /= m;
}

inline FVector VecSub(const FVector& a, const FVector& b)
{
    return {a.X - b.X, a.Y - b.Y, a.Z - b.Z};
}

inline float Dot(const FVector& a, const FVector& b)
{
    return a.X*b.X + a.Y*b.Y + a.Z*b.Z;
}

inline void GetAxes(FRotator rot, FVector& x, FVector& y, FVector& z)
{
    x = RotationToVector(rot);
    Normalize(x);

    rot.Yaw += 89.8f;
    FRotator yr = rot;
    yr.Pitch = 0.0f;
    y = RotationToVector(yr);
    Normalize(y);
    y.Z = 0.0f;

    rot.Yaw -= 89.8f;
    rot.Pitch += 89.8f;
    z = RotationToVector(rot);
    Normalize(z);
}

inline CameraAxes BuildCameraAxes(const FMinimalViewInfo& cam)
{
    CameraAxes a{};
    GetAxes(cam.Rotation, a.AxisX, a.AxisY, a.AxisZ);
    a.CameraLocation = cam.Location;
    a.FovFactor = std::max(std::tan(cam.FOV * kPi / 360.0f), 0.001f);
    return a;
}

inline ScreenPoint WorldToScreen(const FVector& loc, const CameraAxes& axes, const ScreenSize& ss)
{
    FVector d = VecSub(loc, axes.CameraLocation);
    FVector t{};
    t.X = Dot(d, axes.AxisY);
    t.Y = Dot(d, axes.AxisZ);
    t.Z = Dot(d, axes.AxisX);
    if (t.Z < 1.0f) t.Z = 1.0f;

    float cx = static_cast<float>(ss.Width)  * 0.5f;
    float cy = static_cast<float>(ss.Height) * 0.5f;
    return {
        cx + t.X * (cx / axes.FovFactor) / t.Z,
        cy + (-t.Y) * (cx / axes.FovFactor) / t.Z,
    };
}

inline ScreenPoint WorldToScreen(const FVector& loc, const FMinimalViewInfo& cam, const ScreenSize& ss)
{
    return WorldToScreen(loc, BuildCameraAxes(cam), ss);
}

inline bool IsInFront(const FVector& wl, const CameraAxes& a)
{
    return Dot(VecSub(wl, a.CameraLocation), a.AxisX) >= 1.0f;
}

inline bool IsOnScreen(const ScreenPoint& sp, const ScreenSize& ss)
{
    return sp.X >= 0.0f && sp.X <= static_cast<float>(ss.Width) &&
           sp.Y >= 0.0f && sp.Y <= static_cast<float>(ss.Height);
}

}

struct BoundingBox2D {
    float X{}, Y{}, Width{}, Height{};
};

inline std::optional<BoundingBox2D> GetCharacterBoundingBox(
    const PlayerData& p,
    const FMinimalViewInfo& cam,
    const w2s::ScreenSize& ss)
{
    if (!p.has_location || !IsFiniteVector(p.location) || p.capsule.CapsuleHalfHeight <= 0.0f)
        return std::nullopt;

    float hh = p.capsule.CapsuleHalfHeight;
    FVector head{p.location.X, p.location.Y, p.location.Z + hh + hh * 0.15f};
    FVector feet{p.location.X, p.location.Y, p.location.Z - hh};

    auto hsp = w2s::WorldToScreen(head, cam, ss);
    if (!w2s::IsOnScreen(hsp, ss))
        return std::nullopt;

    auto fsp = w2s::WorldToScreen(feet, cam, ss);

    float bh = std::fabs(hsp.Y - fsp.Y);
    float bw = (p.capsule.CapsuleRadius * 2.0f * static_cast<float>(ss.Width) * 0.001f) /
               std::max(p.distance * 100.0f, 1.0f);
    bw = std::max(bw, bh * 0.4f);

    return BoundingBox2D{
        std::min(hsp.X, fsp.X) - (bw / 2.0f),
        std::min(hsp.Y, fsp.Y),
        bw, bh
    };
}

inline std::optional<FVector> GetBoneWorldPosition(
    const std::vector<FTransform>& bones,
    size_t idx,
    const FTransform& c2w,
    float z_correction = 0.0f)
{
    if (idx >= bones.size()) return std::nullopt;
    auto bm = ToMatrixWithScale(bones[idx]);
    auto cm = ToMatrixWithScale(c2w);
    auto pos = GetMatrixTranslation(MultiplyMatrix(bm, cm));
    pos.Z += z_correction;
    return pos;
}

struct EspSettings {
    bool show_box      = true;
    bool show_skeleton = true;
    bool show_distance = true;
    bool show_weapon   = true;
    bool team_filter   = true;
    float max_distance = 300.0f;
    float y_offset     = -30.0f;
};

class EspRenderer {
public:
    EspSettings settings;

    void render(ImDrawList* dl, const HllWorldState& state, int sw, int sh)
    {
        if (!state.valid || !state.has_camera)
            return;

        const w2s::ScreenSize ss{sw, sh};
        const auto axes = w2s::BuildCameraAxes(state.camera);

        for (const auto& p : state.players) {
            if (!p.valid || !p.has_location || !IsFiniteVector(p.location))
                continue;
            if (p.health <= 0.0f)
                continue;
            if (settings.team_filter && state.has_local_team &&
                p.team == state.local_team && p.team != 0)
                continue;
            if (p.distance > settings.max_distance && p.distance > 0)
                continue;
            if (!w2s::IsInFront(p.location, axes))
                continue;

            ImU32 col_esp  = IM_COL32(255, 50, 50, 255);
            ImU32 col_text = IM_COL32(255, 255, 255, 255);

            if (settings.show_box) {
                auto box = GetCharacterBoundingBox(p, state.camera, ss);
                if (box.has_value()) {
                    const auto& b = *box;
                    float yo = settings.y_offset;
                    ImVec2 tl(b.X, b.Y + yo);
                    ImVec2 br(b.X + b.Width, b.Y + b.Height + yo);
                    dl->AddRect(tl, br, col_esp, 0.0f, 0, 1.5f);

                    float label_y = tl.y - 14.0f;

                    if (settings.show_weapon && !p.weapon_name.empty()) {
                        auto ts = ImGui::CalcTextSize(p.weapon_name.c_str());
                        dl->AddText(ImVec2(tl.x + b.Width * 0.5f - ts.x * 0.5f, label_y),
                                    col_text, p.weapon_name.c_str());
                        label_y -= 14.0f;
                    }

                    if (settings.show_distance && p.distance > 0) {
                        char dbuf[32];
                        snprintf(dbuf, sizeof(dbuf), "%.0fm", p.distance);
                        auto ds = ImGui::CalcTextSize(dbuf);
                        dl->AddText(ImVec2(tl.x + b.Width * 0.5f - ds.x * 0.5f, br.y + 4.0f),
                                    col_text, dbuf);
                    }
                }
            }

            if (settings.show_skeleton && p.has_component_to_world && !p.bone_transforms.empty()) {
                constexpr size_t kMaxBones = 80;
                std::array<std::optional<ImVec2>, kMaxBones> bone_pts{};

                for (auto bi : kTrackedBoneIndices) {
                    if (bi >= p.bone_transforms.size()) continue;
                    auto bwp = GetBoneWorldPosition(p.bone_transforms, bi, p.component_to_world);
                    if (!bwp || !IsFiniteVector(*bwp) || !w2s::IsInFront(*bwp, axes))
                        continue;
                    auto bsp = w2s::WorldToScreen(*bwp, axes, ss);
                    if (!w2s::IsOnScreen(bsp, ss)) continue;
                    bone_pts[bi] = ImVec2(bsp.X, bsp.Y + settings.y_offset);
                }

                for (const auto& [from, to] : kSkeletonConnections) {
                    if (from >= kMaxBones || to >= kMaxBones) continue;
                    if (!bone_pts[from] || !bone_pts[to]) continue;
                    dl->AddLine(*bone_pts[from], *bone_pts[to],
                                IM_COL32(0, 0, 0, 255), 3.0f);
                    dl->AddLine(*bone_pts[from], *bone_pts[to],
                                col_esp, 1.5f);
                }
            }
        }
    }

    void render_controls()
    {
        ImGui::Checkbox("Show Boxes", &settings.show_box);
        ImGui::Checkbox("Show Skeleton", &settings.show_skeleton);
        ImGui::Checkbox("Show Distance", &settings.show_distance);
        ImGui::Checkbox("Show Weapon", &settings.show_weapon);
        ImGui::Checkbox("Team Filter (ESP)", &settings.team_filter);
        ImGui::SliderFloat("Max Distance (m)", &settings.max_distance, 50.0f, 1000.0f, "%.0f");
        ImGui::SliderFloat("Y Offset (px)", &settings.y_offset, -100.0f, 100.0f, "%.0f");
    }
};

#endif
