#include "ReagentBankStore.h"
#include "ReagentBankConfig.h"

#include "gtest/gtest.h"

#include <fstream>
#include <iterator>
#include <limits>
#include <string>

using ReagentBank::BagSlot;
using ReagentBank::CanStore;
using ReagentBank::CanStorePrototype;
using ReagentBank::CanRedeemLegacyPrototype;
using ReagentBank::CanMergeWithdrawnInto;
using ReagentBank::CapSum;
using ReagentBank::ConvertClientBagSlot;
using ReagentBank::FormatCreditInsertSql;
using ReagentBank::FormatCreditUpdateSql;
using ReagentBank::FormatDebitDeleteSql;
using ReagentBank::FormatDebitUpdateSql;
using ReagentBank::FormatMutationGuardProbeSql;
using ReagentBank::IsEligibleClass;
using ReagentBank::IsSessionAbortedError;
using ReagentBank::ItemView;
using ReagentBank::kSessionAbortedError;
using ReagentBank::MutationStatus;
using ReagentBank::MutationUnloadedPlayer;
using ReagentBank::kInventorySlotBag0;
using ReagentBank::kInventorySlotBagStart;
using ReagentBank::kInventorySlotItemStart;
using ReagentBank::kItemClassGem;
using ReagentBank::kItemClassReagent;
using ReagentBank::kItemClassTradeGoods;
using ReagentBank::kItemFlagConjured;
using ReagentBank::kItemFlagLootable;
using ReagentBank::kItemFlagWrapper;
using ReagentBank::kNoBind;

namespace
{
    ItemView EligibleView()
    {
        ItemView view;
        view.entry = 1;
        view.itemClass = kItemClassTradeGoods;
        view.itemSubclass = 7;
        view.stackable = 20;
        view.maxCount = 0;
        view.bonding = kNoBind;
        view.flags = 0;
        view.duration = 0;
        view.count = 20;
        view.ownerGuidLow = 42;
        view.bag = kInventorySlotBag0;
        view.slot = kInventorySlotItemStart;
        view.itemGuid = 1001;
        view.protoExists = true;
        view.ownedByRequester = true;
        view.inCarriedBags = true;
        return view;
    }
}

TEST(ReagentBankRules, BackpackSlot1MapsTo255_23)
{
    BagSlot out;
    ASSERT_TRUE(ConvertClientBagSlot(0, 1, out, nullptr));
    EXPECT_EQ(out.bag, kInventorySlotBag0);
    EXPECT_EQ(out.slot, static_cast<uint8_t>(23));
}

TEST(ReagentBankRules, BackpackSlot16MapsTo255_38)
{
    BagSlot out;
    ASSERT_TRUE(ConvertClientBagSlot(0, 16, out, nullptr));
    EXPECT_EQ(out.bag, kInventorySlotBag0);
    EXPECT_EQ(out.slot, static_cast<uint8_t>(38));
}

TEST(ReagentBankRules, BackpackSlot0Rejected)
{
    BagSlot out{};
    std::string reason;
    EXPECT_FALSE(ConvertClientBagSlot(0, 0, out, &reason));
    EXPECT_FALSE(reason.empty());
}

TEST(ReagentBankRules, BackpackSlot17Rejected)
{
    BagSlot out{};
    EXPECT_FALSE(ConvertClientBagSlot(0, 17, out, nullptr));
}

TEST(ReagentBankRules, CarriedBag1Slot1MapsTo19_0)
{
    BagSlot out;
    ASSERT_TRUE(ConvertClientBagSlot(1, 1, out, nullptr));
    EXPECT_EQ(out.bag, static_cast<uint8_t>(19));
    EXPECT_EQ(out.slot, static_cast<uint8_t>(0));
}

TEST(ReagentBankRules, CarriedBag4Slot1MapsTo22_0)
{
    BagSlot out;
    ASSERT_TRUE(ConvertClientBagSlot(4, 1, out, nullptr));
    EXPECT_EQ(out.bag, static_cast<uint8_t>(22));
    EXPECT_EQ(out.slot, static_cast<uint8_t>(0));
}

TEST(ReagentBankRules, CarriedBag5Rejected)
{
    BagSlot out{};
    EXPECT_FALSE(ConvertClientBagSlot(5, 1, out, nullptr));
}

TEST(ReagentBankRules, CarriedBagSlotNotBoundedBy36)
{
    BagSlot out;
    ASSERT_TRUE(ConvertClientBagSlot(1, 37, out, nullptr));
    EXPECT_EQ(out.bag, kInventorySlotBagStart);
    EXPECT_EQ(out.slot, static_cast<uint8_t>(36));
}

TEST(ReagentBankRules, CarriedBagSlot0Rejected)
{
    BagSlot out{};
    EXPECT_FALSE(ConvertClientBagSlot(2, 0, out, nullptr));
}

TEST(ReagentBankRules, IsEligibleClassAcceptsGemReagentTradeGoods)
{
    EXPECT_TRUE(IsEligibleClass(kItemClassGem));
    EXPECT_TRUE(IsEligibleClass(kItemClassReagent));
    EXPECT_TRUE(IsEligibleClass(kItemClassTradeGoods));
    EXPECT_FALSE(IsEligibleClass(0));
    EXPECT_FALSE(IsEligibleClass(1));
    EXPECT_FALSE(IsEligibleClass(2));
    EXPECT_FALSE(IsEligibleClass(4));
    EXPECT_FALSE(IsEligibleClass(6));
}

TEST(ReagentBankRules, EligibleTradeGoodsPass)
{
    ItemView view = EligibleView();
    view.itemClass = kItemClassTradeGoods;
    EXPECT_TRUE(CanStorePrototype(view, nullptr));
    EXPECT_TRUE(CanStore(view, nullptr));
}

TEST(ReagentBankRules, EligibleGemPass)
{
    ItemView view = EligibleView();
    view.itemClass = kItemClassGem;
    EXPECT_TRUE(CanStorePrototype(view, nullptr));
    EXPECT_TRUE(CanStore(view, nullptr));
}

TEST(ReagentBankRules, EligibleReagentPass)
{
    ItemView view = EligibleView();
    view.itemClass = kItemClassReagent;
    EXPECT_TRUE(CanStorePrototype(view, nullptr));
    EXPECT_TRUE(CanStore(view, nullptr));
}

TEST(ReagentBankRules, LegacyRedeemAllowsOnlyUpstreamGemAndTradeGoods)
{
    ItemView view = EligibleView();
    view.itemClass = kItemClassTradeGoods;
    view.stackable = 1;
    view.bonding = 1;
    EXPECT_TRUE(CanRedeemLegacyPrototype(view, nullptr));

    view.itemClass = kItemClassGem;
    EXPECT_TRUE(CanRedeemLegacyPrototype(view, nullptr));

    view.itemClass = kItemClassReagent;
    EXPECT_FALSE(CanRedeemLegacyPrototype(view, nullptr));

    view.itemClass = kItemClassTradeGoods;
    view.protoExists = false;
    EXPECT_FALSE(CanRedeemLegacyPrototype(view, nullptr));
}

TEST(ReagentBankRules, MaxAmountParserAcceptsFullUint32RangeOnly)
{
    uint32_t value = 0;
    EXPECT_TRUE(ReagentBank::ParseMaxAmount("1", value));
    EXPECT_EQ(value, 1u);
    EXPECT_TRUE(ReagentBank::ParseMaxAmount("4294967295", value));
    EXPECT_EQ(value, std::numeric_limits<uint32_t>::max());

    EXPECT_FALSE(ReagentBank::ParseMaxAmount("", value));
    EXPECT_FALSE(ReagentBank::ParseMaxAmount("0", value));
    EXPECT_FALSE(ReagentBank::ParseMaxAmount("-1", value));
    EXPECT_FALSE(ReagentBank::ParseMaxAmount("4294967296", value));
    EXPECT_FALSE(ReagentBank::ParseMaxAmount(" 1", value));
    EXPECT_FALSE(ReagentBank::ParseMaxAmount("1x", value));
}

TEST(ReagentBankRules, PurchaseCostParserAcceptsSafeGoldRange)
{
    uint32_t value = 999;
    EXPECT_TRUE(ReagentBank::ParsePurchaseCostGold("0", value));
    EXPECT_EQ(value, 0u);
    EXPECT_TRUE(ReagentBank::ParsePurchaseCostGold("250", value));
    EXPECT_EQ(value, 250u);
    EXPECT_TRUE(ReagentBank::ParsePurchaseCostGold("214748", value));
    EXPECT_EQ(value, 214748u);
    EXPECT_FALSE(ReagentBank::ParsePurchaseCostGold("214749", value));
    EXPECT_FALSE(ReagentBank::ParsePurchaseCostGold("-1", value));
    EXPECT_FALSE(ReagentBank::ParsePurchaseCostGold("1.5", value));
}

TEST(ReagentBankRules, RejectsWrongClass)
{
    ItemView view = EligibleView();
    view.itemClass = 1;
    std::string reason;
    EXPECT_FALSE(CanStorePrototype(view, &reason));
    EXPECT_FALSE(CanStore(view, nullptr));
    EXPECT_FALSE(reason.empty());
}

TEST(ReagentBankRules, RejectsStackableOne)
{
    ItemView view = EligibleView();
    view.stackable = 1;
    EXPECT_FALSE(CanStorePrototype(view, nullptr));
    EXPECT_FALSE(CanStore(view, nullptr));
}

TEST(ReagentBankRules, RejectsMaxCountNonZero)
{
    ItemView view = EligibleView();
    view.maxCount = 1;
    EXPECT_FALSE(CanStorePrototype(view, nullptr));
    EXPECT_FALSE(CanStore(view, nullptr));
}

TEST(ReagentBankRules, RejectsBondingNonZero)
{
    ItemView view = EligibleView();
    view.bonding = 1;
    EXPECT_FALSE(CanStorePrototype(view, nullptr));
    EXPECT_FALSE(CanStore(view, nullptr));
}

TEST(ReagentBankRules, RejectsConjured)
{
    ItemView view = EligibleView();
    view.flags = kItemFlagConjured;
    EXPECT_FALSE(CanStorePrototype(view, nullptr));
    EXPECT_FALSE(CanStore(view, nullptr));
}

TEST(ReagentBankRules, RejectsLootable)
{
    ItemView view = EligibleView();
    view.flags = kItemFlagLootable;
    EXPECT_FALSE(CanStorePrototype(view, nullptr));
    EXPECT_FALSE(CanStore(view, nullptr));
}

TEST(ReagentBankRules, RejectsWrapper)
{
    ItemView view = EligibleView();
    view.flags = kItemFlagWrapper;
    EXPECT_FALSE(CanStorePrototype(view, nullptr));
    EXPECT_FALSE(CanStore(view, nullptr));
}

TEST(ReagentBankRules, RejectsDuration)
{
    ItemView view = EligibleView();
    view.duration = 60;
    EXPECT_FALSE(CanStorePrototype(view, nullptr));
    EXPECT_FALSE(CanStore(view, nullptr));
}

TEST(ReagentBankRules, RejectsSoulbound)
{
    ItemView view = EligibleView();
    view.soulbound = true;
    EXPECT_TRUE(CanStorePrototype(view, nullptr));
    EXPECT_FALSE(CanStore(view, nullptr));
}

TEST(ReagentBankRules, RejectsAccountBound)
{
    ItemView view = EligibleView();
    view.accountBound = true;
    EXPECT_FALSE(CanStore(view, nullptr));
}

TEST(ReagentBankRules, RejectsWrapped)
{
    ItemView view = EligibleView();
    view.wrapped = true;
    EXPECT_FALSE(CanStore(view, nullptr));
}

TEST(ReagentBankRules, RejectsInTrade)
{
    ItemView view = EligibleView();
    view.inTrade = true;
    EXPECT_FALSE(CanStore(view, nullptr));
}

TEST(ReagentBankRules, RejectsRandomProperty)
{
    ItemView view = EligibleView();
    view.hasRandomProperty = true;
    EXPECT_FALSE(CanStore(view, nullptr));
}

TEST(ReagentBankRules, RejectsEnchantment)
{
    ItemView view = EligibleView();
    view.hasEnchantment = true;
    EXPECT_FALSE(CanStore(view, nullptr));
}

TEST(ReagentBankRules, RejectsGeneratedLoot)
{
    ItemView view = EligibleView();
    view.hasGeneratedLoot = true;
    EXPECT_FALSE(CanStore(view, nullptr));
}

TEST(ReagentBankRules, RejectsNotOwned)
{
    ItemView view = EligibleView();
    view.ownedByRequester = false;
    EXPECT_FALSE(CanStore(view, nullptr));
}

TEST(ReagentBankRules, RejectsNotInCarriedBags)
{
    ItemView view = EligibleView();
    view.inCarriedBags = false;
    EXPECT_FALSE(CanStore(view, nullptr));
}

TEST(ReagentBankRules, RejectsMissingPrototype)
{
    ItemView view = EligibleView();
    view.protoExists = false;
    EXPECT_FALSE(CanStorePrototype(view, nullptr));
    EXPECT_FALSE(CanStore(view, nullptr));
}

TEST(ReagentBankRules, RejectsEmptyCount)
{
    ItemView view = EligibleView();
    view.count = 0;
    EXPECT_TRUE(CanStorePrototype(view, nullptr));
    EXPECT_FALSE(CanStore(view, nullptr));
}

TEST(ReagentBankRules, SingleDepositAndDepositAllShareCanStore)
{
    ItemView view = EligibleView();
    EXPECT_TRUE(CanStore(view, nullptr));
    view.soulbound = true;
    EXPECT_FALSE(CanStore(view, nullptr));
    view = EligibleView();
    view.stackable = 1;
    EXPECT_FALSE(CanStore(view, nullptr));
}

TEST(ReagentBankRules, CapSumExactBoundaryDoesNotOverflow)
{
    bool overflows = true;
    EXPECT_EQ(CapSum(100, 50, 150, overflows), 150u);
    EXPECT_FALSE(overflows);

    overflows = true;
    EXPECT_EQ(CapSum(0, 1, 1, overflows), 1u);
    EXPECT_FALSE(overflows);
}

TEST(ReagentBankRules, CapSumOverCapOverflows)
{
    bool overflows = false;
    EXPECT_EQ(CapSum(100, 51, 150, overflows), 151u);
    EXPECT_TRUE(overflows);

    overflows = false;
    EXPECT_EQ(CapSum(150, 1, 150, overflows), 151u);
    EXPECT_TRUE(overflows);
}

TEST(ReagentBankRules, CapSumUint64AddOverflow)
{
    bool overflows = false;
    uint64_t const sum = CapSum(std::numeric_limits<uint64_t>::max(), 1, 100, overflows);
    EXPECT_TRUE(overflows);
    EXPECT_EQ(sum, std::numeric_limits<uint64_t>::max());
}

TEST(ReagentBankRules, SessionAbortedIsNotAClientResultCode)
{
    EXPECT_STREQ(ReagentBank::kSessionAbortedError, "SESSION_ABORTED");
    EXPECT_TRUE(ReagentBank::IsSessionAbortedError(ReagentBank::kSessionAbortedError));
    EXPECT_FALSE(ReagentBank::IsSessionAbortedError("DB_ERROR"));
    EXPECT_FALSE(ReagentBank::IsSessionAbortedError(""));

    EXPECT_TRUE(ReagentBank::MutationUnloadedPlayer(ReagentBank::MutationStatus::SessionAborted));
    EXPECT_FALSE(ReagentBank::MutationUnloadedPlayer(ReagentBank::MutationStatus::Ok));
    EXPECT_FALSE(ReagentBank::MutationUnloadedPlayer(ReagentBank::MutationStatus::Failed));

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
        EXPECT_STRNE(ReagentBank::kSessionAbortedError, ReagentBank::ResultCodeToken(code));
}

TEST(ReagentBankRules, MergeAllowsPlainDestinationEvenOutsideCanStoreBags)
{
    ItemView view = EligibleView();
    view.inCarriedBags = false;
    view.ownedByRequester = false;
    EXPECT_FALSE(CanStore(view, nullptr));
    EXPECT_TRUE(CanMergeWithdrawnInto(view, nullptr));
}

TEST(ReagentBankRules, MergeRejectsModifiedExistingStacks)
{
    ItemView view = EligibleView();
    EXPECT_TRUE(CanMergeWithdrawnInto(view, nullptr));

    view = EligibleView();
    view.soulbound = true;
    EXPECT_FALSE(CanMergeWithdrawnInto(view, nullptr));

    view = EligibleView();
    view.hasEnchantment = true;
    EXPECT_FALSE(CanMergeWithdrawnInto(view, nullptr));

    view = EligibleView();
    view.hasRandomProperty = true;
    EXPECT_FALSE(CanMergeWithdrawnInto(view, nullptr));

    view = EligibleView();
    view.wrapped = true;
    EXPECT_FALSE(CanMergeWithdrawnInto(view, nullptr));

    view = EligibleView();
    view.hasCreator = true;
    EXPECT_FALSE(CanMergeWithdrawnInto(view, nullptr));

    view = EligibleView();
    view.duration = 30;
    EXPECT_FALSE(CanMergeWithdrawnInto(view, nullptr));

    view = EligibleView();
    view.hasGeneratedLoot = true;
    EXPECT_FALSE(CanMergeWithdrawnInto(view, nullptr));
}

TEST(ReagentBankRules, MutationSqlNeverWritesNullAndFailsStaleIndependentlyOfSqlMode)
{
    std::string const probe = FormatMutationGuardProbeSql(42, 2589, 20, 3);
    EXPECT_NE(probe.find("INSERT INTO custom_reagent_bank_mutation_guard"), std::string::npos);
    EXPECT_NE(probe.find("SELECT 1"), std::string::npos);
    EXPECT_NE(probe.find("NOT EXISTS"), std::string::npos);
    EXPECT_NE(probe.find("character_id = 42"), std::string::npos);
    EXPECT_NE(probe.find("item_entry = 2589"), std::string::npos);
    EXPECT_NE(probe.find("amount = 20"), std::string::npos);
    EXPECT_NE(probe.find("revision = 3"), std::string::npos);
    EXPECT_EQ(probe.find("NULL"), std::string::npos);

    std::string const insert = FormatCreditInsertSql(42, 2589, 7, 20);
    EXPECT_NE(insert.find("legacy, mutation_guard"), std::string::npos);
    EXPECT_NE(insert.find(", 1, 0, 1)"), std::string::npos);
    EXPECT_EQ(insert.find("NULL"), std::string::npos);

    std::string const credit = FormatCreditUpdateSql(42, 2589, 7, 20, 999980, 10, 2, 0);
    EXPECT_NE(credit.find("mutation_guard = IF("), std::string::npos);
    EXPECT_NE(credit.find(", 1, 0)"), std::string::npos);
    EXPECT_NE(credit.find("amount = 10"), std::string::npos);
    EXPECT_NE(credit.find("revision = 2"), std::string::npos);
    EXPECT_NE(credit.find("amount = amount + 20"), std::string::npos);
    EXPECT_NE(credit.find("item_subclass = 7"), std::string::npos);
    EXPECT_NE(credit.find("revision = revision + 1"), std::string::npos);
    EXPECT_EQ(credit.find("amount = IF("), std::string::npos);
    EXPECT_EQ(credit.find("revision = IF("), std::string::npos);
    EXPECT_EQ(credit.find("NULL"), std::string::npos);
    EXPECT_EQ(credit.find("mutation_guard = NULL"), std::string::npos);

    std::string const debit = FormatDebitUpdateSql(42, 2589, 0, 10, 10, 2, 1);
    EXPECT_NE(debit.find("mutation_guard = IF("), std::string::npos);
    EXPECT_NE(debit.find(", 1, 0)"), std::string::npos);
    EXPECT_NE(debit.find("amount = 0"), std::string::npos);
    EXPECT_NE(debit.find("revision = revision + 1"), std::string::npos);
    EXPECT_EQ(debit.find("amount = IF("), std::string::npos);
    EXPECT_EQ(debit.find("revision = IF("), std::string::npos);
    EXPECT_EQ(debit.find("NULL"), std::string::npos);

    std::string const del = FormatDebitDeleteSql(42, 2589, 3, 1);
    EXPECT_NE(del.find("amount = 0"), std::string::npos);
    EXPECT_NE(del.find("revision = 3"), std::string::npos);
    EXPECT_NE(del.find("legacy = 1"), std::string::npos);
}

#ifdef REAGENT_BANK_SQL_PATH
TEST(ReagentBankRules, MigrationSqlQuarantinesSignedRowsAndKeepsFkGuard)
{
    std::ifstream in(REAGENT_BANK_SQL_PATH);
    ASSERT_TRUE(in) << REAGENT_BANK_SQL_PATH;
    std::string const sql((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    EXPECT_NE(sql.find("custom_reagent_bank_legacy_quarantine"), std::string::npos);
    EXPECT_NE(sql.find("INVALID_SIGNED_ROW"), std::string::npos);
    EXPECT_NE(sql.find("character_id` <= 0 OR `item_entry` <= 0 OR `amount` <= 0"), std::string::npos);
    EXPECT_NE(sql.find("@legacy_column_missing"), std::string::npos);
    EXPECT_NE(sql.find("ADD COLUMN `legacy` TINYINT UNSIGNED NOT NULL DEFAULT 0"), std::string::npos);
    EXPECT_NE(sql.find("SET `legacy` = 1 WHERE `legacy` <> 1"), std::string::npos);
    EXPECT_EQ(sql.find("SET `legacy` = 1 WHERE `legacy` = 0"), std::string::npos);
    EXPECT_NE(sql.find("fk_reagent_bank_mutation_guard"), std::string::npos);
    EXPECT_NE(sql.find("custom_reagent_bank_mutation_guard"), std::string::npos);
    EXPECT_EQ(sql.find("mutation_guard` = NULL"), std::string::npos);
    EXPECT_EQ(sql.find("SET `mutation_guard` = NULL"), std::string::npos);
}
#endif
