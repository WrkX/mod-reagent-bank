/*
 * Headless tests for the Material Storage addon protocol.
 */

#include "gtest/gtest.h"
#include "ReagentBankProtocol.h"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace
{
    using ReagentBank::C2SMessage;
    using ReagentBank::Command;
    using ReagentBank::MaxC2SWireBytes;
    using ReagentBank::MaxPayloadBytes;
    using ReagentBank::MaxRequestId;
    using ReagentBank::ResultCode;
    using ReagentBank::SnapshotChunk;
    using ReagentBank::StoredRow;

    constexpr uint32_t kU32Max = std::numeric_limits<uint32_t>::max();

    bool ParseU32(std::string const& text, uint32_t minValue, uint32_t maxValue, uint32_t& out)
    {
        return ReagentBank::ParseUInt32Strict(text, minValue, maxValue, out);
    }

    std::vector<std::string> SplitChar(std::string const& text, char sep)
    {
        std::vector<std::string> fields;
        std::size_t start = 0;
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == sep)
            {
                fields.emplace_back(text, start, i - start);
                start = i + 1;
            }
        }
        fields.emplace_back(text, start, text.size() - start);
        return fields;
    }

    std::string FormatRow(StoredRow const& row)
    {
        return std::to_string(row.entry) + ':' + std::to_string(row.amount) + ':'
            + std::to_string(row.itemClass) + ':' + std::to_string(row.itemSubclass);
    }

    std::vector<StoredRow> ReconstructRows(uint32_t requestId, std::vector<SnapshotChunk> const& chunks)
    {
        std::vector<StoredRow> rows;
        std::string const header = "1\tITEMS\t" + std::to_string(requestId) + '\t';
        for (SnapshotChunk const& chunk : chunks)
        {
            EXPECT_LE(chunk.payload.size(), MaxPayloadBytes);
            EXPECT_EQ(chunk.payload.compare(0, header.size(), header), 0);
            std::vector<std::string> const fields = ReagentBank::SplitTabs(chunk.payload);
            EXPECT_EQ(fields.size(), 4u);
            EXPECT_EQ(fields[0], "1");
            EXPECT_EQ(fields[1], "ITEMS");
            uint32_t parsedRequest = 0;
            EXPECT_TRUE(ParseU32(fields[2], 0, kU32Max, parsedRequest));
            EXPECT_EQ(parsedRequest, requestId);

            std::vector<std::string> const bodies = SplitChar(fields[3], ';');
            for (std::string const& body : bodies)
            {
                if (body.empty())
                    continue;
                std::vector<std::string> const parts = SplitChar(body, ':');
                EXPECT_EQ(parts.size(), 4u);
                StoredRow row;
                EXPECT_TRUE(ParseU32(parts[0], 0, kU32Max, row.entry));
                EXPECT_TRUE(ParseU32(parts[1], 0, kU32Max, row.amount));
                EXPECT_TRUE(ParseU32(parts[2], 0, kU32Max, row.itemClass));
                EXPECT_TRUE(ParseU32(parts[3], 0, kU32Max, row.itemSubclass));
                rows.push_back(row);
            }
        }
        return rows;
    }

    bool RowsEqual(StoredRow const& a, StoredRow const& b)
    {
        return a.entry == b.entry && a.amount == b.amount
            && a.itemClass == b.itemClass && a.itemSubclass == b.itemSubclass;
    }
}

TEST(ReagentBankProtocolTest, SplitTabsEmpty)
{
    std::vector<std::string> const fields = ReagentBank::SplitTabs("");
    ASSERT_EQ(fields.size(), 1u);
    EXPECT_EQ(fields[0], "");
}

TEST(ReagentBankProtocolTest, SplitTabsConsecutiveTabs)
{
    std::vector<std::string> const fields = ReagentBank::SplitTabs("a\t\tb");
    ASSERT_EQ(fields.size(), 3u);
    EXPECT_EQ(fields[0], "a");
    EXPECT_EQ(fields[1], "");
    EXPECT_EQ(fields[2], "b");
}

TEST(ReagentBankProtocolTest, SplitTabsTrailingTab)
{
    std::vector<std::string> const fields = ReagentBank::SplitTabs("a\tb\t");
    ASSERT_EQ(fields.size(), 3u);
    EXPECT_EQ(fields[0], "a");
    EXPECT_EQ(fields[1], "b");
    EXPECT_EQ(fields[2], "");
}

TEST(ReagentBankProtocolTest, ParseUInt32StrictAcceptsZeroMaxPlusAndLeadingZeros)
{
    uint32_t value = 99;
    ASSERT_TRUE(ParseU32("0", 0, kU32Max, value));
    EXPECT_EQ(value, 0u);

    ASSERT_TRUE(ParseU32("4294967295", 0, kU32Max, value));
    EXPECT_EQ(value, kU32Max);

    ASSERT_TRUE(ParseU32("+0", 0, kU32Max, value));
    EXPECT_EQ(value, 0u);

    ASSERT_TRUE(ParseU32("+4294967295", 0, kU32Max, value));
    EXPECT_EQ(value, kU32Max);

    ASSERT_TRUE(ParseU32("01", 0, kU32Max, value));
    EXPECT_EQ(value, 1u);

    ASSERT_TRUE(ParseU32("+01", 0, 100, value));
    EXPECT_EQ(value, 1u);

    ASSERT_TRUE(ParseU32("10", 10, 10, value));
    EXPECT_EQ(value, 10u);
}

TEST(ReagentBankProtocolTest, ParseUInt32StrictRejectsOverflowNegativeEmptyJunkPlusWhitespace)
{
    uint32_t value = 77;
    EXPECT_FALSE(ParseU32("999999999999", 0, kU32Max, value));
    EXPECT_EQ(value, 77u);

    EXPECT_FALSE(ParseU32("4294967296", 0, kU32Max, value));
    EXPECT_EQ(value, 77u);

    EXPECT_FALSE(ParseU32("-1", 0, kU32Max, value));
    EXPECT_FALSE(ParseU32("-0", 0, kU32Max, value));
    EXPECT_FALSE(ParseU32("", 0, kU32Max, value));
    EXPECT_FALSE(ParseU32("1abc", 0, kU32Max, value));
    EXPECT_FALSE(ParseU32("abc", 0, kU32Max, value));
    EXPECT_FALSE(ParseU32("1.0", 0, kU32Max, value));
    EXPECT_FALSE(ParseU32("+", 0, kU32Max, value));
    EXPECT_FALSE(ParseU32("++1", 0, kU32Max, value));
    EXPECT_FALSE(ParseU32(" 1", 0, kU32Max, value));
    EXPECT_FALSE(ParseU32("1 ", 0, kU32Max, value));
    EXPECT_FALSE(ParseU32("1\t", 0, kU32Max, value));
    EXPECT_FALSE(ParseU32("\t1", 0, kU32Max, value));
    EXPECT_FALSE(ParseU32("1\n", 0, kU32Max, value));
    EXPECT_FALSE(ParseU32("0x1", 0, kU32Max, value));
    EXPECT_FALSE(ParseU32("11", 0, 10, value));
    EXPECT_EQ(value, 77u);
}

TEST(ReagentBankProtocolTest, ParseC2SQuery)
{
    C2SMessage const msg = ReagentBank::ParseC2S("RBANK\t1\tQUERY\t42");
    EXPECT_TRUE(msg.valid);
    EXPECT_TRUE(msg.error.empty());
    EXPECT_EQ(msg.version, 1u);
    EXPECT_EQ(msg.command, Command::Query);
    EXPECT_EQ(msg.requestId, 42u);
}

TEST(ReagentBankProtocolTest, ParseC2SDeposit)
{
    C2SMessage const msg = ReagentBank::ParseC2S("RBANK\t1\tDEPOSIT\t8\t0\t16");
    EXPECT_TRUE(msg.valid);
    EXPECT_EQ(msg.command, Command::Deposit);
    EXPECT_EQ(msg.requestId, 8u);
    EXPECT_EQ(msg.clientBag, 0u);
    EXPECT_EQ(msg.clientSlot, 16u);
}

TEST(ReagentBankProtocolTest, ParseC2SDepositAll)
{
    C2SMessage const msg = ReagentBank::ParseC2S("RBANK\t1\tDEPOSIT_ALL\t3");
    EXPECT_TRUE(msg.valid);
    EXPECT_EQ(msg.command, Command::DepositAll);
    EXPECT_EQ(msg.requestId, 3u);
}

TEST(ReagentBankProtocolTest, ParseC2SWithdraw)
{
    C2SMessage const msg = ReagentBank::ParseC2S("RBANK\t1\tWITHDRAW\t9\t1234\t20");
    EXPECT_TRUE(msg.valid);
    EXPECT_EQ(msg.command, Command::Withdraw);
    EXPECT_EQ(msg.requestId, 9u);
    EXPECT_EQ(msg.itemEntry, 1234u);
    EXPECT_EQ(msg.amount, 20u);
}

TEST(ReagentBankProtocolTest, ParseC2SClose)
{
    C2SMessage const msg = ReagentBank::ParseC2S("RBANK\t1\tCLOSE\t1");
    EXPECT_TRUE(msg.valid);
    EXPECT_EQ(msg.command, Command::Close);
    EXPECT_EQ(msg.requestId, 1u);
}

TEST(ReagentBankProtocolTest, ParseC2SAcceptsUint32MaxFields)
{
    C2SMessage const msg = ReagentBank::ParseC2S("RBANK\t1\tWITHDRAW\t1000000\t4294967295\t0");
    EXPECT_TRUE(msg.valid);
    EXPECT_EQ(msg.requestId, MaxRequestId);
    EXPECT_EQ(msg.itemEntry, kU32Max);
    EXPECT_EQ(msg.amount, 0u);
}

TEST(ReagentBankProtocolTest, ParseC2SRejectsReservedAndOutOfRingRequestIds)
{
    EXPECT_FALSE(ReagentBank::ParseC2S("RBANK\t1\tQUERY\t0").valid);
    EXPECT_FALSE(ReagentBank::ParseC2S("RBANK\t1\tQUERY\t1000001").valid);
}

TEST(ReagentBankProtocolTest, ParseC2SRejectsOversizedPayloadBeforeTokenization)
{
    std::string const oversized = "RBANK\t1\tQUERY\t1" + std::string(MaxC2SWireBytes, '\t');
    C2SMessage const msg = ReagentBank::ParseC2S(oversized);
    EXPECT_FALSE(msg.valid);
    EXPECT_EQ(msg.error, "payload too long");
}

TEST(ReagentBankProtocolTest, RequestIdsAreStrictlyForwardInTheAddonRing)
{
    EXPECT_TRUE(ReagentBank::IsNewerRequestId(2, 1));
    EXPECT_TRUE(ReagentBank::IsNewerRequestId(1, MaxRequestId));
    EXPECT_TRUE(ReagentBank::IsNewerRequestId(17, MaxRequestId - 3));

    EXPECT_FALSE(ReagentBank::IsNewerRequestId(1, 1));
    EXPECT_FALSE(ReagentBank::IsNewerRequestId(1, 2));
    EXPECT_FALSE(ReagentBank::IsNewerRequestId(0, 1));
    EXPECT_FALSE(ReagentBank::IsNewerRequestId(1, 0));
    EXPECT_FALSE(ReagentBank::IsNewerRequestId(1 + (MaxRequestId / 2), 1));
}

TEST(ReagentBankProtocolTest, ParseC2SUnknownVersion)
{
    C2SMessage const msg = ReagentBank::ParseC2S("RBANK\t2\tQUERY\t1");
    EXPECT_FALSE(msg.valid);
    EXPECT_FALSE(msg.error.empty());
    EXPECT_EQ(msg.version, 2u);
    EXPECT_EQ(msg.command, Command::Unknown);
}

TEST(ReagentBankProtocolTest, ParseC2SUnknownCommand)
{
    C2SMessage const msg = ReagentBank::ParseC2S("RBANK\t1\tPING\t1");
    EXPECT_FALSE(msg.valid);
    EXPECT_FALSE(msg.error.empty());
    EXPECT_EQ(msg.command, Command::Unknown);

    C2SMessage const lower = ReagentBank::ParseC2S("RBANK\t1\tquery\t1");
    EXPECT_FALSE(lower.valid);
    EXPECT_EQ(lower.command, Command::Unknown);
}

TEST(ReagentBankProtocolTest, ParseC2SExtraFields)
{
    C2SMessage const query = ReagentBank::ParseC2S("RBANK\t1\tQUERY\t1\textra");
    EXPECT_FALSE(query.valid);
    EXPECT_FALSE(query.error.empty());

    C2SMessage const deposit = ReagentBank::ParseC2S("RBANK\t1\tDEPOSIT\t1\t0\t1\t0");
    EXPECT_FALSE(deposit.valid);

    C2SMessage const depositAll = ReagentBank::ParseC2S("RBANK\t1\tDEPOSIT_ALL\t1\t0");
    EXPECT_FALSE(depositAll.valid);

    C2SMessage const withdraw = ReagentBank::ParseC2S("RBANK\t1\tWITHDRAW\t1\t10\t1\t1");
    EXPECT_FALSE(withdraw.valid);

    C2SMessage const close = ReagentBank::ParseC2S("RBANK\t1\tCLOSE\t1\t");
    EXPECT_FALSE(close.valid);
}

TEST(ReagentBankProtocolTest, ParseC2SMissingFields)
{
    C2SMessage const query = ReagentBank::ParseC2S("RBANK\t1\tQUERY");
    EXPECT_FALSE(query.valid);
    EXPECT_FALSE(query.error.empty());

    C2SMessage const deposit = ReagentBank::ParseC2S("RBANK\t1\tDEPOSIT\t1\t0");
    EXPECT_FALSE(deposit.valid);

    C2SMessage const depositAll = ReagentBank::ParseC2S("RBANK\t1\tDEPOSIT_ALL");
    EXPECT_FALSE(depositAll.valid);

    C2SMessage const withdraw = ReagentBank::ParseC2S("RBANK\t1\tWITHDRAW\t1\t100");
    EXPECT_FALSE(withdraw.valid);

    C2SMessage const close = ReagentBank::ParseC2S("RBANK\t1\tCLOSE");
    EXPECT_FALSE(close.valid);

    C2SMessage const badId = ReagentBank::ParseC2S("RBANK\t1\tQUERY\t");
    EXPECT_FALSE(badId.valid);
}

TEST(ReagentBankProtocolTest, ParseC2SNotPrefixed)
{
    C2SMessage const payload = ReagentBank::ParseC2S("1\tQUERY\t1");
    EXPECT_FALSE(payload.valid);
    EXPECT_FALSE(payload.error.empty());
    EXPECT_EQ(payload.command, Command::Unknown);

    C2SMessage const bare = ReagentBank::ParseC2S("QUERY\t1");
    EXPECT_FALSE(bare.valid);
}

TEST(ReagentBankProtocolTest, IsExactC2SPrefix)
{
    EXPECT_TRUE(ReagentBank::IsExactC2SPrefix("RBANK\t1\tQUERY\t1"));
    EXPECT_TRUE(ReagentBank::IsExactC2SPrefix("RBANK\t1\t"));
    EXPECT_FALSE(ReagentBank::IsExactC2SPrefix("xRBANK\t1\t"));
    EXPECT_FALSE(ReagentBank::IsExactC2SPrefix("RBANK\t1"));
    EXPECT_FALSE(ReagentBank::IsExactC2SPrefix("RBANK\t2\tQUERY\t1"));
    EXPECT_FALSE(ReagentBank::IsExactC2SPrefix(""));
}

TEST(ReagentBankProtocolTest, FormatHelpers)
{
    EXPECT_EQ(ReagentBank::FormatOpen(0), "1\tOPEN\t0");
    EXPECT_EQ(ReagentBank::FormatOpen(7), "1\tOPEN\t7");
    EXPECT_EQ(ReagentBank::FormatBegin(1, 2, 3), "1\tBEGIN\t1\t2\t3");
    EXPECT_EQ(ReagentBank::FormatEnd(9, 4), "1\tEND\t9\t4");
    EXPECT_EQ(ReagentBank::FormatResult(5, ResultCode::Ok), "1\tRESULT\t5\tOK");
    EXPECT_EQ(ReagentBank::FormatResult(5, ResultCode::Ok, "3 stacks"), "1\tRESULT\t5\tOK\t3 stacks");
    EXPECT_EQ(ReagentBank::FormatResult(5, ResultCode::Ok, "has\ttab"), "1\tRESULT\t5\tOK");
    EXPECT_EQ(ReagentBank::FormatResult(5, ResultCode::Limit, "has\nnl"), "1\tRESULT\t5\tLIMIT");
    EXPECT_EQ(ReagentBank::FormatClose("NO_ACCESS"), "1\tCLOSE\tNO_ACCESS");
    EXPECT_EQ(ReagentBank::FormatClose(""), "1\tCLOSE\tCLOSED");
}

TEST(ReagentBankProtocolTest, ResultCodeTokenMapping)
{
    EXPECT_STREQ(ReagentBank::ResultCodeToken(ResultCode::Ok), "OK");
    EXPECT_STREQ(ReagentBank::ResultCodeToken(ResultCode::Disabled), "DISABLED");
    EXPECT_STREQ(ReagentBank::ResultCodeToken(ResultCode::NoAccess), "NO_ACCESS");
    EXPECT_STREQ(ReagentBank::ResultCodeToken(ResultCode::BadRequest), "BAD_REQUEST");
    EXPECT_STREQ(ReagentBank::ResultCodeToken(ResultCode::BadSlot), "BAD_SLOT");
    EXPECT_STREQ(ReagentBank::ResultCodeToken(ResultCode::NotEligible), "NOT_ELIGIBLE");
    EXPECT_STREQ(ReagentBank::ResultCodeToken(ResultCode::Limit), "LIMIT");
    EXPECT_STREQ(ReagentBank::ResultCodeToken(ResultCode::NoSpace), "NO_SPACE");
    EXPECT_STREQ(ReagentBank::ResultCodeToken(ResultCode::NotFound), "NOT_FOUND");
    EXPECT_STREQ(ReagentBank::ResultCodeToken(ResultCode::DbError), "DB_ERROR");

    EXPECT_EQ(ReagentBank::FormatResult(1, ResultCode::Disabled), "1\tRESULT\t1\tDISABLED");
    EXPECT_EQ(ReagentBank::FormatResult(1, ResultCode::NoAccess), "1\tRESULT\t1\tNO_ACCESS");
    EXPECT_EQ(ReagentBank::FormatResult(1, ResultCode::BadRequest), "1\tRESULT\t1\tBAD_REQUEST");
    EXPECT_EQ(ReagentBank::FormatResult(1, ResultCode::BadSlot), "1\tRESULT\t1\tBAD_SLOT");
    EXPECT_EQ(ReagentBank::FormatResult(1, ResultCode::NotEligible), "1\tRESULT\t1\tNOT_ELIGIBLE");
    EXPECT_EQ(ReagentBank::FormatResult(1, ResultCode::NoSpace), "1\tRESULT\t1\tNO_SPACE");
    EXPECT_EQ(ReagentBank::FormatResult(1, ResultCode::NotFound), "1\tRESULT\t1\tNOT_FOUND");
    EXPECT_EQ(ReagentBank::FormatResult(1, ResultCode::DbError), "1\tRESULT\t1\tDB_ERROR");
}

TEST(ReagentBankProtocolTest, BuildItemsChunksEmpty)
{
    std::vector<SnapshotChunk> const chunks = ReagentBank::BuildItemsChunks(1, {});
    EXPECT_TRUE(chunks.empty());
}

TEST(ReagentBankProtocolTest, BuildItemsChunksOneRow)
{
    std::vector<StoredRow> const rows = { StoredRow{1234, 20, 7, 1} };
    std::vector<SnapshotChunk> const chunks = ReagentBank::BuildItemsChunks(11, rows);
    ASSERT_EQ(chunks.size(), 1u);
    EXPECT_LE(chunks[0].payload.size(), MaxPayloadBytes);
    EXPECT_EQ(chunks[0].payload, "1\tITEMS\t11\t1234:20:7:1");

    std::vector<StoredRow> const reconstructed = ReconstructRows(11, chunks);
    ASSERT_EQ(reconstructed.size(), 1u);
    EXPECT_TRUE(RowsEqual(reconstructed[0], rows[0]));
}

TEST(ReagentBankProtocolTest, BuildItemsChunksMultipleChunksAndReconstruct)
{
    uint32_t const requestId = 99;
    std::vector<StoredRow> rows;
    rows.reserve(50);
    for (uint32_t i = 0; i < 50; ++i)
        rows.push_back(StoredRow{100000 + i, 100000, 7, 0});

    std::vector<SnapshotChunk> const chunks = ReagentBank::BuildItemsChunks(requestId, rows);
    ASSERT_GT(chunks.size(), 1u);
    for (SnapshotChunk const& chunk : chunks)
    {
        EXPECT_LE(chunk.payload.size(), MaxPayloadBytes);
        EXPECT_FALSE(chunk.payload.empty());
    }

    std::vector<StoredRow> const reconstructed = ReconstructRows(requestId, chunks);
    ASSERT_EQ(reconstructed.size(), rows.size());
    for (std::size_t i = 0; i < rows.size(); ++i)
        EXPECT_TRUE(RowsEqual(reconstructed[i], rows[i]));
}

TEST(ReagentBankProtocolTest, BuildItemsChunksSkipsRowThatCannotFitAlone)
{
    uint32_t const requestId = 1;
    std::string const header = "1\tITEMS\t" + std::to_string(requestId) + '\t';

    StoredRow const maxRow{kU32Max, kU32Max, kU32Max, kU32Max};
    std::vector<StoredRow> input;
    input.push_back(StoredRow{1000, 1, 7, 0});
    input.push_back(maxRow);
    for (uint32_t i = 0; i < 40; ++i)
        input.push_back(StoredRow{200000 + i, 250000, 5, 1});

    std::vector<StoredRow> expected;
    for (StoredRow const& row : input)
    {
        if (header.size() + FormatRow(row).size() <= MaxPayloadBytes)
            expected.push_back(row);
    }

    std::vector<SnapshotChunk> const chunks = ReagentBank::BuildItemsChunks(requestId, input);
    for (SnapshotChunk const& chunk : chunks)
        EXPECT_LE(chunk.payload.size(), MaxPayloadBytes);

    std::vector<StoredRow> const reconstructed = ReconstructRows(requestId, chunks);
    ASSERT_EQ(reconstructed.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i)
        EXPECT_TRUE(RowsEqual(reconstructed[i], expected[i]));

    // Extreme uint32 fields still fit alone; an oversized payload is never emitted.
    std::vector<SnapshotChunk> const extreme = ReagentBank::BuildItemsChunks(kU32Max, { maxRow });
    ASSERT_EQ(extreme.size(), 1u);
    EXPECT_LE(extreme[0].payload.size(), MaxPayloadBytes);
    EXPECT_EQ(extreme[0].payload, "1\tITEMS\t4294967295\t4294967295:4294967295:4294967295:4294967295");
}
