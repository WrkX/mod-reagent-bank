# mpq-overlay — tracked FrameXML input tree

This folder tracks only repository-owned MPQ inputs. It is **not** the final
release stage and should not contain client-derived baseline files.

## Tracked files in this folder

- `Interface/FrameXML/ModReagentBank.lua` (FrameXML loader variant)
- this `README.md`
- `.gitignore` rule that blocks committed `CharacterFrame.xml`

`marble.blp` and baseline `CharacterFrame.xml` are expected as explicit
packaging inputs, not silently assumed from this folder.

## Canonical internal MPQ paths

The generated stage/final archive must contain exactly:

```text
Interface\FrameXML\CharacterFrame.xml
Interface\FrameXML\ModReagentBank.lua
Interface\FrameXML\ModReagentBank\textures\marble.blp
```

No other upstream patch files belong in this module archive.

## Workflow

Use `../prepare-mpq-overlay.sh` to build a throwaway staging tree:

```sh
../prepare-mpq-overlay.sh \
  --baseline /path/to/CharacterFrame.xml \
  --baseline-sha256 <64-hex> \
  --framexml-lua ./Interface/FrameXML/ModReagentBank.lua \
  --marble /path/to/marble.blp \
  --stage /tmp/mod-reagent-bank-stage
```

That script enforces:

- baseline hash pinning;
- exactly one added `<Script file="ModReagentBank.lua"/>` include;
- exact three-file stage layout;
- manifest hash match for all required entries.

Optional `--archive` + `--archive-list` validates an already-built MPQ but
still does not create one.
