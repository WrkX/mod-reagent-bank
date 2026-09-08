-- ModReagentBank — Material Storage client for Tortoise/Turtle 1.12.
-- Protocol: SendAddonMessage("RBANK", payload, "GUILD")

if ModReagentBankLoaded then return end
ModReagentBankLoaded = true

local AddonName = "ModReagentBank"
local Prefix = "RBANK"

-- Packaging substitutes this for FrameXML delivery.
local TEXTURE_ROOT = "Interface\\AddOns\\ModReagentBank\\textures\\marble"
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

local CATEGORY_ORDER = {
    "gems", "reagents", "general", "parts", "explosives", "devices",
    "cloth", "leather", "metal", "meat", "herbs", "elemental", "enchanting", "other",
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
    meat = "Meat / Cooking",
    herbs = "Herbs",
    elemental = "Elemental",
    enchanting = "Enchanting",
    other = "Other",
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
    if texture then
        unresolvedItems[entry] = nil
        return texture, name, quality, stackCount
    end
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
        existing.category = row.category or existing.category
        existing.bag = row.bag or existing.bag
        existing.slot = row.slot or existing.slot
        existing.carriedOnly = (existing.stored or 0) <= 0 and (existing.carried or 0) > 0
    else
        table.insert(visibleRows, row)
    end
end

local function BuildDisplayModel()
    visibleRows = {}

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
    if string.find(TEXTURE_ROOT, "FrameXML", 1, true) then
        bgTexturePath = TEXTURE_ROOT
    else
        bgTexturePath = FALLBACK_BG
    end
end

local function ApplyBackdrop(frame)
    frame:SetBackdrop({
        bgFile = bgTexturePath or FALLBACK_BG,
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        tile = true,
        tileSize = 16,
        edgeSize = 16,
        insets = { left = 4, right = 4, top = 4, bottom = 4 },
    })
    frame:SetBackdropColor(1, 1, 1, 0.92)
    frame:SetBackdropBorderColor(0.6, 0.6, 0.6, 1)
end

local function CreateSlotButton(parent, index)
    local btn = CreateFrame("Button", nil, parent)
    btn:SetWidth(36)
    btn:SetHeight(36)
    btn:EnableMouse(true)
    btn:RegisterForClicks("LeftButtonUp", "RightButtonUp")

    local icon = btn:CreateTexture(nil, "ARTWORK")
    icon:SetAllPoints(btn)
    icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
    btn.icon = icon

    local border = btn:CreateTexture(nil, "OVERLAY")
    border:SetTexture("Interface\\Buttons\\UI-ActionButton-Border")
    border:SetBlendMode("ADD")
    border:SetWidth(64)
    border:SetHeight(64)
    border:SetPoint("CENTER", btn, "CENTER", 0, 0)
    border:Hide()
    btn.border = border

    local countText = btn:CreateFontString(nil, "OVERLAY", "NumberFontNormal")
    countText:SetPoint("BOTTOMRIGHT", btn, "BOTTOMRIGHT", -2, 2)
    btn.countText = countText

    btn:SetScript("OnEnter", function(self)
        if self.entry then
            GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
            GameTooltip:SetHyperlink("item:" .. self.entry .. ":0:0:0")
            if self.mode == "stored" then
                GameTooltip:AddLine("Right-click: withdraw one stack", 0.8, 0.8, 0.8)
                GameTooltip:AddLine("Shift-right-click: withdraw amount", 0.8, 0.8, 0.8)
            elseif self.mode == "carried" then
                GameTooltip:AddLine("Carried — right-click bag slot to deposit", 0.8, 0.8, 0.8)
            end
            GameTooltip:Show()
        end
    end)
    btn:SetScript("OnLeave", function() GameTooltip:Hide() end)

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

    local y = -8
    local slotIndex = 0
    local col = 0
    local cols = 10
    local cell = 40
    local headerIndex = 0

    for _, catKey in ipairs(CATEGORY_ORDER) do
        local items = grouped[catKey]
        if items and table.getn(items) > 0 then
            headerIndex = headerIndex + 1
            local header = categoryHeaders[headerIndex]
            if not header then
                header = scrollChild:CreateFontString(nil, "OVERLAY", "GameFontNormal")
                categoryHeaders[headerIndex] = header
            end
            header:ClearAllPoints()
            header:SetPoint("TOPLEFT", scrollChild, "TOPLEFT", 8, y)
            header:SetText(CATEGORY_LABELS[catKey] or catKey)
            header:SetTextColor(0.85, 0.72, 0.25)
            header:Show()
            y = y - 18
            col = 0

            for j = 1, table.getn(items) do
                slotIndex = slotIndex + 1
                local btn = slotButtons[slotIndex]
                if not btn then btn = CreateSlotButton(scrollChild, slotIndex) end

                local data = items[j]
                btn:ClearAllPoints()
                btn:SetPoint("TOPLEFT", scrollChild, "TOPLEFT", 8 + col * cell, y - 36)

                btn.entry = data.entry
                btn.mode = (data.stored and data.stored > 0) and "stored" or "carried"

                local texture = ResolveItemIcon(data.entry)
                if texture then
                    btn.icon:SetTexture(texture)
                    btn.icon:SetVertexColor(1, 1, 1)
                    btn.icon:SetAlpha(1)
                else
                    btn.icon:SetTexture("Interface\\Icons\\INV_Misc_QuestionMark")
                    btn.icon:SetVertexColor(0.6, 0.6, 0.6)
                    btn.icon:SetAlpha(0.85)
                end

                local count = (data.stored or 0) + (data.carried or 0)
                if data.carried and data.carried > 0 and data.stored and data.stored > 0 then
                    btn.countText:SetText(tostring(data.stored) .. "+" .. tostring(data.carried))
                elseif data.stored and data.stored > 0 then
                    btn.countText:SetText(tostring(data.stored))
                elseif data.carried and data.carried > 0 then
                    btn.countText:SetText(tostring(data.carried))
                else
                    btn.countText:SetText("")
                end

                if btn.mode == "stored" then
                    btn.border:Show()
                    if pendingRequest then btn:Disable() else btn:Enable() end
                else
                    btn.border:Hide()
                    btn:Disable()
                end

                btn:Show()
                col = col + 1
                if col >= cols then
                    col = 0
                    y = y - cell
                end
            end
            if col > 0 then y = y - cell end
            y = y - 8
        end
    end

    local height = -y + 16
    if height < 200 then height = 200 end
    scrollChild:SetWidth(420)
    scrollChild:SetHeight(height)
end

RefreshUI = function()
    LayoutScrollContent()
end

StaticPopupDialogs["MODREAGENTBANK_DEPOSIT_ALL"] = {
    text = "Deposit all eligible crafting materials from your backpack and carried bags into Material Storage?\n\nThis does NOT scan your normal bank.",
    button1 = YES,
    button2 = NO,
    OnAccept = function() SendDepositAll() end,
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
    mainFrame:SetWidth(448)
    mainFrame:SetHeight(360)
    mainFrame:SetMovable(true)
    mainFrame:EnableMouse(true)
    mainFrame:SetClampedToScreen(true)
    mainFrame:SetFrameStrata("DIALOG")
    mainFrame:SetToplevel(true)
    mainFrame:Hide()

    ApplyBackdrop(mainFrame)

    local titleBar = CreateFrame("Frame", nil, mainFrame)
    titleBar:SetWidth(448)
    titleBar:SetHeight(28)
    titleBar:SetPoint("TOP", mainFrame, "TOP", 0, 0)
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

    local title = mainFrame:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
    title:SetPoint("TOP", mainFrame, "TOP", 0, -10)
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
    statusLabel:SetPoint("TOPLEFT", mainFrame, "TOPLEFT", 12, -34)
    statusLabel:SetWidth(420)
    statusLabel:SetJustifyH("LEFT")
    statusLabel:SetText("")

    depositAllBtn = CreateFrame("Button", nil, mainFrame, "UIPanelButtonTemplate")
    depositAllBtn:SetWidth(120)
    depositAllBtn:SetHeight(22)
    depositAllBtn:SetPoint("TOPRIGHT", mainFrame, "TOPRIGHT", -36, -30)
    depositAllBtn:SetText("Deposit All")
    depositAllBtn:SetScript("OnClick", function()
        StaticPopup_Show("MODREAGENTBANK_DEPOSIT_ALL")
    end)

    scrollFrame = CreateFrame("ScrollFrame", "ModReagentBankScroll", mainFrame, "UIPanelScrollFrameTemplate")
    scrollFrame:SetPoint("TOPLEFT", mainFrame, "TOPLEFT", 12, -56)
    scrollFrame:SetPoint("BOTTOMRIGHT", mainFrame, "BOTTOMRIGHT", -30, 12)

    scrollChild = CreateFrame("Frame", nil, scrollFrame)
    scrollChild:SetWidth(420)
    scrollChild:SetHeight(200)
    scrollFrame:SetScrollChild(scrollChild)

    table.insert(UISpecialFrames, "ModReagentBankFrame")

    mainFrame:SetScript("OnUpdate", function(self, elapsed)
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

local function ResolveBagButton(a, b)
    -- Turtle/WotLK: (self, button). Vanilla 1.12: this=button, first arg=click type.
    if type(a) == "table" then
        return a, b
    end
    return this, a
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

eventFrame:SetScript("OnEvent", function(self, event, arg1, arg2, arg3, arg4)
    if event == "ADDON_LOADED" then
        if arg1 == AddonName then
            Initialize()
        end
    elseif event == "CHAT_MSG_ADDON" then
        OnAddonMessage(arg1, arg2, arg3, arg4)
    elseif event == "BAG_UPDATE" then
        if mainFrame and mainFrame:IsShown() and not pendingRequest then
            BuildDisplayModel()
            RefreshUI()
        end
    elseif event == "PLAYER_LEAVING_WORLD" or event == "PLAYER_LOGOUT" then
        ClearAllSnapshots()
        sessionOpen = false
        recentOpen = false
        if mainFrame and mainFrame:IsShown() then
            mainFrame:Hide()
        end
        ClearPendingRequest()
    elseif event == "PLAYER_LOGIN" or event == "PLAYER_ENTERING_WORLD" then
        -- FrameXML load order differs between normal addons and MPQ overlays.
        -- Retry until the stock container handlers exist; InstallBagHook itself
        -- is idempotent and never wraps a handler twice.
        InstallBagHook()
        if not recentOpen and mainFrame and mainFrame:IsShown() then
            HideAndReset()
        end
    end
end)

bgTexturePath = FALLBACK_BG
CreateMainFrame()
Initialize()

DEFAULT_CHAT_FRAME:AddMessage("|cff33ccffMaterial Storage|r loaded.")
