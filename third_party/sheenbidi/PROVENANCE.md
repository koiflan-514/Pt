# SheenBidi provenance

This directory vendors the Unicode Bidirectional Algorithm implementation that
backs Pt's 2.0 RTL/bidi support. Pt links it as one hidden-internal archive
object; none of its `SB*` symbols leave `libpt`.

## Upstream

- Project: SheenBidi
- URL: <https://github.com/Tehreer/SheenBidi>
- Tag: `v3.0.0`
- Commit: `cfe430e7375a7845b679adae9d51dac6deaa8858` (peeled `v3.0.0`)
- Source tarball: <https://github.com/Tehreer/SheenBidi/archive/refs/tags/v3.0.0.tar.gz>
- Tarball SHA-256:
  `86c56014034739ba39a24c23eb00323b0bf6f737354f665786015fca842af786`
- Fetched: 2026-11-22
- License: Apache License 2.0 (see `LICENSE`; upstream ships no separate NOTICE)

## Vendored subset

Only what the engine needs at build and run time is vendored:

- `Source/**` — 75 files, the unity entry point `Source/SheenBidi.c` plus the
  API, core, data, script and UBA implementation units and internal headers.
- `Headers/SheenBidi/**` — 23 public headers.
- `LICENSE`
- `README.md`
- `PROVENANCE.md` (this file, written by Pt)

Excluded upstream material: `Tools/` (the generator and its Unicode input
files), `Tests/`, and the CMake/Meson/Make build descriptions. The engine is
compiled by Pt's Makefile as a single translation unit with
`-DSB_CONFIG_UNITY`; the experimental `SB_CONFIG_EXPERIMENTAL_TEXT_API` is not
enabled.

Every vendored source and header retains its upstream Apache-2.0 copyright
header verbatim. The files are otherwise byte-identical to the tag.

## Unicode data version

The generated tables in `Source/Data/` were produced by upstream from the
Unicode Character Database **17.0.0** (the upstream `Tools/Unicode/` inputs are
`DerivedBidiClass-17.0.0.txt`, `Scripts-17.0.0.txt` and friends; the script
list in `Headers/SheenBidi/SBScript.h` ends with the Unicode 17.0 additions).
`pt_bidi_unicode_version()` reports `17.0.0` for test output.

The conformance fixtures under `tests/data/bidi/` are versioned separately;
see `THIRD_PARTY.md` for their URLs and hashes.

## Upgrade policy

A SheenBidi upgrade is one reviewed change that moves together:

1. the vendored `Source/` + `Headers/` subset and this provenance record
   (tag, commit, tarball hash, regenerated table note);
2. the Unicode version reported by `pt_bidi_unicode_version()`;
3. the BidiTest/BidiCharacterTest data in `tests/data/bidi/` (new version,
   URL and SHA-256 in `THIRD_PARTY.md`);
4. the `THIRD_PARTY.md` SheenBidi row; and
5. the conformance test output pasted into the change description, showing
   the exact case and failure counts.

The generated lookup tables in `Source/Data/` are never hand-edited. They are
replaced wholesale from the upstream tag so that the generator input and the
checked-in tables cannot drift apart.

## Integration note

Pt's adapter (`src/bidi.c`) decodes input to UTF-32 with the engine's own
`SBCodepointDecodeNextFromUTF8` and keeps a code-point-to-byte table, then
calls `SBAlgorithmCreateParagraph` / `SBParagraphCreateLine` on the UTF-32
sequence and maps run offsets back to logical UTF-8 byte positions.

This is deliberate. In the direct UTF-8 path, SheenBidi marks UTF-8
continuation bytes as `BN`; `SBLine`'s L1 reset accumulates those bytes across
an isolate formatting character after a segment separator and resets too much.
For `S RLE PDI R` (UTF-32 levels `0 0 1 1`) the UTF-8 line run becomes
`[0,6,0][6,3,1]` instead of `[0,2,0][2,2,1]`, dropping the PDI's level. That
path fails 1,514 of the 770,241 BidiTest cases; the UTF-32 path passes all of
them. The vendored sources are unchanged; the conversion lives entirely in
Pt's adapter.