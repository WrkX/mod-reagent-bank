#!/bin/sh
# Prepare and validate the reproducible three-file FrameXML overlay for
# mod-reagent-bank.
#
# This script intentionally never builds an MPQ. It stages and validates input
# files only; archive assembly and compression policy stay distribution-specific.
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
default_manifest="$script_dir/manifest.sha256"
default_framexml_lua="$script_dir/mpq-overlay/Interface/FrameXML/ModReagentBank.lua"

baseline=
baseline_sha256=
stage=
manifest=$default_manifest
framexml_lua=$default_framexml_lua
marble=
archive=
archive_list=

usage() {
    cat <<'EOF'
Usage:
  prepare-mpq-overlay.sh \
      --baseline /path/to/CharacterFrame.xml \
      --baseline-sha256 <64-hex> \
      --marble /path/to/marble.blp \
      --stage /empty/output/path \
      [--framexml-lua /path/to/ModReagentBank.lua] \
      [--manifest manifest.sha256]

Optional archive verification:
  prepare-mpq-overlay.sh ... \
      --archive /path/to/mod-reagent-bank-dev.mpq \
      --archive-list /path/to/paths.txt

Inputs are explicit by design:
  - baseline CharacterFrame.xml and its expected SHA-256 must be provided;
  - marble.blp must be provided as an explicit source input;
  - FrameXML Lua defaults to the tracked mpq-overlay file (overrideable);
  - manifest entries must be real SHA-256 values (UNSET is rejected).
EOF
}

die() {
    printf '%s\n' "error: $*" >&2
    exit 1
}

require_value() {
    test "$#" -eq 2 || die "internal argument error"
    test -n "$2" || die "missing value for $1"
}

normalize_sha256() {
    value=$1
    case "$value" in
        *[!0123456789abcdefABCDEF]*|"")
            die "invalid SHA-256 value: $value"
            ;;
    esac
    test "${#value}" -eq 64 || die "SHA-256 must be exactly 64 hex characters"
    printf '%s\n' "$(printf '%s' "$value" | tr '[:upper:]' '[:lower:]')"
}

while test "$#" -gt 0; do
    case "$1" in
        --baseline)
            shift; require_value --baseline "${1-}"; baseline=$1 ;;
        --baseline-sha256)
            shift; require_value --baseline-sha256 "${1-}"; baseline_sha256=$1 ;;
        --stage)
            shift; require_value --stage "${1-}"; stage=$1 ;;
        --manifest)
            shift; require_value --manifest "${1-}"; manifest=$1 ;;
        --framexml-lua)
            shift; require_value --framexml-lua "${1-}"; framexml_lua=$1 ;;
        --marble)
            shift; require_value --marble "${1-}"; marble=$1 ;;
        --archive)
            shift; require_value --archive "${1-}"; archive=$1 ;;
        --archive-list)
            shift; require_value --archive-list "${1-}"; archive_list=$1 ;;
        -h|--help)
            usage; exit 0 ;;
        *)
            die "unknown option: $1" ;;
    esac
    shift
done

test -n "$baseline" || die "--baseline is required"
test -n "$baseline_sha256" || die "--baseline-sha256 is required"
test -n "$framexml_lua" || die "--framexml-lua is required"
test -n "$marble" || die "--marble is required"
test -n "$stage" || die "--stage is required"
test -f "$baseline" || die "baseline does not exist or is not a regular file: $baseline"
test -f "$manifest" || die "manifest not found: $manifest"
test -f "$framexml_lua" || die "FrameXML Lua input missing: $framexml_lua"
test -f "$marble" || die "marble input missing: $marble"
test ! -e "$stage" || die "refusing to overwrite existing stage path: $stage"

if test -n "$archive" || test -n "$archive_list"; then
    test -n "$archive" && test -n "$archive_list" || die "--archive and --archive-list must be supplied together"
    test -f "$archive" || die "archive does not exist or is not a regular file: $archive"
    test -f "$archive_list" || die "archive list does not exist or is not a regular file: $archive_list"
fi

command -v awk >/dev/null 2>&1 || die "awk is required"
command -v find >/dev/null 2>&1 || die "find is required"
command -v perl >/dev/null 2>&1 || die "perl is required"
command -v sha256sum >/dev/null 2>&1 || die "sha256sum is required"
command -v sort >/dev/null 2>&1 || die "sort is required"
command -v wc >/dev/null 2>&1 || die "wc is required"

sha256_file() {
    sha256sum "$1" | awk '{ print tolower($1) }'
}

manifest_hash() {
    requested_path=$1
    result=$(
        awk -v wanted="$requested_path" '
            BEGIN { count = 0 }
            /^[[:space:]]*#/ || NF == 0 { next }
            {
                if ($2 == wanted) {
                    count++
                    hash = $1
                }
            }
            END {
                if (count != 1)
                    exit 2
                print hash
            }
        ' "$manifest"
    ) || die "manifest must contain exactly one hash entry for $requested_path"

    case "$result" in
        UNSET|"")
            die "manifest hash is unset for $requested_path"
            ;;
    esac
    normalize_sha256 "$result"
}

expect_hash() {
    expected=$(manifest_hash "$1")
    actual=$(sha256_file "$2")
    test "$actual" = "$expected" || die "SHA-256 mismatch for $1 (expected $expected, got $actual)"
}

assert_exact_file_set() {
    root=$1
    expected_a="Interface/FrameXML/CharacterFrame.xml"
    expected_b="Interface/FrameXML/ModReagentBank.lua"
    expected_c="Interface/FrameXML/ModReagentBank/textures/marble.blp"

    test -f "$root/$expected_a" || die "generated stage is missing $expected_a"
    test -f "$root/$expected_b" || die "generated stage is missing $expected_b"
    test -f "$root/$expected_c" || die "generated stage is missing $expected_c"

    count=$(find "$root" -type f | wc -l | tr -d '[:space:]')
    test "$count" = "3" || die "generated stage contains $count files; expected exactly 3"

    unexpected=$(
        find "$root" -type f | LC_ALL=C sort | awk -v base="$root/" '
            BEGIN {
                allow["Interface/FrameXML/CharacterFrame.xml"] = 1
                allow["Interface/FrameXML/ModReagentBank.lua"] = 1
                allow["Interface/FrameXML/ModReagentBank/textures/marble.blp"] = 1
            }
            {
                rel = $0
                if (index(rel, base) == 1)
                    rel = substr(rel, length(base) + 1)
                if (!(rel in allow))
                    print rel
            }
        '
    )
    test -z "$unexpected" || die "generated stage has unexpected files: $unexpected"
}

baseline_sha256=$(normalize_sha256 "$baseline_sha256")
manifest_baseline_sha256=$(manifest_hash CharacterFrame.xml.original)
test "$baseline_sha256" = "$manifest_baseline_sha256" || die "--baseline-sha256 does not match manifest CharacterFrame.xml.original"

manifest_patched_sha256=$(manifest_hash CharacterFrame.xml.patched)
manifest_overlay_character_sha256=$(manifest_hash Interface/FrameXML/CharacterFrame.xml)
test "$manifest_patched_sha256" = "$manifest_overlay_character_sha256" || die "manifest CharacterFrame.xml.patched and Interface/FrameXML/CharacterFrame.xml hashes must match"

expect_hash CharacterFrame.xml.original "$baseline"
expect_hash Interface/FrameXML/ModReagentBank.lua "$framexml_lua"
expect_hash Interface/FrameXML/ModReagentBank/textures/marble.blp "$marble"

mkdir -p "$stage/Interface/FrameXML/ModReagentBank/textures"
patched_character_frame="$stage/Interface/FrameXML/CharacterFrame.xml"
cp "$baseline" "$patched_character_frame"

# Preserve all baseline bytes except one inserted line immediately following
# the existing CharacterFrame.lua include.
perl -0777 -i -pe '
    my $anchor = q{<Script file="CharacterFrame.lua"/>};
    my $loader = q{<Script file="ModReagentBank.lua"/>};

    my $anchor_count = () = $_ =~ /\Q$anchor\E/g;
    die "expected exactly one CharacterFrame.lua include\n" unless $anchor_count == 1;
    die "ModReagentBank.lua loader already exists in baseline\n" if /\Q$loader\E/;

    my $insertions = s/^([ \t]*)(<Script file="CharacterFrame\.lua"\/>[^\r\n]*)(\r?\n|$)/
        my $newline = $3 eq q{} ? qq{\n} : $3;
        qq{$1$2$newline$1$loader$newline};
    /me;
    die "failed to add ModReagentBank loader\n" unless $insertions == 1;
' "$patched_character_frame" 2>/dev/null || die "could not insert the ModReagentBank.lua loader line next to CharacterFrame.lua"

perl -0777 -ne '
    my $loader = q{<Script file="ModReagentBank.lua"/>};
    my $count = () = $_ =~ /\Q$loader\E/g;
    exit($count == 1 ? 0 : 1);
' "$patched_character_frame" || die "patched CharacterFrame.xml must contain exactly one ModReagentBank loader line"

cp "$framexml_lua" "$stage/Interface/FrameXML/ModReagentBank.lua"
cp "$marble" "$stage/Interface/FrameXML/ModReagentBank/textures/marble.blp"

assert_exact_file_set "$stage"
expect_hash CharacterFrame.xml.patched "$patched_character_frame"
expect_hash Interface/FrameXML/CharacterFrame.xml "$patched_character_frame"

if test -n "$archive"; then
    expect_hash mod-reagent-bank-dev.mpq "$archive"
    normalized_list=$(
        awk '
            NF {
                gsub(/\\/, "/", $0)
                sub(/^\.\//, "", $0)
                print
            }
        ' "$archive_list" | LC_ALL=C sort
    )
    expected_list='Interface/FrameXML/CharacterFrame.xml
Interface/FrameXML/ModReagentBank.lua
Interface/FrameXML/ModReagentBank/textures/marble.blp'
    test "$normalized_list" = "$expected_list" || die "MPQ archive listing does not contain exactly the three declared internal paths"
fi

printf '%s\n' "MPQ overlay stage is valid: $stage"
if test -n "$archive"; then
    printf '%s\n' "MPQ archive hash and internal path audit are valid: $archive"
else
    printf '%s\n' "No MPQ binary was created. Build it from this stage, then rerun with --archive and --archive-list."
fi
