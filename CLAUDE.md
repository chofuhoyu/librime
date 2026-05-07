# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Test Commands

```bash
make                    # Release build (build/)
make debug              # Debug build with ALSO_LOG_TO_STDERR
make test               # Release build + run ctest
make test-debug         # Debug build + run ctest
make clean              # Remove build dir
sudo make install       # Install to prefix

# Code formatting (Chromium style, SortIncludes: false)
make clang-format-lint   # Check formatting
make clang-format-apply  # Apply formatting

# Build with merged plugins (no external plugin loading)
make merged-plugins

# Build static library
make librime-static
```

## Architecture

librime is the Rime input method engine — a modular, extensible C++17 library that converts keystrokes into Chinese text. It exposes a versioned C API (`src/rime_api.h`) wrapping an internal C++ engine.

### Module System

Four built-in modules loaded at startup (defined in `src/rime/setup.cc`):

| Module | Source | Purpose |
|--------|--------|---------|
| `core` | `src/rime/` (base files) | Engine, context, composition, key events, menu |
| `dict` | `src/rime/algo/` + `src/rime/dict/` | Spelling algebra, dictionary data structures, user DB |
| `gears` | `src/rime/gear/` | Schema-driven input method components (processors, segmentors, translators, filters) |
| `levers` | `src/rime/lever/` | Deployment tasks, schema customization, user dict sync |

External plugins (librime-lua, librime-octagram, librime-predict) are loaded via `ENABLE_EXTERNAL_PLUGINS`.

### Key Processing Pipeline

Every keystroke goes through `Engine::ProcessKey`:

1. **Processors** — each key event is offered to `engine/processors` in order. Returns `kAccepted` (consumed), `kRejected` (stop), or `kNoop` (continue to next processor). The Switcher (handles Ctrl+` for schema switching) is always the first processor.
2. **Post-processors** — run after unhandled keys (e.g., shape_processor for full-width character conversion).
3. **Segmentation** — when composition updates, segmentors (`engine/segmentors`) partition raw input into translatable segments.
4. **Translation** — translators (`engine/translators`) convert each segment's input string into candidate lists. Multiple translators can contribute to the same segment.
5. **Filters** — filters (`engine/filters`) modify candidate lists after translation (dedup, charset filtering, simplification, etc.).
6. **Formatters** — shape_formatter converts output text (half-width → full-width forms).

### Configuration System

Schema configuration is YAML-based. `Config` objects support includes, patches, and inheritance (`__include`, `__patch`). The engine pipeline is entirely schema-driven — each schema's YAML file lists which processors/segmentors/translators/filters to load. The `ConfigCompiler` resolves dependencies and applies patches.

### Naming Conventions

- `the<T>` = `std::unique_ptr<T>`
- `an<T>` / `of<T>` = `std::shared_ptr<T>`
- `New<T>(args...)` = `std::make_shared<T>(args...)`
- `As<T>(ptr)` = `std::dynamic_pointer_cast<T>(ptr)`

### Component Registry

All pipeline components (Processor, Segmentor, Translator, Filter) use a self-registration pattern via `Class<T, Arg>`. Components are created from Ticket objects, which carry the engine pointer, component type, and class name. The `Registry` singleton maps string names to `ComponentBase*` factories.
