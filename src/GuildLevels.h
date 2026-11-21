/*
 * mod-guild-levels: Cataclysm-style guild levels for WotLK 3.3.5a.
 *
 * A guild earns guild XP from its members' quests and from dungeon and raid bosses killed in a
 * guild group, and climbs from level 1 to 25. Every level from 2 to 25 unlocks one perk. The perks
 * live in GuildLevelsPerks.cpp, the two castable ones in GuildLevelsSpells.cpp, guild XP and
 * storage in GuildLevelsMgr.cpp.
 *
 * Released under the MIT License.
 */

#ifndef MOD_GUILD_LEVELS_H
#define MOD_GUILD_LEVELS_H

#include "Define.h"
#include "DatabaseEnvFwd.h"
#include "ObjectGuid.h"
#include <array>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

class Player;
class WorldSession;

namespace GuildLevels
{
    constexpr uint8 MAX_LEVEL = 25;
    constexpr char const* PREFIX = "GuildLevels";

    // The guild level each perk unlocks at. A perk with two ranks has a level for each.
    enum PerkLevel : uint8
    {
        PERK_GOOD_STANDING_1        = 2,
        PERK_HAGGLER_1              = 3,
        PERK_BATTLEFIELD_RENOWN_1   = 4,
        PERK_SWIFT_STEEDS_1         = 5,
        PERK_LOYAL_FOLLOWING_1      = 6,
        PERK_GUILD_TITHE_1          = 7,
        PERK_FIELD_MEDICINE_1       = 8,
        PERK_REINFORCED_GEAR_1      = 9,
        PERK_HAGGLER_2              = 10,
        PERK_GUILD_VAULT            = 11,
        PERK_GOOD_STANDING_2        = 12,
        PERK_BOUNTIFUL_HARVEST_1    = 13,
        PERK_BATTLEFIELD_RENOWN_2   = 14,
        PERK_SECOND_WIND            = 15,
        PERK_GUILD_TITHE_2          = 16,
        PERK_REINFORCED_GEAR_2      = 17,
        PERK_LOYAL_FOLLOWING_2      = 18,
        PERK_LASTING_PROVISIONS_1   = 19,
        PERK_ALCHEMISTS_BOUNTY      = 20,
        PERK_FIELD_MEDICINE_2       = 21,
        PERK_BOUNTIFUL_HARVEST_2    = 22,
        PERK_LASTING_PROVISIONS_2   = 23,
        PERK_SWIFT_STEEDS_2         = 24,
        PERK_MASS_RESURRECTION      = 25,
    };

    // Spells. 90450/90451 exist only on the server (spell_dbc); the two castable ones are in the
    // client's Spell.dbc too (tools/build_patch.py).
    enum Spells : uint32
    {
        SPELL_SWIFT_STEEDS      = 90450,    // hidden passive: mounted and flying speed
        SPELL_SECOND_WIND       = 90451,    // hidden: ghost run speed
        SPELL_GUILD_VAULT       = 90452,
        SPELL_MASS_RESURRECTION = 90454,
    };

    constexpr uint32 GO_GUILD_CHEST = 9503400;  // Guild Vault's guild bank, gameobject type 34

    // The stock Guild Tabard. Wearing it earns GuildLevels.XP.TabardKill.
    constexpr uint32 ITEM_GUILD_TABARD = 5976;

    struct Config
    {
        bool enabled = true;
        bool announce = true;

        // Guild XP
        float curveScale = 0.0125f;     // Cataclysm's XP per level times this
        float questShare = 0.25f;       // share of a quest's XP that goes to the guild
        uint32 bossXPDungeon = 2000;
        uint32 bossXPRaid = 6000;
        float heroicBonus = 1.5f;
        uint32 dailyCap = 150000;       // 0 = no cap
        uint8 dailyCapUntilLevel = 20;  // the cap stops applying once the guild reaches this level
        uint32 raidGuildMembers = 10;   // raid kills: guild members needed (at most 80% of the raid)
        bool botGuildsEarn = false;     // guilds without a real player never earn XP when false
        uint32 tabardKillXP = 10;       // flat guild XP per kill in a guild tabard, 0 = off

        // Perks (percent unless noted); rank 1 / rank 2
        uint32 goodStanding1 = 5, goodStanding2 = 10;
        uint32 haggler1 = 5, haggler2 = 10;
        uint32 battlefieldRenown1 = 5, battlefieldRenown2 = 10;
        uint32 swiftSteeds1 = 10, swiftSteeds2 = 20;
        uint32 loyalFollowing1 = 10, loyalFollowing2 = 20;
        uint32 guildTithe1 = 5, guildTithe2 = 10;
        uint32 fieldMedicine1 = 25, fieldMedicine2 = 50;
        uint32 reinforcedGear1 = 10, reinforcedGear2 = 20;
        uint32 bountifulHarvest1 = 10, bountifulHarvest2 = 20;
        uint32 secondWindSpeed = 100, secondWindRes = 50;
        uint32 lastingProvisions1 = 50, lastingProvisions2 = 100;
        uint32 alchemistsBountyChance = 50;
        uint32 massResHealth = 35;
        uint32 massResMinutes = 10;     // a player can be raised by Mass Resurrection once per this
    };

    Config const& GetConfig();
    void LoadConfig();

    // --- guild state (GuildLevelsMgr.cpp). Safe from any thread. ---

    struct GuildState
    {
        uint8 level = 1;
        uint64 xp = 0;          // into the current level
        uint64 totalXp = 0;
        uint32 day = 0;
        uint64 dayXp = 0;
        uint32 cashWeek = 0;    // the week cashWeekMoney is for
        uint64 cashWeekMoney = 0;   // Guild Tithe into the guild bank, copper
        uint64 cashTotal = 0;
    };

    // XP from level to level + 1 (0 at the cap).
    uint64 XPForLevel(uint8 level);
    uint64 DailyCap(uint8 level);

    uint8 GetLevel(uint32 guildId);
    GuildState GetState(uint32 guildId);
    bool HasPerk(Player const* player, uint8 perkLevel);

    // A two-rank perk's value for a guild of this level: rank 2's from level2, rank 1's from
    // level1, else 0. The ranks don't add up.
    uint32 RankValue(uint8 guildLevel, uint8 level2, uint32 value2, uint8 level1, uint32 value1);

    // Adds guild XP, with Loyal Following's bonus, capped by the daily cap. Contributors are
    // characters who earned it; the XP is split between them for the roster. Returns what the
    // guild got.
    uint64 AddXP(uint32 guildId, uint64 xp, std::vector<ObjectGuid::LowType> const& contributors);

    // GM commands: set the level outright, or add XP without the cap.
    void SetLevel(uint32 guildId, uint8 level);
    void AddXPUncapped(uint32 guildId, uint64 xp);

    // Guild Tithe: money paid into the guild bank from a member's loot, for the totals.
    void AddCashFlow(uint32 guildId, ObjectGuid::LowType guid, uint64 money);

    void ForgetMember(ObjectGuid::LowType guid);
    void ForgetGuild(uint32 guildId);

    struct MemberStats
    {
        uint64 weekXp = 0;
        uint64 totalXp = 0;
        uint64 weekCash = 0;    // Guild Tithe this member's loot paid into the bank, copper
    };
    MemberStats GetMemberStats(ObjectGuid::LowType guid, uint32 guildId);
    std::vector<std::pair<ObjectGuid::LowType, MemberStats>> GetGuildMembers(uint32 guildId);

    void Load();
    void Save(bool now);            // writes what changed; now = synchronous (shutdown)
    void SaveNews(CharacterDatabaseTransaction trans, bool& any);   // GuildLevelsNews.cpp, from Save
    void ProcessEvents();           // world thread: level-up announcements

    // --- perks (GuildLevelsPerks.cpp) ---

    // Brings a player's perk auras and spells in line with their guild's level. Map thread.
    void RefreshPerks(Player* player, bool force);

    // Field Medicine: the heal spells of healing potions and healthstones, from the item
    // templates. Built once at startup, read-only after.
    void LoadHealingConsumables();

    // GuildLevelsSpells.cpp: a resurrection from a request was accepted; if it was Mass
    // Resurrection's, the player can't be raised by it again for a while.
    void MassResurrectionAccepted(Player* player);

    struct PerkInfo
    {
        uint8 level;
        char const* name;
        std::string description;
    };
    std::vector<PerkInfo> PerkList();
    std::string PerkName(uint8 level);

    // --- news feed (GuildLevelsNews.cpp). Safe from any thread. ---

    enum NewsType : uint8
    {
        NEWS_GUILD_LEVEL    = 0,    // value = level
        NEWS_ENCOUNTER      = 1,    // value = DungeonEncounter.dbc id, extra = map difficulty
        NEWS_ITEM_LOOTED    = 2,    // guid = member, value = item entry, extra = quality
        NEWS_ITEM_CRAFTED   = 3,    // the same
        NEWS_ACHIEVEMENT    = 4,    // guid = member, value = achievement id, extra = points
    };

    struct NewsItem
    {
        uint32 id;
        uint8 type;
        uint32 time;                // unix seconds
        ObjectGuid::LowType guid;   // the member it's about, 0 for the whole guild
        uint32 value;
        uint32 extra;
    };

    void LoadNewsConfig();
    void AddNews(uint32 guildId, NewsType type, ObjectGuid::LowType guid, uint32 value, uint32 extra = 0);
    std::vector<NewsItem> GetNews(uint32 guildId);     // newest first
    void ForgetGuildNews(uint32 guildId);
    void LoadNews();

    // Guild XP from kills: is the player wearing the stock Guild Tabard? (GuildLevelsXP.cpp)
    bool WearsGuildTabard(Player* player);

    // --- addon (GuildLevelsAddon.cpp) ---

    void SendAddon(Player* player, std::string const& payload);
    void SendState(Player* player);

    // Bot sessions: IsHeadless() on current playerbots cores, IsBot() on older ones.
    template <typename Session, typename = void>
    struct HasIsHeadless : std::false_type { };

    template <typename Session>
    struct HasIsHeadless<Session, std::void_t<decltype(std::declval<Session&>().IsHeadless())>> : std::true_type { };

    template <typename Session, typename = void>
    struct HasIsBot : std::false_type { };

    template <typename Session>
    struct HasIsBot<Session, std::void_t<decltype(std::declval<Session&>().IsBot())>> : std::true_type { };

    template <typename Session>
    bool IsBotSession(Session* session)
    {
        if constexpr (HasIsHeadless<Session>::value)
            return session->IsHeadless();
        else if constexpr (HasIsBot<Session>::value)
            return session->IsBot();
        else
            return false;
    }

    bool IsRealPlayer(Player const* player);
}

#endif
