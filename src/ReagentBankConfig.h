#ifndef MODULES_MOD_REAGENT_BANK_REAGENTBANKCONFIG_H
#define MODULES_MOD_REAGENT_BANK_REAGENTBANKCONFIG_H

#include <cstdint>
#include <string>

class ReagentBankConfig
{
public:
    static ReagentBankConfig& Instance();

    void Load(bool reload);

    bool Enabled() const { return m_enabled; }
    bool DepositAllEnabled() const { return m_depositAllEnabled; }
    uint32_t MaxAmountPerItem() const { return m_maxAmountPerItem; }
    uint32_t PurchaseCostGold() const { return m_purchaseCostGold; }
    bool Debug() const { return m_debug; }

private:
    ReagentBankConfig() = default;

    bool m_enabled = true;
    bool m_depositAllEnabled = true;
    uint32_t m_maxAmountPerItem = 1000000;
    uint32_t m_purchaseCostGold = 250;
    bool m_debug = false;
};

#define sReagentBankConfig (ReagentBankConfig::Instance())

namespace ReagentBank
{
    // Strictly accepts decimal values in [1, UINT32_MAX]. Kept independent of
    // the core config service so boundary handling is unit-testable.
    bool ParseMaxAmount(std::string const& value, uint32_t& out);
    // Purchase costs are configured in whole gold and must fit the core's
    // signed money delta after conversion to copper.
    bool ParsePurchaseCostGold(std::string const& value, uint32_t& out);
}

#endif
