/*
 * Framed Material Storage protocol: tokenizer, integer parser, C2S decode,
 * and S2C payload builders. No game/core includes.
 */

#include "ReagentBankProtocol.h"

#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace ReagentBank
{
    namespace
    {
        constexpr std::size_t kC2SPrefixSize = 8; // "RBANK\t1\t"

        bool HasTabOrNewline(std::string const& text)
        {
            for (char const c : text)
            {
                if (c == '\t' || c == '\n' || c == '\r')
                    return true;
            }
            return false;
        }

        bool ParseFieldU32(std::string const& text, uint32_t& out)
        {
            return ParseUInt32Strict(text, 0, std::numeric_limits<uint32_t>::max(), out);
        }

        std::string FormatRow(StoredRow const& row)
        {
            return std::to_string(row.entry) + ':' + std::to_string(row.amount) + ':'
                + std::to_string(row.itemClass) + ':' + std::to_string(row.itemSubclass);
        }
    }

    char const* ResultCodeToken(ResultCode code)
    {
        switch (code)
        {
            case ResultCode::Ok:          return "OK";
            case ResultCode::Disabled:    return "DISABLED";
            case ResultCode::NoAccess:    return "NO_ACCESS";
            case ResultCode::BadRequest:  return "BAD_REQUEST";
            case ResultCode::BadSlot:     return "BAD_SLOT";
            case ResultCode::NotEligible: return "NOT_ELIGIBLE";
            case ResultCode::Limit:       return "LIMIT";
            case ResultCode::NoSpace:     return "NO_SPACE";
            case ResultCode::NotFound:    return "NOT_FOUND";
            case ResultCode::DbError:     return "DB_ERROR";
        }
        return "BAD_REQUEST";
    }

    std::vector<std::string> SplitTabs(std::string const& text)
    {
        std::vector<std::string> fields;
        std::size_t start = 0;
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == '\t')
            {
                fields.emplace_back(text, start, i - start);
                start = i + 1;
            }
        }
        fields.emplace_back(text, start, text.size() - start);
        return fields;
    }

    bool ParseUInt32Strict(std::string const& text, uint32_t minValue, uint32_t maxValue, uint32_t& out)
    {
        if (text.empty())
            return false;

        std::size_t i = 0;
        if (text[i] == '+')
        {
            ++i;
            if (i >= text.size())
                return false;
        }

        if (text[i] < '0' || text[i] > '9')
            return false;

        uint64_t acc = 0;
        constexpr uint64_t kUint32Max = std::numeric_limits<uint32_t>::max();
        for (; i < text.size(); ++i)
        {
            char const c = text[i];
            if (c < '0' || c > '9')
                return false;
            uint64_t const digit = static_cast<uint64_t>(c - '0');
            if (acc > (kUint32Max - digit) / 10)
                return false;
            acc = acc * 10 + digit;
        }

        if (acc < minValue || acc > maxValue)
            return false;

        out = static_cast<uint32_t>(acc);
        return true;
    }

    bool IsNewerRequestId(uint32_t candidate, uint32_t previous)
    {
        if (candidate == 0 || previous == 0)
            return false;

        // IDs wrap MaxRequestId -> 1. Exactly half a ring is ambiguous, so it
        // is deliberately rejected instead of risking an old mutation replay.
        uint32_t const forward = candidate > previous
            ? candidate - previous
            : MaxRequestId - previous + candidate;
        return forward != 0 && forward < (MaxRequestId / 2);
    }

    bool IsExactC2SPrefix(std::string const& wire)
    {
        return wire.compare(0, kC2SPrefixSize, C2SExactPrefix) == 0;
    }

    C2SMessage ParseC2S(std::string const& wire)
    {
        C2SMessage msg;

        // Do this before SplitTabs: addon chat is client-controlled and a
        // tab-heavy forged packet would otherwise allocate one string per tab.
        if (wire.size() > MaxC2SWireBytes)
        {
            msg.error = "payload too long";
            return msg;
        }

        std::vector<std::string> const fields = SplitTabs(wire);

        if (fields.size() < 2 || fields[0] != Prefix)
        {
            msg.error = "bad prefix";
            return msg;
        }

        if (!ParseFieldU32(fields[1], msg.version))
        {
            msg.error = "unknown version";
            return msg;
        }

        if (msg.version != ProtocolVersion)
        {
            msg.error = "unknown version";
            return msg;
        }

        // Version 1 is accepted only with the exact wire prefix "RBANK\t1\t".
        if (!IsExactC2SPrefix(wire))
        {
            msg.error = "bad prefix";
            return msg;
        }

        if (fields.size() < 3)
        {
            msg.error = "missing command";
            return msg;
        }

        std::string const& name = fields[2];
        std::size_t expected = 0;
        if (name == "PURCHASE")
        {
            msg.command = Command::Purchase;
            expected = 4;
        }
        else if (name == "QUERY")
        {
            msg.command = Command::Query;
            expected = 4;
        }
        else if (name == "DEPOSIT")
        {
            msg.command = Command::Deposit;
            expected = 6;
        }
        else if (name == "DEPOSIT_ALL")
        {
            msg.command = Command::DepositAll;
            expected = 4;
        }
        else if (name == "WITHDRAW")
        {
            msg.command = Command::Withdraw;
            expected = 6;
        }
        else if (name == "CLOSE")
        {
            msg.command = Command::Close;
            expected = 4;
        }
        else
        {
            msg.error = "unknown command";
            return msg;
        }

        if (fields.size() != expected)
        {
            msg.error = "bad field count";
            return msg;
        }

        if (!ParseUInt32Strict(fields[3], 1, MaxRequestId, msg.requestId))
        {
            msg.error = "bad requestId";
            return msg;
        }

        if (msg.command == Command::Deposit)
        {
            if (!ParseFieldU32(fields[4], msg.clientBag))
            {
                msg.error = "bad clientBag";
                return msg;
            }
            if (!ParseFieldU32(fields[5], msg.clientSlot))
            {
                msg.error = "bad clientSlot";
                return msg;
            }
        }
        else if (msg.command == Command::Withdraw)
        {
            if (!ParseFieldU32(fields[4], msg.itemEntry))
            {
                msg.error = "bad itemEntry";
                return msg;
            }
            if (!ParseFieldU32(fields[5], msg.amount))
            {
                msg.error = "bad amount";
                return msg;
            }
        }

        msg.valid = true;
        return msg;
    }

    std::string FormatOpen(uint32_t revision)
    {
        return "1\tOPEN\t" + std::to_string(revision);
    }

    std::string FormatBegin(uint32_t requestId, uint32_t revision, uint32_t rowCount)
    {
        return "1\tBEGIN\t" + std::to_string(requestId) + '\t' + std::to_string(revision)
            + '\t' + std::to_string(rowCount);
    }

    std::string FormatEnd(uint32_t requestId, uint32_t revision)
    {
        return "1\tEND\t" + std::to_string(requestId) + '\t' + std::to_string(revision);
    }

    std::string FormatResult(uint32_t requestId, ResultCode code, std::string const& detail)
    {
        std::string payload = "1\tRESULT\t" + std::to_string(requestId) + '\t' + ResultCodeToken(code);
        if (!detail.empty() && !HasTabOrNewline(detail))
            payload += '\t' + detail;
        return payload;
    }

    std::string FormatClose(std::string const& reason)
    {
        std::string const token = reason.empty() ? "CLOSED" : reason;
        return "1\tCLOSE\t" + token;
    }

    std::vector<SnapshotChunk> BuildItemsChunks(uint32_t requestId, std::vector<StoredRow> const& rows)
    {
        std::vector<SnapshotChunk> chunks;
        if (rows.empty())
            return chunks;

        std::string const header = "1\tITEMS\t" + std::to_string(requestId) + '\t';
        std::string current;

        auto flush = [&]() {
            if (current.empty())
                return;
            chunks.push_back(SnapshotChunk{current});
            current.clear();
        };

        for (StoredRow const& row : rows)
        {
            std::string const body = FormatRow(row);
            std::string candidate = current.empty() ? (header + body) : (current + ';' + body);
            if (candidate.size() <= MaxPayloadBytes)
            {
                current = std::move(candidate);
                continue;
            }

            if (!current.empty())
            {
                flush();
                candidate = header + body;
                if (candidate.size() <= MaxPayloadBytes)
                {
                    current = std::move(candidate);
                    continue;
                }
            }

            // A single row cannot fit in a chunk of its own: skip it.
        }

        flush();
        return chunks;
    }
}
