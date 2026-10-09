# AGENTS.md

This file provides guidance to Codex (Codex.ai/code) when working with code in this repository.

## Build

```
make          # incremental optimized Release build
make debug    # incremental Debug build with debugging symbols
make test     # build and run unit + startup smoke tests
make release  # clean optimized build
make clean    # delete build/ and terminote.exe
```

Compiler: `gcc -std=c11 -static-libgcc -finput-charset=UTF-8 -fexec-charset=UTF-8 -Iinclude -Wall -Wextra`. Debug and Release object/dependency files are isolated under `build/debug/` and `build/release/`; test executables are isolated under `build/tests/<mode>/` and retain assertions in both modes. Release uses `-Os`, LTO, unused-section removal, and stripped symbols. Source files are UTF-8 and string literals are embedded as UTF-8 for the Windows console running code page 65001.

## Architecture

Terminote is a nano-inspired Windows console text editor. It uses the raw Windows Console API — **not** ncurses or any Unix terminal library.

**Global state** lives in a single `struct editorConfig E` (declared in `include/common.h`, defined in `src/common.c`). All modules access it via `extern`. There are also global undo/redo stacks (`undo_stack`, `redo_stack`, their counts/capacities, and `undo_suspended`).

### Module responsibilities

| Module     | Role                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `common`   | Global state (`E`), data structures (`erow`, `undoRecord`, `abuf`, `findMatch`), enums for key codes / syntax types / undo ops, GBK/UTF-8 character helpers, `die()`, `editor_strdup()`, `is_separator()`, `editorSyntaxToColor()`                                                                                                                                                                                                                                                                                                                                                                  |
| `terminal` | Raw mode lifecycle only: configures console input/output modes, alternate screen, UTF-8 code pages, and restores the original console state on exit. |
| `terminal_input` | Decodes `ReadConsoleInputA` keyboard, mouse, resize, and terminal-expanded paste events into `editorKey` values. Distinguishes physical typing from synthetic terminal text and owns the pending atomic paste buffer. |
| `cli` | Parses `--config`, `--help`, `--verbose`, `--version`, positional files, and runs the cyclic interactive configuration picker. |
| `logger` | Creates one timestamp-named file per run under `logs/`; mirrors complete color-coded logs only in verbose mode and emits concise CLI-style errors otherwise. |
| `paths` | Resolves files relative to `terminote.exe`, independent of the process working directory. |
| `brand` | Derives lowercase, title-case, and uppercase display names from the current executable filename and expands the corresponding localization placeholders. |
| `ascii_banner` | Contains the complete integrated `ascii-banner-c` renderer under its retained Apache-2.0 license. The root Makefile scans the left-aligned plain-text `A.inc` through `Z.inc` files under `resources/fonts/` and generates the font index under `build/generated/`; common blank edge columns are removed before adjacent glyphs are joined. |
| `banner` | Adapts the integrated ASCII-banner renderer's native unboxed rows to the welcome screen and right-pads every row to the maximum rendered width. |
| `ini_config` | Shared lightweight INI parser for preferences, key bindings, execution commands, localization, and syntax themes. It retains ordered entries, source line numbers, and literal values such as `#`, `;`, quotes, and backslashes. |
| `preferences` | Strictly validates `config/preferences.ini`, including the positive ready timeout and enum-named theme colors. |
| `localization` | Loads `resources/localization/<language>.ini` before CLI parsing, using the `language` abbreviation from `[general]`; provides localized CLI and editor UI strings. |
| `restart` | Detects saved project-local INI files, writes one recovery session directly under project-local `temp/`, launches a replacement process, and waits for its startup-ready handshake before allowing the old process to exit. It automatically discovers a failed `temp/session.txt` on a later successful startup and preserves recovery files until restoration succeeds. Only a process that successfully restored the session registers cleanup: Ctrl+Q uses `atexit()` and the console close button uses `CTRL_CLOSE_EVENT`; ordinary sessions do not manage `temp/`. Session entries distinguish new, saved, and modified tabs; restoration reads backup contents while retaining original document paths and dirty state, so Save targets the displayed original path. |
| `colors` | `include/colors.h` defines named ANSI color enums and global SGR strings indexed by `enum editorColorSlot`; renderers consume color slots instead of hardcoded theme colors. |
| `syntax` | Lazily locates and strictly loads only the `resources/languages/*.ini` theme matching the current filename extension. Unnamed, extensionless, and unknown-extension files remain unhighlighted. |
| `keybindings` | Before raw mode starts, loads and strictly validates `config/key-bindings.ini`, maps physical key combinations to semantic editor actions, and provides display labels for the status and help bars. |
| `console_window` | Console window sizing, minimum help dimensions, screen-buffer fitting, native scrollbar visibility, and `getWindowSize()`. |
| `row`      | Row lifecycle (insert, delete, free, update).`editorUpdateRow()` expands tabs to spaces into `row->render` and calls `editorUpdateSyntax()`. Character-level ops (`editorRowInsertChar`, `editorRowDelChar`, `editorRowAppendString`) push undo records. Cursor↔render position conversion (`editorRowCxToRx` / `editorRowRxToCx`) accounts for tab stops and multi-byte display width. `editorUpdateLineNumWidth()` recalculates the gutter width.                                                                                                                                                    |
| `edit`     | Character insertion/deletion:`editorInsertChar()` (handles empty-file case by inserting a row first), `editorInsertNewline()` (splits current row), `editorDelChar()` (handles backspace at row start by joining rows). Also owns configured-space Tab insertion, automatic indentation, bracket/quote pairing, and paired deletion as atomic undo operations.                                                                                                                                                                                                                                                                                                                                                                                |
| `cursor`   | Cursor movement and viewport scrolling.`editorMoveCursor()` is multi-byte aware via `editorPrevCharByte`/`editorNextCharByte`. `editorScroll()` keeps the cursor in view based on `E.rowoff`/`E.coloff`.                                                                                                                                                                                                                                                                                                                                                                                               |
| `select`   | Text selection and clipboard cut.`editorClearSelection()`, `editorStartSelection()`, `editorGetSelectedText()` (supports all 4 directions), `editorDeleteSelection()` (shared by cut and Backspace/Delete), `editorCutSelection()`.                                                                                                                                                                                                                                                                                                                                                                    |
| `keypress` | Semantic action dispatcher plus clipboard copy/paste. Physical keyboard combinations are resolved by `keybindings`; both configured paste and terminal-expanded paste use the same atomic paste transaction. |
| `mouse`    | Mouse state machine: wheel scrolling, scrollbar drag, four-direction selection, auto-scroll, and atomic selection drag-move. |
| `output`   | Append-buffer ownership and text-row rendering, including syntax colors, selection highlighting, line-number gutter, and horizontal scrollbar. |
| `screen`   | Status/event bars and full-frame refresh orchestration (`editorRefreshScreen()`). |
| `help`     | Full-screen help layout, minimum dimensions, hoverable return button, and help-specific rendering. |
| `file`     | `editorOpen()` reads a file line-by-line, stripping `\r` before `\n`, calling `editorInsertRow()` for each line. Sets `E.dirty = 0` after load. `editorSave()` serializes all rows to a buffer with `\r\n` line endings and writes via `fwrite`.                                                                                                                                                                                                                                                                                                                                                       |
| `undo`     | Undo-stack record creation, continuous-typing groups, shared transaction helpers, inverse-op mapping, and `editorUndo()`. Atomic insert/delete/replace/move records transfer intact between stacks. |
| `redo`     | `editorRedo()` execution for character, row, block, indentation, replacement, and atomic selection transactions. |
| `find`     | `editorPrompt()` provides a generic input line with cursor movement, GBK-aware backspace, history (static, file-local), and a callback invoked on every keypress. `editorFindCallback()` is the callback for incremental search — it collects all matches, highlights the current match by temporarily overwriting `row->hl` bytes with `HL_MATCH`, and restores the original highlight on the next keypress. `editorReplace()` does a two-pass replace: collect all matches, then iterate with per-match Y/N/A/arrows/Esc confirmation.                                                               |

### Character encoding

The editor uses **pure UTF-8** internally. `utf8_seq_len()` returns the byte length of a UTF-8 character (1–4). `is_utf8_cont()` checks for continuation bytes (0x80–0xBF). All cursor movement and deletion walks to character boundaries. The display width for multi-byte characters is 2 columns.

### Rendering pipeline

1. `editorRefreshScreen()` calls `editorScroll()` to compute `E.rowoff`/`E.coloff`
2. An `abuf` is built incrementally via `abAppend()`
3. `editorDrawRows()` writes ANSI escape sequences for syntax colors, selection inverse, and line numbers, then `\x1b[K\r\n` per line
4. `editorDrawStatusBar()` renders the inverted status line
5. `editorDrawMessageBar()` shows `E.statusmsg` if it's < 5 seconds old
6. A final `\x1b[y;xH` escape positions the cursor
7. The entire buffer is written in one `WriteConsoleA` call

### Key bindings

Keyboard bindings are loaded exclusively from `config/key-bindings.ini` at
startup. Source code dispatches semantic `ACTION_*` values and contains no
fallback key assignments. The loader rejects unknown actions or keys, duplicate
actions, conflicting combinations, missing keys, and invalid modifier order.
Saving the file in Terminote triggers the safe automatic restart flow.

### CLI

`terminote [options] [file]` supports `--config [name]`, `--help`, `--verbose`,
and `--version`. `--config` names omit the `.ini` suffix. Preferences are loaded
from `config/preferences.ini`; every normal run writes a timestamped log to
`logs/`. General preferences include the localization language abbreviation,
the ready timeout, strict `true`/`false`
switches for automatic indentation and pairing, and a positive `tab_width`
that controls both inserted indentation spaces and rendering of existing tabs.
Language packs live in module-sectioned `resources/localization/<abbreviation>.ini` files;
`zh`, `en`, `ja`, `ko`, `es`, `fr`, `ru`, `pt`, and `it` are provided. Every pack must define the same
complete set of CLI, editor, status/event-bar, log, and diagnostic strings.
Saving `preferences.ini` in Terminote triggers the safe automatic restart flow.

Language keyword themes live in `resources/languages/<language>.ini`. The configuration
picker exposes them under `lang`, and direct selection uses names such as
`--config lang=c` without the `.ini` suffix. Each file defines general syntax
colors under `[colors]` and any number of semantic keyword groups under
`[keywords.<category>]`; every keyword group requires a named ANSI `color` and
a comma-separated `words` list. Keywords must not be duplicated across groups
within one language. Languages may optionally define matching `section_start`,
`section_end`, and `section` color settings for section-header highlighting.
Header files belong in `include/`; `src/` contains
implementation files only.

### Windows-specific dependencies

- `windows.h`, `conio.h` — Console API, clipboard, `GetStdHandle`, `SetConsoleMode`, `ReadConsoleInputA`, `WriteConsoleA`
- Links `-lgdi32` (required for `GetStdHandle` and console functions)
- Console code page set to 65001 (UTF-8) via `SetConsoleCP`/`SetConsoleOutputCP`
- Clipboard operations use `OpenClipboard`/`GetClipboardData`/`SetClipboardData` (Windows API, CF_TEXT format)
