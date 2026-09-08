/*
 * Material Storage config cache. Reloaded from WorldScript::OnAfterConfigLoad.
 */

#include "ReagentBankConfig.h"

#ifndef REAGENT_BANK_HEADLESS_TESTS
#include "Config/Config.h"
#include "Log.h"
#endif

#include <limits>

namespace ReagentBank
{
    bool ParseMaxAmount(std::string const& value, uint32_t& out)
    {
        if (value.empty())
            return false;

        uint64_t parsed = 0;
        constexpr uint64_t kMax = std::numeric_limits<uint32_t>::max();
        for (char const c : value)
        {
            if (c < '0' || c > '9')
                return false;
            uint64_t const digit = static_cast<uint64_t>(c - '0');
            if (parsed > (kMax - digit) / 10)
                return false;
            parsed = parsed * 10 + digit;
        }

        if (parsed == 0)
            return false;
        out = static_cast<uint32_t>(parsed);
        return true;
    }
}

#ifndef REAGENT_BANK_HEADLESS_TESTS
ReagentBankConfig& ReagentBankConfig::Instance()
{
    static ReagentBankConfig instance;
    return instance;
}

void ReagentBankConfig::Load(bool reload)
{
    m_enabled = sConfig.GetBoolDefault("ReagentBank.Enable", true);
    m_depositAllEnabled = sConfig.GetBoolDefault("ReagentBank.DepositAllEnable", true);
    m_debug = sConfig.GetBoolDefault("ReagentBank.Debug", false);

    std::string const rawMax = sConfig.GetStringDefault("ReagentBank.MaxAmountPerItem", "1000000");
    if (!ReagentBank::ParseMaxAmount(rawMax, m_maxAmountPerItem))
    {
        m_maxAmountPerItem = 1000000;
        sLog.outError("ReagentBank: invalid ReagentBank.MaxAmountPerItem '%s'; using %u",
            rawMax.c_str(), m_maxAmountPerItem);
    }

    sLog.outString("ReagentBank: %s (reload=%u) DepositAll=%u MaxAmountPerItem=%u Debug=%u",
        m_enabled ? "enabled" : "disabled",
        reload ? 1 : 0,
        m_depositAllEnabled ? 1 : 0,
        m_maxAmountPerItem,
        m_debug ? 1 : 0);
}
#endif
