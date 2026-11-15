# Repository Guidelines

## Project Structure & Module Organization

This Windows desktop Markdown editor uses C++20, Qt 6 Widgets, and bundled cmark-gfm. `src/main.cpp` starts the application; `src/core/` owns text, positions, edit transactions, and undo history. Parsing lives in `src/parser/`, display projection in `src/mapping/`, layout and viewport behavior in `src/editor/`, and asynchronous math/Mermaid rendering in `src/render/`. File persistence belongs in `src/file/`; window components and themes live in `src/ui/` and `src/theme/`.

`tests/TestProbes.cpp` contains the Qt Test regression suite. `3rdparty/cmark-gfm/` is vendored code; avoid unrelated edits there. `welcome.md` is the sample document. The root `CMakeLists.txt` defines application and test targets.

## Build, Test, and Development Commands

Use a VS 2022 x64 developer PowerShell with CMake 3.20+, Ninja, and Qt 6 available. Adjust the Qt installation path:

```powershell
$env:CMAKE_PREFIX_PATH = 'C:\Qt\6.8.2\msvc2022_64'
$env:PATH = "$env:CMAKE_PREFIX_PATH\bin;$env:PATH"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
.\build\test_probes.exe -v2
windeployqt --compiler-runtime build\MarkdownNativeEditor.exe
.\build\MarkdownNativeEditor.exe welcome.md
```

These configure, compile, run the registered suite or verbose probes, deploy Qt dependencies, and launch the editor. Preserve existing build directories.

## Coding Style & Naming Conventions

Follow surrounding C++ style: four-space indentation, attached braces, `PascalCase` classes and matching `.h`/`.cpp` filenames, `camelCase` functions, and `m_` member prefixes. Use the appropriate `MarkdownTool` subnamespace and Qt ownership conventions. No project formatter or linter configuration is provided.

## Testing Guidelines

Add regression slots named `testDescriptiveBehavior()` to `TestProbes`. Use Qt Test assertions and temporary directories for file operations. No numeric coverage threshold is configured. Cover source-byte preservation, Unicode positions, undo, IME, rendering races, or viewport behavior when affected. Run the full suite and manually inspect changed visual interactions; report passed, failed, and skipped checks separately.

## Commit & Pull Request Guidelines

Recent commits use `feat(ui): ...`, `fix(core,ui): ...`, and `test: ...`. Keep subjects imperative and scope-specific. PRs should explain the problem, resulting behavior, and validation commands/results; link relevant issues and include screenshots for visual changes.

## Architecture Constraints

Markdown source remains the single source of truth. Preserve untouched bytes, encoding, line endings, atomic-save conflict checks, Qt object lifetimes, and asynchronous cancellation guarantees. Keep edits focused and preserve existing user changes.
