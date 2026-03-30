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
#define DBD_PLAYER_DATA_PERK_IDS         0x0010
#define DBD_PLAYER_DATA_PERK_LEVELS      0x0020
#define DBD_PLAYER_DATA_PRESTIGE         0x007C

#define DBD_SELECTED_SURVIVOR_INDEX      0x05E8
#define DBD_SELECTED_KILLER_INDEX        0x05EC

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

#define DBD_ACTOR_COMPONENTS             0x02C0

#define DBD_AURA_COLOR_R                 0x364
#define DBD_AURA_COLOR_G                 0x368
#define DBD_AURA_COLOR_B                 0x36C
#define DBD_AURA_COLOR_A                 0x370

#define DBD_MAX_PERKS 4

inline const char* DbdSurvivorNames[] = {
    "Dwight", "Meg", "Claudette", "Jake",
    "Nea", "Laurie", "Ace", "Bill",
    "Feng", "David", "Quentin", "Tapp",
    "Kate", "Adam", "Jeff", "Jane",
    "Ash", "Nancy", "Steve", "Yui",
    "Zarina", "Cheryl", "Felix", "Elodie",
    "Yun-Jin", "Jill", "Leon", "Mikaela",
    "Jonah", "Yoichi", "Haddie", "Ada",
    "Rebecca", "Vittorio", "Thalita", "Renato",
    "Gabriel", "Nicolas", "Ellen", "Alan",
    "Sable", "Aestri", "Lara", "Trevor",
    "Taurie",
};
constexpr int DBD_SURVIVOR_NAME_COUNT = sizeof(DbdSurvivorNames) / sizeof(DbdSurvivorNames[0]);

inline const char* DbdKillerNames[] = {
    "Trapper", "Wraith", "Hillbilly", "Nurse",
    "Shape", "Hag", "Doctor", "Huntress",
    "Cannibal", "Nightmare", "Pig", "Clown",
    "Spirit", "Legion", "Plague", "Ghost Face",
    "Demogorgon", "Oni", "Deathslinger", "Executioner",
    "Blight", "Twins", "Trickster", "Nemesis",
    "Cenobite", "Artist", "Onryo", "Dredge",
    "Mastermind", "Knight", "Skull Merchant", "Singularity",
    "Xenomorph", "Good Guy", "Unknown", "Lich",
    "Dark Lord", "Houndmaster", "Trooper", "Valkyrie",
};
constexpr int DBD_KILLER_NAME_COUNT = sizeof(DbdKillerNames) / sizeof(DbdKillerNames[0]);

inline const char* DbdMapClassToCharacter(const std::string& cls) {
    struct ClassMap { const char* pat; const char* name; };
    static const ClassMap surv_map[] = {
        {"CamperMale01", "Dwight"}, {"CamperFemale01", "Meg"},
        {"CamperFemale03", "Claudette"}, {"CamperMale02", "Jake"},
        {"CamperFemale04", "Nea"}, {"CamperFemale05", "Laurie"},
        {"CamperMale03", "Ace"}, {"CamperMale04", "Bill"},
        {"CamperFemale06", "Feng"}, {"CamperMale05", "David"},
        {"CamperMale06", "Quentin"}, {"CamperMale07", "Tapp"},
        {"CamperFemale07", "Kate"}, {"CamperMale08", "Adam"},
        {"CamperMale09", "Jeff"}, {"CamperFemale08", "Jane"},
        {"CamperMale10", "Ash"}, {"CamperFemale09", "Nancy"},
        {"CamperMale11", "Steve"}, {"CamperFemale10", "Yui"},
        {"CamperFemale11", "Zarina"}, {"CamperFemale12", "Cheryl"},
        {"CamperMale12", "Felix"}, {"CamperFemale13", "Elodie"},
        {"CamperFemale14", "Yun-Jin"}, {"CamperFemale15", "Jill"},
        {"CamperMale13", "Leon"}, {"CamperFemale16", "Mikaela"},
        {"CamperMale14", "Jonah"}, {"CamperMale15", "Yoichi"},
        {"CamperFemale17", "Haddie"}, {"CamperFemale18", "Ada"},
        {"CamperFemale19", "Rebecca"}, {"CamperMale16", "Vittorio"},
        {"CamperFemale20", "Thalita"}, {"CamperMale17", "Renato"},
        {"CamperMale18", "Gabriel"}, {"CamperMale19", "Nicolas"},
        {"CamperFemale21", "Ellen"}, {"CamperMale20", "Alan"},
        {"CamperFemale22", "Sable"}, {"CamperFemale23", "Aestri"},
        {"CamperFemale24", "Lara"}, {"CamperMale21", "Trevor"},
        {"CamperFemale25", "Taurie"},
    };
    for (auto& e : surv_map)
        if (cls.find(e.pat) != std::string::npos) return e.name;

    if (cls.find("Slasher") != std::string::npos) {
        for (int i = DBD_KILLER_NAME_COUNT - 1; i >= 0; i--) {
            char pat[8];
            snprintf(pat, sizeof(pat), "_%02d", i + 1);
            auto pos = cls.find(pat);
            if (pos != std::string::npos) {
                char next = (pos + strlen(pat) < cls.size()) ? cls[pos + strlen(pat)] : 0;
                if (next == '_' || next == 0 || next == '.')
                    return DbdKillerNames[i];
            }
        }
    }
    return nullptr;
}

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
    char           name[64]{};
    char           character_name[32]{};
    EDbdActorType  type{EDbdActorType::Unknown};
    EDbdPlayerRole role{EDbdPlayerRole::Role_None};
    DbdUEVector    position{};
    float          distance{};
    bool           valid{};
    bool           is_local{};
    bool           is_bot{};
    int32_t        health_states{-1};
    int32_t        level{-1};
    int32_t        prestige{-1};
    int32_t        character_index{-1};
    int32_t        perk_ids[DBD_MAX_PERKS]{};
    int32_t        perk_levels[DBD_MAX_PERKS]{};
    char           perk_names[DBD_MAX_PERKS][48]{};
    bool           perks_valid{};
    uint64_t       mesh_component{};
    DbdUEVector    bone_positions[DBD_MAX_BONES];
    uint32_t       bone_count{};
    int            bone_map[BONE_COUNT];
    bool           bones_mapped{};
    uint64_t       aura_component{};
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
    uint64_t        aura_component{};
};

struct DbdAuraColor {
    float r{}, g{}, b{}, a{1.0f};
};

struct DbdAuraConfig {
    bool enabled = false;
    bool survivor_aura = true;
    bool killer_aura = true;
    DbdAuraColor survivor_color{0.0f, 1.0f, 0.0f, 0.5f};
    DbdAuraColor killer_color{1.0f, 0.0f, 0.0f, 0.75f};
    bool obj_aura[static_cast<int>(EDbdObjectType::OBJ_COUNT)] = {
        true, true, true, true, true, false, true, true, true, true, false
    };
    DbdAuraColor obj_color[static_cast<int>(EDbdObjectType::OBJ_COUNT)] = {
        {0.13f, 0.83f, 0.69f, 0.5f},
        {0.09f, 0.12f, 1.0f, 0.25f},
        {0.86f, 0.86f, 0.0f, 0.35f},
        {0.31f, 0.50f, 0.88f, 0.5f},
        {0.58f, 0.0f, 0.83f, 0.5f},
        {0.5f, 0.5f, 0.5f, 0.5f},
        {0.85f, 0.65f, 0.13f, 0.5f},
        {0.95f, 0.50f, 0.0f, 0.35f},
        {0.86f, 0.08f, 0.24f, 0.5f},
        {0.2f, 0.8f, 0.2f, 0.5f},
        {0.82f, 0.41f, 0.12f, 0.5f},
    };
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
