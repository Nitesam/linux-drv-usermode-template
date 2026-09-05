#ifndef HLL_OFFSETS_H
#define HLL_OFFSETS_H

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <array>
#include <string_view>

#define HLL_GWORLD                          0x467BC30

#define HLL_GWORLD_OWNING_GAME_INSTANCE     0x188
#define HLL_GAME_INSTANCE_LOCAL_PLAYERS      0x38
#define HLL_ULOCAL_PLAYER_PLAYER_CONTROLLER 0x30
#define HLL_PLAYER_CONTROLLER_ACK_PAWN      0x02A0
#define HLL_PLAYER_CONTROLLER_CAMERA_MANAGER 0x2B8
#define HLL_CAMERA_MANAGER_CAMERA_CACHE     0x1A70

#define HLL_GWORLD_PERSISTENT_LEVEL         0x30
#define HLL_GWORLD_GAME_STATE               0x130
#define HLL_GWORLD_TIME_SECONDS             0x0520

#define HLL_LEVEL_ACTORS                    0x98
#define HLL_ACTOR_ROOT_COMPONENT            0x130
#define HLL_ACTOR_ROOT_COMPONENT_REL_LOC    0x11C

#define HLL_GAME_STATE_PLAYERS              0x238
#define HLL_GAME_STATE_REPLICATED_TIME      0x024C

#define HLL_REP_PLAYER_INFO                 0x04AC
#define HLL_PLAYER_INFO_TEAM                0x00
#define HLL_PLAYER_INFO_ROLE                0x01
#define HLL_PLAYER_INFO_PLATOON             0x04

#define HLL_PAWN_PRIVATE                    0x280
#define HLL_PAWN_PLAYER_STATE               0x240
#define HLL_PAWN_CHARACTER_HEALTH           0x0A84
#define HLL_PAWN_CAPSULE_COMPONENT          0x0290
#define HLL_PAWN_CAPSULE_HALF_HEIGHT        0x410
#define HLL_PAWN_CURRENT_WEAPON             0x05F0
#define HLL_WEAPON_TYPE                     0x0300
#define HLL_WEAPON_AMMO_INFO                0x0586
#define HLL_WEAPON_CLIP_INFO                0x058A

#define HLL_PAWN_SKELETAL_MESH              0x0280
#define HLL_SKINNED_MESH_SPACE_TRANSFORMS   0x0440
#define HLL_SKINNED_MESH_SPACE_TRANSFORMS_1 0x0450
#define HLL_MESH_COMPONENT_TO_WORLD         0x01C0
#define HLL_MESH_LAST_SUBMIT_TIME           0x0278
#define HLL_MESH_LAST_RENDER_TIME           0x027C
#define HLL_MESH_LAST_RENDER_ON_SCREEN      0x0280

struct FVector {
    float X{}, Y{}, Z{};
};

struct FQuat {
    float X{}, Y{}, Z{}, W{};
};

struct FRotator {
    float Pitch{}, Yaw{}, Roll{};
};

struct FMinimalViewInfo {
    FVector Location{};
    FRotator Rotation{};
    float FOV{};
    float DesiredFOV{};
    float OrthoWidth{};
    float OrthoNearClipPlane{};
    float OrthoFarClipPlane{};
    float AspectRatio{};
};

struct UETArray {
    uint64_t ArrayPointer{};
    int32_t Length{};
    int32_t MaxLength{};
};

struct CapsuleData {
    float CapsuleHalfHeight{};
    float CapsuleRadius{};
};

struct FWeaponAmmoInfo {
    uint16_t CurrentAmmo{};
    int32_t IncrementCounter{};
};

struct FMatrix4x4 {
    float M[4][4]{};
};

struct FTransform {
    FQuat Rotation{};
    FVector Translation{};
    float TranslationPad{};
    FVector Scale3D{1.0f, 1.0f, 1.0f};
    float ScalePad{};
};

inline FMatrix4x4 ToMatrixWithScale(const FTransform& t)
{
    const auto x2 = t.Rotation.X + t.Rotation.X;
    const auto y2 = t.Rotation.Y + t.Rotation.Y;
    const auto z2 = t.Rotation.Z + t.Rotation.Z;
    const auto xx2 = t.Rotation.X * x2;
    const auto yy2 = t.Rotation.Y * y2;
    const auto zz2 = t.Rotation.Z * z2;
    const auto yz2 = t.Rotation.Y * z2;
    const auto wx2 = t.Rotation.W * x2;
    const auto xy2 = t.Rotation.X * y2;
    const auto wz2 = t.Rotation.W * z2;
    const auto xz2 = t.Rotation.X * z2;
    const auto wy2 = t.Rotation.W * y2;

    FMatrix4x4 m{};
    m.M[0][0] = (1.0f - (yy2 + zz2)) * t.Scale3D.X;
    m.M[0][1] = (xy2 + wz2) * t.Scale3D.X;
    m.M[0][2] = (xz2 - wy2) * t.Scale3D.X;
    m.M[1][0] = (xy2 - wz2) * t.Scale3D.Y;
    m.M[1][1] = (1.0f - (xx2 + zz2)) * t.Scale3D.Y;
    m.M[1][2] = (yz2 + wx2) * t.Scale3D.Y;
    m.M[2][0] = (xz2 + wy2) * t.Scale3D.Z;
    m.M[2][1] = (yz2 - wx2) * t.Scale3D.Z;
    m.M[2][2] = (1.0f - (xx2 + yy2)) * t.Scale3D.Z;
    m.M[3][0] = t.Translation.X;
    m.M[3][1] = t.Translation.Y;
    m.M[3][2] = t.Translation.Z;
    m.M[3][3] = 1.0f;
    return m;
}

inline FMatrix4x4 MultiplyMatrix(const FMatrix4x4& a, const FMatrix4x4& b)
{
    FMatrix4x4 r{};
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            r.M[row][col] =
                a.M[row][0] * b.M[0][col] +
                a.M[row][1] * b.M[1][col] +
                a.M[row][2] * b.M[2][col] +
                a.M[row][3] * b.M[3][col];
    return r;
}

inline FVector GetMatrixTranslation(const FMatrix4x4& m)
{
    return {m.M[3][0], m.M[3][1], m.M[3][2]};
}

enum class EWeaponType : uint8_t {
    Weapon_None = 0, Kar98, Garand, MP40, Thompson,
    M24_Grenade, MK2_Grenade, MG42, M1918A2_BAR,
    M1_Carbine, M43, G43, M1903, M1911, M1919,
    M1A1, M2, Tellermine43, SMine, Luger,
    Kar98_Sniper, STG44, Bazooka, Panzershreck,
    Panzerfaust, Bandage, Morphine_US, Morphine_GER,
    M18_Smoke, Nb39_Smoke, AlliesBinoculars, AxisBinoculars,
    USSupplyCrate, GerSupplyCrate, USAmmoBox, GerAmmoBox,
    P38, Watch_US, Watch_GER, Wrench,
    Flamethrower_US, Flamethrower_GER, Molotov,
    Rifle_Mosin_M38, Rifle_Mosin_M1891, Rifle_Mosin_M9130,
    Rifle_SVT40, Pistol_M1895, SMG_PPSH41,
    Equipment_Hammer, Satchel_M37, Satchel_3KG,
    Torch_US, Torch_GER, SMG_M3_GreaseGun,
    Rifle_SVT40_Sniper, Rifle_Mosin_M9130_Sniper,
    SMG_PPSH41_Drum, ATRifle_PTRS41, LMG_DP27,
    Mine_POMZ, Mine_TM35, Grenade_Frag_RDG42,
    Grenade_Smoke_RDG2, Gammon_Bomb, MG34, Knife_US,
    TokarevTT33, Watch_RU, Torch_RU, Spade_GER,
    Spade_RU, Satchel_RU, AmmoBox_RU, SupplyCrate_RU,
    Binoculars_RU, Revive_RU, TrenchGun, FG42, FlareGun,
    GB_Thompson, GB_Thompson_Drum, GB_Thompson_B,
    GB_Thompson_Drum_B, LeeEnfield1914, LeeEnfieldNo4Mk1,
    LeeEnfieldNo4Mk1Sniper, LeeEnfieldCarbine,
    LeeEnfield1914Sniper, SMLE_No1_Mk3, StenGun,
    StenGunMk5, Lanchester, BrenGun, LewisGun,
    WebleyMkVI, BoysATRifle, PIAT,
    Grenade_Frag_MillsBomb, Grenade_Smoke_No77,
    Mine_GSMKV, Mine_ShrapnelMk2, FairbairnSykes,
    Binoculars_COM, SupplyCrate_COM, AmmoBox_COM,
    Torch_COM, Satchel_COM, Watch_COM, Morphine_COM,
    Flamethrower_COM, Tnt, EWeaponType_MAX
};

inline const char* WeaponName(EWeaponType w)
{
    static const char* names[] = {
        "None","Kar98","Garand","MP40","Thompson",
        "M24_Grenade","MK2_Grenade","MG42","BAR",
        "M1_Carbine","M43","G43","M1903","M1911","M1919",
        "M1A1","M2","Tellermine43","SMine","Luger",
        "Kar98_Sniper","STG44","Bazooka","Panzershreck",
        "Panzerfaust","Bandage","Morphine_US","Morphine_GER",
        "M18_Smoke","Nb39_Smoke","Binoculars_US","Binoculars_GER",
        "SupplyCrate_US","SupplyCrate_GER","AmmoBox_US","AmmoBox_GER",
        "P38","Watch_US","Watch_GER","Wrench",
        "Flamethrower_US","Flamethrower_GER","Molotov",
        "Mosin_M38","Mosin_M1891","Mosin_M9130",
        "SVT40","Nagant_M1895","PPSH41",
        "Hammer","Satchel_M37","Satchel_3KG",
        "Torch_US","Torch_GER","M3_GreaseGun",
        "SVT40_Sniper","Mosin_Sniper",
        "PPSH41_Drum","PTRS41","DP27",
        "POMZ","TM35","RDG42","RDG2_Smoke",
        "Gammon_Bomb","MG34","Knife_US",
        "TT33","Watch_RU","Torch_RU","Spade_GER",
        "Spade_RU","Satchel_RU","AmmoBox_RU","SupplyCrate_RU",
        "Binoculars_RU","Revive_RU","TrenchGun","FG42","FlareGun",
        "Thompson_GB","Thompson_Drum_GB","Thompson_B_GB",
        "Thompson_DrumB_GB","LeeEnfield1914","LeeEnfieldNo4Mk1",
        "LeeEnfield_Sniper","LeeEnfield_Carbine",
        "LeeEnfield1914_Sniper","SMLE_No1_Mk3","StenGun",
        "StenGunMk5","Lanchester","BrenGun","LewisGun",
        "Webley","BoysATRifle","PIAT",
        "MillsBomb","No77_Smoke",
        "GSMKV","ShrapnelMk2","FairbairnSykes",
        "Binoculars_COM","SupplyCrate_COM","AmmoBox_COM",
        "Torch_COM","Satchel_COM","Watch_COM","Morphine_COM",
        "Flamethrower_COM","Tnt","MAX"
    };
    auto idx = static_cast<int>(w);
    if (idx < 0 || idx >= static_cast<int>(sizeof(names)/sizeof(names[0])))
        return "Unknown";
    return names[idx];
}

constexpr size_t HLL_MAX_BONES = 80;

constexpr std::array<size_t, 20> kTrackedBoneIndices = {
    4, 5, 6, 7, 10, 11, 36, 14, 16, 17,
    60, 38, 40, 41, 62, 64, 66, 69, 71, 73
};

constexpr std::array<std::pair<size_t, size_t>, 20> kSkeletonConnections = {{
    {11,10},{10,7},{7,6},{6,5},{5,4},
    {7,36},{36,14},{14,16},{16,17},
    {7,60},{60,38},{38,40},{40,41},
    {36,60},
    {4,62},{62,64},{64,66},
    {4,69},{69,71},{71,73},
}};

inline bool IsLikelyPointer(uint64_t addr)
{
    return addr != 0 &&
           ((addr >= 0x10000 && addr < 0x00007FFFFFFFFFFFULL) ||
            addr >= 0xFFFF800000000000ULL);
}

inline bool IsFiniteVector(const FVector& v)
{
    return std::isfinite(v.X) && std::isfinite(v.Y) && std::isfinite(v.Z);
}

inline float VectorDistance(const FVector& a, const FVector& b)
{
    float dx = a.X - b.X, dy = a.Y - b.Y, dz = a.Z - b.Z;
    return std::sqrt(dx*dx + dy*dy + dz*dz);
}

#endif
