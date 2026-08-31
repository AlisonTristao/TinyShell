#include "ShellStyle.h"

namespace {

// ---------------------------------------------------------------------------
// Case-insensitive, allocation-free string ops over std::string_view.
// ---------------------------------------------------------------------------

constexpr char lower_ascii(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool ci_equal(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (lower_ascii(a[i]) != lower_ascii(b[i])) {
            return false;
        }
    }
    return true;
}

bool ci_starts_with(std::string_view s, std::string_view prefix) noexcept {
    return s.size() >= prefix.size() && ci_equal(s.substr(0, prefix.size()), prefix);
}

bool ci_contains(std::string_view hay, std::string_view needle) noexcept {
    if (needle.empty() || needle.size() > hay.size()) {
        return false;
    }
    const std::size_t last = hay.size() - needle.size();
    for (std::size_t i = 0; i <= last; ++i) {
        std::size_t j = 0;
        for (; j < needle.size(); ++j) {
            if (lower_ascii(hay[i + j]) != lower_ascii(needle[j])) {
                break;
            }
        }
        if (j == needle.size()) {
            return true;
        }
    }
    return false;
}

template <std::size_t N>
bool ci_contains_any(std::string_view hay, const std::string_view (&needles)[N]) noexcept {
    for (const std::string_view needle : needles) {
        if (ci_contains(hay, needle)) {
            return true;
        }
    }
    return false;
}

constexpr bool is_ws(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

// Advance past leading whitespace and CSI escape runs (ESC '[' ... 0x40..0x7E)
// plus any lone ESC, then trim a trailing whitespace run. The returned view is
// what a human reads on that line -- no copy.
std::string_view logical_view(std::string_view s) noexcept {
    std::size_t i = 0;
    for (;;) {
        while (i < s.size() && is_ws(s[i])) {
            ++i;
        }
        if (i >= s.size() || s[i] != '\x1b') {
            break;
        }
        std::size_t j = i + 1;
        if (j < s.size() && s[j] == '[') {
            ++j;
            while (j < s.size() && (static_cast<unsigned char>(s[j]) < 0x40 ||
                                    static_cast<unsigned char>(s[j]) > 0x7E)) {
                ++j;  // parameter / intermediate bytes
            }
            if (j < s.size()) {
                ++j;  // final byte
            }
        }
        i = j;  // lone ESC (j == i+1) or the CSI just skipped
    }

    std::size_t end = s.size();
    while (end > i && is_ws(s[end - 1])) {
        --end;
    }
    return s.substr(i, end - i);
}

// ESP-NOW structured line: "[<token>][<...>]..." with a known type token
// (ShellOutput::isEspNowTypeToken on the dongle). These pass through the
// dongle's renderResponse() verbatim, so recognise them here to colour them.
bool espnow_structured_class(std::string_view s, ShellMsgClass& outClass) noexcept {
    if (s.size() < 4 || s.front() != '[') {
        return false;
    }
    const std::size_t close = s.find(']');
    if (close == std::string_view::npos || close < 2) {
        return false;
    }
    if (close + 1 >= s.size() || s[close + 1] != '[') {
        return false;
    }

    const std::string_view token = s.substr(1, close - 1);
    static constexpr std::string_view kError[] = {"error", "erro"};
    static constexpr std::string_view kMuted[] = {"info", "cmd",   "cmdo", "telemetry", "tele",
                                                  "debug", "debg", "pakg", "none"};
    for (const std::string_view t : kError) {
        if (ci_equal(token, t)) {
            outClass = ShellMsgClass::Error;
            return true;
        }
    }
    for (const std::string_view t : kMuted) {
        if (ci_equal(token, t)) {
            outClass = ShellMsgClass::Muted;
            return true;
        }
    }
    return false;
}

// The loose keyword scan is fuzzy; suppress it when the line clearly *denies*
// the thing ("sem falhas", "nenhum erro", "no error"). Kept deliberately tight
// -- unambiguous phrases only, no digit matching ("0 erro" would also catch
// "10 erros"). Structured prefixes / ESP-NOW tags bypass this entirely.
bool line_denies(std::string_view s) noexcept {
    static constexpr std::string_view kNegations[] = {
        "nenhum", "nenhuma", "sem erro", "sem falha", "sem problema",
        "no error", "no failure",
    };
    return ci_contains_any(s, kNegations);
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

ShellMsgClass shell_classify_line(std::string_view rawLine) noexcept {
    const std::string_view line = logical_view(rawLine);
    if (line.empty()) {
        return ShellMsgClass::Output;
    }

    ShellMsgClass espnowClass;
    if (espnow_structured_class(line, espnowClass)) {
        return espnowClass;
    }

    // Structured prefixes emitted by the dongle's ShellCommandSupport --
    // unambiguous, so they win over (and bypass the guard on) the keyword scan.
    if (ci_starts_with(line, "erro(") || ci_starts_with(line, "erro (")) {
        return ShellMsgClass::Error;
    }
    if (ci_starts_with(line, "aviso(") || ci_starts_with(line, "aviso (")) {
        return ShellMsgClass::Warning;
    }
    if (ci_starts_with(line, "[help]")) {
        return ShellMsgClass::Help;
    }

    if (!line_denies(line)) {
        static constexpr std::string_view kError[] = {
            "erro",  "error",         "falha", "failed", "invalid",
            "invalido", "not found",  "nao encontrad", "negad", "denied"};
        static constexpr std::string_view kWarning[] = {"aviso", "warning", "warn", "atencao",
                                                        "deprecat"};
        static constexpr std::string_view kSuccess[] = {
            "sucesso", "success",  "inicializado", "atualizado", "enviado", "adicionado",
            "removido", "ligado",  "desligado",    "limpo",      "salvo",   "conectado",
            "pong"};
        if (ci_contains_any(line, kError)) {
            return ShellMsgClass::Error;
        }
        if (ci_contains_any(line, kWarning)) {
            return ShellMsgClass::Warning;
        }
        if (ci_contains_any(line, kSuccess)) {
            return ShellMsgClass::Success;
        }
    }

    return ShellMsgClass::Output;
}

std::string shell_strip_sgr(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\x1b') {
            if (i + 1 < s.size() && s[i + 1] == '[') {
                std::size_t j = i + 2;
                while (j < s.size() && (static_cast<unsigned char>(s[j]) < 0x40 ||
                                        static_cast<unsigned char>(s[j]) > 0x7E)) {
                    ++j;
                }
                if (j < s.size()) {
                    ++j;  // final byte
                }
                i = j - 1;  // the loop's ++i lands past the sequence
            }
            continue;  // ESC (lone or CSI): emit nothing
        }
        out += s[i];
    }
    return out;
}

std::string shell_colorize_block(std::string_view text, ShellMsgClass fallback,
                                 ShellLineClassifier classifier) {
    if (!shell_color_enabled() || text.empty() || text.size() > SHELL_MAX_COLORIZE_INPUT) {
        return std::string(text);
    }
    if (classifier == nullptr) {
        classifier = &shell_classify_line;
    }

    std::string out;
    out.reserve(text.size() + text.size() / 8 + 16);

    std::size_t i = 0;
    const std::size_t n = text.size();
    while (i < n) {
        const std::size_t nl = text.find('\n', i);
        const bool hasNewline = (nl != std::string_view::npos);
        const std::size_t chunkEnd = hasNewline ? nl : n;

        std::size_t contentEnd = chunkEnd;
        const bool hasCr = (contentEnd > i && text[contentEnd - 1] == '\r');
        if (hasCr) {
            --contentEnd;
        }

        const std::string_view content = text.substr(i, contentEnd - i);
        if (content.empty() || content.find('\x1b') != std::string_view::npos) {
            out.append(content);  // blank line, or already coloured -> passthrough
        } else {
            ShellMsgClass cls = classifier(content);
            if (cls == ShellMsgClass::Output && fallback != ShellMsgClass::Output) {
                cls = fallback;
            }
            const char* sgr = shell_sgr(cls);
            if (sgr[0] == '\0') {
                out.append(content);
            } else {
                out.append(sgr);
                out.append(content);
                out.append(shell_sgr_reset());
            }
        }

        if (hasCr) {
            out += '\r';
        }
        if (hasNewline) {
            out += '\n';
        }
        i = hasNewline ? nl + 1 : n;
    }

    return out;
}
