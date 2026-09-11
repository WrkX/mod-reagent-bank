#ifndef MODULES_MOD_REAGENT_BANK_REAGENTBANKSTORE_H
#define MODULES_MOD_REAGENT_BANK_REAGENTBANKSTORE_H

#include "ReagentBankProtocol.h"

#include <cstdint>
#include <string>
#include <vector>

class Item;
class Player;
struct ItemPrototype;

namespace ReagentBank
{
    // Inventory constants duplicated for headless tests (must match Player.h).
    constexpr uint8_t kInventorySlotBag0 = 255;
    constexpr uint8_t kInventorySlotBagStart = 19;
    constexpr uint8_t kInventorySlotBagEnd = 23;
    constexpr uint8_t kInventorySlotItemStart = 23;
    constexpr uint8_t kInventorySlotItemEnd = 39;

    constexpr uint32_t kItemClassGem = 3;
    constexpr uint32_t kItemClassReagent = 5;
    constexpr uint32_t kItemClassTradeGoods = 7;
    constexpr uint32_t kNoBind = 0;

    constexpr uint32_t kItemFlagConjured = 0x00000002;
    constexpr uint32_t kItemFlagLootable = 0x00000004;
    constexpr uint32_t kItemFlagWrapper = 0x00000200;

    // A mutator may have changed the live Player object before its direct DB
    // transaction reports failure. SessionAborted is not a client ResultCode:
    // the caller must not send another reply or dereference that Player. The
    // store has scheduled it for a deferred no-save logout, so login reloads
    // the last committed inventory and balance state. Failed still carries a
    // ResultCode token (including DB_ERROR for pre-mutation database errors).
    enum class MutationStatus : uint8_t
    {
        Ok,
        Failed,
        SessionAborted
    };

    constexpr char const* kSessionAbortedError = "SESSION_ABORTED";

    inline bool MutationUnloadedPlayer(MutationStatus status)
    {
        return status == MutationStatus::SessionAborted;
    }
    bool IsSessionAbortedError(std::string const& error);

    constexpr char const* kPurchaseNotEnoughMoneyError = "NOT_ENOUGH_MONEY";

    struct ItemView
    {
        uint32_t entry = 0;
        uint32_t itemClass = 0;
        uint32_t itemSubclass = 0;
        uint32_t stackable = 0;
        uint32_t maxCount = 0;
        uint32_t bonding = 0;
        uint32_t flags = 0;
        uint32_t duration = 0;
        uint32_t count = 0;
        uint32_t ownerGuidLow = 0;
        uint8_t bag = 0;
        uint8_t slot = 0;
        uint64_t itemGuid = 0;
        bool soulbound = false;
        bool accountBound = false;
        bool wrapped = false;
        bool inTrade = false;
        bool hasRandomProperty = false;
        bool hasEnchantment = false;
        bool hasGeneratedLoot = false;
        bool hasCreator = false;
        bool protoExists = false;
        bool ownedByRequester = false;
        bool inCarriedBags = false;
    };

    struct BagSlot
    {
        uint8_t bag = 0;
        uint8_t slot = 0;
    };

    struct DepositAllReport
    {
        uint32_t stacksDeposited = 0;
        uint32_t itemsDeposited = 0;
        uint32_t stacksSkipped = 0;
    };

    // Client bag 0, slot 1..16 -> backpack (255, 23..38).
    // Client bag 1..4, slot 1..N -> equipped bag (19..22, slot-1).
    bool ConvertClientBagSlot(uint32_t clientBag, uint32_t clientSlot, BagSlot& out, std::string* reason = nullptr);

    bool IsEligibleClass(uint32_t itemClass);
    bool CanStorePrototype(ItemView const& view, std::string* reason = nullptr);
    // Upstream rows predate the stricter fungible-item rules. They may be
    // redeemed once if they are an upstream-supported trade good or gem, but
    // can never be deposited by the new module unless CanStorePrototype passes.
    bool CanRedeemLegacyPrototype(ItemView const& view, std::string* reason = nullptr);
    bool CanStore(ItemView const& view, std::string* reason = nullptr);
    // Destinations chosen by CanStoreNewItem may already hold a same-entry stack.
    // Withdrawn objects are plain; refuse to fold them into a modified instance.
    bool CanMergeWithdrawnInto(ItemView const& dest, std::string* reason = nullptr);

    ItemView MakeView(Item const& item, Player const& owner);
    bool CanStore(Item const& item, Player const& owner, std::string* reason = nullptr);

    uint64_t CapSum(uint64_t stored, uint64_t add, uint32_t cap, bool& overflows);

    // SQL used by GUID-serialized mutations. Tests check these do not write NULL
    // and that a stale or missing row still fails independent of sql_mode:
    // mutation_guard=0 violates the InnoDB parent FK, and the probe INSERT of 1
    // into that parent fails on duplicate key when the expected post-state is absent.
    std::string FormatMutationGuardProbeSql(uint32_t characterId, uint32_t itemEntry,
                                            uint32_t expectedAmount, uint32_t expectedRevision);
    std::string FormatCreditInsertSql(uint32_t characterId, uint32_t itemEntry, uint32_t subclass, uint32_t amount);
    std::string FormatCreditUpdateSql(uint32_t characterId, uint32_t itemEntry, uint32_t subclass,
                                      uint32_t add, uint32_t capMinusAdd, uint32_t expectedAmount,
                                      uint32_t expectedRevision, uint32_t expectedLegacy);
    std::string FormatDebitUpdateSql(uint32_t characterId, uint32_t itemEntry, uint32_t nextAmount,
                                     uint32_t debit, uint32_t expectedAmount, uint32_t expectedRevision,
                                     uint32_t expectedLegacy);
    std::string FormatDebitDeleteSql(uint32_t characterId, uint32_t itemEntry, uint32_t expectedRevision,
                                     uint32_t expectedLegacy);

    std::vector<StoredRow> LoadRows(uint32_t characterId);
    uint32_t LoadAmount(uint32_t characterId, uint32_t itemEntry);

    bool HasPurchased(uint32_t characterId);
    // The purchase mutates Player money and the persistent access row in one
    // direct character transaction. On a post-mutation commit failure the
    // caller must stop using Player; the store schedules a no-save logout.
    MutationStatus Purchase(Player& player, uint32_t costCopper, std::string* error);

    bool DeleteCharacterRows(uint32_t characterId);

    // Mutations assume the caller already validated access, eligibility, and
    // caps. They run one GUID-serialized direct character transaction.
    MutationStatus DepositStack(Player& player, Item& item, std::string* error);
    MutationStatus DepositAll(Player& player, DepositAllReport& report, std::string* error);
    MutationStatus Withdraw(Player& player, uint32_t itemEntry, uint32_t amount, std::string* error, uint32_t* withdrawn = nullptr);
}

#endif
