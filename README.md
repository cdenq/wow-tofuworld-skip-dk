# ![logo](https://raw.githubusercontent.com/azerothcore/azerothcore.github.io/master/images/logo-github.png) AzerothCore

## mod-skip-dk-starting-area

- Latest build status with azerothcore:

[![Build Status](https://github.com/azerothcore/mod-skip-dk-starting-area/workflows/core-build/badge.svg)](https://github.com/azerothcore/mod-skip-dk-starting-area)

## Description

- Skips the Death Knight starting zone, for those who want to make a Death Knight without having to go through the starting area.

## Tofu Worldview changes (branch `tofu`)

- The skip completes every obtainable quest of the Death Knight starting zone (57 quests, including the breadcrumbs, Abandoned Mail, The Power Of Blood, Frost And Unholy, Noth's Special Brew, and More Skulls For Brew), so they count as completed for the quest log history and quest-count achievements. Quests 12625 and 12626 are left out because no creature or object starts or ends them. A quest already in the log is turned in; a quest already turned in is skipped.
- The Lich King offers "I wish to skip the starting zone." next to his quest, with no confirmation box. Clicking it opens a gossip menu with the three cloaks of Tonight We Dine In Havenshire (12679), then a menu with the two weapons of The Light of Dawn (12801). The weapon pick carries one "Are you sure?" box. Each menu also prints the items as clickable links in chat, so you can hover them to see the tooltips. Nothing is given until the weapon pick is confirmed, so quitting partway leaves the character untouched. These are gossip options, not client reward windows, so quest addons cannot answer them for the player.
- On the confirmed pick, every quest is turned in (the cloak and weapon quests with the picked items), the level and trained spells are set, the gold is reset, the weapon gets its rune, the hearthstone is set to Stormwind or Orgrimmar, and the player is teleported there 3 seconds later, so the client's item queries for the new rewards are answered before the loading screen (the core drops item queries while the player is between maps). Each step is written to `Server.log` with the prefix `Skip DK:`.
- The final quest, Where Kings Walk (13188) or Warchief's Blessing (13189), is turned in as part of the skip, so its Stormwind or Orgrimmar reputation and XP are applied, and the letter is consumed. The skip option disappears once that quest is turned in.
- While the skip is available the Lich King shows only his gossip text and the "Skip DK zone" option (chat icon), with no quest entries, so addons that auto-click quests cannot trigger it.
- A character left half-skipped by an older build (the cloak or weapon quest still in the log, or already turned in) is handled by the same menus: a quest already turned in is not offered again, and a quest in the log is turned in with the picked item.
- The click keeps gossip action 1, and the cloak and weapon picks use actions 1100 to 1299, so a module that watches those actions (the Tofu challenge-modes fork) still blocks them.
- The first-login automatic skip (`Skip.Deathknight.Starter.Enable`) has no menus and gives the first reward of each choice quest.

## How to use ingame

1. Enable in conf
2. Go into conf and set any features you want to enable or modify
3. Make a Death Knight and login.

## Notice

Due to the uniquiness of the module you will get this message on the worldconsole, but nothing is broken. It is due to the npc not exactly having a gossip menu in the database, the script handles the gossip menu because of the additional locales.

![image](https://user-images.githubusercontent.com/16887899/152654078-7b94c62c-a5e8-42ed-96ed-39ffaacf38b1.png)

## Requirements

- Latest AzerothCore Wotlk commit [de13bf4](https://github.com/azerothcore/azerothcore-wotlk/commit/de13bf426e162ee10cbd5470cec74122d1d4afa0) or newer

## Installation

1. Simply place the module under the `modules` directory of your AzerothCore source.
2. Re-run CMake and rebuild the AzerothCore source.
3. Edit module configuration (optional)

If you need to change the module configuration, go to your server configuration folder (where your worldserver or worldserver.exe is), copy SkipDKModule.conf.dist to SkipDKModule.conf and edit that new file.

### Credits

- [acidmanifesto (MDic)](https://github.com/acidmanifesto/TC-Custom-Scripts/tree/main/335%20TC/Official%20Trinitycore%20Custom%20Scripts/Official%20Merged/Skip%20Death%20Knight%20Starter%20Area%20Module)
- [Jinnaix](https://github.com/Jinnaix) For Translations assistance and sql formatting.
- AzerothCore: [repository](https://github.com/azerothcore) - [website](http://azerothcore.org/) - [discord chat community](https://discord.gg/PaqQRkd)
