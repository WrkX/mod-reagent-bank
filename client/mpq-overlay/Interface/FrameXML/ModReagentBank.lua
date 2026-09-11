-- ModReagentBank — Material Storage client for Tortoise/Turtle 1.12.
-- Protocol: SendAddonMessage("RBANK", payload, "GUILD")

if ModReagentBankLoaded then return end
ModReagentBankLoaded = true

local AddonName = "ModReagentBank"
local Prefix = "RBANK"

-- Packaging substitutes this for FrameXML delivery.
local TEXTURE_ROOT = "Interface\\FrameXML\\ModReagentBank\\textures\\marble.tga"
local FALLBACK_BG = "Interface\\Tooltips\\UI-Tooltip-Background"

local ITEM_CLASS_GEM = 3
local ITEM_CLASS_REAGENT = 5
local ITEM_CLASS_TRADE_GOODS = 7

local REQUEST_TIMEOUT = 8.0
local SNAPSHOT_TIMEOUT = 8.0
local MAX_SERVER_PAYLOAD_BYTES = 240
local MAX_PENDING_SNAPSHOTS = 4
local MAX_SNAPSHOT_ROWS = 4096
local ITEMINFO_RETRY_INTERVAL = 0.35
local RANGE_CHECK_INTERVAL = 0.5

local FRAME_WIDTH = 520
local FRAME_HEIGHT = 560
local SLOT_SIZE = 42
local SLOT_GAP = 2
local GRID_COLUMNS = 10
local SCROLL_CHILD_WIDTH = 440
local GRID_START_X = ((SCROLL_CHILD_WIDTH - (GRID_COLUMNS * SLOT_SIZE
    + (GRID_COLUMNS - 1) * SLOT_GAP)) / 2) + 12

local CATEGORY_ORDER = {
    "cloth", "leather", "metal", "enchanting", "gems", "herbs",
    "elemental", "meat", "reagents", "general", "parts", "explosives",
    "devices", "other",
}

local CATEGORY_LABELS = {
    gems = "Gems",
    reagents = "Reagents",
    general = "General Trade Goods",
    parts = "Parts",
    explosives = "Explosives",
    devices = "Devices",
    cloth = "Cloth",
    leather = "Leather",
    metal = "Metal & Stone",
    meat = "Cooking",
    herbs = "Herbs",
    elemental = "Elements",
    enchanting = "Enchanting",
    other = "Other Materials",
}

-- The original AzerothCore addon always showed its material catalogue, with
-- empty entries dimmed. Keep the Vanilla-era portion of that catalogue so an
-- empty bank still looks and behaves like the original UI. Stored, carried,
-- and Turtle-specific entries are merged into this model dynamically.
local MATERIAL_CATALOG = {
    cloth = {
        2589, 2996, 2592, 2997, 4306, 4305, 4337, 4338, 14047, 14048, 14342, 14256,
    },
    leather = {
        2934, 2318, 783, 4231, 2319, 4232, 4233, 4234, 4235, 4236, 4304,
        8169, 8172, 8170, 8171, 15407,
    },
    metal = {
        2770, 2835, 3470, 2840, 2771, 2836, 3478, 3576, 2841, 2775, 2842,
        2772, 2838, 3486, 3575, 3859, 2776, 3577, 3858, 7912, 7966, 3860,
        7911, 6037, 10620, 12365, 12644, 12359, 11370, 11099, 11371,
    },
    enchanting = {
        10940, 10938, 10939, 10978, 10998, 11082, 11083, 11084, 11134,
        11135, 11137, 11174, 11175, 11176, 11138, 11139, 11177, 11178,
        16202, 16203, 16204, 14343, 14344, 20725,
    },
    gems = {
        774, 818, 1210, 1705, 1206, 1529, 3864, 7909, 9262, 7910, 12799,
        12361, 12364, 12800, 12363, 11754, 18335,
    },
    herbs = {
        765, 2447, 785, 2449, 2453, 2450, 2452, 3355, 3356, 3357, 3358,
        3369, 3818, 3819, 3820, 3821, 4625, 8153, 8831, 8836, 8838, 8839,
        8845, 8846, 13463, 13464, 13465, 13466, 13467, 13468,
    },
    elemental = {
        7067, 7068, 7070, 7082, 7076, 7078, 7080, 12803, 12808,
    },
    meat = {
        2672, 2673, 2674, 2675, 769, 1080, 1081, 2924, 3712, 3713, 5471,
        5469, 5467, 5503, 5504, 4655, 12202, 12203, 12204, 12205, 12206,
        12208, 12223, 20424,
    },
}

-- Vanilla/Turtle clients do not consistently expose an icon path for an item
-- that is not already cached. Keep the catalogue icons deterministic so every
-- empty material slot can still display its dimmed, correct icon immediately.
local CATALOG_ICON_NAMES = {
    [765] = "INV_Misc_Herb_10", [769] = "INV_Misc_Food_14",
    [774] = "INV_Misc_Gem_Emerald_03", [783] = "INV_Misc_Pelt_Wolf_Ruin_02",
    [785] = "INV_Jewelry_Talisman_03", [818] = "INV_Misc_Gem_Opal_03",
    [1080] = "INV_Misc_Food_72", [1081] = "INV_Misc_MonsterSpiderCarapace_01",
    [1206] = "INV_Misc_Gem_Emerald_02", [1210] = "INV_Misc_Gem_Amethyst_01",
    [1529] = "INV_Misc_Gem_Stone_01", [1705] = "INV_Misc_Gem_Crystal_01",
    [2318] = "INV_Misc_LeatherScrap_03", [2319] = "INV_Misc_LeatherScrap_05",
    [2447] = "INV_Misc_Flower_02", [2449] = "INV_Misc_Herb_07",
    [2450] = "INV_Misc_Root_01", [2452] = "INV_Misc_Herb_04",
    [2453] = "INV_Misc_Herb_01", [2589] = "INV_Fabric_Linen_01",
    [2592] = "INV_Fabric_Wool_01", [2672] = "INV_Misc_Food_14",
    [2673] = "INV_Misc_Food_69", [2674] = "INV_Misc_Food_51",
    [2675] = "INV_Misc_Birdbeck_02", [2770] = "INV_Ore_Copper_01",
    [2771] = "INV_Ore_Tin_01", [2772] = "INV_Ore_Iron_01",
    [2775] = "INV_Stone_16", [2776] = "INV_Ore_Gold_01",
    [2835] = "INV_Stone_06", [2836] = "INV_Stone_09",
    [2838] = "INV_Stone_12", [2840] = "INV_Ingot_02",
    [2841] = "INV_Ingot_Bronze", [2842] = "INV_Ingot_01",
    [2924] = "INV_Misc_Food_14", [2934] = "INV_Misc_Pelt_Bear_Ruin_05",
    [2996] = "INV_Fabric_Linen_02", [2997] = "INV_Fabric_Wool_03",
    [3355] = "INV_Misc_Flower_01", [3356] = "INV_Misc_Herb_03",
    [3357] = "INV_Misc_Root_02", [3358] = "INV_Misc_Herb_08",
    [3369] = "INV_Misc_Dust_02", [3470] = "INV_Stone_GrindingStone_01",
    [3478] = "INV_Stone_GrindingStone_02", [3486] = "INV_Stone_GrindingStone_03",
    [3575] = "INV_Ingot_Iron", [3576] = "INV_Ingot_05",
    [3577] = "INV_Ingot_03", [3712] = "INV_Misc_Food_70",
    [3713] = "INV_Misc_Food_Wheat_02", [3818] = "INV_Misc_Herb_12",
    [3819] = "INV_Misc_Flower_03", [3820] = "INV_Misc_Herb_11",
    [3821] = "INV_Misc_Herb_15", [3858] = "INV_Ore_Mithril_02",
    [3859] = "INV_Ingot_Steel", [3860] = "INV_Ingot_06",
    [3864] = "INV_Misc_Gem_Opal_02", [4231] = "INV_Misc_Pelt_Wolf_01",
    [4232] = "INV_Misc_Pelt_Boar_Ruin_02", [4233] = "INV_Misc_Pelt_Bear_02",
    [4234] = "INV_Misc_LeatherScrap_07", [4235] = "INV_Misc_Pelt_Wolf_Ruin_03",
    [4236] = "INV_Misc_Pelt_Wolf_02", [4304] = "INV_Misc_LeatherScrap_08",
    [4305] = "INV_Fabric_Silk_03", [4306] = "INV_Fabric_Silk_01",
    [4337] = "Spell_Nature_Web", [4338] = "INV_Fabric_Mageweave_01",
    [4625] = "INV_Misc_Herb_19", [4655] = "INV_Misc_Food_51",
    [5467] = "INV_Misc_Food_14", [5469] = "INV_Misc_Food_16",
    [5471] = "INV_Misc_Pelt_Wolf_Ruin_03", [5503] = "INV_Misc_Food_51",
    [5504] = "INV_Misc_Food_51", [6037] = "INV_Ingot_08",
    [7067] = "INV_Ore_Iron_01", [7068] = "Spell_Fire_Fire",
    [7070] = "INV_Potion_03", [7076] = "Spell_Nature_StrengthOfEarthTotem02",
    [7078] = "Spell_Fire_Volcano", [7080] = "Spell_Nature_Acid_01",
    [7082] = "Spell_Nature_EarthBind", [7909] = "INV_Misc_Gem_Crystal_02",
    [7910] = "INV_Misc_Gem_Ruby_02", [7911] = "INV_Ore_TrueSilver_01",
    [7912] = "INV_Stone_10", [7966] = "INV_Stone_GrindingStone_04",
    [8153] = "INV_Misc_Herb_03", [8169] = "INV_Misc_Pelt_Bear_Ruin_01",
    [8170] = "INV_Misc_LeatherScrap_02", [8171] = "INV_Misc_Pelt_Bear_Ruin_02",
    [8172] = "INV_Misc_Pelt_Bear_01", [8831] = "INV_Misc_Herb_17",
    [8836] = "INV_Misc_Herb_13", [8838] = "INV_Misc_Herb_18",
    [8839] = "INV_Misc_Herb_14", [8845] = "INV_Mushroom_08",
    [8846] = "INV_Misc_Herb_16", [9262] = "INV_Misc_Gem_Sapphire_03",
    [10620] = "INV_Ore_Thorium_02", [10938] = "INV_Enchant_EssenceMagicSmall",
    [10939] = "INV_Enchant_EssenceMagicLarge", [10940] = "INV_Enchant_DustStrange",
    [10978] = "INV_Enchant_ShardGlimmeringSmall", [10998] = "INV_Enchant_EssenceAstralSmall",
    [11082] = "INV_Enchant_EssenceAstralLarge", [11083] = "INV_Enchant_DustSoul",
    [11084] = "INV_Enchant_ShardGlimmeringLarge", [11099] = "INV_Ore_Mithril_01",
    [11134] = "INV_Enchant_EssenceMysticalSmall", [11135] = "INV_Enchant_EssenceMysticalLarge",
    [11137] = "INV_Enchant_DustVision", [11138] = "INV_Enchant_ShardGlowingSmall",
    [11139] = "INV_Enchant_ShardGlowingLarge", [11174] = "INV_Enchant_EssenceNetherSmall",
    [11175] = "INV_Enchant_EssenceNetherLarge", [11176] = "INV_Enchant_DustDream",
    [11177] = "INV_Enchant_ShardRadientSmall", [11178] = "INV_Enchant_ShardRadientLarge",
    [11370] = "INV_Ore_Mithril_01", [11371] = "INV_Ingot_Mithril",
    [11754] = "INV_Misc_Gem_01", [12202] = "INV_Misc_Food_14",
    [12203] = "INV_Misc_Food_71", [12204] = "INV_Misc_Food_70",
    [12205] = "INV_Misc_Food_51", [12206] = "INV_Misc_Food_51",
    [12208] = "INV_Misc_Food_14", [12223] = "INV_Misc_Pelt_Bear_Ruin_05",
    [12359] = "INV_Ingot_07", [12361] = "INV_Misc_Gem_Sapphire_02",
    [12363] = "INV_Misc_Gem_Topaz_01", [12364] = "INV_Misc_Gem_Emerald_01",
    [12365] = "INV_Misc_StoneTablet_07", [12644] = "INV_Stone_GrindingStone_05",
    [12799] = "INV_Misc_Gem_Opal_01", [12800] = "INV_Misc_Gem_Diamond_01",
    [12803] = "Spell_Nature_AbolishMagic", [12808] = "Spell_Shadow_ShadeTrueSight",
    [13463] = "INV_Misc_Herb_DreamFoil", [13464] = "INV_Misc_Herb_SansamRoot",
    [13465] = "INV_Misc_Herb_MountainSilverSage", [13466] = "INV_Misc_Herb_PlagueBloom",
    [13467] = "INV_Misc_Herb_IceCap", [13468] = "INV_Misc_Herb_BlackLotus",
    [14047] = "INV_Fabric_PurpleFire_01", [14048] = "INV_Fabric_PurpleFire_02",
    [14256] = "INV_Fabric_FelRag", [14342] = "INV_Fabric_MoonRag_01",
    [14343] = "INV_Enchant_ShardBrilliantSmall", [14344] = "INV_Enchant_ShardBrilliantLarge",
    [15407] = "INV_Misc_Pelt_Bear_03", [16202] = "INV_Enchant_EssenceEternalSmall",
    [16203] = "INV_Enchant_EssenceEternalLarge", [16204] = "INV_Enchant_DustIllusion",
    [18335] = "INV_Misc_Gem_02", [20424] = "INV_Misc_Food_51",
    [20725] = "INV_Enchant_ShardNexusLarge",
}

local RESULT_MESSAGES = {
    OK = "Done.",
    DISABLED = "Material Storage is disabled on this realm.",
    NO_ACCESS = "You must be at a banker to use Material Storage.",
    BAD_REQUEST = "Request rejected.",
    BAD_SLOT = "That bag slot is no longer valid.",
    NOT_ELIGIBLE = "That item cannot be stored here.",
    LIMIT = "Storage limit reached for that item.",
    NO_SPACE = "Not enough bag space.",
    NOT_FOUND = "That item is not in Material Storage.",
    DB_ERROR = "Storage failed (database error).",
}

ModReagentBankDB = ModReagentBankDB or {
    point = "CENTER",
    relativePoint = "CENTER",
    xOfs = 0,
    yOfs = 0,
}

local nextRequestId = 1
local revision = 0
local sessionOpen = false
local recentOpen = false

local visibleRows = {}
local pendingSnapshots = {}
local expectedSnapshots = {}
local visibleSnapshot = { revision = 0, rows = {} }

local pendingRequest = nil
local pendingDeadline = 0
local initialized = false
local statusText = ""
local statusIsError = false

local unresolvedItems = {}
local itemInfoElapsed = 0
local rangeElapsed = 0

local slotButtons = {}
local categoryHeaders = {}
local categorySeparators = {}
local depositAllBtn
local statusLabel
local scrollChild
local scrollFrame
local mainFrame
local bgTexturePath

local RefreshUI
local HideAndReset
local SendQuery
local SetStatus
local ClearPendingRequest
local Initialize
local InstallBagHook
local InstallGossipPurchaseHook
local ExpirePendingSnapshots

-- ===== helpers =====

local function SplitTabs(text)
    local parts = {}
    local startPos = 1
    while true do
        local tabPos = string.find(text, "\t", startPos)
        if not tabPos then
            table.insert(parts, string.sub(text, startPos))
            break
        end
        table.insert(parts, string.sub(text, startPos, tabPos - 1))
        startPos = tabPos + 1
    end
    return parts
end

local function ParseUInt(text)
    if type(text) ~= "string" or text == "" then return nil end
    if not string.find(text, "^%d+$") then return nil end
    local n = tonumber(text)
    if not n or n < 0 or n > 4294967295 or n ~= math.floor(n) then return nil end
    return n
end

-- Turtle/Vanilla GetItemInfo is locale strings + stack size, not numeric class.
-- Some 1.18 builds insert itemLevel as return 4 (WotLK-shaped). Detect both.
local function UnpackItemInfo(entry)
    local a1, a2, a3, a4, a5, a6, a7, a8, a9, a10 = GetItemInfo(entry)
    if not a1 then
        return nil
    end
    -- WotLK-shaped: name, link, quality, itemLevel, minLevel, type, subType, stack, loc, tex
    if type(a4) == "number" and type(a6) == "string" then
        return a1, a2, a3, a5, a6, a7, a8, a9, a10
    end
    -- Vanilla-shaped: name, link, quality, minLevel, type, subType, stack, loc, tex
    return a1, a2, a3, a4, a5, a6, a7, a8, a9
end

local TYPE_CATEGORY = {
    ["Trade Goods"] = "general",
    ["Reagent"] = "reagents",
    ["Reagents"] = "reagents",
    ["Gem"] = "gems",
    ["Gems"] = "gems",
}

local SUBTYPE_CATEGORY = {
    ["Parts"] = "parts",
    ["Explosives"] = "explosives",
    ["Devices"] = "devices",
    ["Cloth"] = "cloth",
    ["Leather"] = "leather",
    ["Metal & Stone"] = "metal",
    ["Meat"] = "meat",
    ["Cooking"] = "meat",
    ["Herb"] = "herbs",
    ["Herbs"] = "herbs",
    ["Elemental"] = "elemental",
    ["Enchanting"] = "enchanting",
}

local function NextRequestId()
    local id = nextRequestId
    nextRequestId = nextRequestId + 1
    if nextRequestId > 1000000 then nextRequestId = 1 end
    return id
end

local function SendPayload(payload)
    SendAddonMessage(Prefix, payload, "GUILD")
end

local function SendCommand(cmd, requestId, arg1, arg2, arg3)
    local payload = "1\t" .. cmd .. "\t" .. tostring(requestId)
    if arg1 ~= nil then payload = payload .. "\t" .. tostring(arg1) end
    if arg2 ~= nil then payload = payload .. "\t" .. tostring(arg2) end
    if arg3 ~= nil then payload = payload .. "\t" .. tostring(arg3) end
    SendPayload(payload)
end

local function QueueItemResolve(entry)
    if not entry or unresolvedItems[entry] then return end
    unresolvedItems[entry] = true
end

local function ResolveItemIcon(entry)
    local name, _, quality, _, _, _, stackCount, _, texture = UnpackItemInfo(entry)
    -- Some Turtle builds return a numeric display/icon id while item data is
    -- uncached. Vanilla Texture:SetTexture treats one number as a solid-color
    -- call, producing the red squares seen in the material grid. Only file
    -- paths are valid here; GetItemInfo above also requests uncached item data.
    if type(texture) ~= "string" or texture == "" then
        texture = nil
    end
    if not texture and GetItemIcon then
        local candidate = GetItemIcon(entry)
        if type(candidate) == "string" and candidate ~= "" then
            texture = candidate
        end
    end
    if not texture and CATALOG_ICON_NAMES[entry] then
        texture = "Interface\\Icons\\" .. CATALOG_ICON_NAMES[entry]
    end
    if texture then
        unresolvedItems[entry] = nil
        return texture, name, quality, stackCount
    end
    QueueItemResolve(entry)
    return nil
end

local function GetCategoryKey(itemClass, itemSubclass)
    itemClass = tonumber(itemClass) or 0
    itemSubclass = tonumber(itemSubclass) or 0
    if itemClass == ITEM_CLASS_GEM then return "gems" end
    if itemClass == ITEM_CLASS_REAGENT then return "reagents" end
    if itemClass == ITEM_CLASS_TRADE_GOODS then
        if itemSubclass == 0 then return "general"
        elseif itemSubclass == 1 then return "parts"
        elseif itemSubclass == 2 then return "explosives"
        elseif itemSubclass == 3 then return "devices"
        elseif itemSubclass == 5 then return "cloth"
        elseif itemSubclass == 6 then return "leather"
        elseif itemSubclass == 7 then return "metal"
        elseif itemSubclass == 8 then return "meat"
        elseif itemSubclass == 9 then return "herbs"
        elseif itemSubclass == 10 then return "elemental"
        elseif itemSubclass == 12 then return "enchanting"
        end
    end
    return "other"
end

local function GetCategoryFromTypes(itemType, itemSubType)
    if itemSubType and SUBTYPE_CATEGORY[itemSubType] then
        return SUBTYPE_CATEGORY[itemSubType]
    end
    if itemType and TYPE_CATEGORY[itemType] then
        return TYPE_CATEGORY[itemType]
    end
    return "other"
end

local REJECT_TYPES = {
    Armor = true, Weapon = true, Recipe = true, Quest = true,
    Container = true, Quiver = true, Projectile = true, Key = true,
    Money = true,
}

local function LooksEligible(entry)
    local name, _, _, _, itemType, itemSubType, maxStack, equipLoc = UnpackItemInfo(entry)
    maxStack = tonumber(maxStack)
    if not name then
        QueueItemResolve(entry)
        return true
    end
    if not maxStack or maxStack <= 1 then return false end
    if equipLoc and equipLoc ~= "" then return false end
    if itemType and REJECT_TYPES[itemType] then return false end
    return true
end

local function FindRowByEntry(entry)
    for i = 1, table.getn(visibleRows) do
        if visibleRows[i].entry == entry then return visibleRows[i] end
    end
    return nil
end

local function UpsertVisibleRow(row)
    local existing = FindRowByEntry(row.entry)
    if existing then
        existing.stored = row.stored or existing.stored or 0
        existing.carried = row.carried or existing.carried or 0
        existing.itemClass = row.itemClass or existing.itemClass
        existing.itemSubclass = row.itemSubclass or existing.itemSubclass
        -- Catalogue entries own a stable visual slot. Server metadata and
        -- localized GetItemInfo subtype strings may disagree between client
        -- builds, but must never move known cloth/leather/etc. to another row.
        if not existing.catalog then
            existing.category = row.category or existing.category
        end
        existing.bag = row.bag or existing.bag
        existing.slot = row.slot or existing.slot
        existing.carriedOnly = (existing.stored or 0) <= 0 and (existing.carried or 0) > 0
    else
        table.insert(visibleRows, row)
    end
end

local function BuildDisplayModel()
    visibleRows = {}

    for _, category in ipairs(CATEGORY_ORDER) do
        local entries = MATERIAL_CATALOG[category]
        if entries then
            for i = 1, table.getn(entries) do
                UpsertVisibleRow({
                    entry = entries[i],
                    stored = 0,
                    carried = 0,
                    category = category,
                    catalog = true,
                    carriedOnly = false,
                })
            end
        end
    end

    for i = 1, table.getn(visibleSnapshot.rows) do
        local r = visibleSnapshot.rows[i]
        UpsertVisibleRow({
            entry = r.entry,
            stored = r.amount,
            carried = 0,
            itemClass = r.itemClass,
            itemSubclass = r.itemSubclass,
            category = GetCategoryKey(r.itemClass, r.itemSubclass),
            carriedOnly = false,
        })
        QueueItemResolve(r.entry)
    end

    for bag = 0, 4 do
        local slots = GetContainerNumSlots(bag)
        if slots and slots > 0 then
            for slot = 1, slots do
                local link = GetContainerItemLink(bag, slot)
                if link then
                    local _, _, entryStr = string.find(link, "item:(%d+)")
                    local entry = ParseUInt(entryStr)
                    if entry and LooksEligible(entry) then
                        local _, count = GetContainerItemInfo(bag, slot)
                        count = count or 1
                        local _, _, _, _, itemType, itemSubType = UnpackItemInfo(entry)
                        local existing = FindRowByEntry(entry)
                        if existing then
                            existing.carried = (existing.carried or 0) + count
                            if not existing.bag then
                                existing.bag = bag
                                existing.slot = slot
                            end
                            existing.carriedOnly = (existing.stored or 0) <= 0
                        else
                            UpsertVisibleRow({
                                entry = entry,
                                stored = 0,
                                carried = count,
                                category = GetCategoryFromTypes(itemType, itemSubType),
                                bag = bag,
                                slot = slot,
                                carriedOnly = true,
                            })
                        end
                        QueueItemResolve(entry)
                    end
                end
            end
        end
    end
end

local function SetMutationEnabled(enabled)
    if depositAllBtn then
        if enabled then depositAllBtn:Enable() else depositAllBtn:Disable() end
    end
    for i = 1, table.getn(slotButtons) do
        local btn = slotButtons[i]
        if btn and btn.mode == "stored" then
            if enabled then btn:Enable() else btn:Disable() end
        end
    end
end

SetStatus = function(text, isError)
    statusText = text or ""
    statusIsError = isError and true or false
    if statusLabel then
        statusLabel:SetText(statusText)
        if statusIsError then
            statusLabel:SetTextColor(1, 0.35, 0.35)
        else
            statusLabel:SetTextColor(0.75, 0.75, 0.75)
        end
    end
end

ClearPendingRequest = function()
    pendingRequest = nil
    pendingDeadline = 0
    SetMutationEnabled(true)
end

local function CountSnapshotRequests()
    local count = 0
    for _, _ in pairs(expectedSnapshots) do
        count = count + 1
    end
    return count
end

local function ClearSnapshotRequest(requestId)
    pendingSnapshots[requestId] = nil
    expectedSnapshots[requestId] = nil
end

local function ClearAllSnapshots()
    pendingSnapshots = {}
    expectedSnapshots = {}
end

ExpirePendingSnapshots = function()
    local now = GetTime()
    for requestId, deadline in pairs(expectedSnapshots) do
        if deadline <= now then
            ClearSnapshotRequest(requestId)
        end
    end
end

local function ExpectSnapshot(requestId)
    ExpirePendingSnapshots()
    if not expectedSnapshots[requestId] and CountSnapshotRequests() >= MAX_PENDING_SNAPSHOTS then
        return false
    end
    expectedSnapshots[requestId] = GetTime() + SNAPSHOT_TIMEOUT
    return true
end

local function BeginPendingRequest(kind, requestId)
    pendingRequest = { kind = kind, requestId = requestId, started = GetTime() }
    pendingDeadline = GetTime() + REQUEST_TIMEOUT
    ExpectSnapshot(requestId)
    SetMutationEnabled(false)
end

SendQuery = function()
    local id = NextRequestId()
    BeginPendingRequest("QUERY", id)
    SendCommand("QUERY", id)
    SetStatus("Loading...", false)
end

local function SendDeposit(bag, slot)
    if pendingRequest then return end
    local id = NextRequestId()
    BeginPendingRequest("DEPOSIT", id)
    SendCommand("DEPOSIT", id, bag, slot)
    SetStatus("Depositing...", false)
end

local function SendDepositAll()
    if pendingRequest then return end
    local id = NextRequestId()
    BeginPendingRequest("DEPOSIT_ALL", id)
    SendCommand("DEPOSIT_ALL", id)
    SetStatus("Depositing all eligible items...", false)
end

local function SendWithdraw(entry, amount)
    if pendingRequest then return end
    local id = NextRequestId()
    BeginPendingRequest("WITHDRAW", id)
    SendCommand("WITHDRAW", id, entry, amount or 0)
    SetStatus("Withdrawing...", false)
end

local function SendCloseMessage()
    local id = NextRequestId()
    SendCommand("CLOSE", id)
end

HideAndReset = function(reason, fromServer)
    local wasOpen = sessionOpen
    if wasOpen and not fromServer then
        SendCloseMessage()
    end
    sessionOpen = false
    recentOpen = false
    ClearAllSnapshots()
    visibleSnapshot = { revision = revision, rows = {} }
    visibleRows = {}
    ClearPendingRequest()
    if mainFrame and mainFrame:IsShown() then
        mainFrame:Hide()
    end
    if reason and reason ~= "" then
        DEFAULT_CHAT_FRAME:AddMessage("|cffffcc00Material Storage:|r " .. reason)
    end
end

local function CommitSnapshot(requestId)
    local snap = pendingSnapshots[requestId]
    if not snap or not snap.complete then return end
    visibleSnapshot = {
        revision = snap.revision or revision,
        rows = snap.rows,
    }
    revision = visibleSnapshot.revision
    ClearSnapshotRequest(requestId)
    BuildDisplayModel()
    RefreshUI()
    if pendingRequest and pendingRequest.requestId == requestId then
        local kind = pendingRequest.kind
        ClearPendingRequest()
        if kind == "QUERY" then
            SetStatus("", false)
        end
    end
end

local function ParseItemRows(chunk)
    local rows = {}
    if type(chunk) ~= "string" or chunk == "" then return nil, false end
    local startPos = 1
    while true do
        local semi = string.find(chunk, ";", startPos)
        local token
        if semi then
            token = string.sub(chunk, startPos, semi - 1)
            startPos = semi + 1
        else
            token = string.sub(chunk, startPos)
            startPos = nil
        end
        local _, _, v1, v2, v3, v4 = string.find(token or "", "^(%d+):(%d+):(%d+):(%d+)$")
        local entry = ParseUInt(v1)
        local amount = ParseUInt(v2)
        local itemClass = ParseUInt(v3)
        local itemSubclass = ParseUInt(v4)
        if not entry or entry == 0 or not amount or amount == 0 or not itemClass or not itemSubclass then
            return nil, false
        end
        table.insert(rows, {
            entry = entry,
            amount = amount,
            itemClass = itemClass,
            itemSubclass = itemSubclass,
        })
        if not startPos then break end
    end
    return rows, true
end

local function HandleResultMessage(parts)
    -- 1, RESULT, requestId, code [, detail]
    local requestId = ParseUInt(parts[3])
    local code = parts[4] or "BAD_REQUEST"
    local detail = parts[5] or ""

    if not requestId or not pendingRequest or pendingRequest.requestId ~= requestId then return end
    if not RESULT_MESSAGES[code] then return end

    ClearPendingRequest()
    if code ~= "OK" then
        ClearSnapshotRequest(requestId)
    end

    if code == "OK" then
        if detail ~= "" then
            SetStatus(detail, false)
        else
            SetStatus(RESULT_MESSAGES.OK, false)
        end
    else
        local msg = RESULT_MESSAGES[code] or ("Error: " .. code)
        if detail ~= "" then msg = msg .. " " .. detail end
        SetStatus(msg, true)
        DEFAULT_CHAT_FRAME:AddMessage("|cffff3333Material Storage:|r " .. msg)
    end
end

local function HandleServerPayload(message)
    if type(message) ~= "string" or string.len(message) > MAX_SERVER_PAYLOAD_BYTES then return end

    ExpirePendingSnapshots()
    local parts = SplitTabs(message)
    if parts[1] ~= "1" then return end

    local op = parts[2]
    if op == "OPEN" then
        local snapRevision = ParseUInt(parts[3])
        if table.getn(parts) ~= 3 or not snapRevision or sessionOpen then return end
        revision = snapRevision
        sessionOpen = true
        recentOpen = true
        ClearAllSnapshots()
        if not ExpectSnapshot(0) then
            HideAndReset("Could not start Material Storage.", true)
            return
        end
        if mainFrame then
            mainFrame:Show()
            SendQuery()
        end
    elseif op == "BEGIN" then
        local requestId = ParseUInt(parts[3])
        local snapRevision = ParseUInt(parts[4])
        local rowCount = ParseUInt(parts[5])
        if table.getn(parts) == 5 and requestId and snapRevision and rowCount
            and rowCount <= MAX_SNAPSHOT_ROWS and expectedSnapshots[requestId] and not pendingSnapshots[requestId] then
            pendingSnapshots[requestId] = {
                revision = snapRevision,
                rowCount = rowCount,
                rows = {},
                entries = {},
                deadline = expectedSnapshots[requestId],
                complete = false,
            }
        end
    elseif op == "ITEMS" then
        local requestId = ParseUInt(parts[3])
        local chunk = parts[4]
        local snap = pendingSnapshots[requestId]
        if table.getn(parts) == 4 and snap then
            local chunkRows, valid = ParseItemRows(chunk)
            if not valid then
                ClearSnapshotRequest(requestId)
                return
            end
            for i = 1, table.getn(chunkRows) do
                local row = chunkRows[i]
                if snap.entries[row.entry] or table.getn(snap.rows) >= snap.rowCount then
                    ClearSnapshotRequest(requestId)
                    return
                end
                snap.entries[row.entry] = true
                table.insert(snap.rows, row)
            end
        end
    elseif op == "END" then
        local requestId = ParseUInt(parts[3])
        local snapRevision = ParseUInt(parts[4])
        local snap = pendingSnapshots[requestId]
        if table.getn(parts) == 4 and snap and snapRevision and snapRevision == snap.revision then
            snap.complete = true
            if table.getn(snap.rows) == snap.rowCount then
                CommitSnapshot(requestId)
            else
                ClearSnapshotRequest(requestId)
            end
        elseif snap then
            ClearSnapshotRequest(requestId)
        end
    elseif op == "RESULT" then
        if table.getn(parts) == 4 or table.getn(parts) == 5 then
            HandleResultMessage(parts)
        end
    elseif op == "CLOSE" then
        if table.getn(parts) == 3 and sessionOpen then
            HideAndReset(parts[3], true)
        end
    end
end

local function OnAddonMessage(prefix, message, channel, sender)
    if prefix ~= Prefix or channel ~= "GUILD" then return end
    local playerName = UnitName("player")
    if not playerName or sender ~= playerName then return end
    HandleServerPayload(message)
end

local function FindLockedCursorSlot()
    local foundBag, foundSlot, count = nil, nil, 0
    for bag = 0, 4 do
        local slots = GetContainerNumSlots(bag)
        if slots and slots > 0 then
            for slot = 1, slots do
                local _, itemCount, locked = GetContainerItemInfo(bag, slot)
                if locked then
                    foundBag = bag
                    foundSlot = slot
                    count = count + 1
                end
            end
        end
    end
    if count == 1 then
        return foundBag, foundSlot
    end
    return nil, nil
end

-- ===== UI =====

local function ResolveBgTexture()
    bgTexturePath = TEXTURE_ROOT
end

local function ApplyBackdrop(frame)
    frame:SetBackdrop({
        bgFile = "Interface\\Buttons\\WHITE8X8",
        edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
        tile = false,
        tileSize = 0,
        edgeSize = 32,
        insets = { left = 11, right = 12, top = 12, bottom = 11 },
    })
    frame:SetBackdropColor(0, 0, 0, 0)
    frame:SetBackdropBorderColor(1, 1, 1, 1)
end

local function CreateSlotButton(parent, index)
    local btn = CreateFrame("Button", nil, parent)
    btn:SetWidth(SLOT_SIZE)
    btn:SetHeight(SLOT_SIZE)
    btn:EnableMouse(true)
    btn:RegisterForClicks("LeftButtonUp", "RightButtonUp")

    local icon = btn:CreateTexture(nil, "ARTWORK")
    icon:SetPoint("TOPLEFT", btn, "TOPLEFT", 3, -3)
    icon:SetPoint("BOTTOMRIGHT", btn, "BOTTOMRIGHT", -3, 3)
    icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
    btn.icon = icon

    local border = btn:CreateTexture(nil, "OVERLAY")
    border:SetAllPoints(icon)
    border:SetTexture("Interface\\Buttons\\ButtonHilight-Square")
    border:SetBlendMode("ADD")
    border:SetAlpha(0.55)
    border:Hide()
    btn.border = border

    local countText = btn:CreateFontString(nil, "OVERLAY", "NumberFontNormal")
    countText:SetPoint("BOTTOMRIGHT", btn, "BOTTOMRIGHT", -2, 2)
    btn.countText = countText

    btn:SetScript("OnEnter", function(self)
        self = self or this
        if not self then return end
        if self.entry then
            GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
            GameTooltip:SetHyperlink("item:" .. self.entry .. ":0:0:0")
            -- Some Turtle/Vanilla builds resolve uncached catalogue entries
            -- only through the item-id API. Keep the hyperlink path for old
            -- clients and use the newer API as a fallback when available.
            if GameTooltip.SetItemByID and not GetItemInfo(self.entry) then
                GameTooltip:SetItemByID(self.entry)
            end
            if self.mode == "stored" then
                GameTooltip:AddLine("Right-click: withdraw one stack", 0.8, 0.8, 0.8)
                GameTooltip:AddLine("Shift-right-click: withdraw amount", 0.8, 0.8, 0.8)
            elseif self.mode == "carried" then
                GameTooltip:AddLine("Carried — right-click bag slot to deposit", 0.8, 0.8, 0.8)
            elseif self.mode == "empty" then
                GameTooltip:AddLine("Not stored in Material Storage", 0.8, 0.8, 0.8)
            end
            if self.border then self.border:Show() end
            GameTooltip:Show()
        end
    end)
    btn:SetScript("OnLeave", function(self)
        self = self or this
        GameTooltip:Hide()
        if self and self.border then self.border:Hide() end
    end)

    btn:SetScript("OnClick", function(self, button)
        self = self or this
        button = button or arg1
        if button == "RightButton" and self.mode == "stored" and self.entry then
            if pendingRequest then return end
            if IsShiftKeyDown() then
                StaticPopup_Show("MODREAGENTBANK_WITHDRAW", nil, nil, { entry = self.entry })
            else
                SendWithdraw(self.entry, 0)
            end
        end
    end)

    slotButtons[index] = btn
    return btn
end

local function LayoutScrollContent()
    if not scrollChild then return end

    for i = 1, table.getn(categoryHeaders) do
        categoryHeaders[i]:Hide()
    end
    for i = 1, table.getn(categorySeparators) do
        categorySeparators[i]:Hide()
    end
    for i = 1, table.getn(slotButtons) do
        slotButtons[i]:Hide()
    end

    local grouped = {}
    for _, key in ipairs(CATEGORY_ORDER) do grouped[key] = {} end

    for i = 1, table.getn(visibleRows) do
        local row = visibleRows[i]
        local key = row.category or "other"
        if not grouped[key] then key = "other" end
        table.insert(grouped[key], row)
    end

    local y = -6
    local slotIndex = 0
    local headerIndex = 0

    for _, catKey in ipairs(CATEGORY_ORDER) do
        local items = grouped[catKey]
        if items and table.getn(items) > 0 then
            headerIndex = headerIndex + 1
            local header = categoryHeaders[headerIndex]
            if not header then
                header = scrollChild:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
                categoryHeaders[headerIndex] = header
            end
            header:ClearAllPoints()
            header:SetPoint("TOP", scrollChild, "TOP", 0, y)
            header:SetText(CATEGORY_LABELS[catKey] or catKey)
            header:SetTextColor(1, 0.82, 0)
            header:Show()
            y = y - 20

            local separator = categorySeparators[headerIndex]
            if not separator then
                separator = scrollChild:CreateTexture(nil, "ARTWORK")
                separator:SetTexture("Interface\\Buttons\\WHITE8X8")
                separator:SetVertexColor(0.8, 0.65, 0.2, 0.5)
                separator:SetHeight(1)
                categorySeparators[headerIndex] = separator
            end
            separator:ClearAllPoints()
            separator:SetPoint("TOPLEFT", scrollChild, "TOPLEFT", 15, y)
            separator:SetPoint("TOPRIGHT", scrollChild, "TOPRIGHT", -9, y)
            separator:Show()
            y = y - 4

            for j = 1, table.getn(items) do
                slotIndex = slotIndex + 1
                local btn = slotButtons[slotIndex]
                if not btn then btn = CreateSlotButton(scrollChild, slotIndex) end

                local data = items[j]
                local col = math.mod(j - 1, GRID_COLUMNS)
                local row = math.floor((j - 1) / GRID_COLUMNS)
                btn:ClearAllPoints()
                btn:SetPoint("TOPLEFT", scrollChild, "TOPLEFT",
                    GRID_START_X + col * (SLOT_SIZE + SLOT_GAP),
                    y - row * (SLOT_SIZE + SLOT_GAP))

                btn.entry = data.entry
                if data.stored and data.stored > 0 then
                    btn.mode = "stored"
                elseif data.carried and data.carried > 0 then
                    btn.mode = "carried"
                else
                    btn.mode = "empty"
                end

                local texture = ResolveItemIcon(data.entry)
                if texture then
                    btn.icon:SetTexture(texture)
                    if btn.mode == "stored" then
                        if btn.icon.SetDesaturated then btn.icon:SetDesaturated(false) end
                        btn.icon:SetVertexColor(1, 1, 1)
                        btn.icon:SetAlpha(1)
                    else
                        if btn.icon.SetDesaturated then btn.icon:SetDesaturated(true) end
                        btn.icon:SetVertexColor(0.55, 0.55, 0.55)
                        btn.icon:SetAlpha(0.28)
                    end
                else
                    btn.icon:SetTexture("Interface\\Icons\\INV_Misc_QuestionMark")
                    if btn.icon.SetDesaturated then btn.icon:SetDesaturated(true) end
                    btn.icon:SetVertexColor(0.45, 0.45, 0.45)
                    btn.icon:SetAlpha(0.28)
                end

                if data.stored and data.stored > 0 then
                    btn.countText:SetText(tostring(data.stored))
                else
                    btn.countText:SetText("")
                end

                -- Keep catalogue slots enabled even while empty. Disabled
                -- Buttons do not consistently receive OnEnter in 1.12,
                -- which prevented their item tooltip from appearing. OnClick
                -- only permits stored items to withdraw, so this introduces
                -- no action for empty/carried entries.
                if btn.mode == "stored" and pendingRequest then
                    btn:Disable()
                else
                    btn:Enable()
                end
                if btn.mode ~= "stored" then btn.border:Hide() end

                btn:Show()
            end

            local rows = math.ceil(table.getn(items) / GRID_COLUMNS)
            y = y - rows * (SLOT_SIZE + SLOT_GAP) - 16
        end
    end

    local height = -y + 40
    if height < 320 then height = 320 end
    scrollChild:SetWidth(SCROLL_CHILD_WIDTH)
    scrollChild:SetHeight(height)
end

RefreshUI = function()
    LayoutScrollContent()
end

StaticPopupDialogs["MODREAGENTBANK_DEPOSIT_ALL"] = {
    text = "Deposit all eligible crafting materials from your backpack and carried bags "
        .. "into Material Storage?\n\nThis does NOT scan your normal bank.",
    button1 = YES,
    button2 = NO,
    OnAccept = function() SendDepositAll() end,
    timeout = 0,
    whileDead = 1,
    hideOnEscape = 1,
}

StaticPopupDialogs["MODREAGENTBANK_PURCHASE"] = {
    text = "%s",
    button1 = YES,
    button2 = NO,
    OnAccept = function()
        -- The server validates the current banker/menu state again. The
        -- popup data is only visual metadata and must not gate the accept
        -- callback on Vanilla clients, which do not always expose it here.
        SendCommand("PURCHASE", NextRequestId())
    end,
    OnShow = function(frame)
        frame = frame or this
        if frame.data and frame.data.costCopper then
            MoneyFrame_Update(frame:GetName() .. "MoneyFrame", frame.data.costCopper)
        end
    end,
    hasMoneyFrame = 1,
    timeout = 0,
    whileDead = 1,
    hideOnEscape = 1,
}

StaticPopupDialogs["MODREAGENTBANK_WITHDRAW"] = {
    text = "Withdraw how many?",
    button1 = ACCEPT,
    button2 = CANCEL,
    hasEditBox = 1,
    maxLetters = 8,
    OnAccept = function(frame)
        frame = frame or this
        local data = frame.data
        local editBox = getglobal(frame:GetName() .. "EditBox")
        local amount = 0
        if editBox then amount = ParseUInt(editBox:GetText()) or 0 end
        if data and data.entry then
            SendWithdraw(data.entry, amount)
        end
    end,
    EditBoxOnEnterPressed = function(box)
        box = box or this
        local parent = box:GetParent()
        StaticPopupDialogs[parent.which].OnAccept(parent)
        parent:Hide()
    end,
    timeout = 0,
    whileDead = 1,
    hideOnEscape = 1,
}

local function CreateMainFrame()
    mainFrame = CreateFrame("Frame", "ModReagentBankFrame", UIParent)
    mainFrame:SetWidth(FRAME_WIDTH)
    mainFrame:SetHeight(FRAME_HEIGHT)
    mainFrame:SetMovable(true)
    mainFrame:EnableMouse(true)
    mainFrame:SetClampedToScreen(true)
    mainFrame:SetFrameStrata("DIALOG")
    mainFrame:SetToplevel(true)
    mainFrame:Hide()

    ApplyBackdrop(mainFrame)

    local contentBg = mainFrame:CreateTexture(nil, "BACKGROUND")
    contentBg:SetTexture(bgTexturePath or FALLBACK_BG)
    -- marble.tga is RGB, so the transparent 7px perimeter from the source
    -- BLP was converted to black. Crop that perimeter instead of displaying
    -- it as a black frame around the UI.
    contentBg:SetTexCoord(7 / 512, 505 / 512, 7 / 512, 505 / 512)
    contentBg:SetPoint("TOPLEFT", mainFrame, "TOPLEFT", 0, 0)
    contentBg:SetPoint("BOTTOMRIGHT", mainFrame, "BOTTOMRIGHT", 0, 0)
    contentBg:SetVertexColor(1, 1, 1, 1)

    -- Keep the material grid readable against the marble artwork. This is an
    -- inner content overlay, intentionally separate from the outer frame rim.
    local scrollOverlay = mainFrame:CreateTexture(nil, "BORDER")
    scrollOverlay:SetTexture("Interface\\Buttons\\WHITE8X8")
    scrollOverlay:SetVertexColor(0, 0, 0, 0.55)
    scrollOverlay:SetPoint("TOPLEFT", mainFrame, "TOPLEFT", 23, -40)
    scrollOverlay:SetPoint("BOTTOMRIGHT", mainFrame, "BOTTOMRIGHT", -23, 42)

    local header = mainFrame:CreateTexture(nil, "ARTWORK")
    header:SetTexture("Interface\\DialogFrame\\UI-DialogBox-Header")
    header:SetWidth(330)
    header:SetHeight(64)
    header:SetPoint("TOP", mainFrame, "TOP", 0, 12)

    local titleBar = CreateFrame("Frame", nil, mainFrame)
    titleBar:SetWidth(330)
    titleBar:SetHeight(36)
    titleBar:SetPoint("TOP", mainFrame, "TOP", 0, 10)
    titleBar:EnableMouse(true)
    titleBar:RegisterForDrag("LeftButton")
    titleBar:SetScript("OnDragStart", function() mainFrame:StartMoving() end)
    titleBar:SetScript("OnDragStop", function()
        mainFrame:StopMovingOrSizing()
        local point, _, relativePoint, xOfs, yOfs = mainFrame:GetPoint()
        ModReagentBankDB.point = point
        ModReagentBankDB.relativePoint = relativePoint
        ModReagentBankDB.xOfs = xOfs
        ModReagentBankDB.yOfs = yOfs
    end)

    mainFrame:SetScript("OnShow", function()
        BuildDisplayModel()
        RefreshUI()
    end)

    mainFrame:SetScript("OnHide", function()
        -- Every way of hiding the frame (close button, Escape, another UI
        -- panel, or an external Hide call) must terminate the server context.
        -- HideAndReset clears sessionOpen before hiding again, so this is
        -- re-entrancy safe.
        if sessionOpen then
            HideAndReset()
        end
    end)

    local title = mainFrame:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
    title:SetPoint("TOP", header, "TOP", 0, -14)
    title:SetText("Material Storage")

    mainFrame:SetScript("OnReceiveDrag", function()
        if pendingRequest then return end
        local bag, slot = FindLockedCursorSlot()
        if bag and slot then
            SendDeposit(bag, slot)
            ClearCursor()
        else
            SetStatus("Could not identify the item being dropped.", true)
        end
    end)

    mainFrame:SetScript("OnMouseUp", function(self, button)
        self = self or this
        button = button or arg1
        if button == "LeftButton" and CursorHasItem() then
            if pendingRequest then return end
            local bag, slot = FindLockedCursorSlot()
            if bag and slot then
                SendDeposit(bag, slot)
                ClearCursor()
            else
                SetStatus("Drop failed: no unique locked bag slot found.", true)
            end
        end
    end)

    local closeBtn = CreateFrame("Button", nil, mainFrame, "UIPanelCloseButton")
    closeBtn:SetPoint("TOPRIGHT", mainFrame, "TOPRIGHT", -4, -4)
    closeBtn:SetScript("OnClick", function() HideAndReset() end)

    statusLabel = mainFrame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    statusLabel:SetPoint("BOTTOMLEFT", mainFrame, "BOTTOMLEFT", 24, 19)
    statusLabel:SetWidth(300)
    statusLabel:SetJustifyH("LEFT")
    statusLabel:SetText("")

    depositAllBtn = CreateFrame("Button", nil, mainFrame, "UIPanelButtonTemplate")
    depositAllBtn:SetWidth(140)
    depositAllBtn:SetHeight(24)
    depositAllBtn:SetPoint("BOTTOMRIGHT", mainFrame, "BOTTOMRIGHT", -38, 14)
    depositAllBtn:SetText("Deposit All Materials")
    depositAllBtn:SetScript("OnClick", function()
        StaticPopup_Show("MODREAGENTBANK_DEPOSIT_ALL")
    end)

    scrollFrame = CreateFrame("ScrollFrame", "ModReagentBankScroll", mainFrame, "UIPanelScrollFrameTemplate")
    scrollFrame:SetPoint("TOPLEFT", mainFrame, "TOPLEFT", 20, -44)
    scrollFrame:SetPoint("BOTTOMRIGHT", mainFrame, "BOTTOMRIGHT", -38, 46)

    local scrollBar = getglobal("ModReagentBankScrollScrollBar")
    if scrollBar then
        scrollBar:ClearAllPoints()
        scrollBar:SetPoint("TOPLEFT", scrollFrame, "TOPRIGHT", -5, -16)
        scrollBar:SetPoint("BOTTOMLEFT", scrollFrame, "BOTTOMRIGHT", -5, 16)
    end

    scrollChild = CreateFrame("Frame", nil, scrollFrame)
    scrollChild:SetWidth(SCROLL_CHILD_WIDTH)
    scrollChild:SetHeight(320)
    scrollFrame:SetScrollChild(scrollChild)

    table.insert(UISpecialFrames, "ModReagentBankFrame")

    mainFrame:SetScript("OnUpdate", function(self, elapsed)
        self = self or this
        elapsed = elapsed or arg1
        if not self or type(elapsed) ~= "number" then return end
        if not self:IsShown() then return end

        itemInfoElapsed = itemInfoElapsed + elapsed
        if itemInfoElapsed >= ITEMINFO_RETRY_INTERVAL then
            itemInfoElapsed = 0
            local any = false
            for entry, _ in pairs(unresolvedItems) do
                if ResolveItemIcon(entry) then any = true end
            end
            if any then RefreshUI() end
        end

        rangeElapsed = rangeElapsed + elapsed
        if rangeElapsed >= RANGE_CHECK_INTERVAL then
            rangeElapsed = 0
            ExpirePendingSnapshots()
            -- Dist index 2 is trade/NPC-interact range. Only close when the
            -- current target is a friendly NPC that walked out of range.
            if sessionOpen and UnitExists("target") and not UnitIsPlayer("target")
                and not UnitCanAttack("player", "target")
                and CheckInteractDistance
                and not CheckInteractDistance("target", 2) then
                HideAndReset("Moved out of range of the banker.")
            end
        end

        if pendingRequest and pendingDeadline > 0 and GetTime() >= pendingDeadline then
            local kind = pendingRequest.kind
            local requestId = pendingRequest.requestId
            ClearPendingRequest()
            ClearSnapshotRequest(requestId)
            SetStatus("Reagent Bank module unavailable", true)
            DEFAULT_CHAT_FRAME:AddMessage("|cffff3333Material Storage:|r Reagent Bank module unavailable.")
            if kind and kind ~= "QUERY" then
                SendQuery()
            end
        end
    end)
end

-- Bag right-click hook (installed once)
local bagModifiedClickHookInstalled = false
local bagClickHookInstalled = false
local origBagModifiedClick
local origBagClick
local gossipPurchaseHookInstalled = false
local origGossipTitleButtonOnClick

local function PurchaseGoldFromGossipText(text)
    local _, _, gold = string.find(text or "", "^Purchase Material Storage %((%d+)g%)$")
    return gold and tonumber(gold) or nil
end

InstallGossipPurchaseHook = function()
    if gossipPurchaseHookInstalled or type(GossipTitleButton_OnClick) ~= "function" then
        return
    end

    origGossipTitleButtonOnClick = GossipTitleButton_OnClick
    GossipTitleButton_OnClick = function(a, b)
        -- Turtle's 1.12 FrameXML uses `this`; newer compatible builds pass
        -- the button as the first argument.
        local button = type(a) == "table" and a or this
        local text = button and button.GetText and button:GetText()
        local gold = button and PurchaseGoldFromGossipText(text)
        if gold then
            StaticPopup_Show(
                "MODREAGENTBANK_PURCHASE",
                "Are you sure you want to purchase Material Storage for " .. gold .. "g?",
                nil,
                { gossipIndex = button:GetID(), costCopper = gold * 10000 }
            )
            return
        end

        return origGossipTitleButtonOnClick(a, b)
    end
    gossipPurchaseHookInstalled = true
end

local function ResolveBagButton(a, b)
    -- Turtle/WotLK: (self, button). Vanilla 1.12: this=button, first arg=click type.
    if type(a) == "table" then
        return a, b
    end
    return this, a or arg1
end

local function TryDepositBagButton(button)
    if not button then return false end
    if not mainFrame or not mainFrame:IsShown() then return false end
    if pendingRequest then return true end
    local parent = button.GetParent and button:GetParent()
    if not parent then return false end
    local bag = parent.GetID and parent:GetID()
    local slot = button.GetID and button:GetID()
    if bag == nil or slot == nil then return false end
    local link = GetContainerItemLink(bag, slot)
    if not link then return false end
    SendDeposit(bag, slot)
    return true
end

InstallBagHook = function()
    if not bagModifiedClickHookInstalled and type(ContainerFrameItemButton_OnModifiedClick) == "function" then
        origBagModifiedClick = ContainerFrameItemButton_OnModifiedClick
        ContainerFrameItemButton_OnModifiedClick = function(a, b)
            local btn, clickButton = ResolveBagButton(a, b)
            if clickButton == "RightButton" and TryDepositBagButton(btn) then
                return
            end
            return origBagModifiedClick(a, b)
        end
        bagModifiedClickHookInstalled = true
    end
    if not bagClickHookInstalled and type(ContainerFrameItemButton_OnClick) == "function" then
        origBagClick = ContainerFrameItemButton_OnClick
        ContainerFrameItemButton_OnClick = function(a, b)
            local btn, clickButton = ResolveBagButton(a, b)
            if clickButton == "RightButton" and TryDepositBagButton(btn) then
                return
            end
            return origBagClick(a, b)
        end
        bagClickHookInstalled = true
    end
    InstallGossipPurchaseHook()
end

Initialize = function()
    -- ADDON_LOADED is not emitted for a FrameXML-only installation, so this
    -- routine is also called directly once the frame has been created.
    if not initialized then
        initialized = true
        ResolveBgTexture()
        ApplyBackdrop(mainFrame)
        mainFrame:ClearAllPoints()
        mainFrame:SetPoint(
            ModReagentBankDB.point or "CENTER",
            UIParent,
            ModReagentBankDB.relativePoint or "CENTER",
            ModReagentBankDB.xOfs or 0,
            ModReagentBankDB.yOfs or 0
        )
        if RegisterAddonMessagePrefix then
            pcall(RegisterAddonMessagePrefix, Prefix)
        end
    end
    InstallBagHook()
end

-- ===== events =====

local eventFrame = CreateFrame("Frame")
eventFrame:RegisterEvent("ADDON_LOADED")
eventFrame:RegisterEvent("CHAT_MSG_ADDON")
eventFrame:RegisterEvent("BAG_UPDATE")
eventFrame:RegisterEvent("PLAYER_LEAVING_WORLD")
eventFrame:RegisterEvent("PLAYER_LOGOUT")
eventFrame:RegisterEvent("PLAYER_LOGIN")
eventFrame:RegisterEvent("PLAYER_ENTERING_WORLD")

eventFrame:SetScript("OnEvent", function(self, eventName, eventArg1, eventArg2, eventArg3, eventArg4)
    -- Vanilla 1.12 exposes event arguments through globals (`event`, `arg1`,
    -- ...), while newer Turtle clients pass (self, event, ...). Normalize
    -- both forms here so server OPEN messages are not silently ignored.
    if type(eventName) ~= "string" then
        eventName = event
        eventArg1 = arg1
        eventArg2 = arg2
        eventArg3 = arg3
        eventArg4 = arg4
    end

    if eventName == "ADDON_LOADED" then
        if eventArg1 == AddonName then
            Initialize()
        end
    elseif eventName == "CHAT_MSG_ADDON" then
        OnAddonMessage(eventArg1, eventArg2, eventArg3, eventArg4)
    elseif eventName == "BAG_UPDATE" then
        if mainFrame and mainFrame:IsShown() and not pendingRequest then
            BuildDisplayModel()
            RefreshUI()
        end
    elseif eventName == "PLAYER_LEAVING_WORLD" or eventName == "PLAYER_LOGOUT" then
        ClearAllSnapshots()
        sessionOpen = false
        recentOpen = false
        if mainFrame and mainFrame:IsShown() then
            mainFrame:Hide()
        end
        ClearPendingRequest()
    elseif eventName == "PLAYER_LOGIN" or eventName == "PLAYER_ENTERING_WORLD" then
        -- FrameXML load order differs between normal addons and MPQ overlays.
        -- Retry until the stock container handlers exist; InstallBagHook itself
        -- is idempotent and never wraps a handler twice.
        InstallBagHook()
        if not recentOpen and mainFrame and mainFrame:IsShown() then
            HideAndReset()
        end
    end
end)

ResolveBgTexture()
CreateMainFrame()
Initialize()

DEFAULT_CHAT_FRAME:AddMessage("|cff33ccffMaterial Storage|r loaded.")
