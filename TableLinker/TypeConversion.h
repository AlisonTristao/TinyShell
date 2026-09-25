#ifndef TYPE_CONVERSION_H
#define TYPE_CONVERSION_H

// Argument parsing for TinyShell commands: one tinyshell::ArgType<T> per
// parameter type a command may take.
//
// Allocation-free and exception-free: a parser reads one argument token and
// writes the value straight into the command's own (stack) argument tuple.
// The token it gets is already trimmed, unescaped and NUL-terminated, lives in
// a writable buffer owned by run_command_line(), and is valid only for the
// duration of that one command -- so std::string_view / const char* parameters
// can point straight at it with no copy. std::string parameters still work;
// they allocate only when the text outgrows the string's small-buffer storage.
//
// Integer types are matched by size and signedness, not by exact type, so
// `int`, `long`, `int32_t` (which is `long` on ESP-IDF 5) all parse the same
// way. The help listing names each parameter with a short type code:
//   i1 i2 i4 i8   signed integers (1/2/4/8 bytes)
//   u1 u2 u4 u8   unsigned integers
//   f4 f8         float / double
//   b1            bool   (1/0, true/false, on/off, yes/no -- any case)
//   c1            char   (x, 'x' or an escape such as \n)
//   s0            text   (std::string, std::string_view, const char*)
//
// Add your own parameter types by specializing tinyshell::ArgType<T> -- see
// UserTypeConverters.h.

#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

namespace tinyshell {

// The contract every specialization follows:
//
//   static constexpr const char* code;   // short name shown by help
//   static bool parse(const char* token, T& out, const char*& error);
//
// parse() returns false on bad input and may set `error` to a static,
// human-readable reason ("integer out of range"); it must not allocate unless
// T itself does, and must not throw.
template <typename T, typename Enable = void>
struct ArgType {
    static_assert(sizeof(T) == 0,
                  "TinyShell: no parser for this command parameter type. Use an integer, "
                  "float/double, bool, char, std::string, std::string_view or const char*, "
                  "or specialize tinyshell::ArgType<T> (see UserTypeConverters.h).");
};

namespace detail {

template <typename T>
constexpr bool is_plain_integer_v = std::is_integral<T>::value && !std::is_same<T, bool>::value &&
                                    !std::is_same<T, char>::value;

template <typename T>
constexpr const char* integer_code() {
    if (std::is_signed<T>::value) {
        return sizeof(T) == 1 ? "i1" : sizeof(T) == 2 ? "i2" : sizeof(T) == 4 ? "i4" : "i8";
    }
    return sizeof(T) == 1 ? "u1" : sizeof(T) == 2 ? "u2" : sizeof(T) == 4 ? "u4" : "u8";
}

inline char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

inline bool equals_ignore_case(const char* a, const char* b) {
    for (; *a != '\0' && *b != '\0'; ++a, ++b) {
        if (lower(*a) != lower(*b)) return false;
    }
    return *a == *b;
}

// Base 0, like strtol: 0x.. hex, a leading 0 octal, decimal otherwise.
template <typename T>
bool parse_signed(const char* token, T& out, const char*& error) {
    if (*token == '\0') {
        error = "empty integer value";
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const long long value = std::strtoll(token, &end, 0);
    if (end == token || *end != '\0') {
        error = "invalid integer format";
        return false;
    }
    if (errno == ERANGE || value < static_cast<long long>(std::numeric_limits<T>::min()) ||
        value > static_cast<long long>(std::numeric_limits<T>::max())) {
        error = "integer out of range";
        return false;
    }
    out = static_cast<T>(value);
    return true;
}

template <typename T>
bool parse_unsigned(const char* token, T& out, const char*& error) {
    if (*token == '\0') {
        error = "empty unsigned integer value";
        return false;
    }
    if (*token == '-') {
        error = "negative value for unsigned type";
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const unsigned long long value = std::strtoull(token, &end, 0);
    if (end == token || *end != '\0') {
        error = "invalid unsigned integer format";
        return false;
    }
    if (errno == ERANGE || value > static_cast<unsigned long long>(std::numeric_limits<T>::max())) {
        error = "unsigned integer out of range";
        return false;
    }
    out = static_cast<T>(value);
    return true;
}

template <typename T>
bool parse_floating(const char* token, T& out, const char*& error) {
    if (*token == '\0') {
        error = "empty floating-point value";
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const double value = std::strtod(token, &end);
    if (end == token || *end != '\0') {
        error = "invalid floating-point format";
        return false;
    }
    if (errno == ERANGE || !std::isfinite(value)) {
        error = "floating-point value out of range";
        return false;
    }
    if (value < -static_cast<double>(std::numeric_limits<T>::max()) ||
        value > static_cast<double>(std::numeric_limits<T>::max())) {
        error = "floating-point value out of target type range";
        return false;
    }
    out = static_cast<T>(value);
    return true;
}

inline bool parse_escaped_char(char escaped, char& out) {
    switch (escaped) {
        case 'n': out = '\n'; return true;
        case 'r': out = '\r'; return true;
        case 't': out = '\t'; return true;
        case '0': out = '\0'; return true;
        case '\\': out = '\\'; return true;
        case '\'': out = '\''; return true;
        case '"': out = '"'; return true;
        default: return false;
    }
}

}  // namespace detail

template <typename T>
struct ArgType<T, std::enable_if_t<detail::is_plain_integer_v<T> && std::is_signed<T>::value>> {
    static constexpr const char* code = detail::integer_code<T>();
    static bool parse(const char* token, T& out, const char*& error) {
        return detail::parse_signed(token, out, error);
    }
};

template <typename T>
struct ArgType<T, std::enable_if_t<detail::is_plain_integer_v<T> && std::is_unsigned<T>::value>> {
    static constexpr const char* code = detail::integer_code<T>();
    static bool parse(const char* token, T& out, const char*& error) {
        return detail::parse_unsigned(token, out, error);
    }
};

template <>
struct ArgType<float> {
    static constexpr const char* code = "f4";
    static bool parse(const char* token, float& out, const char*& error) {
        return detail::parse_floating(token, out, error);
    }
};

template <>
struct ArgType<double> {
    static constexpr const char* code = "f8";
    static bool parse(const char* token, double& out, const char*& error) {
        return detail::parse_floating(token, out, error);
    }
};

template <>
struct ArgType<bool> {
    static constexpr const char* code = "b1";
    static bool parse(const char* token, bool& out, const char*& error) {
        if (*token == '\0') {
            error = "empty boolean value";
            return false;
        }
        for (const char* yes : {"1", "true", "on", "yes"}) {
            if (detail::equals_ignore_case(token, yes)) {
                out = true;
                return true;
            }
        }
        for (const char* no : {"0", "false", "off", "no"}) {
            if (detail::equals_ignore_case(token, no)) {
                out = false;
                return true;
            }
        }
        error = "invalid boolean value";
        return false;
    }
};

template <>
struct ArgType<char> {
    static constexpr const char* code = "c1";
    static bool parse(const char* token, char& out, const char*& error) {
        std::string_view value(token);
        if (value.empty()) {
            error = "empty char value";
            return false;
        }
        if (value.size() >= 3 && value.front() == '\'' && value.back() == '\'') {
            value = value.substr(1, value.size() - 2);
        }
        if (value.size() == 1) {
            out = value[0];
            return true;
        }
        if (value.size() == 2 && value[0] == '\\' && detail::parse_escaped_char(value[1], out)) {
            return true;
        }
        error = "invalid char value";
        return false;
    }
};

// Copies the text -- allocates only past the string's small-buffer size.
template <>
struct ArgType<std::string> {
    static constexpr const char* code = "s0";
    static bool parse(const char* token, std::string& out, const char*& error) {
        (void)error;
        out.assign(token);
        return true;
    }
};

// Zero-copy: views the token, valid only while the command runs.
template <>
struct ArgType<std::string_view> {
    static constexpr const char* code = "s0";
    static bool parse(const char* token, std::string_view& out, const char*& error) {
        (void)error;
        out = token;
        return true;
    }
};

// Zero-copy, NUL-terminated: valid only while the command runs.
template <>
struct ArgType<const char*> {
    static constexpr const char* code = "s0";
    static bool parse(const char* token, const char*& out, const char*& error) {
        (void)error;
        out = token;
        return true;
    }
};

}  // namespace tinyshell

// The reference text "help -types" prints -- keep it in step with the codes
// above.
inline const char* get_types() {
    return "\n"
           "Parameter types shown by help:\n"
           "i1 i2 i4 i8 => signed integer, 1/2/4/8 bytes\n"
           "u1 u2 u4 u8 => unsigned integer, 1/2/4/8 bytes (0x.. hex accepted)\n"
           "f4 f8       => float / double\n"
           "b1          => bool (1/0, true/false, on/off, yes/no)\n"
           "c1          => char (x, 'x' or an escape such as \\n)\n"
           "s0          => text (use \\, for a literal comma)\n";
}

#include "UserTypeConverters.h"

#endif  // TYPE_CONVERSION_H
