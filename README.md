# ![logo](https://raw.githubusercontent.com/azerothcore/azerothcore.github.io/master/images/logo-github.png) AzerothCore

## mod-skip-dk-starting-area

- Latest build status with azerothcore:

[![Build Status](https://github.com/azerothcore/mod-skip-dk-starting-area/workflows/core-build/badge.svg)](https://github.com/azerothcore/mod-skip-dk-starting-area)

## Description

- Skips the Death Knight starting zone, for those who want to make a Death Knight without having to go through the starting area.

## Tofu Worldview changes (branch `tofu`)

- The skip completes every obtainable quest of the Death Knight starting zone (57 quests, including the breadcrumbs, Abandoned Mail, The Power Of Blood, Frost And Unholy, Noth's Special Brew, and More Skulls For Brew), so they count as completed for the quest log history and quest-count achievements. Quests 12625 and 12626 are left out because no creature or object starts or ends them. A quest already in the log is turned in; a quest already turned in is skipped.
- The Lich King offers "I wish to skip the starting zone." next to his quest, with no confirmation box. Clicking it completes the zone, sets the level and trained spells, and then opens the client's own quest reward window for Tonight We Dine In Havenshire (12679, the cloak) and then for The Light of Dawn (12801, the two-hander). A `ServerScript::CanPacketReceive` hook handles the reward pick from those windows, because the Lich King is not those quests' ender. Only the picked items are given.
- After the weapon pick, the gold is reset, the weapon gets its rune, the hearthstone is set to Stormwind or Orgrimmar, and the player is teleported there 3 seconds later, so the client's item queries for the new rewards are answered before the loading screen (the core drops item queries while the player is between maps).
- The final quest, Where Kings Walk (13188) or Warchief's Blessing (13189), is left in the log to turn in to King Varian Wrynn or Thrall. The skip option disappears once that quest is in the log or turned in.
- Closing a reward window or logging out partway is safe: clicking the option again skips the quests already turned in and reopens the pending window.
- The click keeps gossip action 1, so a module that watches that action (the Tofu challenge-modes fork) still blocks it.
- The first-login automatic skip (`Skip.Deathknight.Starter.Enable`) has no windows and gives the first reward of each choice quest.

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
