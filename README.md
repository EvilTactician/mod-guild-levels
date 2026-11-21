# mod-guild-levels 2.0.0

<div align="center">

![Interface Version](https://img.shields.io/badge/Interface-30300-blue)
![WoW Version](https://img.shields.io/badge/WoW-3.3.5a-orange)
[![Version](https://img.shields.io/badge/Version-2.0.0-green)](https://github.com/EvilTactician/mod-guild-levels)
[![License](https://img.shields.io/badge/License-MIT-yellow)](LICENSE)

</div>

> **This is a personalised, simpler fork** of
> [buildthehomelab's mod-guild-levels](https://github.com/buildthehomelab/wow-mod-guild-levels),
> made to work with [Grimfeather's AzerothCore](https://github.com/Grimfeather/azerothcore-wotlk),
> [mod-individual-progression](https://github.com/Grimfeather/mod-individual-progression) and
> [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots). It has no guild reputation,
> no guild vendors or rewards and no perks that need a core hook or another module, and its client
> patch is two spells in Spell.dbc. **If you want guild reputation and the guild rewards, use the
> [upstream version](https://github.com/buildthehomelab/wow-mod-guild-levels).**

Guild levels for [AzerothCore](https://www.azerothcore.org) (WotLK 3.3.5a), in the spirit of
Cataclysm's. A guild earns guild XP from its members' quests, from dungeon and raid bosses killed
as a guild group and from kills made in a guild tabard, and climbs from level 1 to 25. Every level
from 2 to 25 unlocks a guild perk. The GuildLevels addon shows the guild's level, XP, perks, news
and each member's contribution.

## Guild XP

| Source | Guild XP |
|---|---|
| A quest | 25% of the quest's XP (`GuildLevels.XP.QuestShare`). Counts at the level cap too, where the quest pays money instead. Repeat completions that give no XP give none. |
| A dungeon boss killed by a guild group | 2000 (`GuildLevels.XP.BossDungeon`): 50% with 3 guild members in the group, 100% with 4, 125% with 5 |
| A raid boss killed by a guild group | 6000 (`GuildLevels.XP.BossRaid`) with 10 guild members (`GuildLevels.XP.RaidGuildMembers`, at most 80% of the raid) |
| A kill while wearing the Guild Tabard | 10 per member wearing it (`GuildLevels.XP.TabardKill`). A flat amount that no realm rate changes. |

Heroic dungeons and raids pay 1.5×. A boss that is gray to the highest-level guild member pays
nothing. A boss is one of the instance's encounters (DungeonEncounter.dbc), paid when the core
credits it and once per instance, so a council of several creatures pays once. The players in the
instance are the group: the guild with the most members among them is the guild group. Quest XP
is paid when the reward goes through, not when the quest giver's window shows it.

Tabard kills count for each member the kill rewards (the tagger, or each of their group in reach)
who is alive. Creatures gray to the member, critters, totems, pets and creatures that give no XP
pay nothing.

The Loyal Following perk adds 10%, then 20%, to all of the guild's XP, from every source. It raises
the guild's XP only: members' own XP is never changed.

The XP needed for each level is Cataclysm's table (16.58 million for level 2 up to 54.73 million
for level 25, 855.69 million in all) times `GuildLevels.XP.CurveScale`. The default, 0.0125, makes
the whole climb about 10.7 million guild XP: about three months for three to five active players.
A guild earns at most 150,000 guild XP a day (`GuildLevels.XP.DailyCap`, 0 for no cap) until
level 20.

**Bots.** By default only real players earn guild XP. Bots
([mod-playerbots](https://github.com/mod-playerbots/mod-playerbots)) that are members of the guild
count toward the size of a guild group, so one player with four guild bots is a full guild group.
A guild with no real player in it never earns anything, so bot-run guilds stay at level 1.
`GuildLevels.XP.BotGuildsEarn = 1` lets bots earn guild XP too: guild bots' quests and tabard kills
count for your guild, and random bots' own guilds level up as well.

A member's contribution is kept per week (weeks start on Tuesday) and in total, and is reset when
they leave the guild.

## Perks

| Level | Perk | Effect |
|---|---|---|
| 2 | Good Standing | +5% reputation from kills and quests |
| 3 | Haggler | 5% lower prices and repairs at vendors |
| 4 | Battlefield Renown | +5% honor from honorable kills |
| 5 | Swift Steeds | +10% mounted and flying speed (not in battlegrounds and arenas) |
| 6 | Loyal Following | +10% guild XP for the guild (your own XP is unchanged) |
| 7 | Guild Tithe | an extra 5% of money looted from enemies goes into the guild bank (the addon shows the week's total) |
| 8 | Field Medicine | healing potions and healthstones heal 25% more |
| 9 | Reinforced Gear | 10% less durability lost by dying |
| 10 | Haggler (Rank 2) | 10% lower prices |
| 11 | Guild Vault | spell: summons a guild chest that opens your guild bank for 5 min (1 hour cooldown) |
| 12 | Good Standing (Rank 2) | +10% reputation |
| 13 | Bountiful Harvest | a 10% chance of an extra trade good or gem from herb and ore nodes, Skinning and Disenchanting |
| 14 | Battlefield Renown (Rank 2) | +10% honor |
| 15 | Second Wind | +50% health and mana when a guild member of your group resurrects you; +100% speed as a ghost |
| 16 | Guild Tithe (Rank 2) | an extra 10% |
| 17 | Reinforced Gear (Rank 2) | 20% less durability lost |
| 18 | Loyal Following (Rank 2) | +20% guild XP |
| 19 | Lasting Provisions | Well Fed, flask and elixir buffs last 50% longer |
| 20 | Alchemist's Bounty | a 50% chance of an extra flask or elixir when you make one |
| 21 | Field Medicine (Rank 2) | heal 50% more |
| 22 | Bountiful Harvest (Rank 2) | a 20% chance |
| 23 | Lasting Provisions (Rank 2) | buffs last 100% longer |
| 24 | Swift Steeds (Rank 2) | +20% mounted and flying speed |
| 25 | Mass Resurrection | spell: brings dead party and raid members within 100 yards back with 35% health and mana (10 min cooldown; once per player every 10 min) |

Every value is in the config. A perk's rank 2 replaces its rank 1: the two don't add up. Notes:

- **Battlefield Renown** adds to honor from honorable kills, in battlegrounds too. The core has no
  hook for a battleground's bonus honor.
- **Reinforced Gear** gives back part of the durability lost between the killing blow and
  resurrection (the death itself and the spirit healer), because the core has no hook on
  durability loss.
- **Second Wind**'s resurrection bonus counts resurrection spells cast by guild members of your
  group, guild bots included (Mass Resurrection too), not the spirit healer or running back to
  your corpse. A soulstone taken while a guild member's resurrection is still on offer counts too.
- **Haggler** works at vendors (items and repairs), not at trainers or flight masters.
- **Lasting Provisions** and **Alchemist's Bounty** are about food, flasks and elixirs, because
  3.3.5 has no guild cauldrons or feasts.
- **Guild Vault**'s chest is a guild bank like the ones in the capitals: anyone who uses it opens
  their own guild's bank.

Perks work for guild bots as well as players. The two spells are learned when the guild reaches
their level and unlearned when it doesn't have them (left the guild, `.guildlevel set` lower). The
perks are checked every two seconds.

## Guild news

Like Cataclysm's Guild News, the module keeps a feed per guild, newest first: the guild reaching a
level, dungeon and raid bosses killed by a guild group, epic items members looted, won in a roll
or crafted, and achievements members earned. Bots' loot and achievements stay out unless
`GuildLevels.News.Bots` is on. The same member looting or crafting the same item again within an
hour adds nothing new. Items a master looter hands out don't show: the core gives them out without
a script hook. It keeps the newest `GuildLevels.News.Keep` items (100) and deletes older ones. The
feed goes to addons with the `NEWS` request (see `src/GuildLevelsAddon.cpp`); the GuildLevels
addon keeps it in `GuildLevels.news` for a guild window to show.

## Install

```bash
cd modules
git clone https://github.com/EvilTactician/mod-guild-levels.git mod-guild-levels
```

Clone into `mod-guild-levels`: AzerothCore derives the loader symbol from the folder name.
Rebuild, copy `conf/mod_guild_levels.conf.dist` to `mod_guild_levels.conf`, and start the
worldserver; it applies `data/sql/db-world` and `data/sql/db-characters` itself.

### Client patch

Guild Vault and Mass Resurrection are new spells (90452 and 90454), so the client needs them in
its `Spell.dbc`. That is all the patch carries. Add them to the patch that already carries your
realm's Spell.dbc (a patch replaces whole files, so a second patch with its own Spell.dbc would
hide the first one's spells):

```bash
STORMLIB=/path/to/libstorm.so python3 tools/build_patch.py --patch patch-enUS-4.MPQ \
    --out patch-enUS-4-new.MPQ --sql data/sql/db-world/mod_guild_levels_spells.sql
```

The tool copies every other file of the patch unchanged and writes the server's rows from the same
rows (`--sql`), so client and server agree. Running it again on its own output adds nothing. If
you rebuild that patch with another module's tool later, run this one on it again afterwards. It
needs Python 3 and [StormLib](https://github.com/ladislav-zezula/StormLib). Without the patch the
two spells don't show in the spellbook; every other perk works without it.

### The addon

`addon/GuildLevels` goes in `Interface/AddOns/GuildLevels`. It adds a side tab (a guild tabard with
the level on it) to the guild tab of the social window, and `/glevel` opens the window: the guild's
level and XP, today's XP against the daily cap, Guild Tithe paid into the bank this week (the
guild's and yours), and tabs for the perks (the guild's in colour, the rest with the level they
come at), the guild news, and each member's guild XP this week and in total. The window uses only
stock Blizzard art. With [DragonUI](https://github.com/EvilTactician/wow-addon-DragonUI), its own
guild window shows the same instead. When the guild levels up, a banner shows the new level and its
perk, with the level-up sound; clicking it opens the window. With DragonUI's Level Up Enhance
module on, DragonUI's own level-up banner shows it instead.

## Commands

| Command | Who | |
|---|---|---|
| `.guildlevel` | everyone | your guild's level and XP |
| `.guildlevel info [guild]` | GM | any guild's (or the selected player's) |
| `.guildlevel set <level> [guild]` | admin | set a guild's level |
| `.guildlevel addxp <xp> [guild]` | admin | give a guild XP, ignoring the daily cap |

## Uninstall

Remove the module and rebuild, then run `data/sql/uninstall/mod_guild_levels_uninstall_world.sql`
on the world database and `mod_guild_levels_uninstall_characters.sql` on the characters database
(it drops the guild levels, the news and the learned perk spells).

## IDs

| What | IDs |
|---|---|
| spells | 90450 Swift Steeds, 90451 Second Wind (server only, hidden auras); 90452 Guild Vault, 90454 Mass Resurrection |
| gameobjects | 9503400 Guild Chest (a guild bank) |
| characters tables | `mod_guild_levels`, `mod_guild_levels_member`, `mod_guild_levels_news` |

## Credits and license

MIT. Created by [buildthehomelab](https://github.com/buildthehomelab/wow-mod-guild-levels); this
fork by [EvilTactician](https://github.com/EvilTactician). The XP table follows Blizzard's
Cataclysm guild advancement.
