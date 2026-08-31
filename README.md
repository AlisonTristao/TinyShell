# TinyShell

## Version

Current version: **1.2.0**

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
| types a printable char | echoes it |
| Backspace (`0x08` or `0x7f`) | `"\b \b"`, or a full line redraw |
| `←` / `→` (`ESC [ D` / `ESC [ C`) | moves the cursor in place |
| `↑` / `↓` (`ESC [ A` / `ESC [ B`) | recalls a circular command history |
| `Ctrl+R` (`0x12`) | reverse-searches history by prefix |
| `Tab` | cycles completions from a provider you supply |
| a line wider than the render width | scrolls it horizontally (`<`…`>`) |
| `Enter` (`CR` or `LF`) | ends the line and hands it back |

The only control bytes it ever writes are `\r`, `\b` and overwrite-at-cursor —
no full ANSI/CSI vocabulary — so the far terminal needs to understand almost
nothing.

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
sequence. It is framework-free (only `<string>`/`<cstdint>`) and has no
dependency on `TinyShell` or `ShellLineEditor`.

### Classes and colours

| `ShellMsgClass` | SGR (flag on) | matches |
| --- | --- | --- |
| `Output` | *(none)* | anything not matched below |
| `Success` | `\e[32m` green | `sucesso`/`success`, `enviado`, `ligado`, `pong`, … |
| `Warning` | `\e[33m` yellow | `aviso(...)`, `warning`, `deprecated`, … |
| `Error` | `\e[31m` red | `erro(...)`, `falha`, `not found`, `invalid`, … |
| `Help` | `\e[2m` faint | `[help] …`, `get_help()` dumps |
| `Muted` | `\e[90m` grey | ESP-NOW `[info]`/`[debug]`/`[telemetry]`/`[cmd]` lines |
| `Prompt` / `Input` | `\e[36m` / `\e[1m` | reserved for a future `ShellLineEditor` mode |

Keyword matching is bilingual (pt/en) and deliberately conservative — same set
the calling firmware already uses to colour its LCD.

### API

```cpp
#include "ShellStyle.h"

ShellMsgClass shell_classify_line(const std::string& line);   // prefix + keyword, idempotent on coloured text
const char*   shell_sgr(ShellMsgClass);                       // "\x1b[..m"  ("" for Output / flag off)
const char*   shell_sgr_reset();                              // "\x1b[0m"   ("" when flag off)
std::string   shell_colorize_block(const std::string& text,   // split lines, classify, wrap, rejoin
                                   ShellMsgClass fallback = ShellMsgClass::Output);
```

`shell_colorize_block` keeps the exact line terminators of the input, skips any
line that already contains an ESC byte, and — with `TINYSHELL_COLOR` **not**
defined — returns `text` byte-for-byte.

### Integration

`TinyShell` routes its own framework output through `ShellStyle` already:
`format_shell_response()` calls `shell_colorize_block(text, code == RESULT_OK ?
Output : Error)`, and the `help -modules` / `help -types` dumps go through it
with the `Help` fallback. Nothing else is required to get coloured framework
messages — just build with `-DTINYSHELL_COLOR`.

A host program that captures command output itself (a firmware that feeds the
result to its own transport) can call `shell_classify_line()` per line to reuse
the exact same classification for its transport and its display.

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
