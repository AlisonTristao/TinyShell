#ifndef TINY_SHELL_H
#define TINY_SHELL_H

#include <TableLinker/TableLinker.h>
#include <ShellStyle.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// Longest command line run_command_line() accepts. The line is copied into a
// stack buffer of this size (it is tokenized in place), so this is also the
// per-call stack cost -- a command that runs another command nests one more.
#ifndef TINYSHELL_MAX_LINE
#define TINYSHELL_MAX_LINE 512
#endif

// Output is assembled in a stack buffer of this size and handed to the output
// callback in chunks of whole lines: a normal response is one callback call,
// a long help listing a few.
#ifndef TINYSHELL_OUTPUT_CAPACITY
#define TINYSHELL_OUTPUT_CAPACITY 256
#endif

// Output modes:
// - Default: human-readable messages
// - VERBOSE_OFF: returns only numeric status codes ("<code>\n")
// - HARD_VERBOSE_OFF: returns no text at all

// **********************************
// *       Class of TinyShell       *
// **********************************
//
// Runs lines of the form `module -command arg0, arg1, ... argN`: finds the
// command registered under that module, converts each comma-separated
// argument to the matching parameter type (tinyshell::ArgType, see
// TableLinker/TypeConversion.h -- `\,` is a literal comma) and calls it.
//
// Since 2.0.0 the shell allocates nothing to register or run a command:
// fixed-capacity tables (TINYSHELL_MAX_* in TableLinker.h), names kept as
// pointers to your string literals, the line parsed in place, arguments
// converted straight into a stack tuple, no exceptions of its own. The only
// allocations left are the ones you opt into: a std::string parameter longer
// than its small-buffer size, an output callback that takes std::string
// instead of std::string_view, TINYSHELL_COLOR's colouring pass, and
// complete_line()'s result vector.
class TinyShell {
public:
    TinyShell() = default;
    TinyShell(const TinyShell&) = delete;
    TinyShell& operator=(const TinyShell&) = delete;

    /**
     * @brief Registers the built-in "help" module (-modules, -types).
     */
    void begin();

    /**
     * @brief Where every response goes. Prefer a callback taking
     * std::string_view -- it is handed the text straight out of the output
     * buffer. One taking `const std::string&` (the 1.x shape) still works, at
     * the cost of one std::string per call.
     */
    template <typename F>
    void set_output_callback(F&& callback);

    /**
     * @brief Run one command line.
     * @return the command's own status, or RESULT_ERROR / FUNCTION_NOT_FOUND /
     * MODULE_NOT_FOUND when it could not be run.
     */
    std::uint8_t run_command_line(std::string_view command);

    /**
     * @brief Returns line-completion candidates for a partial shell input.
     *
     * Examples:
     * - "do" -> ["dongle "]
     * - "dongle -l" -> ["dongle -lcd ", "dongle -led ", ...]
     */
    std::vector<std::string> complete_line(std::string_view partial_command,
                                           std::size_t max_results = 16);

    /**
     * @brief Add a command to a module. `func` is any callable returning
     * uint8_t (0 = success) whose parameters have a tinyshell::ArgType.
     * Keep `name`, `description` and `module_name` alive as long as the
     * shell (string literals are); the std::string overload copies them into
     * a fixed arena instead.
     * @return RESULT_OK, MODULE_NOT_FOUND, TABLE_FULL, or RESULT_ERROR for a
     * duplicate name.
     */
    template <typename F>
    std::uint8_t add(F&& func, const char* name, const char* description, const char* module_name) {
        return table_linker.add_func_to_module(module_name, std::forward<F>(func), name, description);
    }
    template <typename F>
    std::uint8_t add(F&& func, const std::string& name, const std::string& description,
                     const std::string& module_name) {
        return table_linker.add_func_to_module(module_name, std::forward<F>(func), name, description);
    }

    /**
     * @brief Create a module (same lifetime rule for the strings as add()).
     * @return RESULT_OK, TABLE_FULL, or MODULE_NOT_FOUND if it already exists.
     */
    std::uint8_t create_module(const char* mod_name, const char* mod_description) {
        return table_linker.create_module(mod_name, mod_description);
    }
    std::uint8_t create_module(const std::string& mod_name, const std::string& mod_description) {
        return table_linker.create_module(mod_name, mod_description);
    }

    /**
     * @brief Registrations refused because a table was full. Non-zero means a
     * TINYSHELL_MAX_* capacity is too small for this firmware.
     */
    std::size_t dropped_registrations() const noexcept {
        return table_linker.dropped_registrations();
    }

private:
    class Writer;

    void emit(std::string_view chunk, ShellMsgClass msg_class);
    void write_module_list(Writer& out);
    void write_module_help(Writer& out, std::size_t module);
    void write_types(Writer& out, const tinyshell::Command& command);
    std::uint8_t wrapper_help();
    std::uint8_t wrapper_help_types();

    TableLinker table_linker;
    std::function<void(std::string_view)> output_callback;
};

template <typename F>
void TinyShell::set_output_callback(F&& callback) {
    using Fn = std::decay_t<F>;
    if constexpr (std::is_invocable<Fn&, std::string_view>::value) {
        output_callback = std::function<void(std::string_view)>(std::forward<F>(callback));
    } else {
        static_assert(std::is_invocable<Fn&, const std::string&>::value,
                      "TinyShell: the output callback must take std::string_view or const std::string&");
        output_callback = [fn = Fn(std::forward<F>(callback))](std::string_view text) mutable {
            fn(std::string(text));
        };
    }
}

// 1.x put `using namespace std;` in this header and code that includes it
// relies on that. Kept for compatibility, deprecated: define
// TINYSHELL_NO_USING_NAMESPACE_STD to keep std out of the global namespace.
#ifndef TINYSHELL_NO_USING_NAMESPACE_STD
using namespace std;
#endif

#endif  // TINY_SHELL_H
