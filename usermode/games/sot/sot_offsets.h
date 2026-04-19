#ifndef SOT_OFFSETS_H
#define SOT_OFFSETS_H

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <initializer_list>
#include <string>

// ── Base offsets (Steam build) ───────────────────────────────────
#define SOT_GWORLD_OFFSET           0x9160A10
#define SOT_GNAMES_OFFSET           0x90847F8
#define SOT_GOBJECTS_OFFSET         0x9088B50

// ── UObject ──────────────────────────────────────────────────────
#define SOT_UOBJECT_CLASS           0x0008   // UObject -> ClassPrivate
#define SOT_UOBJECT_OUTER           0x0018   // UObject -> OuterPrivate
#define SOT_UOBJECT_NAME            0x0020   // UObject -> NamePrivate (FName)

// ── UWorld ───────────────────────────────────────────────────────
#define SOT_WORLD_GAMESTATE         0x0040   // UWorld -> GameState
#define SOT_WORLD_PERSISTENT_LEVEL  0x0060   // UWorld -> PersistentLevel
#define SOT_WORLD_LEVELS            0x01A0   // UWorld -> Levels
#define SOT_WORLD_OWNING_INSTANCE   0x0250   // UWorld -> OwningGameInstance

// ── UGameInstance ────────────────────────────────────────────────
#define SOT_GI_LOCAL_PLAYERS        0x0038   // UGameInstance -> LocalPlayers (TArray<ULocalPlayer*>)

// ── ULocalPlayer ─────────────────────────────────────────────────
#define SOT_LP_PLAYER_CONTROLLER    0x0030   // ULocalPlayer -> PlayerController

// ── APlayerController ────────────────────────────────────────────
#define SOT_PC_ACK_PAWN             0x0410   // APlayerController -> AcknowledgedPawn
#define SOT_PC_CAMERA_MANAGER       0x0430   // APlayerController -> PlayerCameraManager
#define SOT_PC_TARGET_VIEW_ROTATION 0x0444   // APlayerController -> TargetViewRotation

// ── AController ──────────────────────────────────────────────────
#define SOT_CONTROLLER_CONTROL_ROT  0x03D0   // AController -> ControlRotation

// ── APlayerCameraManager ─────────────────────────────────────────
#define SOT_CAM_CACHE               0x0410   // APlayerCameraManager -> CameraCache
#define SOT_CAM_LAST_FRAME_CACHE    0x09C0   // APlayerCameraManager -> LastFrameCameraCache
#define SOT_CAM_VIEWTARGET          0x0F70   // APlayerCameraManager -> ViewTarget
#define SOT_CAM_POV_OFFSET          0x0010   // FCameraCacheEntry / FTViewTarget -> POV

// ── AActor ───────────────────────────────────────────────────────
#define SOT_ACTOR_ROOT_COMPONENT    0x0160   // AActor -> RootComponent
#define SOT_ACTOR_INSTIGATOR        0x0148   // AActor -> Instigator
#define SOT_ACTOR_CHILDREN          0x0150   // AActor -> Children
#define SOT_ACTOR_OWNER             0x0088   // AActor -> Owner
#define SOT_ACTOR_REPLICATED_MOVEMENT 0x0090 // AActor -> ReplicatedMovement
#define SOT_ACTOR_ATTACHMENT_REPLICATION 0x00C8 // AActor -> AttachmentReplication

// ── FRepMovement / FRepAttachment ────────────────────────────────
#define SOT_REPMOV_LOCATION         0x0018   // FRepMovement -> Location
#define SOT_REPATT_PARENT           0x0000   // FRepAttachment -> AttachParent
#define SOT_REPATT_LOC_OFFSET       0x0008   // FRepAttachment -> LocationOffset

// ── APawn ────────────────────────────────────────────────────────
#define SOT_PAWN_PLAYERSTATE        0x03C0   // APawn -> PlayerState
#define SOT_PAWN_CONTROLLER         0x03D8   // APawn -> Controller

// ── ACharacter ───────────────────────────────────────────────────
#define SOT_CHAR_MESH               0x0418   // ACharacter -> Mesh

// ── APlayerState ─────────────────────────────────────────────────
#define SOT_PS_PLAYER_NAME          0x03A8   // APlayerState -> PlayerName (FString)

// ── USceneComponent ──────────────────────────────────────────────
#define SOT_SCENE_RELATIVE_LOC      0x00F8   // USceneComponent -> RelativeLocation (FVector: 3 floats)
#define SOT_SCENE_RELATIVE_ROT      0x0104   // USceneComponent -> RelativeRotation (FRotator: 3 floats)
#define SOT_SCENE_COMPONENT_TO_WORLD 0x0120  // Derived from current SDK layout: aligned FTransform after RelativeScale3D
#define SOT_TRANSFORM_TRANSLATION   0x0010   // FTransform -> Translation

// ── ULevel ───────────────────────────────────────────────────────
#define SOT_LEVEL_ACTOR_CLUSTER     0x00C8   // ULevel -> ActorCluster
#define SOT_LEVEL_ACTORS            0x00A0   // ULevel -> Actors array, consistent with working Code layout and current ULevel size/field placement

// ── AthenaPlayerCharacter specific ───────────────────────────────
#define SOT_APC_HEALTH_COMPONENT    0x0888   // Approximation
#define SOT_APC_WIELD_COMPONENT     0x0890   // Approximation

// ── TrackedActorType fields from SDK ─────────────────────────────
#define SOT_AI_TRACKED_ACTOR_TYPE      0x0D30   // AAthenaAICharacter -> TrackedActorType
#define SOT_STORAGE_TRACKED_ACTOR_TYPE 0x0464   // AStorageContainer -> TrackedActorType
#define SOT_SHIPWRECK_TRACKED_ACTOR_TYPE 0x0408 // AShipwreck -> TrackedActorType
#define SOT_GHOSTSHIP_TRACKED_ACTOR_TYPE 0x0590 // AAggressiveGhostShip -> TrackedActorType
#define SOT_ROWBOAT_TRACKED_ACTOR_TYPE 0x08B8   // ARowboat -> TrackedActorType

// ── GNames ───────────────────────────────────────────────────────
#define SOT_GNAMES_BLOCKS_OFFSET    0x10     // FNamePool -> Blocks array offset

// ── Camera cache entry layout ────────────────────────────────────
// FMinimalViewInfo: Location(FVector 12b), Rotation(FRotator 12b), FOV(float 4b)
// CameraCache: float Timestamp(4b) + pad(12b) + FMinimalViewInfo

// ── FString layout (in memory) ───────────────────────────────────
// FString = TArray<wchar_t>: Data*(8b) + Count(4b) + Max(4b)

// ═══════════════════════════════════════════════════════════════════
//  Struct / Enum definitions
// ═══════════════════════════════════════════════════════════════════

struct SotVector {
    float X{}, Y{}, Z{};
};

struct SotRotator {
    float Pitch{}, Yaw{}, Roll{};
};

struct SotMinimalViewInfo {
    SotVector  Location{};
    SotRotator Rotation{};
    float      FOV{};
};

struct SotTArray {
    uint64_t Data{};
    uint32_t Count{};
    uint32_t Max{};
};

enum class ESotActorType : uint8_t {
    Unknown = 0,
    Player,
    Skeleton,
    Ship,
    Barrel,
    Chest,
    Fort,
    Outpost,
    Animal,
    Mermaid,
    Cannon,
    Rowboat,
    Storm,
    WorldEvent,
    Shipwreck,
    Seagulls
};

inline const char* SotActorTypeName(ESotActorType t) {
    switch (t) {
        case ESotActorType::Player:     return "Player";
        case ESotActorType::Skeleton:   return "Skeleton";
        case ESotActorType::Ship:       return "Ship";
        case ESotActorType::Barrel:     return "Barrel";
        case ESotActorType::Chest:      return "Chest";
        case ESotActorType::Fort:       return "Fort";
        case ESotActorType::Outpost:    return "Outpost";
        case ESotActorType::Animal:     return "Animal";
        case ESotActorType::Mermaid:    return "Mermaid";
        case ESotActorType::Cannon:     return "Cannon";
        case ESotActorType::Rowboat:    return "Rowboat";
        case ESotActorType::Storm:      return "Storm";
        case ESotActorType::WorldEvent: return "Event";
        case ESotActorType::Shipwreck:  return "Shipwreck";
        case ESotActorType::Seagulls:   return "Seagulls";
        default: return "?";
    }
}

enum class ESotShipType : uint8_t {
    Unknown = 0,
    Sloop,
    Brigantine,
    Galleon
};

inline const char* SotShipTypeName(ESotShipType t) {
    switch (t) {
        case ESotShipType::Sloop:       return "Sloop";
        case ESotShipType::Brigantine:  return "Brig";
        case ESotShipType::Galleon:     return "Galleon";
        default: return "Ship";
    }
}

struct SotPlayerData {
    uint64_t      address{};
    uint64_t      playerstate{};
    char          name[64]{};
    ESotActorType type{ESotActorType::Player};
    SotVector     position{};
    float         distance{};
    bool          valid{};
    bool          is_local{};
    char          actor_class[64]{};
};

struct SotShipData {
    uint64_t      address{};
    ESotShipType  ship_type{ESotShipType::Unknown};
    SotVector     position{};
    float         distance{};
    bool          valid{};
    char          actor_class[64]{};
};

struct SotObjectData {
    uint64_t      address{};
    ESotActorType type{ESotActorType::Unknown};
    SotVector     position{};
    float         distance{};
    char          class_name[64]{};
};

struct SotDebugInfo {
    uint64_t gworld{};
    uint64_t persistent_level{};
    uint64_t game_state{};
    uint64_t local_pawn{};
    uint64_t camera_manager{};
    bool     gnames_ok{};
    char     camera_source[32]{};
    uint32_t actor_scan_count{};
    uint32_t player_count{};
    uint32_t ship_count{};
    uint32_t object_count{};
    uint32_t unknown_actor_count{};
    char     unknown_actors[32][64]{};
};

struct SotWorldState {
    bool valid{};
    std::string error{};
    uint64_t base_address{};

    bool has_camera{};
    SotMinimalViewInfo camera{};

    int player_count{};
    std::vector<SotPlayerData> players;
    std::vector<SotShipData>   ships;
    std::vector<SotObjectData> objects;

    SotDebugInfo debug{};
};

struct SotEspSettings {
    float max_distance = 2000.0f;

    bool show_players   = true;
    bool show_ships     = true;
    bool show_skeletons = false;
    bool show_barrels   = false;
    bool show_chests    = true;
    bool show_events    = true;
    bool show_mermaids  = true;
    bool show_animals   = false;
    bool show_cannons   = false;
    bool show_rowboats  = false;
    bool show_shipwrecks = true;
    bool show_seagulls  = true;

    std::string to_json() const {
        char buf[512];
        snprintf(buf, sizeof(buf),
            "{\"max_dist\":%.0f,\"players\":%d,\"ships\":%d,\"skeletons\":%d,"
            "\"barrels\":%d,\"chests\":%d,\"events\":%d,\"mermaids\":%d,"
            "\"animals\":%d,\"cannons\":%d,\"rowboats\":%d,\"shipwrecks\":%d,\"seagulls\":%d}",
            max_distance, show_players, show_ships, show_skeletons,
            show_barrels, show_chests, show_events, show_mermaids,
            show_animals, show_cannons, show_rowboats, show_shipwrecks, show_seagulls);
        return buf;
    }

    void from_json(const std::string& json) {
        auto read_float = [&](const char* key, float& val) {
            auto pos = json.find(key);
            if (pos != std::string::npos) {
                pos = json.find(':', pos);
                if (pos != std::string::npos) val = strtof(json.c_str() + pos + 1, nullptr);
            }
        };
        auto read_bool = [&](const char* key, bool& val) {
            auto pos = json.find(key);
            if (pos != std::string::npos) {
                pos = json.find(':', pos);
                if (pos != std::string::npos) {
                    while (pos < json.size() && (json[pos] == ':' || json[pos] == ' ')) pos++;
                    val = (json[pos] == '1' || json[pos] == 't');
                }
            }
        };
        read_float("max_dist", max_distance);
        read_bool("players",    show_players);
        read_bool("ships",      show_ships);
        read_bool("skeletons",  show_skeletons);
        read_bool("barrels",    show_barrels);
        read_bool("chests",     show_chests);
        read_bool("events",     show_events);
        read_bool("mermaids",   show_mermaids);
        read_bool("animals",    show_animals);
        read_bool("cannons",    show_cannons);
        read_bool("rowboats",   show_rowboats);
        read_bool("shipwrecks", show_shipwrecks);
        read_bool("seagulls",   show_seagulls);
    }
};

// ═══════════════════════════════════════════════════════════════════
//  Helpers
// ═══════════════════════════════════════════════════════════════════

inline bool SotIsFiniteVec(const SotVector& v) {
    return std::isfinite(v.X) && std::isfinite(v.Y) && std::isfinite(v.Z);
}

inline bool SotIsLikelyPointer(uint64_t addr) {
    return addr != 0 &&
           ((addr >= 0x10000 && addr < 0x00007FFFFFFFFFFFULL) ||
            addr >= 0xFFFF800000000000ULL);
}

inline float SotVectorDistance(const SotVector& a, const SotVector& b) {
    float dx = a.X - b.X, dy = a.Y - b.Y, dz = a.Z - b.Z;
    return std::sqrt(dx*dx + dy*dy + dz*dz);
}

inline bool SotClassContainsAny(const std::string& cls,
                                std::initializer_list<const char*> needles) {
    for (const char* needle : needles) {
        if (needle && cls.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

inline bool SotClassContainsNone(const std::string& cls,
                                 std::initializer_list<const char*> needles) {
    return !SotClassContainsAny(cls, needles);
}

inline bool SotClassEqualsAny(const std::string& cls,
                              std::initializer_list<const char*> exact_names) {
    for (const char* exact_name : exact_names) {
        if (exact_name && cls == exact_name)
            return true;
    }
    return false;
}

inline bool SotIsProbablyBrokenClassName(const std::string& cls) {
    if (cls.empty())
        return true;

    if (cls.size() < 5 && !SotClassEqualsAny(cls, {"Ship", "Storm"}))
        return true;

    if (SotClassContainsAny(cls, {
            "Widget",
            "WidgetText",
            "TextRender",
            "TextBlock",
            "UserWidget",
            "MapWidget",
            "FrontEnd",
            "Frontend",
            "Menu",
            "Prompt",
            "Tooltip",
            "Toast",
            "Banner",
            "Subtitle",
            "Button",
            "Canvas",
            "Landscape",
            "Material",
            "Light",
            "Sky",
            "Fog",
            "Cloud",
            "PostProcess",
            "Audio",
            "Sound",
            "Niagara",
            "Particle",
            "Emitter",
            "Spline",
            "Decal",
            "Billboard",
            "Brush",
            "Volume",
            "TargetPoint",
            "LevelSequence",
            "Sequencer",
            "AnimBP",
            "AnimBlueprint",
            "BlueprintGeneratedClass",
            "DataAsset",
            "Settings",
            "Definition",
            "Controller",
            "Manager",
            "Service",
        }))
        return true;

    return false;
}

inline bool SotCanUseTrackedActorType(const std::string& cls) {
    if (SotIsProbablyBrokenClassName(cls))
        return false;

    return SotClassContainsAny(cls, {
        "AthenaAICharacter",
        "AICharacter",
        "GoalDrivenCharacter",
        "OceanCrawlerAICharacter",
        "SirenPawn",
        "Phantom",
        "StorageContainer",
        "BuoyantStorageContainer",
        "StorageCrateItemProxy",
        "AmmoChest",
        "WaterBarrel",
        "Jettisoned",
        "Shipwreck",
        "AggressiveGhostShip",
        "AIShip",
        "Rowboat",
    });
}

enum : uint64_t {
    SOT_TRACKED_AI_GHOSTSHIP_GRUNT  = 4,
    SOT_TRACKED_AI_MEGALODON        = 8,
    SOT_TRACKED_AI_OCEAN_CRAWLER    = 12,
    SOT_TRACKED_AI_SIREN            = 16,
    SOT_TRACKED_AI_SHIP_BATTLE      = 20,
    SOT_TRACKED_AI_SHIP_PASSIVE     = 24,
    SOT_TRACKED_ASHEN_LORD_CLOUD    = 28,
    SOT_TRACKED_BOOTY_ASHEN_SKULL   = 32,
    SOT_TRACKED_BUOYANT_ACTOR       = 36,
    SOT_TRACKED_DEPLOYABLE_CANNON   = 40,
    SOT_TRACKED_FISHING_FISH        = 44,
    SOT_TRACKED_GOAL_DRIVEN_CHAR    = 48,
    SOT_TRACKED_JETTISONED_SUPPLIES = 52,
    SOT_TRACKED_MECHANISM_PROXY     = 56,
    SOT_TRACKED_MESSAGE_IN_A_BOTTLE = 60,
    SOT_TRACKED_POUCH_DOUBLOONS     = 64,
    SOT_TRACKED_ROWBOAT_CANNON      = 68,
    SOT_TRACKED_SHIP_SMALL          = 72,
    SOT_TRACKED_SHIPWRECK_SMUGGLER  = 76,
    SOT_TRACKED_SPIRE               = 80,
    SOT_TRACKED_STORM               = 84,
    SOT_TRACKED_WRECK_DEBRIS        = 88,
};

inline ESotActorType SotClassifyTrackedActorType(uint64_t tracked_type) {
    switch (tracked_type) {
        case SOT_TRACKED_AI_GHOSTSHIP_GRUNT:
        case SOT_TRACKED_AI_SHIP_BATTLE:
        case SOT_TRACKED_AI_SHIP_PASSIVE:
        case SOT_TRACKED_SHIP_SMALL:
            return ESotActorType::Ship;

        case SOT_TRACKED_AI_MEGALODON:
        case SOT_TRACKED_ASHEN_LORD_CLOUD:
            return ESotActorType::WorldEvent;

        case SOT_TRACKED_AI_OCEAN_CRAWLER:
        case SOT_TRACKED_GOAL_DRIVEN_CHAR:
            return ESotActorType::Skeleton;

        case SOT_TRACKED_AI_SIREN:
            return ESotActorType::Mermaid;

        case SOT_TRACKED_BOOTY_ASHEN_SKULL:
        case SOT_TRACKED_MESSAGE_IN_A_BOTTLE:
        case SOT_TRACKED_POUCH_DOUBLOONS:
            return ESotActorType::Chest;

        case SOT_TRACKED_BUOYANT_ACTOR:
        case SOT_TRACKED_JETTISONED_SUPPLIES:
            return ESotActorType::Barrel;

        case SOT_TRACKED_DEPLOYABLE_CANNON:
            return ESotActorType::Cannon;

        case SOT_TRACKED_FISHING_FISH:
            return ESotActorType::Animal;

        case SOT_TRACKED_ROWBOAT_CANNON:
            return ESotActorType::Rowboat;

        case SOT_TRACKED_SHIPWRECK_SMUGGLER:
        case SOT_TRACKED_WRECK_DEBRIS:
            return ESotActorType::Shipwreck;

        case SOT_TRACKED_SPIRE:
            return ESotActorType::Fort;

        case SOT_TRACKED_STORM:
            return ESotActorType::Storm;

        default:
            return ESotActorType::Unknown;
    }
}

inline const char* SotTrackedActorTypeName(uint64_t tracked_type) {
    switch (tracked_type) {
        case SOT_TRACKED_AI_GHOSTSHIP_GRUNT:  return "Tracked: AI_GhostShip_Grunt";
        case SOT_TRACKED_AI_MEGALODON:        return "Tracked: AI_Megalodon";
        case SOT_TRACKED_AI_OCEAN_CRAWLER:    return "Tracked: AI_OceanCrawler";
        case SOT_TRACKED_AI_SIREN:            return "Tracked: AI_Siren";
        case SOT_TRACKED_AI_SHIP_BATTLE:      return "Tracked: AI_Ship_Battle";
        case SOT_TRACKED_AI_SHIP_PASSIVE:     return "Tracked: AI_Ship_Passive";
        case SOT_TRACKED_ASHEN_LORD_CLOUD:    return "Tracked: AshenLordCloud";
        case SOT_TRACKED_BOOTY_ASHEN_SKULL:   return "Tracked: Booty_AshenWindsSkull";
        case SOT_TRACKED_BUOYANT_ACTOR:       return "Tracked: BuoyantActor";
        case SOT_TRACKED_DEPLOYABLE_CANNON:   return "Tracked: Deployable_Cannon";
        case SOT_TRACKED_FISHING_FISH:        return "Tracked: FishingFish";
        case SOT_TRACKED_GOAL_DRIVEN_CHAR:    return "Tracked: GoalDrivenCharacter";
        case SOT_TRACKED_JETTISONED_SUPPLIES: return "Tracked: JettisonedSupplies";
        case SOT_TRACKED_MECHANISM_PROXY:     return "Tracked: MechanismProxy";
        case SOT_TRACKED_MESSAGE_IN_A_BOTTLE: return "Tracked: MessageInABottle";
        case SOT_TRACKED_POUCH_DOUBLOONS:     return "Tracked: PouchDoubloons";
        case SOT_TRACKED_ROWBOAT_CANNON:      return "Tracked: Rowboat_Cannon";
        case SOT_TRACKED_SHIP_SMALL:          return "Tracked: Ship_Small";
        case SOT_TRACKED_SHIPWRECK_SMUGGLER:  return "Tracked: Shipwreck_Smuggler";
        case SOT_TRACKED_SPIRE:               return "Tracked: Spire";
        case SOT_TRACKED_STORM:               return "Tracked: Storm";
        case SOT_TRACKED_WRECK_DEBRIS:        return "Tracked: WreckDebris";
        default:                              return "Tracked: Unknown";
    }
}

// ═══════════════════════════════════════════════════════════════════
//  Actor classification by class name
// ═══════════════════════════════════════════════════════════════════

inline ESotActorType SotClassifyActor(const std::string& cls) {
    if (SotIsProbablyBrokenClassName(cls))
        return ESotActorType::Unknown;

    // Players
    if (SotClassContainsAny(cls, {
            "AthenaPlayerCharacter",
            "AthenaGhostPlayerCharacter",
        }) &&
        SotClassContainsNone(cls, {
            "Mock",
            "UnitTest",
            "Nameplate",
        }))
        return ESotActorType::Player;

    // Forts and fort markers
    if (SotClassContainsAny(cls, {
            "FortOfTheDamned",
            "SkeletonFort",
            "SkullCloud",
            "Spire",
        }))
        return ESotActorType::Fort;

    // Seagulls
    if (SotClassContainsAny(cls, {
            "ASeagulls",
            "Seagulls",
            "Seagull",
        }))
        return ESotActorType::Seagulls;

    // Rowboats
    if (SotClassContainsAny(cls, {
            "ARowboat",
            "Rowboat",
        }))
        return ESotActorType::Rowboat;

    // Cannons
    if (SotClassContainsAny(cls, {
            "ACannon",
            "Cannon",
        }) &&
        SotClassContainsNone(cls, {
            "CannonBall",
            "CannonProjectile",
            "Deployable_Cannon_Item",
            "UseCannon",
            "CannonInput",
            "CannonAnim",
        }))
        return ESotActorType::Cannon;

    // Storm
    if (SotClassContainsAny(cls, {
            "AStorm",
            "Storm",
        }) &&
        SotClassContainsNone(cls, {
            "StormService",
            "StormEffectsExclusion",
        }))
        return ESotActorType::Storm;

    // Shipwrecks
    if (SotClassContainsAny(cls, {
            "AShipwreck",
            "Shipwreck",
            "WreckDebris",
        }) &&
        SotClassContainsNone(cls, {
            "ShipwreckService",
            "ShipwreckHullAudio",
            "ShipwreckSiteGeneratorSimulator",
        }))
        return ESotActorType::Shipwreck;

    // Ships
    if (SotClassEqualsAny(cls, {
            "Ship",
            "AShip",
        }) ||
        SotClassContainsAny(cls, {
            "ShipNetProxy",
            "SmallShip",
            "MediumShip",
            "LargeShip",
            "Sloop",
            "Brigantine",
            "Galleon",
            "AIShip",
            "AggressiveGhostShip",
        }))
        return ESotActorType::Ship;

    // Mermaids / Sirens
    if (SotClassContainsAny(cls, {
            "Mermaid",
            "MermaidInteractionProxy",
            "SirenPawn",
        }) &&
        SotClassContainsNone(cls, {
            "MermaidService",
        }))
        return ESotActorType::Mermaid;

    // Barrels / supply
    if (SotClassContainsAny(cls, {
            "StorageContainer",
            "BuoyantStorageContainer",
            "StorageCrateItemProxy",
            "AmmoChest",
            "WaterBarrel",
            "TavernStrangersBarrel",
            "Jettisoned_Supplies",
        }))
        return ESotActorType::Barrel;

    // Chests / treasure
    if (SotClassContainsAny(cls, {
            "TreasureChest",
            "CollectorsChest",
            "MerchantCrate",
            "BountyReward",
            "StrongholdKey",
            "AshenWindsSkull",
            "ReapersChest",
            "Artifact",
            "BookOfSecrets",
            "MessageInABottle",
            "Pouch_Doubloons",
        }))
        return ESotActorType::Chest;

    // Skeletons / hostile humanoid AI
    if (SotClassContainsAny(cls, {
            "Skeleton",
            "AthenaAICharacter",
            "OceanCrawlerAICharacter",
            "Phantom",
        }) &&
        SotClassContainsNone(cls, {
            "SkeletonFortDoor",
            "SkeletonThrone",
            "SkeletonCamp",
            "ActionStateCreatorDefinition",
            "AIController",
        }))
        return ESotActorType::Skeleton;

    // Animals
    if (SotClassContainsAny(cls, {
            "Chicken",
            "Pig",
            "Snake",
            "SharkPawn",
            "TinyShark",
            "FishingFish",
        }) &&
        SotClassContainsNone(cls, {
            "Service",
            "Experience",
            "Event",
        }))
        return ESotActorType::Animal;

    // World events
    if (SotClassContainsAny(cls, {
            "Kraken",
            "Megalodon",
            "AshenLord",
            "Volcano",
            "BurningBlade",
        }) &&
        SotClassContainsNone(cls, {
            "Service",
        }))
        return ESotActorType::WorldEvent;

    return ESotActorType::Unknown;
}

inline ESotShipType SotClassifyShip(const std::string& cls) {
    if (cls.find("SmallShip") != std::string::npos ||
        cls.find("Sloop") != std::string::npos)
        return ESotShipType::Sloop;
    if (cls.find("MediumShip") != std::string::npos ||
        cls.find("Brigantine") != std::string::npos ||
        cls.find("Brig") != std::string::npos)
        return ESotShipType::Brigantine;
    if (cls.find("LargeShip") != std::string::npos ||
        cls.find("Galleon") != std::string::npos)
        return ESotShipType::Galleon;
    return ESotShipType::Unknown;
}

#endif
