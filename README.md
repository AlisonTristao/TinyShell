# TinyShell


### How the Library Works

This library is a tool for **executing functions via text commands**. It uses a **table** to store **function pointers**, each associated with a **name** and a **description**.

When you pass a **formatted string** to the `run_line_command` function, the library **searches for the function name** in this list and, if found, **executes it**.

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
* **Command Format:** The string expected by the `run_line_command` function follows this format:

    ```
    module -function arg0, arg1, arg2, ..., argN
    ```
* **Argument Types:** Argument types are represented by 2-character strings, like `i4`, `u8`, `s0`, etc. You can add your own types in the `tablelinker.h` header file.

    ```cpp
    template<typename T>
    constexpr const char* type_code() {
        if constexpr (is_same<T, uint8_t>::value)   return "u1";
        if constexpr (is_same<T, int8_t>::value)    return "i1";
        if constexpr (is_same<T, int32_t>::value)   return "i4";
        if constexpr (is_same<T, uint32_t>::value)  return "u4";
        if constexpr (is_same<T, float>::value)     return "f4";
        if constexpr (is_same<T, double>::value)    return "f8";
        if constexpr (is_same<T, char>::value)      return "c1";
        if constexpr (is_same<T, string>::value)    return "s0";
        return "??";  // Unknown type
    }

    inline void* convert_type_char(const char* data, const char* type_code) {
        if (strcmp(type_code, "u1") == 0) return new uint8_t(static_cast<uint8_t>(atoi(data)));
        if (strcmp(type_code, "i4") == 0) return new int32_t(atoi(data));
        if (strcmp(type_code, "f4") == 0) return new float(atof(data));
        if (strcmp(type_code, "f8") == 0) return new double(atof(data));
        if (strcmp(type_code, "c1") == 0) return new char(data[0]);
        if (strcmp(type_code, "s0") == 0) return new string(data);
        throw invalid_argument("Unknown type code");
    }
    ```
* **Verbose mode:** You can turn off verbose mode by defining.

    ```c++
    #define VERBOSE_OFF
    ```