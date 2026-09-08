/*
 * Material Storage eligibility, bag conversion, and character-DB mutations.
 * Game headers stay behind REAGENT_BANK_HEADLESS_TESTS so unit tests compile
 * this file without Player.h.
 */

#include "ReagentBankStore.h"

#include <limits>
#include <string>

#ifndef REAGENT_BANK_HEADLESS_TESTS
#include "ReagentBankConfig.h"

#include "Bag.h"
#include "Database/DatabaseEnv.h"
#include "Item.h"
#include "ItemPrototype.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "WorldSession.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#endif

namespace ReagentBank
{
    namespace
    {
        void SetReason(std::string* reason, char const* text)
        {
            if (reason)
                *reason = text;
        }

#ifndef REAGENT_BANK_HEADLESS_TESTS
        void SetError(std::string* error, ResultCode code)
        {
            if (error)
                *error = ResultCodeToken(code);
        }

        struct StoredBalance
        {
            bool found = false;
            uint32 amount = 0;
            uint32 revision = 0;
            bool legacy = false;
        };

        // PExecute/Execute only means that a request entered this core's
        // transaction queue; it does not expose affected rows. A stale existing
        // row is failed by writing mutation_guard=0 (no InnoDB parent). A
        // missing row is a successful 0-row UPDATE in MySQL, so a second
        // statement INSERT 1 into the parent fails on duplicate key unless the
        // expected post-state exists. Neither path writes NULL or depends on
        // strict sql_mode. SqlTransaction then rolls the complete
        // inventory+balance transaction back.
        bool QueueSql(std::string const& sql)
        {
            return CharacterDatabase.Execute(sql.c_str());
        }

        bool QueueCredit(uint32 characterId, uint32 entry, uint32 subclass, uint32 add,
                         uint32 cap, StoredBalance const& before)
        {
            if (!before.found)
            {
                (void)cap;
                if (!QueueSql(FormatCreditInsertSql(characterId, entry, subclass, add)))
                    return false;
                return QueueSql(FormatMutationGuardProbeSql(characterId, entry, add, 1));
            }

            uint32 const capMinusAdd = cap - add;
            uint32 const nextAmount = before.amount + add;
            uint32 const nextRevision = before.revision + 1;
            if (!QueueSql(FormatCreditUpdateSql(characterId, entry, subclass, add, capMinusAdd,
                                                before.amount, before.revision, before.legacy ? 1u : 0u)))
                return false;
            return QueueSql(FormatMutationGuardProbeSql(characterId, entry, nextAmount, nextRevision));
        }

        bool QueueDebit(uint32 characterId, uint32 entry, uint32 amount,
                        StoredBalance const& before)
        {
            uint32 const nextAmount = before.amount - amount;
            uint32 const nextRevision = before.revision + 1;
            uint32 const legacy = before.legacy ? 1u : 0u;
            if (!QueueSql(FormatDebitUpdateSql(characterId, entry, nextAmount, amount,
                                               before.amount, before.revision, legacy)))
                return false;
            // Probe the post-update row before DELETE. A 0-row UPDATE would
            // otherwise commit a withdraw that never debited.
            if (!QueueSql(FormatMutationGuardProbeSql(characterId, entry, nextAmount, nextRevision)))
                return false;
            if (nextAmount == 0)
                return QueueSql(FormatDebitDeleteSql(characterId, entry, nextRevision, legacy));
            return true;
        }

        StoredBalance LoadBalance(uint32 characterId, uint32 itemEntry)
        {
            StoredBalance balance;
            QueryResult* result = CharacterDatabase.PQuery(
                "SELECT amount, revision, legacy FROM custom_reagent_bank "
                "WHERE character_id = %u AND item_entry = %u AND amount > 0",
                characterId, itemEntry);
            if (!result)
                return balance;

            Field* fields = result->Fetch();
            balance.found = true;
            balance.amount = fields[0].GetUInt32();
            balance.revision = fields[1].GetUInt32();
            balance.legacy = fields[2].GetUInt8() != 0;
            delete result;
            return balance;
        }

        void AbortUnsavedAfterMutation(Player& player, char const* operation)
        {
            uint32 const guid = player.GetGUIDLow();
            sLog.outError("ReagentBank: %s failed after mutating player %u; unloading without save to preserve the last committed state",
                operation, guid);

            // This is the same safe no-save logout sequence ObjectAccessor uses
            // when it must evict a player. Do not merely kick: disconnect
            // handling saves the in-memory item queue before logout.
            if (WorldSession* session = player.GetSession())
            {
                session->KickPlayer();
                session->LogoutPlayer(false);
            }
        }

        MutationStatus AbortMutation(Player& player, std::string* error, char const* operation)
        {
            if (CharacterDatabase.InTransaction())
                CharacterDatabase.RollbackTransaction();
            if (error)
                *error = kSessionAbortedError;
            AbortUnsavedAfterMutation(player, operation);
            return MutationStatus::SessionAborted;
        }

        ItemView ViewFromPrototype(ItemPrototype const& proto)
        {
            ItemView view;
            view.entry = proto.ItemId;
            view.itemClass = proto.Class;
            view.itemSubclass = proto.SubClass;
            view.stackable = proto.Stackable;
            view.maxCount = proto.MaxCount;
            view.bonding = proto.Bonding;
            view.flags = proto.Flags;
            view.duration = proto.Duration;
            view.protoExists = true;
            view.count = 1;
            return view;
        }

        struct DepositCandidate
        {
            uint8 bag = 0;
            uint8 slot = 0;
            uint64 itemGuid = 0;
            uint32 entry = 0;
            uint32 count = 0;
        };

        void ConsiderDepositCandidate(Player& player, Item* item, std::vector<DepositCandidate>& out)
        {
            if (!item)
                return;
            if (!CanStore(*item, player, nullptr))
                return;

            DepositCandidate c;
            c.bag = item->GetBagSlot();
            c.slot = item->GetSlot();
            c.itemGuid = item->GetObjectGuid().GetRawValue();
            c.entry = item->GetEntry();
            c.count = item->GetCount();
            out.push_back(c);
        }
#endif
    }

    bool ConvertClientBagSlot(uint32_t clientBag, uint32_t clientSlot, BagSlot& out, std::string* reason)
    {
        if (clientBag == 0)
        {
            if (clientSlot < 1 || clientSlot > 16)
            {
                SetReason(reason, "invalid backpack slot");
                return false;
            }

            out.bag = kInventorySlotBag0;
            out.slot = static_cast<uint8_t>(kInventorySlotItemStart + (clientSlot - 1));
            return true;
        }

        if (clientBag >= 1 && clientBag <= 4)
        {
            if (clientSlot < 1)
            {
                SetReason(reason, "invalid bag slot");
                return false;
            }

            uint32_t const slot = clientSlot - 1;
            if (slot > 255)
            {
                SetReason(reason, "invalid bag slot");
                return false;
            }

            out.bag = static_cast<uint8_t>(kInventorySlotBagStart + (clientBag - 1));
            out.slot = static_cast<uint8_t>(slot);
            return true;
        }

        SetReason(reason, "invalid bag");
        return false;
    }

    bool IsSessionAbortedError(std::string const& error)
    {
        return error == kSessionAbortedError;
    }

    bool IsEligibleClass(uint32_t itemClass)
    {
        return itemClass == kItemClassGem
            || itemClass == kItemClassReagent
            || itemClass == kItemClassTradeGoods;
    }

    bool CanStorePrototype(ItemView const& view, std::string* reason)
    {
        if (!view.protoExists)
        {
            SetReason(reason, "missing prototype");
            return false;
        }
        if (!IsEligibleClass(view.itemClass))
        {
            SetReason(reason, "ineligible class");
            return false;
        }
        if (view.stackable <= 1)
        {
            SetReason(reason, "not stackable");
            return false;
        }
        if (view.maxCount != 0)
        {
            SetReason(reason, "unique maxCount");
            return false;
        }
        if (view.bonding != kNoBind)
        {
            SetReason(reason, "binds");
            return false;
        }
        if (view.flags & kItemFlagConjured)
        {
            SetReason(reason, "conjured");
            return false;
        }
        if (view.flags & kItemFlagLootable)
        {
            SetReason(reason, "lootable");
            return false;
        }
        if (view.flags & kItemFlagWrapper)
        {
            SetReason(reason, "wrapper");
            return false;
        }
        if (view.duration != 0)
        {
            SetReason(reason, "duration");
            return false;
        }
        return true;
    }

    bool CanRedeemLegacyPrototype(ItemView const& view, std::string* reason)
    {
        if (!view.protoExists)
        {
            SetReason(reason, "missing prototype");
            return false;
        }

        // AzerothCore upstream accepted these two classes without the current
        // instance/fungibility restrictions. Reagent was not an upstream class.
        if (view.itemClass != kItemClassGem && view.itemClass != kItemClassTradeGoods)
        {
            SetReason(reason, "not an upstream material class");
            return false;
        }
        return true;
    }

    bool CanStore(ItemView const& view, std::string* reason)
    {
        if (!CanStorePrototype(view, reason))
            return false;
        if (view.soulbound)
        {
            SetReason(reason, "soulbound");
            return false;
        }
        if (view.accountBound)
        {
            SetReason(reason, "account bound");
            return false;
        }
        if (view.wrapped)
        {
            SetReason(reason, "wrapped");
            return false;
        }
        if (view.inTrade)
        {
            SetReason(reason, "in trade");
            return false;
        }
        if (view.hasRandomProperty)
        {
            SetReason(reason, "random property");
            return false;
        }
        if (view.hasEnchantment)
        {
            SetReason(reason, "enchanted");
            return false;
        }
        if (view.hasGeneratedLoot)
        {
            SetReason(reason, "generated loot");
            return false;
        }
        if (!view.ownedByRequester)
        {
            SetReason(reason, "not owned");
            return false;
        }
        if (!view.inCarriedBags)
        {
            SetReason(reason, "not in carried bags");
            return false;
        }
        if (view.count < 1)
        {
            SetReason(reason, "empty stack");
            return false;
        }
        return true;
    }

    bool CanMergeWithdrawnInto(ItemView const& dest, std::string* reason)
    {
        if (dest.soulbound)
        {
            SetReason(reason, "soulbound");
            return false;
        }
        if (dest.accountBound)
        {
            SetReason(reason, "account bound");
            return false;
        }
        if (dest.wrapped)
        {
            SetReason(reason, "wrapped");
            return false;
        }
        if (dest.inTrade)
        {
            SetReason(reason, "in trade");
            return false;
        }
        if (dest.hasRandomProperty)
        {
            SetReason(reason, "random property");
            return false;
        }
        if (dest.hasEnchantment)
        {
            SetReason(reason, "enchanted");
            return false;
        }
        if (dest.hasGeneratedLoot)
        {
            SetReason(reason, "generated loot");
            return false;
        }
        if (dest.hasCreator)
        {
            SetReason(reason, "has creator");
            return false;
        }
        if (dest.duration != 0)
        {
            SetReason(reason, "duration");
            return false;
        }
        return true;
    }

    uint64_t CapSum(uint64_t stored, uint64_t add, uint32_t cap, bool& overflows)
    {
        if (add > std::numeric_limits<uint64_t>::max() - stored)
        {
            overflows = true;
            return std::numeric_limits<uint64_t>::max();
        }

        uint64_t const sum = stored + add;
        overflows = sum > static_cast<uint64_t>(cap);
        return sum;
    }

    namespace
    {
        std::string MatchPredicate(uint32_t amount, uint32_t revision, uint32_t legacy, std::string const& extra)
        {
            return "amount = " + std::to_string(amount)
                + " AND revision = " + std::to_string(revision)
                + " AND legacy = " + std::to_string(legacy)
                + extra;
        }
    }

    std::string FormatMutationGuardProbeSql(uint32_t characterId, uint32_t itemEntry,
                                            uint32_t expectedAmount, uint32_t expectedRevision)
    {
        // INSERT of the only legal parent value fails with duplicate key when
        // the expected post-state is missing. A matching row makes this a no-op.
        return "INSERT INTO custom_reagent_bank_mutation_guard (guard) "
               "SELECT 1 FROM (SELECT 1) AS _rb_probe WHERE NOT EXISTS ("
               "SELECT 1 FROM custom_reagent_bank WHERE character_id = "
            + std::to_string(characterId)
            + " AND item_entry = " + std::to_string(itemEntry)
            + " AND amount = " + std::to_string(expectedAmount)
            + " AND revision = " + std::to_string(expectedRevision)
            + " AND mutation_guard = 1)";
    }

    std::string FormatCreditInsertSql(uint32_t characterId, uint32_t itemEntry, uint32_t subclass, uint32_t amount)
    {
        return "INSERT INTO custom_reagent_bank "
               "(character_id, item_entry, item_subclass, amount, revision, legacy, mutation_guard) "
               "VALUES ("
            + std::to_string(characterId) + ", "
            + std::to_string(itemEntry) + ", "
            + std::to_string(subclass) + ", "
            + std::to_string(amount) + ", 1, 0, 1)";
    }

    std::string FormatCreditUpdateSql(uint32_t characterId, uint32_t itemEntry, uint32_t subclass,
                                      uint32_t add, uint32_t capMinusAdd, uint32_t expectedAmount,
                                      uint32_t expectedRevision, uint32_t expectedLegacy)
    {
        std::string const ok = MatchPredicate(expectedAmount, expectedRevision, expectedLegacy,
            " AND amount <= " + std::to_string(capMinusAdd) + " AND revision < 4294967295");
        return "UPDATE custom_reagent_bank SET "
               "mutation_guard = IF(" + ok + ", 1, 0), "
               "amount = IF(" + ok + ", amount + " + std::to_string(add) + ", amount), "
               "item_subclass = IF(" + ok + ", " + std::to_string(subclass) + ", item_subclass), "
               "revision = IF(" + ok + ", revision + 1, revision) "
               "WHERE character_id = " + std::to_string(characterId)
            + " AND item_entry = " + std::to_string(itemEntry);
    }

    std::string FormatDebitUpdateSql(uint32_t characterId, uint32_t itemEntry, uint32_t nextAmount,
                                     uint32_t debit, uint32_t expectedAmount, uint32_t expectedRevision,
                                     uint32_t expectedLegacy)
    {
        std::string const ok = MatchPredicate(expectedAmount, expectedRevision, expectedLegacy,
            " AND amount >= " + std::to_string(debit) + " AND revision < 4294967295");
        return "UPDATE custom_reagent_bank SET "
               "mutation_guard = IF(" + ok + ", 1, 0), "
               "amount = IF(" + ok + ", " + std::to_string(nextAmount) + ", amount), "
               "revision = IF(" + ok + ", revision + 1, revision) "
               "WHERE character_id = " + std::to_string(characterId)
            + " AND item_entry = " + std::to_string(itemEntry);
    }

    std::string FormatDebitDeleteSql(uint32_t characterId, uint32_t itemEntry, uint32_t expectedRevision,
                                     uint32_t expectedLegacy)
    {
        return "DELETE FROM custom_reagent_bank WHERE character_id = "
            + std::to_string(characterId)
            + " AND item_entry = " + std::to_string(itemEntry)
            + " AND amount = 0 AND revision = " + std::to_string(expectedRevision)
            + " AND legacy = " + std::to_string(expectedLegacy);
    }

#ifndef REAGENT_BANK_HEADLESS_TESTS
    ItemView MakeView(Item const& item, Player const& owner)
    {
        ItemView view;
        view.entry = item.GetEntry();
        view.count = item.GetCount();
        view.ownerGuidLow = item.GetOwnerGuid().GetCounter();
        view.ownedByRequester = (view.ownerGuidLow == owner.GetGUIDLow());
        view.bag = item.GetBagSlot();
        view.slot = item.GetSlot();
        view.itemGuid = item.GetObjectGuid().GetRawValue();
        view.soulbound = item.IsSoulBound();
        view.accountBound = item.IsAccountBound();
        view.wrapped = item.HasFlag(ITEM_FIELD_FLAGS, ITEM_DYNFLAG_WRAPPED);
        view.inTrade = item.IsInTrade();
        view.hasRandomProperty = (item.GetItemRandomPropertyId() != 0);
        view.hasGeneratedLoot = item.HasGeneratedLoot();
        view.hasCreator = !item.GetGuidValue(ITEM_FIELD_CREATOR).IsEmpty();

        view.hasEnchantment = false;
        for (int slot = 0; slot < MAX_ENCHANTMENT_SLOT; ++slot)
        {
            if (item.GetEnchantmentId(static_cast<EnchantmentSlot>(slot)) != 0)
            {
                view.hasEnchantment = true;
                break;
            }
        }

        view.inCarriedBags =
            (view.bag == kInventorySlotBag0
                && view.slot >= kInventorySlotItemStart
                && view.slot < kInventorySlotItemEnd)
            || (view.bag >= kInventorySlotBagStart && view.bag < kInventorySlotBagEnd);

        ItemPrototype const* proto = item.GetProto();
        if (!proto)
        {
            view.protoExists = false;
            view.duration = item.GetUInt32Value(ITEM_FIELD_DURATION);
            return view;
        }

        view.protoExists = true;
        view.itemClass = proto->Class;
        view.itemSubclass = proto->SubClass;
        view.stackable = proto->Stackable;
        view.maxCount = proto->MaxCount;
        view.bonding = proto->Bonding;
        view.flags = proto->Flags;
        view.duration = proto->Duration;
        if (uint32 const itemDuration = item.GetUInt32Value(ITEM_FIELD_DURATION))
            view.duration = itemDuration;
        return view;
    }

    bool CanStore(Item const& item, Player const& owner, std::string* reason)
    {
        return CanStore(MakeView(item, owner), reason);
    }

    std::vector<StoredRow> LoadRows(uint32_t characterId)
    {
        std::vector<StoredRow> rows;
        QueryResult* result = CharacterDatabase.PQuery(
            "SELECT item_entry, amount, legacy FROM custom_reagent_bank "
            "WHERE character_id = %u AND amount > 0",
            characterId);
        if (!result)
            return rows;

        do
        {
            Field* fields = result->Fetch();
            uint32 const entry = fields[0].GetUInt32();
            uint32 const amount = fields[1].GetUInt32();
            bool const legacy = fields[2].GetUInt8() != 0;

            ItemPrototype const* proto = sObjectMgr.GetItemPrototype(entry);
            if (!proto)
            {
                sLog.outError("ReagentBank: unknown item_entry %u for character %u, skipping",
                    entry, characterId);
                continue;
            }

            ItemView const view = ViewFromPrototype(*proto);
            if (legacy ? !CanRedeemLegacyPrototype(view, nullptr) : !CanStorePrototype(view, nullptr))
            {
                sLog.outError("ReagentBank: non-redeemable item_entry %u for character %u, skipping",
                    entry, characterId);
                continue;
            }

            StoredRow row;
            row.entry = entry;
            row.amount = amount;
            row.itemClass = proto->Class;
            row.itemSubclass = proto->SubClass;
            rows.push_back(row);
        } while (result->NextRow());

        delete result;
        return rows;
    }

    uint32_t LoadAmount(uint32_t characterId, uint32_t itemEntry)
    {
        return LoadBalance(characterId, itemEntry).amount;
    }

    bool DeleteCharacterRows(uint32_t characterId)
    {
        bool const ok = CharacterDatabase.PExecute(
            "DELETE FROM custom_reagent_bank WHERE character_id = %u",
            characterId);
        if (!ok)
            sLog.outError("ReagentBank: failed to delete rows for character %u", characterId);
        return ok;
    }

    MutationStatus DepositStack(Player& player, Item& item, std::string* error)
    {
        uint32 const guid = player.GetGUIDLow();
        if (!CanStore(item, player, nullptr))
        {
            SetError(error, ResultCode::NotEligible);
            return MutationStatus::Failed;
        }

        uint32 const count = item.GetCount();
        uint32 const entry = item.GetEntry();
        ItemPrototype const* proto = item.GetProto();
        uint32 const subclass = proto ? proto->SubClass : 0;
        uint8 const bag = item.GetBagSlot();
        uint8 const slot = item.GetSlot();
        StoredBalance const balance = LoadBalance(guid, entry);
        bool overflows = false;
        CapSum(balance.amount, count, sReagentBankConfig.MaxAmountPerItem(), overflows);
        if (overflows)
        {
            SetError(error, ResultCode::Limit);
            return MutationStatus::Failed;
        }

        if (!CharacterDatabase.BeginTransaction(guid))
        {
            sLog.outError("ReagentBank: BeginTransaction failed on deposit for player %u entry %u",
                guid, entry);
            SetError(error, ResultCode::DbError);
            return MutationStatus::Failed;
        }

        player.DestroyItem(bag, slot, true);
        player.SaveInventoryAndGoldToDB();

        if (!QueueCredit(guid, entry, subclass, count, sReagentBankConfig.MaxAmountPerItem(), balance))
        {
            sLog.outError("ReagentBank: failed to queue conditional credit on deposit for player %u entry %u count %u",
                guid, entry, count);
            AbortMutation(player, error, "deposit credit queue");
            return MutationStatus::SessionAborted;
        }

        bool const ok = CharacterDatabase.CommitTransactionDirect();
        if (!ok)
        {
            sLog.outError("ReagentBank: CommitTransactionDirect failed on deposit for player %u entry %u count %u",
                guid, entry, count);
            AbortMutation(player, error, "deposit commit");
            return MutationStatus::SessionAborted;
        }

        if (sReagentBankConfig.Debug())
            sLog.outDebug("ReagentBank: player %u deposited entry %u count %u", guid, entry, count);
        return MutationStatus::Ok;
    }

    MutationStatus DepositAll(Player& player, DepositAllReport& report, std::string* error)
    {
        report = DepositAllReport{};
        uint32 const guid = player.GetGUIDLow();

        if (!sReagentBankConfig.DepositAllEnabled())
        {
            SetError(error, ResultCode::Disabled);
            return MutationStatus::Failed;
        }

        std::vector<DepositCandidate> candidates;
        for (uint8 slot = kInventorySlotItemStart; slot < kInventorySlotItemEnd; ++slot)
            ConsiderDepositCandidate(player, player.GetItemByPos(kInventorySlotBag0, slot), candidates);

        for (uint8 bag = kInventorySlotBagStart; bag < kInventorySlotBagEnd; ++bag)
        {
            Item* bagItem = player.GetItemByPos(kInventorySlotBag0, bag);
            if (!bagItem || !bagItem->IsBag())
                continue;

            uint32 const bagSize = static_cast<Bag*>(bagItem)->GetBagSize();
            for (uint32 slot = 0; slot < bagSize; ++slot)
                ConsiderDepositCandidate(player, player.GetItemByPos(bag, static_cast<uint8>(slot)), candidates);
        }

        std::unordered_map<uint32, uint64> incoming;
        bool incomingOverflow = false;
        for (DepositCandidate const& c : candidates)
        {
            uint64& sum = incoming[c.entry];
            if (sum > std::numeric_limits<uint64>::max() - c.count)
            {
                incomingOverflow = true;
                sum = std::numeric_limits<uint64>::max();
            }
            else
                sum += c.count;
        }

        uint32 const cap = sReagentBankConfig.MaxAmountPerItem();
        std::unordered_set<uint32> rejected;
        if (incomingOverflow)
        {
            for (auto const& kv : incoming)
            {
                if (kv.second == std::numeric_limits<uint64>::max())
                    rejected.insert(kv.first);
            }
        }

        std::unordered_map<uint32, StoredBalance> balances;
        for (auto const& kv : incoming)
        {
            if (rejected.count(kv.first))
                continue;
            StoredBalance const balance = LoadBalance(guid, kv.first);
            balances.emplace(kv.first, balance);
            bool overflows = false;
            CapSum(balance.amount, kv.second, cap, overflows);
            if (overflows)
                rejected.insert(kv.first);
        }

        std::vector<DepositCandidate> accepted;
        accepted.reserve(candidates.size());
        for (DepositCandidate const& c : candidates)
        {
            if (rejected.count(c.entry))
                ++report.stacksSkipped;
            else
                accepted.push_back(c);
        }

        if (accepted.empty())
            return MutationStatus::Ok;

        if (!CharacterDatabase.BeginTransaction(guid))
        {
            sLog.outError("ReagentBank: BeginTransaction failed on deposit-all for player %u", guid);
            SetError(error, ResultCode::DbError);
            return MutationStatus::Failed;
        }

        std::unordered_map<uint32, uint64> deposited;
        std::unordered_map<uint32, uint32> subclassByEntry;

        for (DepositCandidate const& c : accepted)
        {
            Item* item = player.GetItemByPos(c.bag, c.slot);
            if (!item
                || item->GetObjectGuid().GetRawValue() != c.itemGuid
                || item->GetEntry() != c.entry
                || item->GetCount() != c.count
                || !CanStore(*item, player, nullptr))
            {
                ++report.stacksSkipped;
                continue;
            }

            ItemPrototype const* proto = item->GetProto();
            uint32 const subclass = proto ? proto->SubClass : 0;
            uint8 const bag = item->GetBagSlot();
            uint8 const slot = item->GetSlot();
            uint32 const entry = item->GetEntry();
            uint32 const count = item->GetCount();

            player.DestroyItem(bag, slot, true);

            deposited[entry] += count;
            subclassByEntry[entry] = subclass;
            ++report.stacksDeposited;
            report.itemsDeposited += count;
        }

        if (deposited.empty())
        {
            CharacterDatabase.RollbackTransaction();
            return MutationStatus::Ok;
        }

        player.SaveInventoryAndGoldToDB();

        for (auto const& kv : deposited)
        {
            // A non-rejected entry is bounded by MaxAmountPerItem, which itself
            // is uint32, so this cast cannot truncate.
            uint32 const add = static_cast<uint32>(kv.second);
            auto const balanceIt = balances.find(kv.first);
            if (balanceIt == balances.end()
                || !QueueCredit(guid, kv.first, subclassByEntry[kv.first], add, cap, balanceIt->second))
            {
                sLog.outError("ReagentBank: failed to queue conditional credit on deposit-all for player %u entry %u",
                    guid, kv.first);
                AbortMutation(player, error, "deposit-all credit queue");
                return MutationStatus::SessionAborted;
            }
        }

        bool const ok = CharacterDatabase.CommitTransactionDirect();
        if (!ok)
        {
            sLog.outError("ReagentBank: CommitTransactionDirect failed on deposit-all for player %u stacks %u items %u",
                guid, report.stacksDeposited, report.itemsDeposited);
            AbortMutation(player, error, "deposit-all commit");
            return MutationStatus::SessionAborted;
        }

        if (sReagentBankConfig.Debug())
        {
            sLog.outDebug("ReagentBank: player %u deposit-all stacks=%u items=%u skipped=%u",
                guid, report.stacksDeposited, report.itemsDeposited, report.stacksSkipped);
        }
        return MutationStatus::Ok;
    }

    MutationStatus Withdraw(Player& player, uint32_t itemEntry, uint32_t amount, std::string* error, uint32_t* withdrawn)
    {
        uint32 const guid = player.GetGUIDLow();
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemEntry);
        if (!proto)
        {
            SetError(error, ResultCode::NotEligible);
            return MutationStatus::Failed;
        }

        StoredBalance const balance = LoadBalance(guid, itemEntry);
        if (!balance.found)
        {
            SetError(error, ResultCode::NotFound);
            return MutationStatus::Failed;
        }

        ItemView const protoView = ViewFromPrototype(*proto);
        if (balance.legacy ? !CanRedeemLegacyPrototype(protoView, nullptr)
                           : !CanStorePrototype(protoView, nullptr))
        {
            SetError(error, ResultCode::NotEligible);
            return MutationStatus::Failed;
        }

        uint32 const stored = balance.amount;

        if (amount == 0)
            amount = std::min(stored, proto->GetMaxStackSize());
        else if (amount < 1 || amount > stored)
        {
            SetError(error, ResultCode::BadRequest);
            return MutationStatus::Failed;
        }

        ItemPosCountVec dest;
        InventoryResult const msg = player.CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemEntry, amount);
        if (msg != EQUIP_ERR_OK)
        {
            player.SendEquipError(msg, nullptr, nullptr, itemEntry);
            SetError(error, ResultCode::NoSpace);
            return MutationStatus::Failed;
        }

        // CanStoreNewItem merges by entry/count only. Do not extend a bound,
        // enchanted, random-property, wrapped, or otherwise non-fungible stack
        // with virtual plain materials.
        for (ItemPosCount const& position : dest)
        {
            uint8 const bag = static_cast<uint8>(position.pos >> 8);
            uint8 const slot = static_cast<uint8>(position.pos & 0xff);
            if (Item* existing = player.GetItemByPos(bag, slot))
            {
                if (!CanMergeWithdrawnInto(MakeView(*existing, player), nullptr))
                {
                    SetError(error, ResultCode::NoSpace);
                    return MutationStatus::Failed;
                }
            }
        }

        if (!CharacterDatabase.BeginTransaction(guid))
        {
            sLog.outError("ReagentBank: BeginTransaction failed on withdraw for player %u entry %u",
                guid, itemEntry);
            SetError(error, ResultCode::DbError);
            return MutationStatus::Failed;
        }

        if (!QueueDebit(guid, itemEntry, amount, balance))
        {
            sLog.outError("ReagentBank: failed to queue conditional debit on withdraw for player %u entry %u amount %u",
                guid, itemEntry, amount);
            CharacterDatabase.RollbackTransaction();
            SetError(error, ResultCode::DbError);
            return MutationStatus::Failed;
        }

        Item* created = player.StoreNewItem(dest, itemEntry, true, 0);
        if (!created)
        {
            sLog.outError("ReagentBank: StoreNewItem failed on withdraw for player %u entry %u amount %u",
                guid, itemEntry, amount);
            return AbortMutation(player, error, "withdraw item creation");
        }

        player.SaveInventoryAndGoldToDB();
        bool const ok = CharacterDatabase.CommitTransactionDirect();
        if (!ok)
        {
            sLog.outError("ReagentBank: CommitTransactionDirect failed on withdraw for player %u entry %u amount %u",
                guid, itemEntry, amount);
            return AbortMutation(player, error, "withdraw commit");
        }

        player.SendNewItem(created, amount, true, false);
        if (withdrawn)
            *withdrawn = amount;
        if (sReagentBankConfig.Debug())
            sLog.outDebug("ReagentBank: player %u withdrew entry %u amount %u", guid, itemEntry, amount);
        return MutationStatus::Ok;
    }
#endif
}
