#ifndef SHELL_STYLE_H
#define SHELL_STYLE_H

#include <cstdint>
#include <string>

/**
 * @brief Semantic classification + ANSI SGR coloring for shell output lines.
 *
 * The far terminal a TinyShell talks to is usually "dumb" (see ShellLineEditor)
 * -- it prints whatever bytes it receives. This module lets the *shell side*
 * decide what each output line means (a success confirmation, an error, a help
 * dump, an async log line) and wrap it in standard ANSI SGR so a colour-aware
 * viewer can render it. A plain serial monitor shows the same colours a
 * terminal emulator would; a viewer that themes ANSI (TraceView) remaps the
 * basic colours to its own palette tokens instead of literal RGB.
 *
 * Compile-time gate: define `TINYSHELL_COLOR` to emit real escape sequences.
 * Without it every `shell_sgr*` returns "" and `shell_colorize_block()` returns
 * its input byte-for-byte -- the classifier still runs and is still testable,
 * but nothing is added to the stream.
 *
 * Framework-free: only <string>/<cstdint>, no dependency on TinyShell or
 * ShellLineEditor, so it links and unit-tests on a host.
 */

enum class ShellMsgClass : std::uint8_t {
    Output = 0,  ///< normal command output -- no colour (default pen)
    Success,     ///< confirmations ("... executado com sucesso", "enviado")
    Warning,     ///< non-fatal warnings ("aviso(...)", "warning")
    Error,       ///< failures ("erro(...)", "falha", "not found", "invalid")
    Help,        ///< help / usage listings (`get_help()`, "[help] ...")
    Muted,       ///< low-priority: ESP-NOW [info]/[debug]/[telemetry] log lines
    Prompt,      ///< the prompt string -- reserved for ShellLineEditor (Layer 2)
    Input,       ///< echoed user input -- reserved for ShellLineEditor (Layer 2)
};

/**
 * @brief SGR introducer for a message class, e.g. "\x1b[31m" for Error.
 * Returns "" for `Output` and whenever `TINYSHELL_COLOR` is not defined.
 * The returned pointer is a static string literal; do not free it.
 */
const char* shell_sgr(ShellMsgClass cls);

/**
 * @brief SGR reset ("\x1b[0m"), or "" when `TINYSHELL_COLOR` is not defined.
 */
const char* shell_sgr_reset();

/**
 * @brief Classify a single output line by prefix and keyword (bilingual
 * pt/en, parity with the dongle's existing `lcdColorForLine`).
 *
 * Leading/trailing whitespace and any existing SGR sequences are ignored, so
 * the call is idempotent on already-coloured text. Never returns
 * `Prompt`/`Input`. An empty line is `Output`.
 */
ShellMsgClass shell_classify_line(const std::string& line);

/**
 * @brief Split `text` into lines, classify each, wrap each in SGR, rejoin --
 * preserving the exact original line terminators (lone `\n`, `\r\n`, or none
 * on the last line).
 *
 * @param fallback Class applied to lines that classify as `Output` but carry
 *   real content -- pass `Error` when the whole block is known to be a failure
 *   (a non-zero shell status), `Help` for a help dump, `Output` (default) to
 *   leave undecided lines uncoloured.
 *
 * A line that already contains an ESC byte is passed through untouched. When
 * `TINYSHELL_COLOR` is not defined the result equals `text` byte-for-byte.
 */
std::string shell_colorize_block(const std::string& text,
                                 ShellMsgClass fallback = ShellMsgClass::Output);

#endif  // SHELL_STYLE_H
