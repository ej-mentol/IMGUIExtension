---
title: font_pipeline
type: note
permalink: imguiextension/font-pipeline
tags:
- fonts
- imgui
- configuration
---

# Font discovery, configuration and atlas

## Discovery

- Config: `<gamedir>/imguiextension/imguiextension.json`, resolved through the engine
  filesystem (`GAME`), so an addon copy wins over the base mod automatically.
- Font directory preference for scanning/generation: `<gamedir>_addon/imguiextension/fonts`,
  then `<gamedir>/imguiextension/fonts`, then the filesystem-resolved
  `imguiextension/fonts`. `GetGameDirectory()` returns the mod/addon *name*, so these are
  relative paths resolved against the process working directory (the game root).
- The config is generated only when it is missing or lists zero fonts. Existing files are
  never rewritten, so user sizes/ranges are preserved. A config that lists only unusable
  fonts falls back to the built-in font and logs each failure; delete the JSON to rescan.

## Entry validation (before ImGui sees the file)

Dear ImGui 1.92 asserts on unreadable or implausibly small font data
(`IM_ASSERT_USER_ERROR` in `AddFontFromFileTTF`, `font_data_size > 100` in
`AddFontFromMemoryTTF`). Every entry is therefore resolved and checked first:

1. `GetLocalPath("imguiextension/fonts/<file>")` must succeed.
2. `GetFileAttributesExA` must report a regular file (not a directory).
3. The file must be larger than 100 bytes.

Failures are logged and the entry is skipped. `ImFontFlags_NoLoadError` is also set, so a
race (file removed between check and load) cannot assert either. Sizes outside
`[6, 200]` (including 0/negative/NaN) become 18.0 with a warning.

## Load order

- The first usable non-merge entry is the **primary** font: it becomes `Fonts[0]` and is
  the destination for every merge source. If the declared primary is unusable, the first
  usable base takes its place.
- Merge entries follow the primary (all of them attach to the primary font), then any
  additional base fonts are added as separately selectable fonts.
- If no base font is usable at all, the first merge creates a built-in default base with an
  explicit `SizePixels` (merging an explicitly sized font onto an implicitly sized base is
  an ImGui assert), then merges attach to it.

## Glyph ranges

`ranges: ["latin", "cyrillic", "symbols", "default", "korean", "japanese", "chinese"]`.
Names are combined into a single sorted, coalesced, NUL-terminated `ImWchar` list
(`src/FontGlyphRanges.h`, covered by `IMGUIExtensionFontTests`). The combined list is owned
by process-lifetime storage because `ImFontConfig::GlyphRanges` only stores a pointer that
ImGui dereferences for as long as the font exists. A single range is passed through without
copying; unknown names are logged and ignored.

## Atlas lifecycle

The atlas is **not** built by this plugin. ImGui 1.92 builds lazily as glyphs are requested,
and the OpenGL2 backend owns the texture (`ImGuiBackendFlags_RendererHasTextures`), so after
`ImGui_VidReinit` destroys backend objects the texture is re-created from the retained atlas
on the next render. The old `GetTexDataAsAlpha8()` diagnostic call was removed: it forced a
synchronous build, switched `TexDesiredFormat` to Alpha8 (the OpenGL2 backend asserts
RGBA32), and preloaded every glyph on the legacy path.

## Logging

A successful load is silent at normal developer level: per-font "loaded" lines and the final
source counts print only at `developer > 1`. Normal-level output is reserved for actionable
problems: unparsable/missing config, unresolvable or too-small font files, unknown ranges and
a failed load.

## Known limits

- Font file names travel through narrow ANSI file APIs (`GetLocalPath`, `fopen` inside
  ImGui), so names outside the system ANSI code page cannot be loaded; generation writes
  those bytes verbatim and the config then fails to parse as UTF-8.
- A changed font directory does not regenerate an existing non-empty config.
- There is no per-consumer atlas or size: every consumer shares the same primary font and
  global DPI/size scaling.
