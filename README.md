# TinyShell

## Version

Current version: **2.0.0**

### What's New in 2.0.0

The core no longer allocates to register or run a command. 1.x was fine on an
ESP32 with a roomy heap but fragmented it at boot and allocated on every
command -- the wrong shape for a smaller microcontroller.

| | 1.x | 2.0.0 |
| --- | --- | --- |
| registering a command | grew the table by one and deep-copied **every** command already there (O(n²) allocations) | fixed-capacity table, nothing copied |
| names / descriptions | copied into `std::string`s | pointers to your string literals |
| a command's callable | `std::function` + a heap-allocated wrapper | inline slot (`TINYSHELL_CALLABLE_STORAGE`); only an oversized capture costs one allocation, once |
| running a line | several `std::string` copies, `new void*[]`, one `new T` per argument | parsed in place in a stack buffer, arguments converted straight into a stack tuple |
| bad input | exceptions as control flow | error codes; builds with `-fno-exceptions` |
| output | the whole response built as one `std::string` (a big help dump could fail to allocate -- the 1.4.1 crash) | a fixed stack buffer flushed to the callback in whole-line chunks |

What stays the same: `begin()`, `add(func, name, description, module)`,
`create_module()`, `run_command_line()`, `complete_line()`,
`set_output_callback()`, the `module -command a, b, c` syntax (`\,` for a
literal comma), the result codes and every message text.

What to know when upgrading:

- **Capacities are compile-time** (`-D` build flags): `TINYSHELL_MAX_MODULES`
  (32), `TINYSHELL_MAX_COMMANDS` (160, total), `TINYSHELL_MAX_ARGS` (8),
  `TINYSHELL_MAX_LINE` (512 -- also the per-call stack cost),
  `TINYSHELL_OUTPUT_CAPACITY` (256), `TINYSHELL_CALLABLE_STORAGE`
  (4 pointers), `TINYSHELL_NAME_ARENA` (256). A registration past a limit
  returns `TABLE_FULL`, is counted by `dropped_registrations()` and is
  reported by `help -modules`.
- **Names are not copied**: pass string literals (anything that outlives the
  shell). The `std::string` overloads of `add()` / `create_module()` copy
  into the fixed name arena instead.
- **Zero-copy parameters**: a command may take `std::string_view` or
  `const char*`, pointing straight into the parsed line (valid while the
  command runs). `std::string` still works and allocates only past its
  small-buffer size. Integer parameters are matched by size and sign, so
  `int`, `long` and `int32_t` (a `long` on ESP-IDF 5) all work.
- **Output callback**: one taking `std::string_view` gets the buffer as-is;
  the 1.x `const std::string&` shape still works at one `std::string` per
  call. A long response now arrives as several calls, each ending on a line
  boundary.
- **Custom parameter types**: specialize `tinyshell::ArgType<T>` (see
  `TableLinker/UserTypeConverters.h`); the 1.x `convert_type_custom()` hooks
  are gone.
- `using namespace std;` is still injected by `TinyShell.h` for
  compatibility, but deprecated: define `TINYSHELL_NO_USING_NAMESPACE_STD`.
- `TinyShell` is no longer copyable, and a second command with the same name in
  one module is refused (`RESULT_ERROR`) instead of silently shadowed.
- Test: `test/test_tinyshell.cpp` counts allocations through a global
  `operator new` to check the promise above --
  `g++ -std=gnu++17 -I. TinyShell.cpp TableLinker/TableLinker.cpp ShellStyle.cpp test/test_tinyshell.cpp -o ts && ./ts`
  (also with `-DTINYSHELL_COLOR`, `-DVERBOSE_OFF`, `-fno-exceptions`).

### What's New in 1.4.1

`run_command_line()` now catches exceptions thrown while *validating* a
command, not just while *calling* it. A bare module name (no `-function`)
falls through to `get_help()`, which builds a help listing by concatenating
one `std::string` per registered command in that module — on a module with
many commands, if the host is low on (fragmented) heap, that allocation can
throw `std::bad_alloc`. Previously that exception was uncaught and unwound
past `run_command_line()`, aborting the process; it is now caught the same
way a failed `call()` already was, and surfaces as a normal `RESULT_ERROR`
response instead of a crash.

### What's New in 1.4.0

`ShellLineEditor` now colours the live command echo when `TINYSHELL_COLOR` is
enabled: the module (first token) is dark yellow, the function (second token,
including its leading `-`) is dark blue, and arguments are black. Full redraws
from history, Tab completion, cursor movement, mid-line edits and horizontal
scrolling retain the token colours. Every emitted batch ends with an SGR reset
so input colour cannot leak into command output. Builds without the flag remain
byte-for-byte unchanged.

### What's New in 1.3.0

`ShellStyle` refinements for embedded use:

- **`shell_classify_line()` is now allocation-free.** It takes a
  `std::string_view` and scans in place (was 3 `std::string` copies: strip +
  trim + lowercase). Safe on the hot path.
- **`shell_colorize_block()` short-circuits.** Returns the input untouched when
  colour is disabled or the block exceeds `SHELL_MAX_COLORIZE_INPUT` (8 KiB) —
  no per-line work in a no-colour build.
- **Pluggable classification.** `shell_colorize_block()` takes an optional
  `ShellLineClassifier` (plain function pointer). The built-in keyword heuristic
  is just the default — a project keys colours off whatever it wants.
- **`shell_sgr` / `shell_sgr_reset` are `constexpr`**; new `shell_color_enabled()`.
- **Conservative negation guard**: a line that *denies* the thing
  ("sem falhas", "nenhum erro", "no error") is no longer classified `Error` off
  a stray keyword. Structured prefixes / ESP-NOW tags still win outright.
- **API:** `shell_classify_line` / `shell_strip_sgr` / `shell_colorize_block`
  now take `std::string_view` (source-compatible — `std::string` and
  `const char*` convert implicitly).
- **Removed** `ShellMsgClass::Prompt` / `Input` (were reserved, unused). The
  1.4.0 editor work later adds the specific `InputModule`, `InputFunction` and
  `InputArgument` classes actually needed by live echo.

### What's New in 1.2.0

- Added **`ShellStyle`** (`ShellStyle.h` / `.cpp`) — a framework-free semantic
  classifier + ANSI SGR palette for shell output. The shell side decides what a
  line *means* (success / warning / error / help / muted log) and wraps it in
  standard ANSI colour; a plain serial monitor shows it like any terminal, and a
  theme-aware viewer (e.g. TraceView) remaps the basic colours to its own
  palette. Gated by the compile flag **`TINYSHELL_COLOR`** (default off → not a
  single extra byte on the wire, output identical to 1.1.0).
  `TinyShell`'s own framework messages (validation errors, run summaries, help
  dumps) route through it automatically. See
  [Output colouring](#output-colouring-shellstyle).
- Smoke test: `test/test_shell_style.cpp` — compile it **both** ways:
  `g++ -std=gnu++17 -I. ShellStyle.cpp test/test_shell_style.cpp -o ss && ./ss`
  and again with `-DTINYSHELL_COLOR`.

### What's New in 1.1.0

- Added **`ShellLineEditor`** (`ShellLineEditor.h` / `.cpp`) — a framework-free,
  server-side line editor that gives a remote "dumb" terminal the editing a local
  one gets from its TTY driver (echo, backspace, cursor movement, history, Ctrl+R,
  Tab completion, horizontal scroll). It reads bytes and writes bytes, so it drops
  onto any transport. See [Interactive line editing](#interactive-line-editing-shelllineeditor).
- Smoke test: `test/test_shell_line_editor.cpp`
  (`g++ -std=gnu++17 -I. ShellLineEditor.cpp test/test_shell_line_editor.cpp && ./a.out`).

### What's New in 0.2.0+autocomplete

- Added completion API: `TinyShell::complete_line(partial_command, max_results)`.
- Added module/command enumeration helpers in `TableLinker` used by completion logic.
- Improved shell integration support for command auto-complete flows.


### How the Library Works

This library is a tool for **executing functions via text commands**. It uses a **table** to store **function pointers**, each associated with a **name** and a **description**.

When you pass a **formatted string** to the `run_command_line` function, the library **searches for the function name** in this list and, if found, **executes it**.

---

## Interactive line editing (`ShellLineEditor`)

`run_command_line` wants one complete line. `ShellLineEditor` is the piece that
turns a stream of raw keystrokes into those lines, and gives the person typing
them the editing they expect from a real terminal — **without the terminal
having to do any of it**.

### The model

The far end stays a *dumb* terminal: it sends the raw bytes you press and
prints the raw bytes it receives. Everything else happens here, on the host
side:

| The user does | `ShellLineEditor` emits |
| --- | --- |
| types a printable char | echoes it (with token colour when enabled) |
| Backspace (`0x08` or `0x7f`) | `"\b \b"`, or a full line redraw |
| `←` / `→` (`ESC [ D` / `ESC [ C`) | moves the cursor in place |
| `↑` / `↓` (`ESC [ A` / `ESC [ B`) | recalls a circular command history |
| `Ctrl+R` (`0x12`) | reverse-searches history by prefix |
| `Tab` | cycles completions from a provider you supply |
| a line wider than the render width | scrolls it horizontally (`<`…`>`) |
| `Enter` (`CR` or `LF`) | ends the line and hands it back |

For editing, the only control bytes it writes are `\r`, `\b` and
overwrite-at-cursor. A colour-enabled build additionally emits the small ANSI
SGR subset documented below; it still emits no cursor-addressing CSI, so the
far terminal needs to understand almost nothing.

It is **not** an I/O object: it has no `Stream`, does not block, and does no
allocation you cannot see. Input arrives through `feed()`; everything it would
echo is appended to a `std::string` **you own**; you decide how those bytes
reach the wire. That is what makes it portable — a UART, a TCP socket, a
framed radio or USB channel all work the same way. Only `<string>`,
`<vector>` and `<functional>`; it has no dependency on the `TinyShell` class
itself.

### Using it

```cpp
#include "ShellLineEditor.h"
#include "TinyShell.h"

TinyShell shell;
ShellLineEditor editor;
std::string out;                       // the editor's outbound byte buffer

// Tab completion, backed by the shell's own catalog (optional).
editor.setCompletionProvider(
    [&](const std::string& in, std::string* sug, std::size_t max) -> std::size_t {
        const auto matches = shell.complete_line(in, max);
        for (std::size_t i = 0; i < matches.size() && i < max; ++i) sug[i] = matches[i];
        return matches.size();
    });

editor.setPrompt("$ ", out);           // paints the first prompt into `out`
transport.write(out); out.clear();

// ... per received chunk of terminal input ...
editor.feed(rxBytes, rxLen);           // just buffers the bytes
std::string line;
while (editor.poll(out, &line)) {       // drains them; `out` gets echo + redraws
    const uint8_t status = shell.run_command_line(line);
    editor.writeResponse(commandOutput, out);   // command output + a fresh prompt
    (void)status;
}
transport.write(out); out.clear();      // flush whatever `out` accumulated
```

A few notes:

* `poll()` returns `true` once per completed line; call it in a loop to drain
  several lines from one `feed()` (a paste).
* `feed()` / `poll()` are the only input path — call `poll()` promptly after
  `feed()`; unprocessed backlog past `MAX_PENDING_INPUT` (4096 B) is dropped.
* `setPrompt(text)` (no `out`) just stores the text; `setPrompt(text, out)`
  also repaints. `refreshLine(out)` repaints on demand (e.g. after printing an
  async log line).
* `addHistory(line)` seeds the up-arrow history without echoing — useful for
  replaying a persisted history at start-up.
* Command lines and the response are trimmed; `writeResponse` normalises lone
  `\n` to `\r\n`.
* With `TINYSHELL_COLOR`, the first command token (module) uses ANSI 33 (dark
  yellow), the second (function) uses ANSI 34 (dark blue), and all remaining
  tokens (arguments) use ANSI 30 (black). Each output batch ends in ANSI reset.
* One instance is **not thread-safe** — drive it from a single thread/task.

### Constants (compile-time, in `ShellLineEditor.h`)

| | default | meaning |
| --- | --- | --- |
| ctor `historyCapacity` | `64` | history depth (clamped to `128`) |
| `MAX_INPUT_LENGTH` | `512` | longest editable line, in bytes |
| `MAX_RENDER_COLUMNS` | `72` | render width; longer lines scroll |
| `MAX_TAB_SUGGESTIONS` | `24` | completions offered per Tab cycle |
| `MAX_PENDING_INPUT` | `4096` | un-polled `feed()` backlog cap |

---

## Output colouring (`ShellStyle`)

`ShellStyle` turns an output line into a **semantic class** and, when the
compile flag `TINYSHELL_COLOR` is defined, wraps it in a standard ANSI SGR
sequence. Framework-free (standard string headers only), no dependency on
`TinyShell` or `ShellLineEditor`.

### Classes and colours

| `ShellMsgClass` | SGR (flag on) | matches |
| --- | --- | --- |
| `Output` | *(none)* | anything not matched below |
| `Success` | `\e[32m` green | `sucesso`/`success`, `enviado`, `ligado`, `pong`, … |
| `Warning` | `\e[33m` yellow | `aviso(...)`, `warning`, `deprecated`, … |
| `Error` | `\e[31m` red | `erro(...)`, `falha`, `not found`, `invalid`, … |
| `Help` | `\e[2m` faint | `[help] …`, `get_help()` dumps |
| `Muted` | `\e[90m` grey | ESP-NOW `[info]`/`[debug]`/`[telemetry]`/`[cmd]` lines |
| `InputModule` | `\e[33m` dark yellow | first live command token |
| `InputFunction` | `\e[34m` dark blue | second live command token |
| `InputArgument` | `\e[30m` black | remaining live command tokens |

The built-in `shell_classify_line` is bilingual (pt/en). Structured prefixes
(`erro(`, `aviso(`, `[help]`) and ESP-NOW tags (`[error][…]`) are confident; the
looser keyword scan is skipped when the line *denies* the thing ("sem falhas",
"nenhum erro"). It is just the **default** — pass your own classifier to
`shell_colorize_block`.

### API

```cpp
#include "ShellStyle.h"

using ShellLineClassifier = ShellMsgClass (*)(std::string_view);

constexpr bool  shell_color_enabled();                        // == TINYSHELL_COLOR
constexpr const char* shell_sgr(ShellMsgClass);               // "\x1b[..m"  ("" for Output / colour off)
constexpr const char* shell_sgr_reset();                      // "\x1b[0m"   ("" when colour off)

ShellMsgClass shell_classify_line(std::string_view line) noexcept;   // zero-alloc, in-place scan
std::string   shell_strip_sgr(std::string_view text);               // drop every CSI escape (always available)
std::string   shell_colorize_block(std::string_view text,           // split lines, classify, wrap, rejoin
                                   ShellMsgClass fallback = ShellMsgClass::Output,
                                   ShellLineClassifier classifier = &shell_classify_line);
```

`shell_colorize_block` keeps the exact line terminators, skips any line that
already contains an ESC byte, and returns `text` unchanged when colour is
disabled or `text` exceeds `SHELL_MAX_COLORIZE_INPUT` (8 KiB).

### Integration

`TinyShell` routes its own framework output through `ShellStyle` already:
`format_shell_response()` calls `shell_colorize_block(text, code == RESULT_OK ?
Output : Error)`, and the `help -modules` / `help -types` dumps go through it
with the `Help` fallback. Nothing else is required to get coloured framework
messages — just build with `-DTINYSHELL_COLOR`.

A host program that captures command output itself (a firmware that feeds the
result to its own transport) can call `shell_classify_line()` per line to reuse
the exact same classification for its transport and its display — it is
`noexcept` and allocation-free, so calling it on every LCD refresh is fine.

---

### Technical Details

* **Return Value:** Functions are expected to return a  `uint8_t` (byte) to represent error codes.
    ```cpp
    #define RESULT_OK  0
    #define RESULT_ERROR 255
    #define FUNCTION_NOT_FOUND 254
    #define MODULE_NOT_FOUND 253
    ```
* **Wrapper:** If you need to use a function that returns something different, you can create a wrapper function.
    ```cpp
    uint8_t wrapper_h() {
        Serial.println(ts.get_help("").c_str());
        return RESULT_OK;  // return 0 to indicate success
    }
    ```
* **Command Format:** The string expected by the `run_command_line` function follows this format:

    ```
    module -function arg0, arg1, arg2, ..., argN
    ```
* **Escaped Comma in Arguments:** Use `\,` when you need a literal comma inside one argument.

    ```
    modulo -function arg1, hello\, john, arg3
    ```

    In this case, the payload argument is parsed as `hello, john` (single argument).
* **Argument Types:** Argument types are represented by short codes. Core built-in support currently includes:

    ```
    i1 u1 i2 u2 i4 u4 i8 u8 b1 f4 f8 c1 s0
    ```

    Notes:
    - integer and floating-point conversions now validate format and range
    - boolean conversion accepts: `1/0`, `true/false`, `on/off`, `yes/no`
    - char conversion accepts single char, quoted char (`'a'`) and escaped chars (`'\\n'`, `'\\t'`, ...)

* **Code Organization:**
    - core built-in conversion logic lives in `TableLinker/TypeConversion.h`
    - custom user conversion hooks live in `TableLinker/UserTypeConverters.h`

* **Custom Types:** implement your own converters in `TableLinker/UserTypeConverters.h`.

    ```cpp
    namespace TinyShellUserConverters {
      inline bool convert_type_custom(const char* data,
                                      const char* type_code,
                                      void*& out_ptr,
                                      std::string& error_msg);

      inline bool destroy_type_custom(void* ptr,
                                      const char* type_code);
    }
    ```

    - return `false` when your converter does not handle that `type_code`
    - when handled, allocate/deallocate consistently with your parameter type
* **Output Modes (compile-time):**

    Default (no macro): human-readable messages.

    ```c++
    // no define needed
    ```

    `VERBOSE_OFF`: returns only numeric status codes (no extra text).

    ```c++
    #define VERBOSE_OFF
    ```

    `HARD_VERBOSE_OFF`: returns no text at all (not even errors).

    ```c++
    #define HARD_VERBOSE_OFF
    ```

    If both are defined, `HARD_VERBOSE` has priority.
