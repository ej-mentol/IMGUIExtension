---
title: CppFormatting
type: guide
permalink: imguiextension/cpp-formatting
tags:
- build
- tooling
- conventions
---

# C/C++ formatting

Formatting notes for this repository. The aggregator keeps the same note as
`metahooksv/cpp-formatting`; this copy carries the standalone paths.

## Toolchain and style

Formatting uses [MetaHookSv/FormatValidation](https://github.com/MetaHookSv/FormatValidation)
and clang-format **23.1.3**, with the DiligentCore style (four-space indentation, no tabs,
preserved include order). `cmake/FormatValidation.cmake` consumes
`FORMAT_VALIDATION_SOURCE_PATH` (CMake variable or environment) and otherwise fetches the
retained fixed commit; the shared submodule in the aggregator is
`thirdparty/FormatValidation`.

```powershell
python -m pip install clang-format==23.1.3
cmake -S . -B build/format -G "Visual Studio 18 2026" -DFORMAT_VALIDATION_ONLY=ON
cmake --build build/format --config Release --target format-check
cmake --build build/format --config Release --target format
```

The format-only configuration needs no compiler or game SDK. Formatting targets are
explicit dependencies only; a normal DLL build neither runs them nor installs Python
packages.

## Checked scope

Checks cover owned C/C++ files in `src/` and `tests/` (plus the root public header),
including non-ignored new files. Repository-relative exclusions live in
`.clang-format-ignore`; the bundled `thirdparty/` trees and build/install output are
excluded.

## Notes

- CMake generates an ignored `.clang-format` at the repository root; change the shared
  configuration, not that copy.
- `.clang-format-ignore` deliberately keeps `thirdparty/imgui` and `thirdparty/rapidjson`
  untouched even though they are compiled into the DLL.
- The sources were migrated with their original tab indentation. Running `format` once
  converts them to the shared four-space style; do that as its own commit so the
  restructuring diff stays reviewable.
