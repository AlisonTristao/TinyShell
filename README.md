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

    `VERBOSE`: returns only numeric status codes (no extra text).

    ```c++
    #define VERBOSE
    ```

    `HARD_VERBOSE`: returns no text at all (not even errors).

    ```c++
    #define HARD_VERBOSE
    ```

    If both are defined, `HARD_VERBOSE` has priority.