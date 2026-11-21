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
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <iterator>
#include <string>
#include <vector>

constexpr auto YESSKIPDK = 1;

// Tofu Worldview: the skip completes every obtainable Ebon Hold quest. The
// cloak quest (12679) and the weapon quest (12801) are turned in through the
// client's own quest reward window, opened from the Lich King, so the player
// picks the reward there. The final quest (13188 or 13189) is left in the log
// to turn in to King Varian Wrynn or Thrall.
namespace TofuSkipDK
{
    constexpr uint32 NPC_LICH_KING = 25462;
    constexpr uint32 ITEM_HEARTHSTONE = 6948;
    constexpr uint32 QUEST_CLOAK_CHOICE = 12679;  // Tonight We Dine In Havenshire
    constexpr uint32 QUEST_WEAPON_CHOICE = 12801; // The Light of Dawn
    constexpr uint32 QUEST_FINAL_ALLIANCE = 13188; // Where Kings Walk (King Varian Wrynn)
    constexpr uint32 QUEST_FINAL_HORDE = 13189;    // Warchief's Blessing (Thrall)
    constexpr uint32 TELEPORT_DELAY_MS = 3000;

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

    // The two quests whose reward the player picks, in the order they are offered.
    constexpr uint32 CHOICE_QUESTS[] = { QUEST_CLOAK_CHOICE, QUEST_WEAPON_CHOICE };

    bool IsChoiceQuest(uint32 questId)
    {
        return questId == QUEST_CLOAK_CHOICE || questId == QUEST_WEAPON_CHOICE;
    }

    uint32 FinalQuest(Player* player)
    {
        return player->GetTeamId() == TEAM_ALLIANCE ? QUEST_FINAL_ALLIANCE : QUEST_FINAL_HORDE;
    }

    // The skip is finished once the final quest is in the log or turned in.
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
        return quests;
    }

    // Puts a quest in the log as ready to turn in. Returns false if the quest
    // log is full.
    bool MakeReadyToTurnIn(Player* player, Quest const* quest)
    {
        uint32 questId = quest->GetQuestId();
        if (player->GetQuestStatus(questId) == QUEST_STATUS_NONE)
            player->AddQuest(quest, nullptr);
        if (player->GetQuestStatus(questId) == QUEST_STATUS_NONE)
            return false;
        if (player->GetQuestStatus(questId) != QUEST_STATUS_COMPLETE)
            player->CompleteQuest(questId);
        return true;
    }

    // Turns in every starting quest except the two choice quests, which are
    // put in the log as ready to turn in (or, when autoChoose is set, turned in
    // with their first reward). Also gives the Hearthstone, the level, and the
    // trained spells. Returns false if the quest log is full.
    bool CompleteQuests(Player* player, bool autoChoose)
    {
        if (!player->HasItemCount(ITEM_HEARTHSTONE, 1, true))
            player->AddItem(ITEM_HEARTHSTONE, 1);

        bool logFull = false;
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

            if (IsChoiceQuest(questId) && !autoChoose)
            {
                if (!MakeReadyToTurnIn(player, quest))
                    logFull = true;
                continue;
            }

            // A quest already in the log is turned in as it is; any other quest
            // is taken first. Either way it counts as completed (quest log
            // history, quest-count achievements).
            if (player->GetQuestStatus(questId) == QUEST_STATUS_NONE)
                player->AddQuest(quest, nullptr);
            player->RewardQuest(quest, 0, player, false);
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

        player->SaveToDB(false, false);
        return !logFull;
    }

    // Gold, weapon enchantment, homebind, the final quest, and the teleport to
    // the faction capital. The teleport waits a few seconds so the client's item
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

        // The final quest stays in the log for the faction leader to turn in.
        uint32 finalQuestId = FinalQuest(player);
        if (Quest const* finalQuest = sObjectMgr->GetQuestTemplate(finalQuestId))
        {
            if (!player->IsQuestRewarded(finalQuestId) && player->GetQuestStatus(finalQuestId) == QUEST_STATUS_NONE)
                player->AddQuest(finalQuest, nullptr);
        }
        else
            LOG_ERROR("module", "Skip DK: quest {} does not exist, not giving it to player {}", finalQuestId, player->GetName());

        player->SaveToDB(false, false);

        std::string city = alliance ? "Stormwind" : "Orgrimmar";
        std::string leader = alliance ? "King Varian Wrynn" : "Thrall";
        ChatHandler(player->GetSession()).PSendSysMessage("Sending you to {} in {} seconds. Turn in your last quest to {}.", city, TELEPORT_DELAY_MS / 1000, leader);

        // The event belongs to the player, so it is dropped if the player logs
        // out first; the homebind is already set.
        player->m_Events.AddEventAtOffset([player, alliance]
        {
            if (!player->IsInWorld() || player->IsBeingTeleported())
                return;
            if (alliance)
                player->TeleportTo(0, -8833.37f, 628.62f, 94.00f, 1.06f); // Stormwind
            else
                player->TeleportTo(1, 1569.59f, -4397.63f, 7.70f, 0.54f); // Orgrimmar
        }, Milliseconds(TELEPORT_DELAY_MS));
    }

    // Opens the quest reward window for the next choice quest that is not yet
    // turned in, or finishes the skip when both are done.
    void ContinueSkip(Player* player, Creature* lichKing)
    {
        for (uint32 questId : CHOICE_QUESTS)
        {
            if (player->IsQuestRewarded(questId))
                continue;

            Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
            if (!quest)
                continue;

            if (!MakeReadyToTurnIn(player, quest))
            {
                ChatHandler(player->GetSession()).SendSysMessage("Your quest log is full. Abandon a quest and talk to the Lich King again.");
                return;
            }

            player->PlayerTalkClass->SendQuestGiverOfferReward(quest, lichKing->GetGUID(), true);
            return;
        }

        FinishSkip(player);
    }
}

// First-login automatic skip: no windows, first reward of each choice quest.
void Azerothcore_skip_deathknight_HandleSkip(Player* player)
{
    TofuSkipDK::CompleteQuests(player, true);
    TofuSkipDK::FinishSkip(player);
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

#define LOCALE_LICHKING_0 "I wish to skip the starting zone."
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

            if (sConfigMgr->GetOption<bool>("Skip.Deathknight.Optional.Enable", true) && !TofuSkipDK::IsSkipFinished(player))
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
                // Tofu: no confirmation box; the reward windows follow.
                AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, localizedEntry, GOSSIP_SENDER_MAIN, YESSKIPDK);
            }
            player->TalkedToCreature(creature->GetEntry(), creature->GetGUID());
            SendGossipMenuFor(player, player->GetGossipTextId(creature), creature->GetGUID());
            return true;
        }

        bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
        {
            CloseGossipMenuFor(player);

            if (sender != GOSSIP_SENDER_MAIN || action != YESSKIPDK)
                return true;

            if (!sConfigMgr->GetOption<bool>("Skip.Deathknight.Optional.Enable", true) || TofuSkipDK::IsSkipFinished(player))
                return true;

            // Clicking again after closing a reward window resumes: turned-in
            // quests are skipped and the pending window opens again.
            if (!TofuSkipDK::CompleteQuests(player, false))
            {
                ChatHandler(player->GetSession()).SendSysMessage("Your quest log is full. Abandon a quest and talk to the Lich King again.");
                return true;
            }
            TofuSkipDK::ContinueSkip(player, creature);
            return true;
        }
};

// The reward windows are opened from the Lich King, who is not the quest ender
// of 12679 or 12801, so the core's CMSG_QUESTGIVER_CHOOSE_REWARD handler would
// ignore the player's pick. This hook handles that pick instead and drops the
// packet. Any other reward pick goes to the core as usual. The hook runs in the
// world update, where the core's own handler runs.
class Azerothcore_skip_deathknight_reward_choice : public ServerScript
{
public:
    Azerothcore_skip_deathknight_reward_choice() : ServerScript("Azerothcore_skip_deathknight_reward_choice", {
        SERVERHOOK_CAN_PACKET_RECEIVE
    }) { }

    bool CanPacketReceive(WorldSession* session, WorldPacket const& packet) override
    {
        if (packet.GetOpcode() != CMSG_QUESTGIVER_CHOOSE_REWARD)
            return true;

        Player* player = session ? session->GetPlayer() : nullptr;
        if (!player || !player->IsInWorld())
            return true;

        ObjectGuid guid;
        uint32 questId = 0;
        uint32 reward = 0;
        try
        {
            WorldPacket data(packet);
            data >> guid >> questId >> reward;
        }
        catch (ByteBufferException const&)
        {
            return true; // let the core report the malformed packet
        }

        if (!TofuSkipDK::IsChoiceQuest(questId) || !guid.IsCreature())
            return true;

        Creature* lichKing = ObjectAccessor::GetCreature(*player, guid);
        if (!lichKing || lichKing->GetEntry() != TofuSkipDK::NPC_LICH_KING)
            return true;

        // From here on the pick belongs to the skip; the core would ignore it.
        if (reward >= QUEST_REWARD_CHOICES_COUNT || !player->CanInteractWithQuestGiver(lichKing))
            return false;

        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest || player->IsQuestRewarded(questId) || player->GetQuestStatus(questId) != QUEST_STATUS_COMPLETE)
            return false;

        // Checks bag space and reports a full inventory to the player.
        if (!player->CanRewardQuest(quest, reward, true))
            return false;

        player->RewardQuest(quest, reward, lichKing);
        TofuSkipDK::ContinueSkip(player, lichKing);
        return false;
    }
};

void AddSkipDKScripts()
{
    new AzerothCore_skip_deathknight_announce;
    new AzerothCore_skip_deathknight;
    new Azerothcore_optional_deathknight_skip;
    new Azerothcore_skip_deathknight_reward_choice;
}
