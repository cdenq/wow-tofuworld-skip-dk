/*
 *MIT License
 *
 *Copyright (c) 2023 Azerothcore
 *
 *Permission is hereby granted, free of charge, to any person obtaining a copy
 *of this software and associated documentation files (the "Software"), to deal
 *in the Software without restriction, including without limitation the rights
 *to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *copies of the Software, and to permit persons to whom the Software is
 *furnished to do so, subject to the following conditions:
 *
 *The above copyright notice and this permission notice shall be included in all
 *copies or substantial portions of the Software.
 *
 *THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *SOFTWARE.
 *
 * Skip Death Knight Module
 * Original Module From Single Player Project Consolidated Skip Module
 * Rewritten for TC 335 By Single Player Project Developer MDic
 * Original Concept from conanhun513
 * Assistance and Review by JinnaiX
 * Modified for Azerothcore
 */

#include "AccountMgr.h"
#include "Chat.h"
#include "Common.h"
#include "Config.h"
#include "Creature.h"
#include "GossipDef.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "SharedDefines.h"
#include "StringFormat.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <iterator>
#include <string>
#include <vector>

constexpr auto YESSKIPDK = 1;

// Tofu Worldview: the skip completes every obtainable Ebon Hold quest. The
// player picks the cloak (quest 12679) and the two-hander (quest 12801) from
// gossip menus on the Lich King, and nothing is given until the weapon pick is
// confirmed, so a half-finished skip leaves no half-finished character. The
// final quest (13188 or 13189) is turned in too, so its reputation with
// Stormwind or Orgrimmar and its XP are applied. While the skip is available
// the Lich King shows only his gossip text and the skip option, no quest
// entries, so client addons that auto-click quests have nothing to click.
namespace TofuSkipDK
{
    constexpr uint32 NPC_LICH_KING = 25462;
    constexpr uint32 ITEM_HEARTHSTONE = 6948;
    constexpr uint32 QUEST_CLOAK_CHOICE = 12679;  // Tonight We Dine In Havenshire
    constexpr uint32 QUEST_WEAPON_CHOICE = 12801; // The Light of Dawn
    constexpr uint32 QUEST_FINAL_ALLIANCE = 13188; // Where Kings Walk (King Varian Wrynn)
    constexpr uint32 QUEST_FINAL_HORDE = 13189;    // Warchief's Blessing (Thrall)
    constexpr uint32 TELEPORT_DELAY_MS = 3000;

    // Gossip actions above the module's YESSKIPDK (1). The first click stays
    // action 1 so modules that watch it (the Tofu challenge-modes fork, which
    // blocks actions 1 and 1100 to 1299) still see it.
    constexpr uint32 ACTION_CLOAK_BASE = GOSSIP_ACTION_INFO_DEF + 100; // + cloak index
    constexpr uint32 ACTION_SKIP_BASE = GOSSIP_ACTION_INFO_DEF + 200;  // + cloak index * 10 + weapon index

    // Every obtainable quest of the Death Knight starting zone, in chain order,
    // the cloak and weapon quests included. The race quest "A Special Surprise"
    // is added per player. 12625 and 12626 are left out: no creature or object
    // starts or ends them.
    constexpr uint32 STARTER_QUESTS[] =
    {
        12593, 12619, 12842, 12848, 12636, 12641, 12657, 12849, 12850, 12670,
        12678, 12679, 12680, 12687, 12733, 12711, 12697, 12698, 12700, 12701,
        12706, 12714, 12715, 12716, 12717, 12718, 12719, 12720, 12722, 12723,
        12724, 12725, 12727, 12738
    };

    constexpr uint32 FINAL_QUESTS[] =
    {
        12751, 12754, 12755, 12756, 12757, 12778, 12779, 12800, 12801, 13165,
        13166
    };

    uint32 FinalQuest(Player* player)
    {
        return player->GetTeamId() == TEAM_ALLIANCE ? QUEST_FINAL_ALLIANCE : QUEST_FINAL_HORDE;
    }

    // The skip is finished once the final quest is turned in (or, for a
    // character from an older build, still in the log).
    bool IsSkipFinished(Player* player)
    {
        return player->GetQuestStatus(FinalQuest(player)) != QUEST_STATUS_NONE || player->IsQuestRewarded(FinalQuest(player));
    }

    uint32 SpecialSurpriseQuest(Player* player)
    {
        switch (player->getRace())
        {
            case RACE_TAUREN: return 12739;
            case RACE_HUMAN: return 12742;
            case RACE_NIGHTELF: return 12743;
            case RACE_DWARF: return 12744;
            case RACE_GNOME: return 12745;
            case RACE_DRAENEI: return 12746;
            case RACE_BLOODELF: return 12747;
            case RACE_ORC: return 12748;
            case RACE_TROLL: return 12749;
            case RACE_UNDEAD_PLAYER: return 12750;
            default: return 0;
        }
    }

    std::vector<uint32> AllQuests(Player* player)
    {
        std::vector<uint32> quests(std::begin(STARTER_QUESTS), std::end(STARTER_QUESTS));
        if (uint32 surprise = SpecialSurpriseQuest(player))
            quests.push_back(surprise);
        else
            LOG_ERROR("module", "Skip DK: no A Special Surprise quest for player {} (race {})", player->GetName(), player->getRace());
        quests.insert(quests.end(), std::begin(FINAL_QUESTS), std::end(FINAL_QUESTS));
        // Last, so the level is set after the XP of every turn-in.
        quests.push_back(FinalQuest(player));
        return quests;
    }

    // Returns the choice index to use: the requested one if that slot holds an
    // item, otherwise 0 (the first choice).
    uint32 ValidChoice(Quest const* quest, uint32 choice)
    {
        if (choice < QUEST_REWARD_CHOICES_COUNT && quest->RewardChoiceItemId[choice])
            return choice;
        return 0;
    }

    // Choice indexes of a quest that hold an item.
    std::vector<uint32> ChoiceIndexes(uint32 questId)
    {
        std::vector<uint32> indexes;
        if (Quest const* quest = sObjectMgr->GetQuestTemplate(questId))
            for (uint32 i = 0; i < QUEST_REWARD_CHOICES_COUNT; ++i)
                if (quest->RewardChoiceItemId[i])
                    indexes.push_back(i);
        return indexes;
    }

    uint32 ChoiceItem(uint32 questId, uint32 index)
    {
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest || index >= QUEST_REWARD_CHOICES_COUNT)
            return 0;
        return quest->RewardChoiceItemId[index];
    }

    std::string ItemName(uint32 itemId)
    {
        if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId))
            return proto->Name1;
        return "item " + std::to_string(itemId);
    }

    // Chat link the player can hover to see the item's tooltip.
    std::string ItemLink(uint32 itemId)
    {
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
        if (!proto)
            return ItemName(itemId);
        uint32 quality = proto->Quality < MAX_ITEM_QUALITY ? proto->Quality : 0;
        return Acore::StringFormat("|c{:08x}|Hitem:{}:0:0:0:0:0:0:0:0|h[{}]|h|r", ItemQualityColors[quality], itemId, proto->Name1);
    }

    // Turns in every starting quest, the cloak and weapon quests with the given
    // choices. A quest in the log is turned in as it is; a quest turned in
    // before is skipped. Also gives the Hearthstone, the level, and the trained
    // spells.
    void CompleteQuests(Player* player, uint32 cloakChoice, uint32 weaponChoice)
    {
        if (!player->HasItemCount(ITEM_HEARTHSTONE, 1, true))
            player->AddItem(ITEM_HEARTHSTONE, 1);

        uint32 turnedIn = 0;
        for (uint32 questId : AllQuests(player))
        {
            // A quest can be missing from the world database.
            Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
            if (!quest)
            {
                LOG_ERROR("module", "Skip DK: quest {} does not exist, not rewarding it to player {} (race {})", questId, player->GetName(), player->getRace());
                continue;
            }

            if (player->IsQuestRewarded(questId))
                continue;

            uint32 choice = 0;
            if (questId == QUEST_CLOAK_CHOICE)
                choice = ValidChoice(quest, cloakChoice);
            else if (questId == QUEST_WEAPON_CHOICE)
                choice = ValidChoice(quest, weaponChoice);

            // A quest already in the log is turned in as it is; any other quest
            // is taken first. Either way it counts as completed (quest log
            // history, quest-count achievements).
            if (player->GetQuestStatus(questId) == QUEST_STATUS_NONE)
                player->AddQuest(quest, nullptr);
            player->RewardQuest(quest, choice, player, false);
            ++turnedIn;

            if (!player->IsQuestRewarded(questId))
                LOG_ERROR("module", "Skip DK: quest {} is not rewarded after RewardQuest for player {}", questId, player->GetName());
        }

        LOG_INFO("module", "Skip DK: {} turned in {} quests (cloak choice {}, weapon choice {})", player->GetName(), turnedIn, cloakChoice, weaponChoice);

        int DKL = sConfigMgr->GetOption<float>("Skip.Deathknight.Start.Level", 58);
        if (player->GetLevel() <= DKL)
        {
            //GiveLevel updates character properties more thoroughly than SetLevel
            player->GiveLevel(DKL);
        }

        if (sConfigMgr->GetOption<bool>("Skip.Deathknight.Start.Trained", true))
        {
            player->addSpell(49998, SPEC_MASK_ALL, true); // Death Strike rank 1
            player->addSpell(47528, SPEC_MASK_ALL, true); // Mind Freeze
            player->addSpell(46584, SPEC_MASK_ALL, true); // Raise Dead
            player->addSpell(45524, SPEC_MASK_ALL, true); // Chains of Ice
            player->addSpell(48263, SPEC_MASK_ALL, true); // Frost Presence
            player->addSpell(50842, SPEC_MASK_ALL, true); // Pestilence
            player->addSpell(53342, SPEC_MASK_ALL, true); // Rune of Spellshattering
            player->addSpell(48721, SPEC_MASK_ALL, true); // Blood Boil rank 1
            player->addSpell(54447, SPEC_MASK_ALL, true); // Rune of Spellbreaking
        }

        player->SaveToDB(false, false);
    }

    // Gold, weapon enchantment, homebind, and the teleport to the faction
    // capital. The teleport waits a few seconds so the client's item
    // queries for the new rewards are answered before the loading screen; the
    // server drops item queries while the player is between maps.
    void FinishSkip(Player* player)
    {
        if (sConfigMgr->GetOption<bool>("DeleteGold.Deathknight.Optional.Enable", true))
        {
            int DKM = sConfigMgr->GetOption<int32>("StartHeroicPlayerMoney", 2000);
            player->SetMoney(DKM);
        }

        auto ApplyEnchantment = [&](uint32 enchantId, Item* item)
        {
            // Session may be null, so write enchantment fields directly
            uint32 base = ITEM_FIELD_ENCHANTMENT_1_1 + PERM_ENCHANTMENT_SLOT * MAX_ENCHANTMENT_OFFSET;
            item->SetUInt32Value(base + ENCHANTMENT_ID_OFFSET, enchantId);
            item->SetUInt32Value(base + ENCHANTMENT_DURATION_OFFSET, 0);
            item->SetUInt32Value(base + ENCHANTMENT_CHARGES_OFFSET, 0);
            item->SetState(ITEM_CHANGED, player);
        };

        if (uint32 enchantId = sConfigMgr->GetOption<uint32>("Skip.Deathknight.Start.WeaponEnchantment", 3369)) // Rune of Cinderglacier
            for (uint32 itemId : {38632, 38633, 38707}) // Greatsword/Greataxe of the Ebon Blade, Runed Soulblade
                if (Item* item = player->GetItemByEntry(itemId))
                    ApplyEnchantment(enchantId, item);

        bool alliance = player->GetTeamId() == TEAM_ALLIANCE;
        if (alliance)
            player->SetHomebind(WorldLocation(0, -8866.55f, 671.39f, 97.90f, 5.27f), 1519); // Stormwind
        else
            player->SetHomebind(WorldLocation(1, 1637.62f, -4440.22f, 15.78f, 2.42f), 1637); // Orgrimmar

        player->SaveToDB(false, false);

        std::string city = alliance ? "Stormwind" : "Orgrimmar";
        ChatHandler(player->GetSession()).PSendSysMessage("Sending you to {} in {} seconds.", city, TELEPORT_DELAY_MS / 1000);
        LOG_INFO("module", "Skip DK: {} finished the skip, teleport to {} in {} ms", player->GetName(), city, TELEPORT_DELAY_MS);

        // The event belongs to the player, so it is dropped if the player logs
        // out first; the homebind is already set.
        player->m_Events.AddEventAtOffset([player, alliance]
        {
            if (!player->IsInWorld() || player->IsBeingTeleported())
            {
                LOG_INFO("module", "Skip DK: teleport of {} skipped (in world {}, being teleported {})", player->GetName(), player->IsInWorld(), player->IsBeingTeleported());
                return;
            }

            bool ok = alliance ? player->TeleportTo(0, -8833.37f, 628.62f, 94.00f, 1.06f)  // Stormwind
                               : player->TeleportTo(1, 1569.59f, -4397.63f, 7.70f, 0.54f); // Orgrimmar
            if (!ok)
                LOG_ERROR("module", "Skip DK: TeleportTo failed for {}", player->GetName());
        }, Milliseconds(TELEPORT_DELAY_MS));
    }

    void RunSkip(Player* player, uint32 cloakChoice, uint32 weaponChoice)
    {
        CompleteQuests(player, cloakChoice, weaponChoice);
        FinishSkip(player);
    }
}

// First-login automatic skip: first reward of each choice quest.
void Azerothcore_skip_deathknight_HandleSkip(Player* player)
{
    TofuSkipDK::RunSkip(player, 0, 0);
}

class AzerothCore_skip_deathknight_announce : public PlayerScript
{
public:
    AzerothCore_skip_deathknight_announce() : PlayerScript("AzerothCore_skip_deathknight_announce", {
        PLAYERHOOK_ON_LOGIN
    }) { }

    void OnPlayerLogin(Player* Player)
    {
        if (sConfigMgr->GetOption<bool>("Skip.Deathknight.Starter.Announce.enable", true) && (sConfigMgr->GetOption<bool>("Skip.Deathknight.Starter.Enable", true) || sConfigMgr->GetOption<bool>("Skip.Deathknight.Optional.Enable", true)))
            ChatHandler(Player->GetSession()).SendSysMessage("This server is running the |cff4CFF00Azerothcore Skip Deathknight Starter |rmodule.");
    }
};

class AzerothCore_skip_deathknight : public PlayerScript
{
public:
    AzerothCore_skip_deathknight() : PlayerScript("AzerothCore_skip_deathknight", {
        PLAYERHOOK_ON_FIRST_LOGIN
    }) { }

    void OnPlayerFirstLogin(Player* player) override
    {
        if (player->GetAreaId() == 4342)
        {
            //These changes make it so user mistakes in the configuration file don't cause this to run 2x
            if ((sConfigMgr->GetOption<bool>("Skip.Deathknight.Starter.Enable", true) && player->GetSession()->GetSecurity() == SEC_PLAYER)
                || (sConfigMgr->GetOption<bool>("GM.Skip.Deathknight.Starter.Enable", false) && player->GetSession()->GetSecurity() >= SEC_MODERATOR))
            {
                Azerothcore_skip_deathknight_HandleSkip(player);
            }
        }
    }
};

#define LOCALE_LICHKING_0 "Skip DK zone"
#define LOCALE_LICHKING_1 "죽음의 기사 스타터 퀘스트 라인을 건너뛰고 싶습니다."
#define LOCALE_LICHKING_2 "Je souhaite sauter la série de quêtes de démarrage du Chevalier de la mort."
#define LOCALE_LICHKING_3 "Ich möchte die Todesritter-Starter-Questreihe überspringen."
#define LOCALE_LICHKING_4 "我想跳過死亡騎士新手任務線。"
#define LOCALE_LICHKING_5 "我想跳過死亡騎士新手任務線。"
#define LOCALE_LICHKING_6 "Deseo saltarme la línea de misiones de inicio del Caballero de la Muerte."
#define LOCALE_LICHKING_7 "Deseo saltarme la línea de misiones de inicio del Caballero de la Muerte."
#define LOCALE_LICHKING_8 "Я хочу пропустить начальную цепочку заданий Рыцаря Смерти."

class Azerothcore_optional_deathknight_skip : public CreatureScript
{
public:
    Azerothcore_optional_deathknight_skip() : CreatureScript("npc_ac_skip_lich") { }

        bool OnGossipHello(Player* player, Creature* creature) override
        {
            bool skipAvailable = sConfigMgr->GetOption<bool>("Skip.Deathknight.Optional.Enable", true) && !TofuSkipDK::IsSkipFinished(player);

            // Tofu: no quest entries while the skip is available. Addons such
            // as Jamba auto-click quest entries, and the skip completes those
            // quests anyway.
            if (!skipAvailable && creature->IsQuestGiver())
                player->PrepareQuestMenu(creature->GetGUID());

            if (skipAvailable)
            {
                char const* localizedEntry;
                switch (player->GetSession()->GetSessionDbcLocale())
                {
                case LOCALE_koKR: localizedEntry = LOCALE_LICHKING_1; break;
                case LOCALE_frFR: localizedEntry = LOCALE_LICHKING_2; break;
                case LOCALE_deDE: localizedEntry = LOCALE_LICHKING_3; break;
                case LOCALE_zhCN: localizedEntry = LOCALE_LICHKING_4; break;
                case LOCALE_zhTW: localizedEntry = LOCALE_LICHKING_5; break;
                case LOCALE_esES: localizedEntry = LOCALE_LICHKING_6; break;
                case LOCALE_esMX: localizedEntry = LOCALE_LICHKING_7; break;
                case LOCALE_ruRU: localizedEntry = LOCALE_LICHKING_8; break;
                case LOCALE_enUS: localizedEntry = LOCALE_LICHKING_0; break;
                default: localizedEntry = LOCALE_LICHKING_0;
                }
                // Tofu: no confirmation here; the cloak and weapon menus follow,
                // and the weapon pick carries the confirmation.
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, localizedEntry, GOSSIP_SENDER_MAIN, YESSKIPDK);
            }
            player->TalkedToCreature(creature->GetEntry(), creature->GetGUID());
            SendGossipMenuFor(player, player->GetGossipTextId(creature), creature->GetGUID());
            return true;
        }

        // Every action below is server-side: the client only sends the index of
        // an option the server put in its current menu, so the cloak and weapon
        // indexes cannot be forged.
        bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
        {
            if (sender != GOSSIP_SENDER_MAIN)
                return true;

            if (!sConfigMgr->GetOption<bool>("Skip.Deathknight.Optional.Enable", true) || TofuSkipDK::IsSkipFinished(player))
            {
                CloseGossipMenuFor(player);
                return true;
            }

            if (action == YESSKIPDK)
            {
                LOG_INFO("module", "Skip DK: {} chose to skip the starting zone", player->GetName());
                ShowCloakMenu(player, creature);
                return true;
            }

            if (action >= TofuSkipDK::ACTION_CLOAK_BASE && action < TofuSkipDK::ACTION_SKIP_BASE)
            {
                ShowWeaponMenu(player, creature, action - TofuSkipDK::ACTION_CLOAK_BASE);
                return true;
            }

            if (action >= TofuSkipDK::ACTION_SKIP_BASE && action < TofuSkipDK::ACTION_SKIP_BASE + 100)
            {
                uint32 code = action - TofuSkipDK::ACTION_SKIP_BASE;
                CloseGossipMenuFor(player);
                TofuSkipDK::RunSkip(player, code / 10, code % 10);
                return true;
            }

            CloseGossipMenuFor(player);
            return true;
        }

private:
        // The cloak menu. With nothing to choose (no choice items, or the cloak
        // quest was turned in earlier by an older build) it goes straight to the
        // weapon menu.
        static void ShowCloakMenu(Player* player, Creature* creature)
        {
            ClearGossipMenuFor(player);
            std::vector<uint32> cloaks = TofuSkipDK::ChoiceIndexes(TofuSkipDK::QUEST_CLOAK_CHOICE);
            if (cloaks.empty() || player->IsQuestRewarded(TofuSkipDK::QUEST_CLOAK_CHOICE))
            {
                // Nothing to choose: go straight to the weapon menu.
                ShowWeaponMenu(player, creature, 0);
                return;
            }

            std::string links;
            for (uint32 index : cloaks)
            {
                uint32 itemId = TofuSkipDK::ChoiceItem(TofuSkipDK::QUEST_CLOAK_CHOICE, index);
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Cloak: " + TofuSkipDK::ItemName(itemId), GOSSIP_SENDER_MAIN, TofuSkipDK::ACTION_CLOAK_BASE + index);
                links += " " + TofuSkipDK::ItemLink(itemId);
            }
            ChatHandler(player->GetSession()).PSendSysMessage("Choose your cloak:{}", links);
            SendGossipMenuFor(player, player->GetGossipTextId(creature), creature->GetGUID());
        }

        static void ShowWeaponMenu(Player* player, Creature* creature, uint32 cloakIndex)
        {
            ClearGossipMenuFor(player);
            std::string city = player->GetTeamId() == TEAM_ALLIANCE ? "Stormwind" : "Orgrimmar";
            uint32 cloakItem = TofuSkipDK::ChoiceItem(TofuSkipDK::QUEST_CLOAK_CHOICE, cloakIndex);
            std::string cloakText = (cloakItem && !player->IsQuestRewarded(TofuSkipDK::QUEST_CLOAK_CHOICE)) ? TofuSkipDK::ItemName(cloakItem) + " and " : "";

            std::vector<uint32> weapons = TofuSkipDK::ChoiceIndexes(TofuSkipDK::QUEST_WEAPON_CHOICE);
            if (weapons.empty() || player->IsQuestRewarded(TofuSkipDK::QUEST_WEAPON_CHOICE))
            {
                // Nothing to choose (no choice items, or the weapon quest was
                // turned in earlier): one confirmation, then the skip.
                weapons.clear();
                weapons.push_back(0);
            }

            std::string links;
            for (uint32 index : weapons)
            {
                uint32 itemId = TofuSkipDK::ChoiceItem(TofuSkipDK::QUEST_WEAPON_CHOICE, index);
                std::string name = itemId ? TofuSkipDK::ItemName(itemId) : "the default reward";
                std::string popup = "Skip the starting zone with " + cloakText + name + "? Every starting quest is completed and you go to " + city + ".";
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Weapon: " + name, GOSSIP_SENDER_MAIN, TofuSkipDK::ACTION_SKIP_BASE + cloakIndex * 10 + index, popup, 0, false);
                if (itemId)
                    links += " " + TofuSkipDK::ItemLink(itemId);
            }
            if (!links.empty())
                ChatHandler(player->GetSession()).PSendSysMessage("Choose your weapon:{}", links);
            SendGossipMenuFor(player, player->GetGossipTextId(creature), creature->GetGUID());
        }
};

void AddSkipDKScripts()
{
    new AzerothCore_skip_deathknight_announce;
    new AzerothCore_skip_deathknight;
    new Azerothcore_optional_deathknight_skip;
}
