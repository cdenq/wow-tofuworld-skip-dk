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
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "SharedDefines.h"
#include "World.h"
#include "WorldSession.h"
#include <iterator>
#include <string>
#include <vector>

constexpr auto YESSKIPDK = 1;

// Tofu Worldview: the skip completes every obtainable Ebon Hold quest and lets
// the player pick the cloak (quest 12679) and the two-hander (quest 12801)
// instead of handing out every choice.
namespace TofuSkipDK
{
    constexpr uint32 QUEST_CLOAK_CHOICE = 12679;  // Tonight We Dine In Havenshire
    constexpr uint32 QUEST_WEAPON_CHOICE = 12801; // The Light of Dawn

    // Gossip actions above the module's YESSKIPDK (1). The first click stays
    // action 1 so other modules that watch it (Tofu challenge modes) still see it.
    constexpr uint32 ACTION_CLOAK_BASE = GOSSIP_ACTION_INFO_DEF + 100; // + cloak index
    constexpr uint32 ACTION_SKIP_BASE = GOSSIP_ACTION_INFO_DEF + 200;  // + cloak index * 10 + weapon index

    // Every obtainable quest of the Death Knight starting zone, in chain order.
    // The race quest "A Special Surprise" and the faction quest at the end are
    // added per player. 12625 and 12626 are left out: no creature or object
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
}

void Azerothcore_skip_deathknight_HandleSkip(Player* player, uint32 cloakChoice = 0, uint32 weaponChoice = 0)
{
    //Not sure where DKs were supposed to pick this up from, leaving as the one manual add
    player->AddItem(6948, true); //Hearthstone

    uint32 specialSurpriseQuestId = 0;
    switch (player->getRace())
    {
    case RACE_TAUREN:
        specialSurpriseQuestId = 12739;
        break;
    case RACE_HUMAN:
        specialSurpriseQuestId = 12742;
        break;
    case RACE_NIGHTELF:
        specialSurpriseQuestId = 12743;
        break;
    case RACE_DWARF:
        specialSurpriseQuestId = 12744;
        break;
    case RACE_GNOME:
        specialSurpriseQuestId = 12745;
        break;
    case RACE_DRAENEI:
        specialSurpriseQuestId = 12746;
        break;
    case RACE_BLOODELF:
        specialSurpriseQuestId = 12747;
        break;
    case RACE_ORC:
        specialSurpriseQuestId = 12748;
        break;
    case RACE_TROLL:
        specialSurpriseQuestId = 12749;
        break;
    case RACE_UNDEAD_PLAYER:
        specialSurpriseQuestId = 12750;
        break;
    }

    std::vector<uint32> quests(std::begin(TofuSkipDK::STARTER_QUESTS), std::end(TofuSkipDK::STARTER_QUESTS));
    if (specialSurpriseQuestId)
        quests.push_back(specialSurpriseQuestId);
    else
        LOG_ERROR("module", "Skip DK: no A Special Surprise quest for player {} (race {})", player->GetName(), player->getRace());
    quests.insert(quests.end(), std::begin(TofuSkipDK::FINAL_QUESTS), std::end(TofuSkipDK::FINAL_QUESTS));
    quests.push_back(player->GetTeamId() == TEAM_ALLIANCE ? 13188 : 13189);

    for (uint32 questId : quests)
    {
        // A quest can be missing from the world database.
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
        {
            LOG_ERROR("module", "Skip DK: quest {} does not exist, not rewarding it to player {} (race {})",
                questId, player->GetName(), player->getRace());
            continue;
        }

        // Already turned in: nothing to do.
        if (player->IsQuestRewarded(questId))
            continue;

        uint32 choice = 0;
        if (questId == TofuSkipDK::QUEST_CLOAK_CHOICE)
            choice = TofuSkipDK::ValidChoice(quest, cloakChoice);
        else if (questId == TofuSkipDK::QUEST_WEAPON_CHOICE)
            choice = TofuSkipDK::ValidChoice(quest, weaponChoice);

        // A quest already in the log is turned in as it is; any other quest is
        // taken first. Either way it counts as completed (quest log history,
        // quest-count achievements).
        if (player->GetQuestStatus(questId) == QUEST_STATUS_NONE)
            player->AddQuest(quest, nullptr);
        player->RewardQuest(quest, choice, player, false);
    }

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

    //Don't need to save all players, just current
    player->SaveToDB(false, false);

    WorldLocation Aloc = WorldLocation(0, -8866.55f, 671.39f, 97.90f, 5.27f);// Stormwind
    WorldLocation Hloc = WorldLocation(1, 1637.62f, -4440.22f, 15.78f, 2.42f);// Orgrimmar

    if (player->GetTeamId() == TEAM_ALLIANCE)
    {
        player->TeleportTo(0, -8833.37f, 628.62f, 94.00f, 1.06f);//Stormwind
        player->SetHomebind(Aloc, 1519);// Stormwind Homebind location
    }
    else
    {
        player->TeleportTo(1, 1569.59f, -4397.63f, 7.70f, 0.54f);//Orgrimmar
        player->SetHomebind(Hloc, 1637);// Orgrimmar Homebind location
    }

    if (sConfigMgr->GetOption<bool>("DeleteGold.Deathknight.Optional.Enable", true))
    {
        int DKM = sConfigMgr->GetOption<int32>("StartHeroicPlayerMoney", 2000);
        player->SetMoney(DKM);
    }

    auto ApplyEnchantment = [&](uint32 enchantId, Item* item, Player* player)
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
                ApplyEnchantment(enchantId, item, player);
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

#define LOCALE_LICHKING_0 "I wish to skip the Death Knight starter questline."
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
            if (creature->IsQuestGiver())
                player->PrepareQuestMenu(creature->GetGUID());

            if (sConfigMgr->GetOption<bool>("Skip.Deathknight.Optional.Enable", true))
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
                AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, localizedEntry, GOSSIP_SENDER_MAIN, YESSKIPDK);
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

            if (action == YESSKIPDK)
            {
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
                Azerothcore_skip_deathknight_HandleSkip(player, code / 10, code % 10);
                return true;
            }

            CloseGossipMenuFor(player);
            return true;
        }

private:
        static void ShowCloakMenu(Player* player, Creature* creature)
        {
            ClearGossipMenuFor(player);
            std::vector<uint32> cloaks = TofuSkipDK::ChoiceIndexes(TofuSkipDK::QUEST_CLOAK_CHOICE);
            if (cloaks.empty())
            {
                // No choices in the database: go straight to the weapon menu.
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
            std::string cloakText = cloakItem ? TofuSkipDK::ItemName(cloakItem) + " and " : "";

            std::vector<uint32> weapons = TofuSkipDK::ChoiceIndexes(TofuSkipDK::QUEST_WEAPON_CHOICE);
            if (weapons.empty())
                weapons.push_back(0);

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
