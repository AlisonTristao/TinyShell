#ifndef USER_TYPE_CONVERTERS_H
#define USER_TYPE_CONVERTERS_H

#include <string>

// This file is intentionally user-editable.
// Implement custom type conversions here without changing the core TableLinker logic.
//
// Conversion contract:
// - Return true if this function handled the type_code.
// - On success, set out_ptr to a heap-allocated object compatible with the target function type.
// - On handled failure, set error_msg with details and keep out_ptr as nullptr.
// - Return false when type_code is unknown to your custom converter.
//
// Destroy contract:
// - Return true if this function handled type_code and released ptr.
// - Return false when type_code is unknown here.
namespace TinyShellUserConverters {

inline bool convert_type_custom(const char* data, const char* type_code, void*& out_ptr, std::string& error_msg) {
    (void)data;
    (void)type_code;
    out_ptr = nullptr;
    error_msg.clear();
    return false;
}

inline bool destroy_type_custom(void* ptr, const char* type_code) {
    (void)ptr;
    (void)type_code;
    return false;
}

} // namespace TinyShellUserConverters

#endif // USER_TYPE_CONVERTERS_H
