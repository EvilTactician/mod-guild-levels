/*
 * mod-guild-levels: the passive guild perks.
 *
 * Most perks are a hook that checks the player's guild level when it fires. Two are hidden auras
 * (Swift Steeds, Second Wind's ghost speed) and two are spells the player learns
 * (GuildLevelsSpells.cpp). Loyal Following is in AddXP (GuildLevelsMgr.cpp). A player's auras and
 * spells are brought in line with their guild's level from their own map update every two
 * seconds, which also covers joining, leaving and the guild levelling up, without touching a
 * player from another thread.
 *
 * A perk with two ranks reads its value with RankValue: rank 2's replaces rank 1's.
 *
 * Notes, for systems 3.3.5 has or hasn't:
 * - Lasting Provisions: Well Fed, flask and elixir buffs last longer (3.3.5 has no guild cauldrons).
 * - Alchemist's Bounty: a chance of one extra flask or elixir when you make one.
 * - Battlefield Renown adds to honor from honorable kills; battleground bonus honor has no hook.
 * - Haggler lowers prices at vendors (items and repairs), not at trainers or flight masters.
 * - Bountiful Harvest: herb and ore nodes, skinning and disenchanting, trade goods and gems only.
 * - Reinforced Gear gives back part of the durability lost between a killing blow and
 *   resurrection (death and the spirit healer), since the core has no hook on durability loss.
 *
 * Released under the MIT License.
 */

#include "GuildLevels.h"
#include "Bag.h"
#include "Creature.h"
#include "DataMap.h"
#include "GameObject.h"
#include "Group.h"
#include "Guild.h"
#include "Item.h"
#include "LootMgr.h"
#include "Map.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "SpellMgr.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "GuildPackets.h"
#include "Log.h"
#include "Util.h"
#include "ObjectMgr.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_set>

using namespace GuildLevels;

namespace
{
    constexpr uint32 REFRESH_MS = 2000;

    // Castable perks: the level they come with and the spell.
    constexpr std::array<std::pair<uint8, uint32>, 2> PERK_SPELLS =
    {{
        { PERK_GUILD_VAULT, SPELL_GUILD_VAULT },
        { PERK_MASS_RESURRECTION, SPELL_MASS_RESURRECTION },
    }};

    constexpr Seconds SNAPSHOT_STALE = 3s;          // Reinforced Gear: a killing blow that didn't kill

    // Per player. Touched from that player's map update, or from the world thread, which never
    // runs alongside the map updates.
    struct PerkData : public DataMap::Base
    {
        uint32 timer = 0;
        uint32 guildId = 0;
        uint8 level = 0;
        bool known = false;

        // Reinforced Gear: durability of every item at the killing blow, given back at resurrection.
        std::vector<std::pair<ObjectGuid, uint32>> snapshot;
        Seconds snapshotAt = 0s;

        // Second Wind: raised by a guild member, top up once the health is set.
        bool raised = false;
    };

    PerkData* DataOf(Player* player)
    {
        return player->CustomData.GetDefault<PerkData>("GuildLevels");
    }

    uint32 Pick(Player const* player, uint8 level2, uint32 value2, uint8 level1, uint32 value1)
    {
        return RankValue(GetLevel(player->GetGuildId()), level2, value2, level1, value1);
    }

    // Guard so items the perks hand out don't trigger the perks again.
    thread_local bool granting = false;

    void GrantExtra(Player* player, uint32 entry, char const* perk)
    {
        if (granting)
            return;
        granting = true;
        if (player->AddItem(entry, 1))
            ChatHandler(player->GetSession()).PSendSysMessage("|cff40c040{}:|r you get one more.", perk);
        granting = false;
    }

    void ForEachItem(Player* player, std::function<void(Item*)> const& fn)
    {
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                fn(item);
        for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
            if (Bag* bag = player->GetBagByPos(bagSlot))
                for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                    if (Item* item = bag->GetItemByPos(uint8(slot)))
                        fn(item);
    }

    void SetAuraAmounts(Aura* aura, int32 amount)
    {
        if (!aura)
            return;
        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            if (AuraEffect* effect = aura->GetEffect(i))
                if (effect->GetAmount() != amount)
                    effect->ChangeAmount(amount);
    }

    // Adds or removes a hidden aura so the player has it exactly when `want`.
    void Keep(Player* player, uint32 spellId, bool want, int32 amount)
    {
        Aura* aura = player->GetAura(spellId);
        if (want && !aura)
            SetAuraAmounts(player->AddAura(spellId, player), amount);
        else if (want)
            SetAuraAmounts(aura, amount);
        else if (aura)
            player->RemoveAurasDueToSpell(spellId);
    }

    bool IsWellFed(SpellInfo const* spell)
    {
        char const* name = spell->SpellName[0];
        return name && std::string_view(name) == "Well Fed";
    }

    bool IsFlaskOrElixir(SpellInfo const* spell)
    {
        return sSpellMgr->IsSpellMemberOfSpellGroup(spell->Id, SPELL_GROUP_ELIXIR_BATTLE)
            || sSpellMgr->IsSpellMemberOfSpellGroup(spell->Id, SPELL_GROUP_ELIXIR_GUARDIAN);
    }

    // Field Medicine: on-use heal spells of potions and healthstones.
    std::unordered_set<uint32> healingConsumables;

    // A herb or ore node: its lock needs Herbalism or Mining. (Every lock-opened gameobject,
    // treasure chests too, loots as LOOT_SKINNING.)
    bool IsGatheringNode(GameObject const* go)
    {
        LockEntry const* lock = sLockStore.LookupEntry(go->GetGOInfo()->GetLockId());
        if (!lock)
            return false;
        for (uint8 i = 0; i < MAX_LOCK_CASE; ++i)
            if (lock->Type[i] == LOCK_KEY_SKILL && (lock->Index[i] == LOCKTYPE_HERBALISM || lock->Index[i] == LOCKTYPE_MINING))
                return true;
        return false;
    }

    // Bountiful Harvest: was this loot gathered (node, skinning) or disenchanted?
    bool IsGathered(Player* player, ObjectGuid guid)
    {
        if (guid.IsGameObject())
        {
            GameObject* go = player->GetMap()->GetGameObject(guid);
            return go && go->loot.loot_type == LOOT_SKINNING && IsGatheringNode(go);
        }
        if (guid.IsCreatureOrVehicle())
        {
            Creature* creature = player->GetMap()->GetCreature(guid);
            return creature && creature->loot.loot_type == LOOT_SKINNING;
        }
        if (guid.IsItem())
        {
            Item* item = player->GetItemByGuid(guid);
            return item && item->loot.loot_type == LOOT_DISENCHANTING;
        }
        return false;
    }

    // Whether a resurrection came from a request (a spell, Mass Resurrection) and not the
    // corpse or the spirit healer: ResurectUsingRequestData calls ResurrectPlayer(0, false).
    // A soulstone looks the same, so the request also has to be from the right player.
    bool FromRequest(float restorePercent, bool applySickness)
    {
        return restorePercent == 0.0f && !applySickness;
    }
}

namespace GuildLevels
{
    void RefreshPerks(Player* player, bool force)
    {
        PerkData* data = DataOf(player);
        uint32 guildId = player->GetGuildId();
        uint8 level = GetLevel(guildId);
        bool changed = !data->known || data->guildId != guildId || data->level != level;
        if (!changed && !force)
            return;

        Config const& cfg = GetConfig();
        bool pvpMap = player->FindMap() && player->FindMap()->IsBattlegroundOrArena();

        uint32 swiftSteeds = RankValue(level, PERK_SWIFT_STEEDS_2, cfg.swiftSteeds2, PERK_SWIFT_STEEDS_1, cfg.swiftSteeds1);
        Keep(player, SPELL_SWIFT_STEEDS, swiftSteeds && !pvpMap, int32(swiftSteeds));
        Keep(player, SPELL_SECOND_WIND, level >= PERK_SECOND_WIND && !pvpMap
            && player->HasPlayerFlag(PLAYER_FLAGS_GHOST), int32(cfg.secondWindSpeed));

        for (auto const& [perkLevel, spellId] : PERK_SPELLS)
        {
            bool has = player->HasSpell(spellId);
            if (level >= perkLevel && !has)
                player->learnSpell(spellId);
            else if (level < perkLevel && has)
                player->removeSpell(spellId, SPEC_MASK_ALL, false);
        }

        // Not from 0: that is the module being switched back on, not the guild levelling up.
        bool levelUp = data->known && data->guildId == guildId && data->level && level > data->level;
        data->known = true;
        data->guildId = guildId;
        data->level = level;

        if (changed && IsRealPlayer(player))
        {
            SendState(player);
            if (levelUp)
                SendAddon(player, Acore::StringFormat("UP:{}:{}", level, PerkName(level)));
        }
    }

    std::vector<PerkInfo> PerkList()
    {
        Config const& c = GetConfig();
        auto f = [](char const* text, auto... args) { return Acore::StringFormat(text, args...); };
        return
        {
            { PERK_GOOD_STANDING_1, "Good Standing", f("Reputation from killing monsters and completing quests increased by {}%.", c.goodStanding1) },
            { PERK_HAGGLER_1, "Haggler", f("Prices and repairs at vendors reduced by {}%.", c.haggler1) },
            { PERK_BATTLEFIELD_RENOWN_1, "Battlefield Renown", f("Honor from honorable kills increased by {}%.", c.battlefieldRenown1) },
            { PERK_SWIFT_STEEDS_1, "Swift Steeds", f("Mounted and flying speed increased by {}%. Not active in battlegrounds and arenas.", c.swiftSteeds1) },
            { PERK_LOYAL_FOLLOWING_1, "Loyal Following", f("Guild experience earned by the guild increased by {}%. Your own experience is unchanged.", c.loyalFollowing1) },
            { PERK_GUILD_TITHE_1, "Guild Tithe", f("Each time you loot money from an enemy, an extra {}% is deposited into your guild bank.", c.guildTithe1) },
            { PERK_FIELD_MEDICINE_1, "Field Medicine", f("Healing potions and healthstones heal {}% more.", c.fieldMedicine1) },
            { PERK_REINFORCED_GEAR_1, "Reinforced Gear", f("Items lose {}% less durability when you die.", c.reinforcedGear1) },
            { PERK_HAGGLER_2, "Haggler (Rank 2)", f("Prices and repairs at vendors reduced by {}%.", c.haggler2) },
            { PERK_GUILD_VAULT, "Guild Vault", f("Teaches Guild Vault: summons a guild chest for 5 min that opens your guild bank. 1 hour cooldown.") },
            { PERK_GOOD_STANDING_2, "Good Standing (Rank 2)", f("Reputation from killing monsters and completing quests increased by {}%.", c.goodStanding2) },
            { PERK_BOUNTIFUL_HARVEST_1, "Bountiful Harvest", f("A {}% chance of an extra trade good or gem from Mining, Herbalism, Skinning and Disenchanting.", c.bountifulHarvest1) },
            { PERK_BATTLEFIELD_RENOWN_2, "Battlefield Renown (Rank 2)", f("Honor from honorable kills increased by {}%.", c.battlefieldRenown2) },
            { PERK_SECOND_WIND, "Second Wind", f("Health and mana when a guild member resurrects you increased by {}%, and you move {}% faster as a ghost. Not active in battlegrounds and arenas.", c.secondWindRes, c.secondWindSpeed) },
            { PERK_GUILD_TITHE_2, "Guild Tithe (Rank 2)", f("Each time you loot money from an enemy, an extra {}% is deposited into your guild bank.", c.guildTithe2) },
            { PERK_REINFORCED_GEAR_2, "Reinforced Gear (Rank 2)", f("Items lose {}% less durability when you die.", c.reinforcedGear2) },
            { PERK_LOYAL_FOLLOWING_2, "Loyal Following (Rank 2)", f("Guild experience earned by the guild increased by {}%. Your own experience is unchanged.", c.loyalFollowing2) },
            { PERK_LASTING_PROVISIONS_1, "Lasting Provisions", f("Well Fed, flask and elixir buffs last {}% longer.", c.lastingProvisions1) },
            { PERK_ALCHEMISTS_BOUNTY, "Alchemist's Bounty", f("A {}% chance of an extra flask or elixir when you make one.", c.alchemistsBountyChance) },
            { PERK_FIELD_MEDICINE_2, "Field Medicine (Rank 2)", f("Healing potions and healthstones heal {}% more.", c.fieldMedicine2) },
            { PERK_BOUNTIFUL_HARVEST_2, "Bountiful Harvest (Rank 2)", f("A {}% chance of an extra trade good or gem from Mining, Herbalism, Skinning and Disenchanting.", c.bountifulHarvest2) },
            { PERK_LASTING_PROVISIONS_2, "Lasting Provisions (Rank 2)", f("Well Fed, flask and elixir buffs last {}% longer.", c.lastingProvisions2) },
            { PERK_SWIFT_STEEDS_2, "Swift Steeds (Rank 2)", f("Mounted and flying speed increased by {}%. Not active in battlegrounds and arenas.", c.swiftSteeds2) },
            { PERK_MASS_RESURRECTION, "Mass Resurrection", f("Teaches Mass Resurrection: brings all dead party and raid members back to life with {}% health and mana.", c.massResHealth) },
        };
    }

    void LoadHealingConsumables()
    {
        healingConsumables.clear();
        for (auto const& [entry, proto] : *sObjectMgr->GetItemTemplateStore())
        {
            if (proto.Class != ITEM_CLASS_CONSUMABLE)
                continue;
            bool healthstone = proto.Name1.find("Healthstone") != std::string::npos;
            if (proto.SubClass != ITEM_SUBCLASS_POTION && !healthstone)
                continue;
            for (auto const& itemSpell : proto.Spells)
            {
                if (itemSpell.SpellTrigger != ITEM_SPELLTRIGGER_ON_USE || itemSpell.SpellId <= 0)
                    continue;
                SpellInfo const* spell = sSpellMgr->GetSpellInfo(uint32(itemSpell.SpellId));
                if (spell && spell->HasEffect(SPELL_EFFECT_HEAL))
                    healingConsumables.insert(spell->Id);
            }
        }
        LOG_INFO("module", "mod-guild-levels: {} healing potion and healthstone spells", healingConsumables.size());
    }

    std::string PerkName(uint8 level)
    {
        for (PerkInfo const& perk : PerkList())
            if (perk.level == level)
                return perk.name;
        return {};
    }
}

class GuildLevelsPerksPlayerScript : public PlayerScript
{
public:
    GuildLevelsPerksPlayerScript() : PlayerScript("GuildLevelsPerksPlayerScript", {
        PLAYERHOOK_ON_UPDATE,
        PLAYERHOOK_ON_MAP_CHANGED,
        PLAYERHOOK_ON_GIVE_REPUTATION,
        PLAYERHOOK_ON_BEFORE_LOOT_MONEY,
        PLAYERHOOK_ON_PLAYER_RELEASED_GHOST,
        PLAYERHOOK_ON_PLAYER_RESURRECT,
        PLAYERHOOK_ON_VICTIM_REWARD_AFTER,
        PLAYERHOOK_ON_CREATE_ITEM,
        PLAYERHOOK_ON_LOOT_ITEM,
        PLAYERHOOK_ON_GET_REPUTATION_PRICE_DISCOUNT,
    }) { }

    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        if (!GetConfig().enabled)
            return;

        PerkData* data = DataOf(player);

        // Second Wind: the health ResurectUsingRequestData set, half again.
        if (data->raised && player->IsAlive())
        {
            data->raised = false;
            uint32 pct = 100 + GetConfig().secondWindRes;
            player->SetHealth(std::min<uint32>(player->GetMaxHealth(), uint32(uint64(player->GetHealth()) * pct / 100)));
            if (player->GetMaxPower(POWER_MANA))
                player->SetPower(POWER_MANA, std::min<uint32>(player->GetMaxPower(POWER_MANA), uint32(uint64(player->GetPower(POWER_MANA)) * pct / 100)));
        }

        // Reinforced Gear: a killing blow the player survived (a duel) leaves a snapshot behind.
        if (!data->snapshot.empty() && player->IsAlive() && GameTime::GetGameTime() - data->snapshotAt > SNAPSHOT_STALE)
            data->snapshot.clear();

        if (data->timer > diff)
        {
            data->timer -= diff;
            return;
        }
        data->timer = REFRESH_MS;
        // Every tick also re-checks the auras, which some core paths strip (arena entry, etc.).
        RefreshPerks(player, true);
    }

    void OnPlayerMapChanged(Player* player) override
    {
        if (GetConfig().enabled)
            RefreshPerks(player, true);
    }

    // Good Standing
    void OnPlayerGiveReputation(Player* player, int32 /*factionID*/, float& amount, ReputationSource repSource) override
    {
        if (amount <= 0.0f || repSource == REPUTATION_SOURCE_SPELL)
            return;
        if (uint32 pct = Pick(player, PERK_GOOD_STANDING_2, GetConfig().goodStanding2, PERK_GOOD_STANDING_1, GetConfig().goodStanding1))
            amount += amount * pct / 100.0f;
    }

    // Guild Tithe. CMSG_LOOT_MONEY is handled on the world thread, so the guild bank is safe here.
    void OnPlayerBeforeLootMoney(Player* player, Loot* loot) override
    {
        if (!loot || !loot->gold || !loot->sourceWorldObjectGUID.IsCreatureOrVehicle())
            return;
        uint32 pct = Pick(player, PERK_GUILD_TITHE_2, GetConfig().guildTithe2, PERK_GUILD_TITHE_1, GetConfig().guildTithe1);
        if (!pct)
            return;
        Guild* guild = player->GetGuild();
        if (!guild)
            return;

        uint64 extra = uint64(loot->gold) * pct / 100;
        if (!extra || guild->GetTotalBankMoney() + extra > GUILD_BANK_MONEY_LIMIT)
            return;

        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        if (!guild->ModifyBankMoney(trans, extra, true))
            return;
        CharacterDatabase.CommitTransaction(trans);
        AddCashFlow(guild->GetId(), player->GetGUID().GetCounter(), extra);

        // ModifyBankMoney neither logs nor tells anyone; this refreshes open guild bank windows
        // the way a deposit does.
        uint64 total = guild->GetTotalBankMoney();
        WorldPackets::Guild::GuildEvent event;
        event.Type = GE_BANK_MONEY_SET;
        event.Params.push_back(Acore::Impl::ByteArrayToHexStr(reinterpret_cast<uint8 const*>(&total), 8, true));
        guild->BroadcastPacket(event.Write());
    }

    // Second Wind: ghost speed.
    void OnPlayerReleasedGhost(Player* player) override
    {
        if (GetConfig().enabled)
            RefreshPerks(player, true);
    }

    void OnPlayerResurrect(Player* player, float restorePercent, bool& applySickness) override
    {
        if (!GetConfig().enabled)
            return;

        player->RemoveAurasDueToSpell(SPELL_SECOND_WIND);
        PerkData* data = DataOf(player);
        bool fromRequest = FromRequest(restorePercent, applySickness);
        if (fromRequest)
            MassResurrectionAccepted(player);

        // Reinforced Gear
        std::vector<std::pair<ObjectGuid, uint32>> before;
        before.swap(data->snapshot);
        if (uint32 pct = Pick(player, PERK_REINFORCED_GEAR_2, GetConfig().reinforcedGear2, PERK_REINFORCED_GEAR_1, GetConfig().reinforcedGear1))
        {
            for (auto const& [itemGuid, durability] : before)
            {
                Item* item = player->GetItemByGuid(itemGuid);
                if (!item)
                    continue;
                uint32 now = item->GetUInt32Value(ITEM_FIELD_DURABILITY);
                if (now < durability)
                    if (int32 back = int32((durability - now) * pct / 100))
                        player->DurabilityPointsLoss(item, -back);
            }
        }

        // Second Wind: raised by a spell from a guild member of the group (a guild bot too).
        if (!fromRequest || !player->isResurrectRequested() || !HasPerk(player, PERK_SECOND_WIND))
            return;
        if (player->InBattleground())
            return;
        Group* group = player->GetGroup();
        if (!group)
            return;
        for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
        {
            Player* member = itr->GetSource();
            if (member && member != player && member->GetGuildId() == player->GetGuildId()
                && player->isResurrectRequestedBy(member->GetGUID()))
            {
                data->raised = true;
                break;
            }
        }
    }

    // Battlefield Renown
    void OnPlayerVictimRewardAfter(Player* player, Player* /*victim*/, uint32& /*killer_title*/, int32& /*victim_rank*/, float& honor_f) override
    {
        if (uint32 pct = Pick(player, PERK_BATTLEFIELD_RENOWN_2, GetConfig().battlefieldRenown2, PERK_BATTLEFIELD_RENOWN_1, GetConfig().battlefieldRenown1))
            honor_f += honor_f * pct / 100.0f;
    }

    // Alchemist's Bounty
    void OnPlayerCreateItem(Player* player, Item* item, uint32 /*count*/) override
    {
        ItemTemplate const* proto = item->GetTemplate();
        if (granting || proto->Class != ITEM_CLASS_CONSUMABLE)
            return;
        if (proto->SubClass != ITEM_SUBCLASS_FLASK && proto->SubClass != ITEM_SUBCLASS_ELIXIR)
            return;
        if (HasPerk(player, PERK_ALCHEMISTS_BOUNTY) && roll_chance_i(int32(GetConfig().alchemistsBountyChance)))
            GrantExtra(player, item->GetEntry(), "Alchemist's Bounty");
    }

    // Bountiful Harvest
    void OnPlayerLootItem(Player* player, Item* item, uint32 /*count*/, ObjectGuid lootguid) override
    {
        if (granting || !item)
            return;
        uint32 chance = Pick(player, PERK_BOUNTIFUL_HARVEST_2, GetConfig().bountifulHarvest2, PERK_BOUNTIFUL_HARVEST_1, GetConfig().bountifulHarvest1);
        if (!chance)
            return;
        ItemTemplate const* proto = item->GetTemplate();
        if (proto->Class != ITEM_CLASS_TRADE_GOODS && proto->Class != ITEM_CLASS_GEM)
            return;
        if (proto->StartQuest || proto->Bonding == BIND_QUEST_ITEM || !IsGathered(player, lootguid))
            return;
        if (roll_chance_i(int32(chance)))
            GrantExtra(player, item->GetEntry(), "Bountiful Harvest");
    }

    // Haggler. Only the Creature overload: it calls the FactionTemplate one first, so taking
    // both would count twice. Vendors only: the same discount also prices trainers and flights.
    void OnPlayerGetReputationPriceDiscount(Player const* player, Creature const* creature, float& discount) override
    {
        if (!creature || !creature->HasNpcFlag(UNIT_NPC_FLAG_VENDOR))
            return;
        if (uint32 pct = Pick(player, PERK_HAGGLER_2, GetConfig().haggler2, PERK_HAGGLER_1, GetConfig().haggler1))
            discount *= 1.0f - pct / 100.0f;
    }
};

class GuildLevelsPerksUnitScript : public UnitScript
{
public:
    GuildLevelsPerksUnitScript() : UnitScript("GuildLevelsPerksUnitScript", true, { UNITHOOK_ON_DAMAGE, UNITHOOK_MODIFY_HEAL_RECEIVED }) { }

    // Field Medicine. A potion heals whoever drinks it, so healer and target are the same player
    // whichever order a core passes them in.
    void ModifyHealReceived(Unit* target, Unit* healer, uint32& heal, SpellInfo const* spellInfo) override
    {
        if (!spellInfo || !heal || target != healer || !target || !target->IsPlayer())
            return;
        if (!healingConsumables.count(spellInfo->Id))
            return;
        if (uint32 pct = Pick(target->ToPlayer(), PERK_FIELD_MEDICINE_2, GetConfig().fieldMedicine2, PERK_FIELD_MEDICINE_1, GetConfig().fieldMedicine1))
            heal += uint32(uint64(heal) * pct / 100);
    }

    // Reinforced Gear: what everything was worth at the killing blow.
    void OnDamage(Unit* /*attacker*/, Unit* victim, uint32& damage) override
    {
        if (!victim || !victim->IsPlayer() || damage < victim->GetHealth() || !victim->IsAlive())
            return;
        Player* player = victim->ToPlayer();
        if (!HasPerk(player, PERK_REINFORCED_GEAR_1))
            return;

        PerkData* data = DataOf(player);
        data->snapshot.clear();
        data->snapshotAt = GameTime::GetGameTime();
        ForEachItem(player, [data](Item* item)
        {
            if (item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY))
                data->snapshot.emplace_back(item->GetGUID(), item->GetUInt32Value(ITEM_FIELD_DURABILITY));
        });
    }
};

// Lasting Provisions
class GuildLevelsPerksSpellScript : public AllSpellScript
{
public:
    GuildLevelsPerksSpellScript() : AllSpellScript("GuildLevelsPerksSpellScript", { ALLSPELLHOOK_ON_CALC_MAX_DURATION }) { }

    void OnCalcMaxDuration(Aura const* aura, int32& maxDuration) override
    {
        if (maxDuration <= 0 || !GetConfig().enabled)
            return;
        Player* player = aura->GetOwner() ? aura->GetOwner()->ToPlayer() : nullptr;
        if (!player)
            return;
        SpellInfo const* spell = aura->GetSpellInfo();
        if (!IsWellFed(spell) && !IsFlaskOrElixir(spell))
            return;
        if (uint32 pct = Pick(player, PERK_LASTING_PROVISIONS_2, GetConfig().lastingProvisions2, PERK_LASTING_PROVISIONS_1, GetConfig().lastingProvisions1))
            maxDuration = int32(std::min<int64>(int64(maxDuration) * (100 + pct) / 100, INT32_MAX));
    }
};

void AddGuildLevelsPerksScripts()
{
    new GuildLevelsPerksPlayerScript();
    new GuildLevelsPerksUnitScript();
    new GuildLevelsPerksSpellScript();
}
