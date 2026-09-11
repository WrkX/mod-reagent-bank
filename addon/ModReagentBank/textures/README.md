# ModReagentBank textures

`marble.blp` is the original mod-reagent-bank marble background. `marble.tga`
is the same artwork converted to an uncompressed 512x512 RGB TGA, which the
Vanilla/Turtle client can load from a loose addon. The Lua addon uses the TGA.

Packaging may substitute the Lua constant `TEXTURE_ROOT` so the MPQ/FrameXML
build resolves textures under
`Interface\FrameXML\ModReagentBank\textures\marble.tga` instead of
`Interface\AddOns\ModReagentBank\textures\marble.tga`.

For reproducible MPQ delivery, pass this file explicitly to
`client/prepare-mpq-overlay.sh --marble /path/to/marble.blp` and pin its
SHA-256 in `client/manifest.sha256`. The tracked asset came from
`C:/AC/azerothcore/modules/mod-reagent-bank/client/ModReagentBank/textures/marble.blp`
and has SHA-256
`911f8bb1d15f9ca6fccf542d2906906604e034be73d5c6208e9cddcfd5b4f5cd`.
