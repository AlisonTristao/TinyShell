#include "ShellStyle.h"

#include <initializer_list>

namespace {

// ---------------------------------------------------------------------------
// Small string helpers (kept local -- ShellStyle has no dependency on the
// trim/lower helpers that live in TinyShell.cpp / ShellLineEditor.cpp).
// ---------------------------------------------------------------------------

std::string to_lower_ascii(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

// Drop CSI sequences (ESC '[' ... final-byte-0x40..0x7E) and lone ESCs, so a
// line that was already coloured classifies by its text, not its escapes.
// (Public entry point: shell_strip_sgr, below.)
std::string strip_sgr(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\x1b') {
            std::size_t j = i + 1;
            if (j < s.size() && s[j] == '[') {
                ++j;
                while (j < s.size() &&
                       (static_cast<unsigned char>(s[j]) < 0x40 ||
                        static_cast<unsigned char>(s[j]) > 0x7E)) {
                    ++j;  // parameter / intermediate bytes
                }
                if (j < s.size()) {
                    ++j;  // final byte
                }
                i = j - 1;
            }
            continue;  // lone ESC (or the CSI just skipped): emit nothing
        }
        out += s[i];
    }
    return out;
}

std::string trim_copy(const std::string& s) {
    const char* const ws = " \t\r\n\v\f";
    const std::size_t begin = s.find_first_not_of(ws);
    if (begin == std::string::npos) {
        return std::string();
    }
    const std::size_t end = s.find_last_not_of(ws);
    return s.substr(begin, end - begin + 1);
}

bool starts_with(const std::string& s, const char* prefix) {
    std::size_t i = 0;
    for (; prefix[i] != '\0'; ++i) {
        if (i >= s.size() || s[i] != prefix[i]) {
            return false;
        }
    }
    return true;
}

bool contains_any(const std::string& hay, std::initializer_list<const char*> needles) {
    for (const char* needle : needles) {
        if (hay.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// ESP-NOW structured line: "[<token>][<...>]..." where <token> is one of the
// known type tokens (ShellOutput::isEspNowTypeToken on the dongle). These pass
// through the dongle's renderResponse() verbatim, so the classifier has to
// recognise them here to colour them.
bool espnow_structured_class(const std::string& lower, ShellMsgClass& outClass) {
    if (lower.size() < 4 || lower[0] != '[') {
        return false;
    }
    const std::size_t close = lower.find(']');
    if (close == std::string::npos || close < 2) {
        return false;
    }
    if (close + 1 >= lower.size() || lower[close + 1] != '[') {
        return false;
    }

    const std::string token = lower.substr(1, close - 1);
    if (token == "error" || token == "erro") {
        outClass = ShellMsgClass::Error;
        return true;
    }
    if (token == "info" || token == "cmd" || token == "cmdo" || token == "telemetry" ||
        token == "tele" || token == "debug" || token == "debg" || token == "pakg" ||
        token == "none") {
        outClass = ShellMsgClass::Muted;
        return true;
    }
    return false;
}

// One line -> its SGR-wrapped form, honouring `fallback` and idempotency.
std::string colorize_line(const std::string& content, ShellMsgClass fallback) {
    if (content.empty()) {
        return content;
    }
    if (content.find('\x1b') != std::string::npos) {
        return content;  // already coloured upstream -- leave it alone
    }

    ShellMsgClass cls = shell_classify_line(content);
    if (cls == ShellMsgClass::Output && fallback != ShellMsgClass::Output) {
        cls = fallback;
    }

    const char* sgr = shell_sgr(cls);
    if (sgr[0] == '\0') {
        return content;  // Output class, or TINYSHELL_COLOR off
    }
    return std::string(sgr) + content + shell_sgr_reset();
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

const char* shell_sgr(ShellMsgClass cls) {
#ifdef TINYSHELL_COLOR
    switch (cls) {
        case ShellMsgClass::Success: return "\x1b[32m";  // green
        case ShellMsgClass::Warning: return "\x1b[33m";  // yellow
        case ShellMsgClass::Error:   return "\x1b[31m";  // red
        case ShellMsgClass::Help:    return "\x1b[2m";   // faint
        case ShellMsgClass::Muted:   return "\x1b[90m";  // bright black / grey
        case ShellMsgClass::Prompt:  return "\x1b[36m";  // cyan   (reserved)
        case ShellMsgClass::Input:   return "\x1b[1m";   // bold   (reserved)
        case ShellMsgClass::Output:
        default:                     return "";
    }
#else
    (void)cls;
    return "";
#endif
}

const char* shell_sgr_reset() {
#ifdef TINYSHELL_COLOR
    return "\x1b[0m";
#else
    return "";
#endif
}

std::string shell_strip_sgr(const std::string& text) {
    return strip_sgr(text);
}

ShellMsgClass shell_classify_line(const std::string& rawLine) {
    const std::string line = trim_copy(strip_sgr(rawLine));
    if (line.empty()) {
        return ShellMsgClass::Output;
    }

    const std::string lower = to_lower_ascii(line);

    ShellMsgClass espnowClass;
    if (espnow_structured_class(lower, espnowClass)) {
        return espnowClass;
    }

    // Explicit prefixes emitted by the dongle's ShellCommandSupport.
    if (starts_with(lower, "erro(") || starts_with(lower, "erro (")) {
        return ShellMsgClass::Error;
    }
    if (starts_with(lower, "aviso(") || starts_with(lower, "aviso (")) {
        return ShellMsgClass::Warning;
    }
    if (starts_with(lower, "[help]")) {
        return ShellMsgClass::Help;
    }

    // Keyword scan -- parity with the dongle's lcdColorForLine, plus a few
    // conservative additions. Error wins over warning wins over success.
    if (contains_any(lower, {"erro", "error", "falha", "failed", "invalid", "invalido",
                             "not found", "nao encontrad", "negad", "denied"})) {
        return ShellMsgClass::Error;
    }
    if (contains_any(lower, {"aviso", "warning", "warn", "atencao", "deprecat"})) {
        return ShellMsgClass::Warning;
    }
    if (contains_any(lower, {"sucesso", "success", "inicializado", "atualizado", "enviado",
                             "adicionado", "removido", "ligado", "desligado", "limpo",
                             "salvo", "conectado", "pong"})) {
        return ShellMsgClass::Success;
    }

    return ShellMsgClass::Output;
}

std::string shell_colorize_block(const std::string& text, ShellMsgClass fallback) {
    if (text.empty()) {
        return text;
    }

    std::string out;
    out.reserve(text.size() + text.size() / 8 + 16);

    std::size_t i = 0;
    const std::size_t n = text.size();
    while (i < n) {
        const std::size_t nl = text.find('\n', i);
        const bool hasNewline = (nl != std::string::npos);
        const std::size_t chunkEnd = hasNewline ? nl : n;

        std::size_t contentEnd = chunkEnd;
        const bool hasCr = (contentEnd > i && text[contentEnd - 1] == '\r');
        if (hasCr) {
            --contentEnd;
        }

        out += colorize_line(text.substr(i, contentEnd - i), fallback);
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
