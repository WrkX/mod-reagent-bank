# ModReagentBank textures

`marble.blp` is optional for the normal addon. If it is missing, the addon
falls back to `Interface\Tooltips\UI-Tooltip-Background`.

Packaging may substitute the Lua constant `TEXTURE_ROOT` so the MPQ/FrameXML
build resolves textures under
`Interface\FrameXML\ModReagentBank\textures\marble` instead of
`Interface\AddOns\ModReagentBank\textures\marble`.

For reproducible MPQ delivery, provide `marble.blp` explicitly to
`client/prepare-mpq-overlay.sh --marble /path/to/marble.blp` and pin its
SHA-256 in `client/manifest.sha256`. Record the source repository/path/commit
in release notes.
