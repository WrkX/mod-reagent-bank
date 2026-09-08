/*
 * Material Storage scripts: banker gossip, gossip-select intercept, access
 * context, and RBANK addon C2S/S2C. Tortoise/Turtle 1.12 (MaNGOS-family).
 */

#include "ScriptObjects.h"
#include "Player.h"
#include "Creature.h"
#include "Item.h"
#include "Bag.h"
#include "GossipDef.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "ObjectGuid.h"
#include "Opcodes.h"
#include "SharedDefines.h"
#include "Log.h"
#include "Map.h"
#include "MapManager.h"
#include "Timer.h"
#include "ObjectMgr.h"
#include "Database/DatabaseEnv.h"

#include "ReagentBankConfig.h"
#include "ReagentBankProtocol.h"
#include "ReagentBankStore.h"

#include <algorithm>
#include <deque>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    constexpr uint32 kSender = 0x52424B01u; // 'RBK' + 1
    constexpr uint32 kAction = 0x52420001u; // not a GOSSIP_OPTION_* value
    constexpr std::size_t kReplayCacheCapacity = 32;

    // Cache response *metadata*, not complete snapshots. A max-size snapshot is
    // large enough that retaining 32 payload copies per open player would be an
    // avoidable memory sink. Replays regenerate a snapshot at the current
    // context revision after returning the original result code.
    struct ReplayEntry
    {
        uint32 requestId = 0;
        bool sendResult = false;
        ReagentBank::ResultCode result = ReagentBank::ResultCode::BadRequest;
        std::string detail;
        bool sendSnapshot = false;
    };

    struct AccessContext
    {
        ObjectGuid bankerGuid;
        uint32 openedAt = 0;
        bool hasNewestRequestId = false;
        uint32 newestRequestId = 0;
        uint32 revision = 0;
        uint32 lastCommandAt = 0;
        uint32 lastMutationAt = 0;
        std::deque<ReplayEntry> replayCache;
    };

    std::mutex g_accessMutex;
    std::unordered_map<uint32, AccessContext> g_accessByPlayer;

    void SendS2C(Player* player, std::string const& payload)
    {
        if (!player)
            return;
        player->SendAddonMessage(ReagentBank::Prefix, payload);
    }

    void SendPayloads(Player* player, std::vector<std::string> const& payloads)
    {
        for (std::string const& payload : payloads)
            SendS2C(player, payload);
    }

    void AppendSnapshot(std::vector<std::string>& out, uint32 requestId, uint32 revision,
                        std::vector<ReagentBank::StoredRow> const& rows)
    {
        out.push_back(ReagentBank::FormatBegin(requestId, revision, static_cast<uint32>(rows.size())));
        for (ReagentBank::SnapshotChunk const& chunk : ReagentBank::BuildItemsChunks(requestId, rows))
            out.push_back(chunk.payload);
        out.push_back(ReagentBank::FormatEnd(requestId, revision));
    }

    Player* PlayerFromLowGuid(uint32 guidLow)
    {
        return sObjectMgr.GetPlayer(ObjectGuid(HIGHGUID_PLAYER, guidLow));
    }

    bool EraseContext(uint32 guidLow)
    {
        std::lock_guard<std::mutex> lock(g_accessMutex);
        return g_accessByPlayer.erase(guidLow) > 0;
    }

    void ClearContext(uint32 guidLow, Player* player, char const* closeReason)
    {
        bool const had = EraseContext(guidLow);
        if (had && player && closeReason)
            SendS2C(player, ReagentBank::FormatClose(closeReason));
    }

    void CloseAllContexts(char const* reason)
    {
        std::unordered_map<uint32, AccessContext> snapshot;
        {
            std::lock_guard<std::mutex> lock(g_accessMutex);
            snapshot.swap(g_accessByPlayer);
        }

        for (auto const& kv : snapshot)
        {
            if (Player* player = PlayerFromLowGuid(kv.first))
                SendS2C(player, ReagentBank::FormatClose(reason));
        }
    }

    void CreateContext(Player* player, ObjectGuid bankerGuid)
    {
        AccessContext ctx;
        ctx.bankerGuid = bankerGuid;
        ctx.openedAt = WorldTimer::getMSTime();
        ctx.hasNewestRequestId = false;
        ctx.newestRequestId = 0;
        ctx.revision = 0;
        ctx.lastCommandAt = 0;
        ctx.lastMutationAt = 0;

        std::lock_guard<std::mutex> lock(g_accessMutex);
        g_accessByPlayer[player->GetGUIDLow()] = std::move(ctx);
    }

    bool ValidateAccess(Player* player)
    {
        if (!player)
            return false;

        if (!sReagentBankConfig.Enabled() || !player->IsInWorld())
        {
            ClearContext(player->GetGUIDLow(), player, sReagentBankConfig.Enabled() ? "NO_ACCESS" : "DISABLED");
            return false;
        }

        ObjectGuid bankerGuid;
        bool hasCtx = false;
        {
            std::lock_guard<std::mutex> lock(g_accessMutex);
            auto it = g_accessByPlayer.find(player->GetGUIDLow());
            if (it != g_accessByPlayer.end())
            {
                hasCtx = true;
                bankerGuid = it->second.bankerGuid;
            }
        }

        if (!hasCtx)
            return false;

        if (!player->GetNPCIfCanInteractWith(bankerGuid, UNIT_NPC_FLAG_BANKER))
        {
            ClearContext(player->GetGUIDLow(), player, "NO_ACCESS");
            return false;
        }

        return true;
    }

    uint32 GetRevision(uint32 guidLow)
    {
        std::lock_guard<std::mutex> lock(g_accessMutex);
        auto it = g_accessByPlayer.find(guidLow);
        if (it == g_accessByPlayer.end())
            return 0;
        return it->second.revision;
    }

    uint32 IncrementRevision(uint32 guidLow)
    {
        std::lock_guard<std::mutex> lock(g_accessMutex);
        auto it = g_accessByPlayer.find(guidLow);
        if (it == g_accessByPlayer.end())
            return 0;
        ++it->second.revision;
        return it->second.revision;
    }

    bool IsNewRequestId(uint32 guidLow, uint32 requestId)
    {
        std::lock_guard<std::mutex> lock(g_accessMutex);
        auto it = g_accessByPlayer.find(guidLow);
        return it != g_accessByPlayer.end()
            && (!it->second.hasNewestRequestId
                || ReagentBank::IsNewerRequestId(requestId, it->second.newestRequestId));
    }

    void RememberReply(uint32 guidLow, ReplayEntry entry)
    {
        std::lock_guard<std::mutex> lock(g_accessMutex);
        auto it = g_accessByPlayer.find(guidLow);
        if (it == g_accessByPlayer.end())
            return;

        AccessContext& context = it->second;
        context.hasNewestRequestId = true;
        context.newestRequestId = entry.requestId;
        context.replayCache.push_back(std::move(entry));
        while (context.replayCache.size() > kReplayCacheCapacity)
            context.replayCache.pop_front();
    }

    bool TryReplay(Player* player, uint32 requestId)
    {
        ReplayEntry replay;
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(g_accessMutex);
            auto it = g_accessByPlayer.find(player->GetGUIDLow());
            if (it == g_accessByPlayer.end())
                return false;

            auto const entry = std::find_if(it->second.replayCache.begin(), it->second.replayCache.end(),
                [requestId](ReplayEntry const& candidate) { return candidate.requestId == requestId; });
            if (entry == it->second.replayCache.end())
                return false;
            replay = *entry;
            found = true;
        }

        if (!found)
            return false;

        uint32 const guidLow = player->GetGUIDLow();
        if (replay.sendResult)
            SendS2C(player, ReagentBank::FormatResult(requestId, replay.result, replay.detail));
        if (replay.sendSnapshot)
        {
            std::vector<std::string> snapshot;
            AppendSnapshot(snapshot, requestId, GetRevision(guidLow), ReagentBank::LoadRows(guidLow));
            SendPayloads(player, snapshot);
        }
        return true;
    }

    bool ConsumeThrottle(uint32 guidLow, bool mutation, uint32 now)
    {
        std::lock_guard<std::mutex> lock(g_accessMutex);
        auto it = g_accessByPlayer.find(guidLow);
        if (it == g_accessByPlayer.end())
            return false;

        uint32& stamp = mutation ? it->second.lastMutationAt : it->second.lastCommandAt;
        uint32 const minMs = mutation ? ReagentBank::MutationThrottleMs : ReagentBank::QueryThrottleMs;
        if (stamp != 0 && WorldTimer::getMSTimeDiff(stamp, now) < minMs)
            return false;

        stamp = now;
        return true;
    }

    ReagentBank::ResultCode MapStoreError(std::string const& error)
    {
        using ReagentBank::ResultCode;
        ResultCode const codes[] = {
            ResultCode::Ok,
            ResultCode::Disabled,
            ResultCode::NoAccess,
            ResultCode::BadRequest,
            ResultCode::BadSlot,
            ResultCode::NotEligible,
            ResultCode::Limit,
            ResultCode::NoSpace,
            ResultCode::NotFound,
            ResultCode::DbError
        };

        for (ResultCode const code : codes)
        {
            if (error == ReagentBank::ResultCodeToken(code))
                return code;
        }

        return ResultCode::DbError;
    }

    void ReplyResult(Player* player, uint32 guidLow, uint32 requestId,
                     ReagentBank::ResultCode code, std::string const& detail = {}, bool remember = false)
    {
        if (remember)
            RememberReply(guidLow, ReplayEntry{requestId, true, code, detail, false});
        SendS2C(player, ReagentBank::FormatResult(requestId, code, detail));
    }

    // After a store mutation: SessionAborted means Player was unloaded — no
    // addon reply and no further dereference. Failed keeps Player live and
    // maps to a ResultCode (DB_ERROR for pre-mutation database failures).
    bool ContinueAfterMutation(Player* player, uint32 guidLow, uint32 requestId,
                               ReagentBank::MutationStatus status, std::string const& error)
    {
        if (ReagentBank::MutationUnloadedPlayer(status)
            || ReagentBank::IsSessionAbortedError(error))
            return false;

        if (status != ReagentBank::MutationStatus::Ok)
        {
            ReplyResult(player, guidLow, requestId, MapStoreError(error), {}, true);
            return false;
        }
        return true;
    }

    void ReplyResultAndSnapshot(Player* player, uint32 guidLow, uint32 requestId, uint32 revision,
                                ReagentBank::ResultCode code, std::string const& detail = {}, bool remember = false)
    {
        std::vector<ReagentBank::StoredRow> const rows = ReagentBank::LoadRows(guidLow);
        std::vector<std::string> payloads;
        payloads.push_back(ReagentBank::FormatResult(requestId, code, detail));
        AppendSnapshot(payloads, requestId, revision, rows);
        if (remember)
            RememberReply(guidLow, ReplayEntry{requestId, true, code, detail, true});
        SendPayloads(player, payloads);
    }

    void ReplySnapshot(Player* player, uint32 guidLow, uint32 requestId, uint32 revision,
                       std::vector<ReagentBank::StoredRow> const& rows)
    {
        std::vector<std::string> payloads;
        AppendSnapshot(payloads, requestId, revision, rows);
        RememberReply(guidLow, ReplayEntry{requestId, false, ReagentBank::ResultCode::Ok, {}, true});
        SendPayloads(player, payloads);
    }

    void SendOpenAndSnapshot(Player* player)
    {
        uint32 const guidLow = player->GetGUIDLow();
        uint32 const revision = GetRevision(guidLow);
        std::vector<ReagentBank::StoredRow> const rows = ReagentBank::LoadRows(guidLow);

        SendS2C(player, ReagentBank::FormatOpen(revision));

        std::vector<std::string> snapshot;
        AppendSnapshot(snapshot, 0, revision, rows);
        SendPayloads(player, snapshot);
    }

    bool VerifyEquippedBagSlot(Player* player, ReagentBank::BagSlot const& pos)
    {
        if (pos.bag < ReagentBank::kInventorySlotBagStart || pos.bag >= ReagentBank::kInventorySlotBagEnd)
            return true;

        Item* bagItem = player->GetItemByPos(ReagentBank::kInventorySlotBag0, pos.bag);
        if (!bagItem)
            return false;

        Bag const* bag = bagItem->ToBag();
        if (!bag)
            return false;

        return pos.slot < bag->GetBagSize();
    }

    void HandleQuery(Player* player, uint32 requestId)
    {
        uint32 const guidLow = player->GetGUIDLow();
        uint32 const revision = GetRevision(guidLow);
        std::vector<ReagentBank::StoredRow> const rows = ReagentBank::LoadRows(guidLow);

        ReplySnapshot(player, guidLow, requestId, revision, rows);

        if (sReagentBankConfig.Debug())
            sLog.outDebug("ReagentBank: QUERY guid=%u requestId=%u rows=%u revision=%u",
                          guidLow, requestId, static_cast<uint32>(rows.size()), revision);
    }

    void HandleDeposit(Player* player, ReagentBank::C2SMessage const& cmd)
    {
        uint32 const guidLow = player->GetGUIDLow();
        uint32 const requestId = cmd.requestId;

        ReagentBank::BagSlot pos;
        if (!ReagentBank::ConvertClientBagSlot(cmd.clientBag, cmd.clientSlot, pos))
        {
            ReplyResult(player, guidLow, requestId, ReagentBank::ResultCode::BadSlot, {}, true);
            return;
        }

        if (!VerifyEquippedBagSlot(player, pos))
        {
            ReplyResult(player, guidLow, requestId, ReagentBank::ResultCode::BadSlot, {}, true);
            return;
        }

        Item* item = player->GetItemByPos(pos.bag, pos.slot);
        if (!item)
        {
            ReplyResult(player, guidLow, requestId, ReagentBank::ResultCode::BadSlot, {}, true);
            return;
        }

        if (!ReagentBank::CanStore(*item, *player))
        {
            ReplyResult(player, guidLow, requestId, ReagentBank::ResultCode::NotEligible, {}, true);
            return;
        }

        bool overflows = false;
        ReagentBank::CapSum(ReagentBank::LoadAmount(guidLow, item->GetEntry()),
                            item->GetCount(), sReagentBankConfig.MaxAmountPerItem(), overflows);
        if (overflows)
        {
            ReplyResult(player, guidLow, requestId, ReagentBank::ResultCode::Limit, {}, true);
            return;
        }

        std::string error;
        ReagentBank::MutationStatus const status = ReagentBank::DepositStack(*player, *item, &error);
        if (!ContinueAfterMutation(player, guidLow, requestId, status, error))
            return;

        uint32 const revision = IncrementRevision(guidLow);
        ReplyResultAndSnapshot(player, guidLow, requestId, revision, ReagentBank::ResultCode::Ok, {}, true);
        if (sReagentBankConfig.Debug())
            sLog.outDebug("ReagentBank: DEPOSIT guid=%u requestId=%u revision=%u", guidLow, requestId, revision);
    }

    void HandleDepositAll(Player* player, uint32 requestId)
    {
        uint32 const guidLow = player->GetGUIDLow();

        if (!sReagentBankConfig.DepositAllEnabled())
        {
            ReplyResult(player, guidLow, requestId, ReagentBank::ResultCode::BadRequest, "DEPOSIT_ALL_DISABLED", true);
            return;
        }

        ReagentBank::DepositAllReport report;
        std::string error;
        ReagentBank::MutationStatus const status = ReagentBank::DepositAll(*player, report, &error);
        if (!ContinueAfterMutation(player, guidLow, requestId, status, error))
            return;

        uint32 revision = GetRevision(guidLow);
        if (report.stacksDeposited > 0)
            revision = IncrementRevision(guidLow);

        std::string const detail = std::to_string(report.stacksDeposited) + ":" +
                                   std::to_string(report.itemsDeposited) + ":" +
                                   std::to_string(report.stacksSkipped);
        ReplyResultAndSnapshot(player, guidLow, requestId, revision, ReagentBank::ResultCode::Ok, detail, true);
        if (sReagentBankConfig.Debug())
            sLog.outDebug("ReagentBank: DEPOSIT_ALL guid=%u requestId=%u revision=%u %s",
                          guidLow, requestId, revision, detail.c_str());
    }

    void HandleWithdraw(Player* player, ReagentBank::C2SMessage const& cmd)
    {
        uint32 const guidLow = player->GetGUIDLow();
        uint32 const requestId = cmd.requestId;

        std::string error;
        ReagentBank::MutationStatus const status = ReagentBank::Withdraw(*player, cmd.itemEntry, cmd.amount, &error);
        if (!ContinueAfterMutation(player, guidLow, requestId, status, error))
            return;

        uint32 const revision = IncrementRevision(guidLow);
        ReplyResultAndSnapshot(player, guidLow, requestId, revision, ReagentBank::ResultCode::Ok, {}, true);
        if (sReagentBankConfig.Debug())
            sLog.outDebug("ReagentBank: WITHDRAW guid=%u requestId=%u entry=%u amount=%u revision=%u",
                          guidLow, requestId, cmd.itemEntry, cmd.amount, revision);
    }

    void Dispatch(Player* player, ReagentBank::C2SMessage const& cmd)
    {
        if (!player)
            return;

        uint32 const guidLow = player->GetGUIDLow();
        uint32 const requestId = cmd.requestId;

        if (!sReagentBankConfig.Enabled())
        {
            SendS2C(player, ReagentBank::FormatResult(requestId, ReagentBank::ResultCode::Disabled));
            ClearContext(guidLow, player, "DISABLED");
            return;
        }

        if (!cmd.valid)
        {
            SendS2C(player, ReagentBank::FormatResult(requestId, ReagentBank::ResultCode::BadRequest));
            return;
        }

        if (cmd.command == ReagentBank::Command::Close)
        {
            EraseContext(guidLow);
            SendS2C(player, ReagentBank::FormatClose("CLOSED"));
            return;
        }

        if (!ValidateAccess(player))
        {
            SendS2C(player, ReagentBank::FormatResult(requestId, ReagentBank::ResultCode::NoAccess));
            return;
        }

        if (TryReplay(player, requestId))
            return;

        if (!IsNewRequestId(guidLow, requestId))
        {
            SendS2C(player, ReagentBank::FormatResult(requestId, ReagentBank::ResultCode::BadRequest, "STALE_REQUEST"));
            return;
        }

        bool const isMutation = cmd.command == ReagentBank::Command::Deposit
            || cmd.command == ReagentBank::Command::DepositAll
            || cmd.command == ReagentBank::Command::Withdraw;
        bool const isQuery = cmd.command == ReagentBank::Command::Query;

        if ((isQuery || isMutation) && !ConsumeThrottle(guidLow, isMutation, WorldTimer::getMSTime()))
            return;

        switch (cmd.command)
        {
            case ReagentBank::Command::Query:
                HandleQuery(player, requestId);
                break;
            case ReagentBank::Command::Deposit:
                HandleDeposit(player, cmd);
                break;
            case ReagentBank::Command::DepositAll:
                HandleDepositAll(player, requestId);
                break;
            case ReagentBank::Command::Withdraw:
                HandleWithdraw(player, cmd);
                break;
            default:
                ReplyResult(player, guidLow, requestId, ReagentBank::ResultCode::BadRequest);
                break;
        }
    }

    bool IsRbankCommand(uint32 type, uint32 lang, std::string const& msg)
    {
        return lang == LANG_ADDON && type == CHAT_MSG_GUILD && ReagentBank::IsExactC2SPrefix(msg);
    }

    void EnsureBankerHasGossipFlag(Creature* creature)
    {
        if (creature && creature->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_BANKER))
            creature->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
    }

    void MarkLoadedBankersGossipable()
    {
        // GetCreatureBySpawnIdStore returns a lock-protected snapshot, so a
        // grid unload cannot invalidate this traversal during config reload.
        sMapMgr.DoForAllMaps([](Map* map)
        {
            if (!map)
                return;

            for (auto const& pair : map->GetCreatureBySpawnIdStore())
                EnsureBankerHasGossipFlag(pair.second);
        });
    }

    bool AppendMaterialStorageMenu(Player* player, Creature* banker)
    {
        if (!player || !player->PlayerTalkClass || !banker || !sReagentBankConfig.Enabled()
            || !banker->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_BANKER))
            return false;

        GossipMenu& menu = player->PlayerTalkClass->GetGossipMenu();
        for (unsigned int item = 0; item < menu.MenuItemCount(); ++item)
        {
            if (menu.MenuItemSender(item) == kSender && menu.MenuItemAction(item) == kAction)
                return false;
        }

        if (menu.MenuItemCount() >= GOSSIP_MAX_MENU_ITEMS)
            return false;

        menu.AddMenuItem(GOSSIP_ICON_MONEY_BAG, "Material Storage", kSender, kAction, "", false);
        // GossipMenu keeps a parallel action-data vector. The select packet is
        // intercepted below, but keeping vectors aligned also makes a stale or
        // malformed packet harmless to the core's normal select path.
        menu.AddGossipMenuItemData(0, 0, 0);
        return true;
    }
}

class ReagentBankWorldScript : public WorldScript
{
public:
    ReagentBankWorldScript()
        : WorldScript("ReagentBankWorldScript", { WORLDHOOK_ON_AFTER_CONFIG_LOAD })
    {
    }

    void OnAfterConfigLoad(bool reload) override
    {
        bool const wasEnabled = sReagentBankConfig.Enabled();
        sReagentBankConfig.Load(reload);
        if (wasEnabled && !sReagentBankConfig.Enabled())
            CloseAllContexts("DISABLED");
        // On the initial load, this is harmless and covers maps that happened
        // to be populated before scripts initialized. On a reload it is needed
        // specifically for the disabled -> enabled transition.
        else if (sReagentBankConfig.Enabled() && (!reload || !wasEnabled))
            MarkLoadedBankersGossipable();
    }
};

class ReagentBankAllCreatureScript : public AllCreatureScript
{
public:
    ReagentBankAllCreatureScript()
        : AllCreatureScript("ReagentBankAllCreatureScript")
    {
    }

    void OnCreatureAddWorld(Creature* creature) override
    {
        if (!sReagentBankConfig.Enabled() || !creature)
            return;

        EnsureBankerHasGossipFlag(creature);
    }
};

class ReagentBankPacketScript : public ServerScript
{
public:
    ReagentBankPacketScript()
        : ServerScript("ReagentBankPacketScript", {
              SERVERHOOK_CAN_PACKET_RECEIVE,
              SERVERHOOK_ON_PACKET_HANDLED
          })
    {
    }

    void OnPacketHandled(WorldSession* session, WorldPacket const& packet) override
    {
        // This runs after the core has invoked an NPC-specific gossip script.
        // Appending here, rather than from AllCreatureScript::CanCreatureGossipHello,
        // keeps Material Storage available on scripted bankers without clearing
        // or rebuilding the menu that script supplied.
        if (packet.GetOpcode() != CMSG_GOSSIP_HELLO || !session || !sReagentBankConfig.Enabled())
            return;

        Player* player = session->GetPlayer();
        if (!player)
            return;

        ObjectGuid const guid = session->GetCurrentGossipGUID();
        Creature* banker = player->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_BANKER);
        if (!banker)
            return;

        if (AppendMaterialStorageMenu(player, banker))
            player->SendPreparedGossip(banker);
    }

    bool CanPacketReceive(WorldSession* session, WorldPacket const& packet) override
    {
        if (packet.GetOpcode() != CMSG_GOSSIP_SELECT_OPTION)
            return true;

        WorldPacket data(packet);
        data.rpos(0);

        // This core reads ObjectGuid as a raw uint64 (not packed).
        if (data.size() < sizeof(uint64) + sizeof(uint32))
            return true;

        ObjectGuid guid;
        uint32 gossipListId = 0;
        data >> guid >> gossipListId;

        Player* player = session ? session->GetPlayer() : nullptr;
        if (!player || !player->PlayerTalkClass)
            return true;

        GossipMenu& menu = player->PlayerTalkClass->GetGossipMenu();
        if (gossipListId >= menu.MenuItemCount())
            return true;

        if (menu.MenuItemSender(gossipListId) != kSender || menu.MenuItemAction(gossipListId) != kAction)
            return true;

        if (guid != session->GetCurrentGossipGUID())
            return true;

        Creature* banker = player->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_BANKER);
        if (!banker)
            return true;

        if (!sReagentBankConfig.Enabled())
        {
            player->PlayerTalkClass->CloseGossip();
            return false;
        }

        player->PlayerTalkClass->CloseGossip();
        CreateContext(player, guid);
        SendOpenAndSnapshot(player);
        if (sReagentBankConfig.Debug())
            sLog.outDebug("ReagentBank: OPEN player=%u bankerEntry=%u", player->GetGUIDLow(), banker->GetEntry());
        return false;
    }
};

class ReagentBankPlayerScript : public PlayerScript
{
public:
    ReagentBankPlayerScript()
        : PlayerScript("ReagentBankPlayerScript", {
              PLAYERHOOK_ON_BEFORE_SEND_CHAT_MESSAGE,
              PLAYERHOOK_CAN_USE_GROUP_CHAT,
              PLAYERHOOK_ON_LOGOUT,
              PLAYERHOOK_ON_DELETE,
              PLAYERHOOK_ON_MAP_CHANGED,
              PLAYERHOOK_ON_BEFORE_TELEPORT
          })
    {
    }

    bool CanUseGroupChat(Player* /*player*/, uint32 type, uint32 lang, std::string& msg) override
    {
        return !IsRbankCommand(type, lang, msg);
    }

    void OnBeforeSendChatMessage(Player* player, uint32& type, uint32& lang, std::string& msg) override
    {
        if (!IsRbankCommand(type, lang, msg))
            return;

        ReagentBank::C2SMessage const parsed = ReagentBank::ParseC2S(msg);
        Dispatch(player, parsed);
    }

    void OnLogout(Player* player) override
    {
        if (!player)
            return;
        ClearContext(player->GetGUIDLow(), player, "CLOSED");
    }

    void OnDelete(ObjectGuid guid, uint32 /*accountId*/) override
    {
        uint32 const guidLow = guid.GetCounter();
        ReagentBank::DeleteCharacterRows(guidLow);
        ClearContext(guidLow, nullptr, nullptr);
    }

    void OnMapChanged(Player* player) override
    {
        if (!player)
            return;
        ClearContext(player->GetGUIDLow(), player, "NO_ACCESS");
    }

    void OnBeforeTeleport(Player* player, uint32 /*mapId*/, float /*x*/, float /*y*/, float /*z*/, float /*orientation*/) override
    {
        if (!player)
            return;
        ClearContext(player->GetGUIDLow(), player, "NO_ACCESS");
    }
};

void AddSC_reagent_bank()
{
    new ReagentBankWorldScript();
    new ReagentBankAllCreatureScript();
    new ReagentBankPacketScript();
    new ReagentBankPlayerScript();
}
