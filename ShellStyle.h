#ifndef SHELL_STYLE_H
#define SHELL_STYLE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

/**
 * @brief Semantic classification + ANSI SGR colouring for shell output lines.
 *
 * The far terminal a TinyShell talks to is usually "dumb" (see ShellLineEditor)
 * -- it prints whatever bytes it receives. This module lets the *shell side*
 * decide what each output line means (a success confirmation, an error, a help
 * dump, an async log line) and wrap it in standard ANSI SGR so a colour-aware
 * viewer can render it. A plain serial monitor shows the same colours a
 * terminal emulator would; a viewer that themes ANSI (TraceView) remaps the
 * basic colours to its own palette tokens instead of literal RGB.
 *
 * Compile-time gate: define `TINYSHELL_COLOR` (a build-wide flag, never
 * per-file) to emit real escape sequences. Without it `shell_sgr*` return "",
 * `shell_colorize_block()` returns its input unchanged, and the escape-string
 * tables are dead-code-eliminated. `shell_classify_line()` still runs (it is
 * also useful on its own, e.g. to colour an LCD) and allocates nothing.
 *
 * Framework-free: only the standard string headers, no dependency on TinyShell
 * or ShellLineEditor, so it links and unit-tests on a host.
 */

enum class ShellMsgClass : std::uint8_t {
    Output = 0,  ///< normal command output -- no colour (default pen)
    Success,     ///< confirmations ("... executado com sucesso", "enviado")
    Warning,     ///< non-fatal warnings ("aviso(...)", "warning")
    Error,       ///< failures ("erro(...)", "falha", "not found", "invalid")
    Help,        ///< help / usage listings (`get_help()`, "[help] ...")
    Muted,       ///< low-priority: ESP-NOW [info]/[debug]/[telemetry] log lines
    // Prompt / Input are intentionally absent: colouring the live prompt and the
    // input echo is "Layer 2" work that needs ShellLineEditor to track printable
    // width. The version that does it re-introduces the values here.
};

/// A line -> its semantic class. Same shape as `shell_classify_line`, so that
/// is the drop-in default; a plain function pointer keeps it allocation-free.
using ShellLineClassifier = ShellMsgClass (*)(std::string_view line);

/// True iff built with `TINYSHELL_COLOR`. `constexpr` so every caller folds the
/// branch and the colour paths vanish from a no-colour build.
constexpr bool shell_color_enabled() noexcept {
#ifdef TINYSHELL_COLOR
    return true;
#else
    return false;
#endif
}

/// SGR introducer for a class ("\x1b[31m" for Error, ...). "" for `Output` and
/// whenever colour is disabled. The pointer is a static string literal.
constexpr const char* shell_sgr(ShellMsgClass cls) noexcept {
    if (!shell_color_enabled()) {
        return "";
    }
    switch (cls) {
        case ShellMsgClass::Success: return "\x1b[32m";  // green
        case ShellMsgClass::Warning: return "\x1b[33m";  // yellow
        case ShellMsgClass::Error:   return "\x1b[31m";  // red
        case ShellMsgClass::Help:    return "\x1b[2m";   // faint
        case ShellMsgClass::Muted:   return "\x1b[90m";  // bright black / grey
        case ShellMsgClass::Output:
        default:                     return "";
    }
}

/// SGR reset ("\x1b[0m"), or "" when colour is disabled.
constexpr const char* shell_sgr_reset() noexcept {
    return shell_color_enabled() ? "\x1b[0m" : "";
}

/// Longest block `shell_colorize_block()` will process; a larger input is
/// returned unchanged -- a dump that big is not shell output worth colouring,
/// and the sibling ShellLineEditor caps everything it touches too.
inline constexpr std::size_t SHELL_MAX_COLORIZE_INPUT = 8192;

/**
 * @brief Classify one line by prefix + bilingual (pt/en) keyword.
 *
 * Zero allocation: scans the view in place, skipping leading whitespace and CSI
 * escapes, so it is safe to call on the hot path (an LCD refresh, every shell
 * response line). Structured prefixes (`erro(`, `aviso(`, `[help]`) and ESP-NOW
 * tags (`[error][...]`) are confident; the looser keyword scan is suppressed
 * when the line is denying or counting the thing ("0 erros", "sem falhas").
 * An empty or blank line is `Output`.
 *
 * This is the *default* policy. Pass your own `ShellLineClassifier` to
 * `shell_colorize_block()` to key colours off something else entirely.
 */
ShellMsgClass shell_classify_line(std::string_view line) noexcept;

/**
 * @brief Remove every CSI escape (ESC '[' ... final byte) and any lone ESC.
 * Always available. Returns a fresh std::string.
 */
std::string shell_strip_sgr(std::string_view text);

/**
 * @brief Split `text` into lines, classify each (via `classifier`), wrap each
 * in SGR, rejoin -- preserving the exact original line terminators.
 *
 * A line that already contains an ESC is passed through untouched. Returns
 * `text` unchanged when colour is disabled or `text` is longer than
 * `SHELL_MAX_COLORIZE_INPUT`.
 *
 * @param fallback Class applied to lines that classify as `Output` but carry
 *   real content -- pass `Error` when the whole block is a known failure,
 *   `Help` for a help dump, `Output` (default) to leave undecided lines plain.
 * @param classifier Line -> class. Defaults to `shell_classify_line`; a null
 *   pointer is treated as the default.
 */
std::string shell_colorize_block(std::string_view text,
                                 ShellMsgClass fallback = ShellMsgClass::Output,
                                 ShellLineClassifier classifier = &shell_classify_line);

#endif  // SHELL_STYLE_H
