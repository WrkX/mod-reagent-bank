# mod-reagent-bank — Material Storage

Native Tortoise/Turtle module that ports the useful behavior of
[WoWGreymane/mod-reagent-Bank](https://github.com/WoWGreymane/mod-reagent-Bank)
(audited at `ec9e3c73a58b80c0456200b9706b43a5bb5f2dec`) into this repository's
module system and Vanilla 1.12 client environment.

This is **not** an AzerothCore module. It uses Tortoise script hooks, `sConfig`,
this core's database APIs, and Turtle-compatible addon transport — no
AzerothCore headers, `AC_*` CMake helpers, or WotLK client requirements.

## What Material Storage does

Every normal banker that already offers personal banking also offers **Material
Storage** in its gossip menu. Material Storage holds a large per-character count
of eligible crafting materials as **virtual fungible balances** — they do not
occupy normal bank slots.

Players can:

- purchase Material Storage once per character from a banker;
- deposit one carried stack or deposit all eligible carried stacks;
- query stored balances; and
- withdraw a requested amount (one normal stack by default).

Recipes, quest objectives, vendors, mail, and the auction house **do not**
consume items directly from Material Storage. The player must **withdraw**
materials into inventory before use.

Server-side validation is authoritative: the client UI is convenience only;
mutations require an open session tied to a banker in range.

## Gossip and protocol safety

The menu entry is appended after the core has run a banker-specific gossip
script, so scripted bankers retain their own menu and behavior. On the next
config load that enables the module, already-loaded bankers gain the gossip
flag too; no worldserver restart is required.

Every inbound `RBANK` payload is capped at 240 bytes before tokenization.
Client request IDs are the inclusive `1..1000000` ring (zero belongs only to
the initial server snapshot). Per open banker session, the server records the
latest 32 executed request IDs. A repeat inside that window returns its prior
result and a newly generated snapshot at the current revision, without running
the mutation again. Older, backward, or ambiguous-half-ring IDs return
`BAD_REQUEST` with `STALE_REQUEST`; successful mutations alone advance the
revision.

Characters that have not purchased Material Storage see the bundled standard
StaticPopup confirmation box with the configured gold price and a Yes/No
choice. The server validates the character's purchase state and current gold
again after the client confirms.

## Module layout

```text
modules/mod-reagent-bank/
  README.md                 ← this file
  LICENSE                   ← MIT; upstream attribution
  conf/
    mod-reagent-bank.conf.dist
  data/sql/character/
    20260907000000_reagent_bank.sql
    20260911000000_reagent_bank_purchase.sql
  src/
    ReagentBankConfig.{h,cpp}
    ReagentBankProtocol.{h,cpp}
    ReagentBankStore.{h,cpp}
    ReagentBankScripts.cpp
    mod_reagent_bank_loader.cpp
  addon/
    ModReagentBank/         ← recommended client install (Interface: 1800)
      ModReagentBank.toc
      ModReagentBank.lua
      textures/             ← optional external marble.blp; addon falls back to the tooltip background
  client/                   ← optional MPQ/FrameXML packaging (see client/README.md)
    mpq-overlay/
    manifest.sha256
    README.md
  t/                        ← optional headless tests
    TestReagentBankProtocol.cpp
    TestReagentBankRules.cpp
```

## Build

Enable modules when configuring CMake. Static linking is the recommended
deployment:

```sh
cmake -S . -B build -DMODULES=static
cmake --build build -- -j1
```

Per-module control uses the generated cache variable:

```sh
cmake -S . -B build -DMODULE_MOD_REAGENT_BANK=static
```

CMake discovers the module from `src/` and installs `conf/mod-reagent-bank.conf.dist`.
Copy it to `mod-reagent-bank.conf` (without `.dist`) in the module config directory
and review settings before starting the worldserver.

## Worldserver prerequisites

These keys belong under **`[worldserver]`** in the main world configuration (not
only in the module config file):

| Setting | Purpose |
|---|---|
| `AddonChannel = 1` | Required for `RBANK` addon messages |
| `ALLOW_TURTLE_ADDONS=ON` | Keep this fork's default so addon transport works |
| `Database.AutoUpdate.AllowedModules = "all"` | Or an allowlist containing `mod-reagent-bank` |

Module-specific settings live in `conf/mod-reagent-bank.conf.dist` under
`[worldserver]`:

| Key | Default | Description |
|---|---|---|
| `ReagentBank.Enable` | `1` | Master switch; when `0`, no gossip option and open sessions close; stored rows are untouched |
| `ReagentBank.DepositAllEnable` | `1` | Allow `DEPOSIT_ALL` |
| `ReagentBank.PurchaseCost` | `250` | One-time unlock price per character, in whole gold; valid range `0..214748` |
| `ReagentBank.MaxAmountPerItem` | `1000000` | Per-character ceiling per item entry; strict decimal `1..4294967295`, invalid values fall back to the default |
| `ReagentBank.Debug` | `0` | Extra protocol/mutation logging |

## Database

### Table: `custom_reagent_bank`

The migration keeps the **upstream table name** so existing realm balances can
be imported without a lossy conversion:

| Column | Notes |
|---|---|
| `character_id` | Character low GUID |
| `item_entry` | Item template entry |
| `item_subclass` | Retained for import compatibility; **re-derived from `ItemPrototype` on every write** — never trusted |
| `amount` | Stored fungible count; unsigned and NOT NULL |
| `revision` | Compare-and-change revision used to reject stale balance writes |
| `legacy` | `1` only for balances present when this port migrates an upstream table |
| `mutation_guard` | Internal FK-backed compare-and-change guard; always `1` in a valid row |

### Table: `custom_reagent_bank_access`

This table records which characters have purchased Material Storage. The
purchase deducts the configured amount of gold and inserts this row in the
same direct character transaction. Existing characters with Material Storage
balances are migrated as already purchased.

The table uses InnoDB. There is **no foreign key** to `characters` because this
tree's `characters` table is MyISAM. It does have a private foreign key to the
single-row `custom_reagent_bank_mutation_guard` table. Do not remove that
constraint or its parent row. A stale debit/credit writes guard value `0`, which
has no parent and fails the statement. A missing row is a successful 0-row
`UPDATE` in MySQL, so the same transaction also `INSERT`s parent value `1` unless
the expected post-state exists; a duplicate primary key then fails the complete
inventory-and-balance transaction. Neither path writes `NULL` or depends on
strict `sql_mode`. If a direct commit still fails after inventory was already
mutated in memory, the store unloads that character without saving so login
reloads the last committed bags and balances.

### Upstream import and legacy redemption

The upstream table used signed `INT` columns. This migration first copies rows
with non-positive IDs or amounts into `custom_reagent_bank_legacy_quarantine`,
then removes those invalid rows before converting the active table to unsigned
columns. Quarantine rows require explicit operator review; they are never
silently converted into a huge material balance.

Every positive pre-port row is marked `legacy = 1`. Upstream accepted all gems
and trade goods, while this port intentionally has stricter deposit rules.
Legacy rows can therefore be **withdrawn only** when their current item template
is a gem or trade good; they cannot be topped up unless the normal strict
eligibility rules pass. Once fully withdrawn, the row is deleted. This avoids
stranding valid upstream balances without weakening new deposits.

### Pre-deploy audit (realms with existing data)

Before deploying, back up the table and run these queries. Record the results:

```sql
SELECT COUNT(*), SUM(amount) FROM custom_reagent_bank;

SELECT character_id, item_entry, amount
FROM custom_reagent_bank
WHERE amount <= 0 OR amount > 1000000;

SELECT character_id, item_entry, amount, reason, quarantined_at
FROM custom_reagent_bank_legacy_quarantine
ORDER BY id;

SELECT rb.item_entry
FROM custom_reagent_bank rb
LEFT JOIN item_template i ON i.entry = rb.item_entry
WHERE i.entry IS NULL;
```

Invalid amounts and orphan item entries must be reported and corrected by an
operator before go-live. The module never turns an unknown entry into an item.
Also audit imported legacy rows against the world `item_template` data: a legacy
row whose item is no longer a gem or trade good remains visible only to staff in
the database and needs a manual compensation/export decision.

### Character deletion cleanup

On character delete, the module removes all `custom_reagent_bank` rows for that
character's low GUID so balances cannot be orphaned.

## Client installation (recommended)

Copy the addon folder to the Turtle client:

```text
modules/mod-reagent-bank/addon/ModReagentBank
  → <WoW>/Interface/AddOns/ModReagentBank
```

The addon does **not** require an MPQ. See `client/README.md` for the optional
FrameXML/MPQ path used by curated client distributions.

## Optional MPQ / FrameXML

An MPQ overlay is supported for distributions that want the UI loaded as
FrameXML without a separate addon install. **Do not commit a production `.mpq`**
assembled against an unknown client build. This repository ships the overlay
tree, manifest placeholders, and reproducible packaging instructions only.

Details: [`client/README.md`](client/README.md).

## Deployment and rollback

### Deploy

1. Back up `custom_reagent_bank`, `character_inventory`, and `item_instance`.
2. Run and record the pre-migration audit queries above.
3. Deploy server module, config, and migration — consider `ReagentBank.Enable = 0`
   initially when importing production balances.
4. Install the addon on a test client; validate one test character and the
   material-count invariant.
5. Enable for a limited realm test; monitor logs; then deploy the chosen client
   package if using MPQ.

### Rollback

**Server:** set `ReagentBank.Enable = 0`. Do **not** drop `custom_reagent_bank` —
balances remain for re-enable or export. Remove the module from the next build
if needed.

**Client:** remove `Interface/AddOns/ModReagentBank`, or remove/restore the MPQ
artifact (see `client/README.md`). No server or database changes are required
for client-only rollback.

## Attribution

Licensed under the [MIT License](LICENSE).

Upstream behaviour reference:
[WoWGreymane/mod-reagent-Bank](https://github.com/WoWGreymane/mod-reagent-Bank)
at commit `ec9e3c73a58b80c0456200b9706b43a5bb5f2dec`.

The Tortoise port rewrites the integration layer, protocol, eligibility rules,
gossip path, and client UI for this core. It is not a copy of the AzerothCore
sources.

## Rewritten vs inherited

### Inherited behaviour (conceptual / data)

- Material Storage offered from normal bankers alongside personal bank.
- Virtual fungible storage for eligible trade goods, gems, and reagents.
- Deposit one stack, deposit all eligible carried stacks, query, withdraw.
- Table name `custom_reagent_bank` and compatible row shape for zero-copy import.
- General UI flows: categories, right-click deposit, shift-right-click withdraw
  amount, deposit-all confirmation, server-driven snapshots.
- Optional `textures/marble.blp` background art path compatibility in addon and FrameXML delivery.

### Rewritten or native-only (not copied from upstream integration)

| Area | This port |
|---|---|
| C++ module loader, config, store, protocol, scripts | Native Tortoise `ScriptObjects`, `sConfig`, `PQuery`/`PExecute`, GUID-serialized commits |
| Gossip / banker access | Post-`CMSG_GOSSIP_HELLO` menu append preserves creature-specific gossip; narrow `CanPacketReceive` intercept on `CMSG_GOSSIP_SELECT_OPTION`; no AzerothCore `CanCreatureGossipSelect` |
| Addon transport | `RBANK` prefix, versioned framed protocol, `SendAddonMessage` / GUILD channel; no `LANG_ADDON` whispers |
| Eligibility | Single `CanStore` predicate including `ITEM_CLASS_REAGENT`; strict fungible-instance guards |
| Deposit / withdraw | Precondition checks, inventory persistence inside GUID-serialized transaction, revision snapshots |
| Client Lua | Lua 5.0 / Interface 1800 rewrite; no WotLK APIs or expansion item whitelist |
| `CharacterFrame.xml` | Extracted from the supported Turtle client baseline; **one** loader line added — not upstream's bundled replacement |
| MPQ contents | Only three module-owned files; no `PersonalLoot.lua`, `GreymaneTutorials.lua`, reroll icon, or custom NPC `290011` |
