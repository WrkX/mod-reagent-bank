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
    namespace
    {
        bool ParseDecimal(std::string const& value, uint32_t maxValue, uint32_t& out, bool allowZero)
        {
            if (value.empty())
                return false;

            uint64_t parsed = 0;
            for (char const c : value)
            {
                if (c < '0' || c > '9')
                    return false;
                uint64_t const digit = static_cast<uint64_t>(c - '0');
                if (parsed > (static_cast<uint64_t>(maxValue) - digit) / 10)
                    return false;
                parsed = parsed * 10 + digit;
            }

            if (!allowZero && parsed == 0)
                return false;
            out = static_cast<uint32_t>(parsed);
            return true;
        }
    }

    bool ParseMaxAmount(std::string const& value, uint32_t& out)
    {
        constexpr uint64_t kMax = std::numeric_limits<uint32_t>::max();
        return ParseDecimal(value, static_cast<uint32_t>(kMax), out, false);
    }

    bool ParsePurchaseCostGold(std::string const& value, uint32_t& out)
    {
        // MAX_MONEY_AMOUNT is 0x7ffffffe copper and GOLD is 10000 copper.
        // Use the largest whole-gold value whose copper representation fits
        // the signed money delta accepted by Player::ModifyMoney.
        constexpr uint32_t kMaxPurchaseCostGold = 214748;
        return ParseDecimal(value, kMaxPurchaseCostGold, out, true);
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

    std::string const rawPurchaseCost = sConfig.GetStringDefault("ReagentBank.PurchaseCost", "250");
    if (!ReagentBank::ParsePurchaseCostGold(rawPurchaseCost, m_purchaseCostGold))
    {
        m_purchaseCostGold = 250;
        sLog.outError("ReagentBank: invalid ReagentBank.PurchaseCost '%s'; using %u gold",
            rawPurchaseCost.c_str(), m_purchaseCostGold);
    }

    std::string const rawMax = sConfig.GetStringDefault("ReagentBank.MaxAmountPerItem", "1000000");
    if (!ReagentBank::ParseMaxAmount(rawMax, m_maxAmountPerItem))
    {
        m_maxAmountPerItem = 1000000;
        sLog.outError("ReagentBank: invalid ReagentBank.MaxAmountPerItem '%s'; using %u",
            rawMax.c_str(), m_maxAmountPerItem);
    }

    sLog.outString("ReagentBank: %s (reload=%u) DepositAll=%u MaxAmountPerItem=%u PurchaseCost=%ug Debug=%u",
        m_enabled ? "enabled" : "disabled",
        reload ? 1 : 0,
        m_depositAllEnabled ? 1 : 0,
        m_maxAmountPerItem,
        m_purchaseCostGold,
        m_debug ? 1 : 0);
}
#endif
