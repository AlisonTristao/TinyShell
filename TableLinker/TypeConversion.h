#ifndef TYPE_CONVERSION_H
#define TYPE_CONVERSION_H

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "UserTypeConverters.h"

// ****************************************
// * Templates to convert typeid to codes *
// ****************************************
template<typename T>
constexpr const char* type_code() {
    using Type = typename std::decay<T>::type;

    if constexpr (std::is_same<Type, int8_t>::value) return "i1";
    if constexpr (std::is_same<Type, uint8_t>::value) return "u1";
    if constexpr (std::is_same<Type, int16_t>::value) return "i2";
    if constexpr (std::is_same<Type, uint16_t>::value) return "u2";
    if constexpr (std::is_same<Type, int32_t>::value) return "i4";
    if constexpr (std::is_same<Type, uint32_t>::value) return "u4";
    if constexpr (std::is_same<Type, int64_t>::value) return "i8";
    if constexpr (std::is_same<Type, uint64_t>::value) return "u8";
    if constexpr (std::is_same<Type, bool>::value) return "b1";
    if constexpr (std::is_same<Type, float>::value) return "f4";
    if constexpr (std::is_same<Type, double>::value) return "f8";
    if constexpr (std::is_same<Type, char>::value) return "c1";
    if constexpr (std::is_same<Type, std::string>::value) return "s0";
    return "??";
}

namespace tinyshell_detail {

inline std::invalid_argument make_conversion_error(const char* type_code, const std::string& detail) {
    return std::invalid_argument(
        "Conversion failed for type '" + std::string(type_code ? type_code : "<null>") + "': " + detail
    );
}

inline std::string trim_copy(const char* data) {
    if (data == nullptr) return "";

    std::string value(data);
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }

    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }

    return value.substr(start, end - start);
}

template<typename T>
bool parse_signed_integer(const char* data, T& out, std::string& error_msg) {
    const std::string value = trim_copy(data);
    if (value.empty()) {
        error_msg = "empty integer value";
        return false;
    }

    errno = 0;
    char* end_ptr = nullptr;
    const long long parsed = std::strtoll(value.c_str(), &end_ptr, 0);

    if (end_ptr == value.c_str() || *end_ptr != '\0') {
        error_msg = "invalid integer format";
        return false;
    }

    if (errno == ERANGE ||
        parsed < static_cast<long long>(std::numeric_limits<T>::min()) ||
        parsed > static_cast<long long>(std::numeric_limits<T>::max())) {
        error_msg = "integer out of range";
        return false;
    }

    out = static_cast<T>(parsed);
    return true;
}

template<typename T>
bool parse_unsigned_integer(const char* data, T& out, std::string& error_msg) {
    const std::string value = trim_copy(data);
    if (value.empty()) {
        error_msg = "empty unsigned integer value";
        return false;
    }

    if (value[0] == '-') {
        error_msg = "negative value for unsigned type";
        return false;
    }

    errno = 0;
    char* end_ptr = nullptr;
    const unsigned long long parsed = std::strtoull(value.c_str(), &end_ptr, 0);

    if (end_ptr == value.c_str() || *end_ptr != '\0') {
        error_msg = "invalid unsigned integer format";
        return false;
    }

    if (errno == ERANGE || parsed > static_cast<unsigned long long>(std::numeric_limits<T>::max())) {
        error_msg = "unsigned integer out of range";
        return false;
    }

    out = static_cast<T>(parsed);
    return true;
}

template<typename T>
bool parse_floating_point(const char* data, T& out, std::string& error_msg) {
    const std::string value = trim_copy(data);
    if (value.empty()) {
        error_msg = "empty floating-point value";
        return false;
    }

    errno = 0;
    char* end_ptr = nullptr;
    const long double parsed = std::strtold(value.c_str(), &end_ptr);

    if (end_ptr == value.c_str() || *end_ptr != '\0') {
        error_msg = "invalid floating-point format";
        return false;
    }

    if (errno == ERANGE || !std::isfinite(parsed)) {
        error_msg = "floating-point value out of range";
        return false;
    }

    if (parsed < -static_cast<long double>(std::numeric_limits<T>::max()) ||
        parsed > static_cast<long double>(std::numeric_limits<T>::max())) {
        error_msg = "floating-point value out of target type range";
        return false;
    }

    out = static_cast<T>(parsed);
    return true;
}

inline bool parse_bool(const char* data, bool& out, std::string& error_msg) {
    const std::string value = trim_copy(data);
    if (value.empty()) {
        error_msg = "empty boolean value";
        return false;
    }

    std::string lowered;
    lowered.reserve(value.size());
    for (char c : value) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    if (lowered == "1" || lowered == "true" || lowered == "on" || lowered == "yes") {
        out = true;
        return true;
    }

    if (lowered == "0" || lowered == "false" || lowered == "off" || lowered == "no") {
        out = false;
        return true;
    }

    error_msg = "invalid boolean value";
    return false;
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

inline bool parse_char(const char* data, char& out, std::string& error_msg) {
    std::string value = trim_copy(data);
    if (value.empty()) {
        error_msg = "empty char value";
        return false;
    }

    if (value.size() >= 3 && value.front() == '\'' && value.back() == '\'') {
        value = value.substr(1, value.size() - 2);
    }

    if (value.size() == 1) {
        out = value[0];
        return true;
    }

    if (value.size() == 2 && value[0] == '\\' && parse_escaped_char(value[1], out)) {
        return true;
    }

    error_msg = "invalid char value";
    return false;
}

template<typename T>
void* parse_and_allocate_signed(const char* data, const char* type_code) {
    T value{};
    std::string error_msg;
    if (!parse_signed_integer<T>(data, value, error_msg)) {
        throw make_conversion_error(type_code, error_msg);
    }
    return new T(value);
}

template<typename T>
void* parse_and_allocate_unsigned(const char* data, const char* type_code) {
    T value{};
    std::string error_msg;
    if (!parse_unsigned_integer<T>(data, value, error_msg)) {
        throw make_conversion_error(type_code, error_msg);
    }
    return new T(value);
}

template<typename T>
void* parse_and_allocate_float(const char* data, const char* type_code) {
    T value{};
    std::string error_msg;
    if (!parse_floating_point<T>(data, value, error_msg)) {
        throw make_conversion_error(type_code, error_msg);
    }
    return new T(value);
}

template<typename T>
void destroy_as(void* ptr) {
    delete static_cast<T*>(ptr);
}

} // namespace tinyshell_detail

inline void* convert_type_char(const char* data, const char* type_code) {
    if (type_code == nullptr) {
        throw std::invalid_argument("Type code is null");
    }

    if (std::strcmp(type_code, "i1") == 0) return tinyshell_detail::parse_and_allocate_signed<int8_t>(data, type_code);
    if (std::strcmp(type_code, "u1") == 0) return tinyshell_detail::parse_and_allocate_unsigned<uint8_t>(data, type_code);
    if (std::strcmp(type_code, "i2") == 0) return tinyshell_detail::parse_and_allocate_signed<int16_t>(data, type_code);
    if (std::strcmp(type_code, "u2") == 0) return tinyshell_detail::parse_and_allocate_unsigned<uint16_t>(data, type_code);
    if (std::strcmp(type_code, "i4") == 0) return tinyshell_detail::parse_and_allocate_signed<int32_t>(data, type_code);
    if (std::strcmp(type_code, "u4") == 0) return tinyshell_detail::parse_and_allocate_unsigned<uint32_t>(data, type_code);
    if (std::strcmp(type_code, "i8") == 0) return tinyshell_detail::parse_and_allocate_signed<int64_t>(data, type_code);
    if (std::strcmp(type_code, "u8") == 0) return tinyshell_detail::parse_and_allocate_unsigned<uint64_t>(data, type_code);

    if (std::strcmp(type_code, "f4") == 0) return tinyshell_detail::parse_and_allocate_float<float>(data, type_code);
    if (std::strcmp(type_code, "f8") == 0) return tinyshell_detail::parse_and_allocate_float<double>(data, type_code);

    if (std::strcmp(type_code, "b1") == 0) {
        bool value = false;
        std::string error_msg;
        if (!tinyshell_detail::parse_bool(data, value, error_msg)) {
            throw tinyshell_detail::make_conversion_error(type_code, error_msg);
        }
        return new bool(value);
    }

    if (std::strcmp(type_code, "c1") == 0) {
        char value = '\0';
        std::string error_msg;
        if (!tinyshell_detail::parse_char(data, value, error_msg)) {
            throw tinyshell_detail::make_conversion_error(type_code, error_msg);
        }
        return new char(value);
    }

    if (std::strcmp(type_code, "s0") == 0) return new std::string(data ? data : "");

    void* custom_ptr = nullptr;
    std::string custom_error;
    if (TinyShellUserConverters::convert_type_custom(data, type_code, custom_ptr, custom_error)) {
        if (custom_ptr == nullptr) {
            throw tinyshell_detail::make_conversion_error(
                type_code,
                custom_error.empty() ? "custom converter returned null pointer" : custom_error
            );
        }
        return custom_ptr;
    }

    if (!custom_error.empty()) {
        throw tinyshell_detail::make_conversion_error(type_code, custom_error);
    }

    throw std::invalid_argument("Unknown type code: " + std::string(type_code));
}

inline void destroy_type_char(void* ptr, const char* type_code) {
    if (ptr == nullptr || type_code == nullptr) return;

    if (std::strcmp(type_code, "i1") == 0) { tinyshell_detail::destroy_as<int8_t>(ptr); return; }
    if (std::strcmp(type_code, "u1") == 0) { tinyshell_detail::destroy_as<uint8_t>(ptr); return; }
    if (std::strcmp(type_code, "i2") == 0) { tinyshell_detail::destroy_as<int16_t>(ptr); return; }
    if (std::strcmp(type_code, "u2") == 0) { tinyshell_detail::destroy_as<uint16_t>(ptr); return; }
    if (std::strcmp(type_code, "i4") == 0) { tinyshell_detail::destroy_as<int32_t>(ptr); return; }
    if (std::strcmp(type_code, "u4") == 0) { tinyshell_detail::destroy_as<uint32_t>(ptr); return; }
    if (std::strcmp(type_code, "i8") == 0) { tinyshell_detail::destroy_as<int64_t>(ptr); return; }
    if (std::strcmp(type_code, "u8") == 0) { tinyshell_detail::destroy_as<uint64_t>(ptr); return; }
    if (std::strcmp(type_code, "f4") == 0) { tinyshell_detail::destroy_as<float>(ptr); return; }
    if (std::strcmp(type_code, "f8") == 0) { tinyshell_detail::destroy_as<double>(ptr); return; }
    if (std::strcmp(type_code, "b1") == 0) { tinyshell_detail::destroy_as<bool>(ptr); return; }
    if (std::strcmp(type_code, "c1") == 0) { tinyshell_detail::destroy_as<char>(ptr); return; }
    if (std::strcmp(type_code, "s0") == 0) { tinyshell_detail::destroy_as<std::string>(ptr); return; }

    (void)TinyShellUserConverters::destroy_type_custom(ptr, type_code);
}

#endif // TYPE_CONVERSION_H
