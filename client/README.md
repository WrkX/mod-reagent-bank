# MPQ / FrameXML packaging — mod-reagent-bank

The normal addon under `../addon/ModReagentBank/` is the development and
recommended installation path. This directory documents the optional MPQ
workflow for curated client distributions.

The repository intentionally ships **tooling + manifest**, not a production
MPQ. Do not publish an MPQ built from an undocumented Turtle client baseline.

## Required explicit inputs

`prepare-mpq-overlay.sh` requires explicit baseline + hash + marble inputs:

- baseline `CharacterFrame.xml` path (`--baseline`);
- expected baseline SHA-256 (`--baseline-sha256`);
- FrameXML Lua input (`--framexml-lua`, defaults to tracked
  `client/mpq-overlay/Interface/FrameXML/ModReagentBank.lua`);
- `marble.blp` source path (`--marble`);
- empty output stage path (`--stage`).

`marble.blp` is not assumed to ship from this tree. Supply a legitimate source
asset and record its provenance in your release notes (source repo/path/commit
and SHA-256).

## Supported client baseline

Extract baseline `CharacterFrame.xml` from the exact supported Turtle client:

- Version **1.18.1.7272**
- Plus the repository-documented **2026-04-12 hotfix set**

Do not hash or pin a different client extract as a substitute.

## Exact MPQ contents

Only these three internal paths are valid in the stage and final archive:

```text
Interface\FrameXML\ModReagentBank.lua
Interface\FrameXML\ModReagentBank\textures\marble.blp
Interface\FrameXML\CharacterFrame.xml
```

No unrelated upstream patch files (`PersonalLoot.lua`, tutorials, reroll icon,
etc.) belong in this module archive.

## CharacterFrame.xml rule

Packaging inserts exactly one line, immediately after the existing
`CharacterFrame.lua` include:

```xml
<Script file="ModReagentBank.lua"/>
```

Every other baseline byte must remain unchanged. If baseline SHA-256 changes,
the process must fail until `CharacterFrame.xml` is manually rebased and
revalidated.

## Staging and validation script

From `modules/mod-reagent-bank/client/`:

```sh
./prepare-mpq-overlay.sh \
  --baseline /path/to/CharacterFrame.xml \
  --baseline-sha256 <64-hex> \
  --framexml-lua ./mpq-overlay/Interface/FrameXML/ModReagentBank.lua \
  --marble /path/to/marble.blp \
  --stage /tmp/mod-reagent-bank-stage
```

What it enforces:

- baseline file hash equals `--baseline-sha256`;
- manifest hash for `CharacterFrame.xml.original` matches that same value;
- manifest has no `UNSET` placeholders for required entries;
- patched `CharacterFrame.xml` contains exactly one
  `<Script file="ModReagentBank.lua"/>`;
- staged tree contains exactly the three valid internal file paths;
- staged file hashes match `manifest.sha256`.

Optional archive verification (still does **not** build an MPQ):

```sh
./prepare-mpq-overlay.sh ... \
  --archive /path/to/mod-reagent-bank-dev.mpq \
  --archive-list /path/to/archive-listing.txt
```

`--archive-list` must normalize to exactly the same three internal paths.

## Manifest and hash pinning

`manifest.sha256` pins:

- baseline `CharacterFrame.xml` hash (`CharacterFrame.xml.original`);
- patched `CharacterFrame.xml` hash (`CharacterFrame.xml.patched` and
  `Interface/FrameXML/CharacterFrame.xml`);
- FrameXML Lua hash;
- `marble.blp` hash;
- optional final MPQ hash (`mod-reagent-bank-dev.mpq`).

Any required `UNSET` value is a hard failure.

## Build/install policy

- Use project-specific artifact names for development (example:
  `mod-reagent-bank-dev.mpq`).
- Never overwrite a live patch blindly; back up first.
- For production rollout, merge into the distribution's highest-priority patch
  or follow its established load order. Do not assume `patch-I.mpq` is free.
- Addon install (`Interface/AddOns/ModReagentBank`) remains independent and
  works without MPQ delivery.
