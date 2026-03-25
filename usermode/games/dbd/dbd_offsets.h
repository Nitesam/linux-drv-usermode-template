#ifndef DBD_OFFSETS_H
#define DBD_OFFSETS_H

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <string>

#define DBD_STEAM_GWORLD_OFFSET          0x0BF53D30
#define DBD_STEAM_GNAMES_OFFSET          0x0BCC7740
#define DBD_STEAM_GOBJECTS_OFFSET        0x0BDA1D00

#define DBD_EGS_GWORLD_OFFSET            0x0B877140
#define DBD_EGS_GNAMES_OFFSET            0x0B5EBF40
#define DBD_EGS_GOBJECTS_OFFSET          0x0B6BF540

#define DBD_PERSISTENT_LEVEL             0x50
#define DBD_OWNING_GAME_INSTANCE         0x200
#define DBD_LOCAL_PLAYERS                0x58
#define DBD_PLAYER_CONTROLLER            0x50
#define DBD_ACK_PAWN                     0x388
#define DBD_CAMERA_MANAGER               0x398
#define DBD_CAMERA_CACHE_PRIVATE         0x1400

#define DBD_GAME_STATE                   0x0188
#define DBD_PLAYER_ARRAY                 0x02F0
#define DBD_PLAYER_DATA                  0x0558
#define DBD_PLAYER_DATA_CHAR_LEVEL       0x0000
#define DBD_PLAYER_DATA_PRESTIGE         0x007C

#define DBD_DIRECT_ACTORS_ARRAY          0x0C0
#define DBD_DIRECT_ACTORS_COUNT          0x0C8
#define DBD_ACTOR_CLUSTER                0x100
#define DBD_MODEL_COMPONENTS             0x0F0
#define DBD_ACTOR_ARRAY                  0x48

#define DBD_OBJECT_CLASS                 0x10
#define DBD_OBJECT_NAME                  0x18
#define DBD_PLAYER_STATE                 0x02F8
#define DBD_PAWN_PRIVATE                 0x0358
#define DBD_ROOT_COMPONENT               0x01C8
#define DBD_GAME_ROLE                    0x03C2
#define DBD_PLAYER_NAME_PRIVATE          0x0378
#define DBD_RELATIVE_LOCATION            0x0160
#define DBD_COMPONENT_TO_WORLD           0x01E0

#define DBD_GNAMES_BLOCKS_OFFSET         0x10

#define DBD_GEN_CHARGEABLE               0x04F0
#define DBD_GEN_IS_BLOCKED               0x0730
#define DBD_GEN_CHARGE_SECONDS           0x0670
#define DBD_CHARGEABLE_CHARGE            0x01B0
#define DBD_CHARGE_REPLICATED            0x0018

#define DBD_PALLET_STATE                 0x0538
#define DBD_TOTEM_STATE                  0x0438
#define DBD_HATCH_STATE                  0x0448
#define DBD_HOOK_SURVIVOR                0x05B0
#define DBD_HOOK_IS_BASEMENT             0x0509
#define DBD_CHEST_IS_OPENED              0x04A8
#define DBD_ESCAPE_ACTIVATED             0x0448

#define DBD_SURVIVOR_HEALTH_COMP         0x18A0
#define DBD_HEALTH_STATE_COUNT           0x0280
#define DBD_KILLER_CARRIED_SURVIVOR      0x1A68
#define DBD_CHARACTER_MESH               0x0360

#define DBD_SKEL_MESH_PTR                0x0608
#define DBD_BONE_INFO_ARRAY              0x02E8
#define DBD_COMPONENT_TO_WORLD_BONE      0x01E0

enum class EDbdPlayerRole : uint8_t {
    Role_None     = 0,
    Role_Slasher  = 1,
    Role_Camper   = 2,
    Role_Observer = 3,
    Role_Max      = 4
};

enum class EDbdActorType : uint8_t {
    Unknown  = 0,
    Survivor,
    Killer
};

enum class EDbdObjectType : uint8_t {
    Generator = 0,
    Totem,
    Pallet,
    Hook,
    Hatch,
    Locker,
    Chest,
    Window,
    Trap,
    EscapeDoor,
    BreakableDoor,
    OBJ_COUNT
};

inline const char* DbdObjectTypeName(EDbdObjectType t) {
    switch (t) {
        case EDbdObjectType::Generator:     return "Generator";
        case EDbdObjectType::Totem:         return "Totem";
        case EDbdObjectType::Pallet:        return "Pallet";
        case EDbdObjectType::Hook:          return "Hook";
        case EDbdObjectType::Hatch:         return "Hatch";
        case EDbdObjectType::Locker:        return "Locker";
        case EDbdObjectType::Chest:         return "Chest";
        case EDbdObjectType::Window:        return "Window";
        case EDbdObjectType::Trap:          return "Trap";
        case EDbdObjectType::EscapeDoor:    return "Exit Gate";
        case EDbdObjectType::BreakableDoor: return "Breakable";
        default: return "?";
    }
}

struct DbdUEVector {
    double X{}, Y{}, Z{};
};

struct DbdUERotator {
    double Pitch{}, Yaw{}, Roll{};
};

struct DbdMinimalViewInfo {
    DbdUEVector  Location{};
    DbdUERotator Rotation{};
    float        FOV{};
};

struct DbdCameraCacheEntry {
    float            Timestamp{};
    char             pad[0xC]{};
    DbdMinimalViewInfo POV{};
};

struct DbdTArray {
    uint64_t Data{};
    uint32_t Count{};
    uint32_t Max{};
};

struct DbdFTransform {
    double RotX{}, RotY{}, RotZ{}, RotW{};
    double PosX{}, PosY{}, PosZ{};
    double _pad0{};
    double ScaleX{}, ScaleY{}, ScaleZ{};
    double _pad1{};
};

#define DBD_MAX_BONES 128

enum EDbdBone {
    BONE_HEAD, BONE_NECK, BONE_TORSO, BONE_PELVIS,
    BONE_SHOULDER_L, BONE_ELBOW_L, BONE_HAND_L,
    BONE_SHOULDER_R, BONE_ELBOW_R, BONE_HAND_R,
    BONE_HIP_L, BONE_KNEE_L, BONE_FOOT_L,
    BONE_HIP_R, BONE_KNEE_R, BONE_FOOT_R,
    BONE_COUNT
};

inline const char* DbdBoneNames[BONE_COUNT] = {
    "joint_Head_01", "joint_NeckA_01", "joint_TorsoC_01", "joint_Pelvis_01",
    "joint_ShoulderLT_01", "joint_ElbowLT_01", "joint_HandLT_01",
    "joint_ShoulderRT_01", "joint_ElbowRT_01", "joint_HandRT_01",
    "joint_HipLT_01", "joint_KneeLT_01", "joint_FootLT_01",
    "joint_HipRT_01", "joint_KneeRT_01", "joint_FootRT_01",
};

struct DbdPlayerData {
    uint64_t       address{};
    std::string    name{};
    EDbdActorType  type{EDbdActorType::Unknown};
    EDbdPlayerRole role{EDbdPlayerRole::Role_None};
    DbdUEVector    position{};
    float          distance{};
    bool           valid{};
    bool           is_bot{};
    int32_t        health_states{-1};
    int32_t        level{-1};
    int32_t        prestige{-1};
    uint64_t       mesh_component{};
    std::vector<DbdUEVector> bone_positions;
    int            bone_map[BONE_COUNT];
    bool           bones_mapped{};
};

struct DbdObjectData {
    uint64_t        address{};
    EDbdObjectType  type{};
    DbdUEVector     position{};
    float           distance{};
    float           gen_progress{-1.0f};
    float           gen_max_charge{90.0f};
    bool            gen_blocked{};
    uint8_t         pallet_state{255};
    uint8_t         totem_state{255};
    uint8_t         hatch_state{255};
    bool            hook_occupied{};
    bool            hook_basement{};
    bool            chest_opened{};
    bool            escape_activated{};
};

inline bool DbdIsFiniteVec(const DbdUEVector& v) {
    return std::isfinite(v.X) && std::isfinite(v.Y) && std::isfinite(v.Z);
}

inline bool DbdIsLikelyPointer(uint64_t addr) {
    return addr != 0 &&
           ((addr >= 0x10000 && addr < 0x00007FFFFFFFFFFFULL) ||
            addr >= 0xFFFF800000000000ULL);
}

inline float DbdVectorDistance(const DbdUEVector& a, const DbdUEVector& b) {
    double dx = a.X - b.X, dy = a.Y - b.Y, dz = a.Z - b.Z;
    return static_cast<float>(std::sqrt(dx*dx + dy*dy + dz*dz));
}

#endif
