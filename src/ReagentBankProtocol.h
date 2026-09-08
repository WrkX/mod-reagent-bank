#ifndef MODULES_MOD_REAGENT_BANK_REAGENTBANKPROTOCOL_H
#define MODULES_MOD_REAGENT_BANK_REAGENTBANKPROTOCOL_H

#include <cstdint>
#include <string>
#include <vector>

// Framed Material Storage protocol. No game headers: tokenizer, integer parser,
// and snapshot chunking are unit-tested without linking the worldserver.

namespace ReagentBank
{
    constexpr char const* Prefix = "RBANK";
    constexpr uint32_t ProtocolVersion = 1;
    constexpr std::size_t MaxPayloadBytes = 240;
    // Client-to-server payloads include the RBANK prefix and are bounded before
    // tokenization. Keep this comfortably below the Vanilla addon-chat limit.
    constexpr std::size_t MaxC2SWireBytes = 240;
    constexpr char const* C2SExactPrefix = "RBANK\t1\t";

    // The bundled Vanilla addon emits request IDs in this ring, starting at 1
    // and wrapping from MaxRequestId back to 1. Zero is server-reserved for the
    // unsolicited snapshot sent immediately after OPEN.
    constexpr uint32_t MaxRequestId = 1000000;

    constexpr uint32_t MutationThrottleMs = 100;
    constexpr uint32_t QueryThrottleMs = 250;

    enum class Command
    {
        Unknown = 0,
        Query,
        Deposit,
        DepositAll,
        Withdraw,
        Close
    };

    enum class ResultCode
    {
        Ok,
        Disabled,
        NoAccess,
        BadRequest,
        BadSlot,
        NotEligible,
        Limit,
        NoSpace,
        NotFound,
        DbError
    };

    char const* ResultCodeToken(ResultCode code);

    struct C2SMessage
    {
        bool valid = false;
        uint32_t version = 0;
        Command command = Command::Unknown;
        uint32_t requestId = 0;
        uint32_t clientBag = 0;
        uint32_t clientSlot = 0;
        uint32_t itemEntry = 0;
        uint32_t amount = 0;
        std::string error;
    };

    struct StoredRow
    {
        uint32_t entry = 0;
        uint32_t amount = 0;
        uint32_t itemClass = 0;
        uint32_t itemSubclass = 0;
    };

    struct SnapshotChunk
    {
        std::string payload; // body after "RBANK\t", at most MaxPayloadBytes
    };

    // Split `text` on '\t'. Empty fields are preserved. No regex.
    std::vector<std::string> SplitTabs(std::string const& text);

    // Strict decimal parse: optional leading '+', then digits only, full
    // consumption, no whitespace, value in [minValue, maxValue].
    bool ParseUInt32Strict(std::string const& text, uint32_t minValue, uint32_t maxValue, uint32_t& out);

    // True only when `candidate` is forward of `previous` in the bounded
    // [1, MaxRequestId] request-ID ring. Equal IDs, old IDs, and an ambiguous
    // half-ring jump are rejected.
    bool IsNewerRequestId(uint32_t candidate, uint32_t previous);

    // `wire` is the full C2S chat body, including the RBANK prefix, and must
    // be no longer than MaxC2SWireBytes:
    //   RBANK\t1\tQUERY\t<requestId>
    C2SMessage ParseC2S(std::string const& wire);

    bool IsExactC2SPrefix(std::string const& wire);

    std::string FormatOpen(uint32_t revision);
    std::string FormatBegin(uint32_t requestId, uint32_t revision, uint32_t rowCount);
    std::string FormatEnd(uint32_t requestId, uint32_t revision);
    std::string FormatResult(uint32_t requestId, ResultCode code, std::string const& detail = {});
    std::string FormatClose(std::string const& reason);

    // Chunk complete `entry:amount:class:subclass` rows. Each ITEMS payload
    // (the string returned as SnapshotChunk::payload) is "1\tITEMS\t..." and
    // stays at or below MaxPayloadBytes. Empty snapshots produce no ITEMS
    // chunks.
    std::vector<SnapshotChunk> BuildItemsChunks(uint32_t requestId, std::vector<StoredRow> const& rows);
}

#endif
