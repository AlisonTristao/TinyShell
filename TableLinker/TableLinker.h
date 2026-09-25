#ifndef TABLE_LINKER_H
#define TABLE_LINKER_H

// TinyShell's command registry: modules and the commands inside them.
//
// Fixed capacity, allocated inline, never resized: registering a command never
// copies, clones or re-allocates the ones already there (2.0.0 -- the old
// registry grew by one slot per add() and deep-copied every command each
// time, O(n^2) allocations at boot). Names and descriptions are stored as
// pointers, not copied, so pass string literals (or anything else that
// outlives the shell); a std::string argument is copied once into a small
// fixed arena instead. A command's callable (a lambda, function pointer or
// std::function) lives in an inline slot when it fits
// TINYSHELL_CALLABLE_STORAGE -- the usual [this] / [&x] capture does -- and
// only a bigger one costs a single allocation, once, at registration.
//
// Capacities are compile-time knobs; override them with -D build flags:

#ifndef TINYSHELL_MAX_MODULES
#define TINYSHELL_MAX_MODULES 32
#endif
#ifndef TINYSHELL_MAX_COMMANDS
#define TINYSHELL_MAX_COMMANDS 160  // total, across every module
#endif
#ifndef TINYSHELL_MAX_ARGS
#define TINYSHELL_MAX_ARGS 8
#endif
#ifndef TINYSHELL_CALLABLE_STORAGE
#define TINYSHELL_CALLABLE_STORAGE (4 * sizeof(void*))
#endif
#ifndef TINYSHELL_NAME_ARENA
#define TINYSHELL_NAME_ARENA 256  // bytes for names passed as std::string
#endif

// Result codes (a command's own return value is any other uint8_t).
#define RESULT_OK 0
#define RESULT_ERROR 255
#define FUNCTION_NOT_FOUND 254
#define MODULE_NOT_FOUND 253
#define TABLE_FULL 252

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <new>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "TypeConversion.h"

namespace tinyshell {

// Where a conversion failed, for the error message.
struct ConvError {
    std::size_t index = 0;
    const char* detail = nullptr;
};

namespace detail {

// Parameter / return types of whatever add() is handed.
template <typename T>
struct callable_traits : callable_traits<decltype(&T::operator())> {};
template <typename R, typename... A>
struct callable_traits<R (*)(A...)> {
    using result = R;
    using args = std::tuple<A...>;
};
template <typename R, typename... A>
struct callable_traits<R (*)(A...) noexcept> : callable_traits<R (*)(A...)> {};
template <typename R, typename... A>
struct callable_traits<R(A...)> : callable_traits<R (*)(A...)> {};
template <typename C, typename R, typename... A>
struct callable_traits<R (C::*)(A...)> : callable_traits<R (*)(A...)> {};
template <typename C, typename R, typename... A>
struct callable_traits<R (C::*)(A...) const> : callable_traits<R (*)(A...)> {};
template <typename C, typename R, typename... A>
struct callable_traits<R (C::*)(A...) noexcept> : callable_traits<R (*)(A...)> {};
template <typename C, typename R, typename... A>
struct callable_traits<R (C::*)(A...) const noexcept> : callable_traits<R (*)(A...)> {};

template <typename... A>
struct TypeCodes {
    static constexpr const char* value[sizeof...(A) + 1] = {ArgType<std::decay_t<A>>::code..., nullptr};
};

template <typename Tuple>
struct TypeCodesOf;
template <typename... A>
struct TypeCodesOf<std::tuple<A...>> : TypeCodes<A...> {};

template <typename Tuple, std::size_t I>
bool parse_one(Tuple& values, char* const* argv, ConvError* error) {
    using T = std::tuple_element_t<I, Tuple>;
    const char* reason = nullptr;
    if (!ArgType<T>::parse(argv[I], std::get<I>(values), reason)) {
        error->index = I;
        error->detail = reason != nullptr ? reason : "invalid value";
        return false;
    }
    return true;
}

template <typename Tuple, std::size_t... I>
bool parse_all(Tuple& values, char* const* argv, ConvError* error, std::index_sequence<I...>) {
    (void)values;  // unused for a command with no parameters
    (void)argv;
    (void)error;
    return (true && ... && parse_one<Tuple, I>(values, argv, error));
}

template <typename F, typename... A>
std::uint8_t invoke(void* callable, char* const* argv, ConvError* error, std::tuple<A...>*) {
    std::tuple<std::decay_t<A>...> values{};
    if (!parse_all(values, argv, error, std::index_sequence_for<A...>{})) {
        return RESULT_ERROR;
    }
    return static_cast<std::uint8_t>(std::apply(*static_cast<F*>(callable), values));
}

}  // namespace detail

struct Command {
    const char* name = nullptr;
    const char* description = nullptr;
    const char* const* types = nullptr;  // `arity` type codes
    std::uint8_t arity = 0;
    std::uint8_t module = 0;
    std::uint8_t (*invoke)(void*, char* const*, ConvError*) = nullptr;
    void (*destroy)(void*, bool on_heap) = nullptr;
    void* heap = nullptr;  // non-null only for a callable too big for `storage`
    alignas(std::max_align_t) unsigned char storage[TINYSHELL_CALLABLE_STORAGE];

    void* callable() noexcept { return heap != nullptr ? heap : static_cast<void*>(storage); }
};

struct Module {
    const char* name = nullptr;
    const char* description = nullptr;
};

}  // namespace tinyshell

class TableLinker {
public:
    static constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);

    TableLinker() = default;
    ~TableLinker();
    TableLinker(const TableLinker&) = delete;
    TableLinker& operator=(const TableLinker&) = delete;

    // RESULT_OK; MODULE_NOT_FOUND if the name is already taken (kept from
    // 1.x); TABLE_FULL past TINYSHELL_MAX_MODULES / the name arena.
    std::uint8_t create_module(const char* mod_name, const char* mod_description);
    std::uint8_t create_module(const std::string& mod_name, const std::string& mod_description);

    // RESULT_OK; MODULE_NOT_FOUND for an unknown module; RESULT_ERROR for a
    // name already used in that module or an allocation failure;
    // TABLE_FULL past TINYSHELL_MAX_COMMANDS.
    template <typename F>
    std::uint8_t add_func_to_module(const char* module_name, F&& func, const char* func_name,
                                    const char* func_description);
    template <typename F>
    std::uint8_t add_func_to_module(const std::string& module_name, F&& func,
                                    const std::string& func_name,
                                    const std::string& func_description);

    std::size_t find_module(std::string_view name) const noexcept;
    std::size_t find_command(std::size_t module, std::string_view name) const noexcept;

    std::size_t module_count() const noexcept { return module_count_; }
    const tinyshell::Module& module_at(std::size_t idx) const noexcept { return modules_[idx]; }
    std::size_t command_count() const noexcept { return command_count_; }
    const tinyshell::Command& command_at(std::size_t idx) const noexcept { return commands_[idx]; }

    // Registrations refused because a table or the name arena was full --
    // non-zero means TINYSHELL_MAX_* is too small for this firmware.
    std::size_t dropped_registrations() const noexcept { return dropped_; }

    // Runs command `idx` with its `arity` NUL-terminated argument tokens.
    std::uint8_t call(std::size_t idx, char* const* argv, tinyshell::ConvError* error);

private:
    const char* intern(const std::string& text);

    tinyshell::Module modules_[TINYSHELL_MAX_MODULES];
    tinyshell::Command commands_[TINYSHELL_MAX_COMMANDS];
    std::size_t module_count_ = 0;
    std::size_t command_count_ = 0;
    std::size_t dropped_ = 0;
    char names_[TINYSHELL_NAME_ARENA] = {};
    std::size_t names_used_ = 0;
};

template <typename F>
std::uint8_t TableLinker::add_func_to_module(const char* module_name, F&& func,
                                             const char* func_name, const char* func_description) {
    using Fn = std::decay_t<F>;
    using Traits = tinyshell::detail::callable_traits<Fn>;
    using Args = typename Traits::args;
    static_assert(std::is_convertible<typename Traits::result, std::uint8_t>::value,
                  "TinyShell: a command must return uint8_t (0 = success)");
    static_assert(std::tuple_size<Args>::value <= TINYSHELL_MAX_ARGS,
                  "TinyShell: too many parameters -- raise TINYSHELL_MAX_ARGS");

    if (module_name == nullptr || func_name == nullptr) return RESULT_ERROR;
    const std::size_t module = find_module(module_name);
    if (module == kNotFound) return MODULE_NOT_FOUND;
    if (find_command(module, func_name) != kNotFound) return RESULT_ERROR;
    if (command_count_ >= TINYSHELL_MAX_COMMANDS) {
        ++dropped_;
        return TABLE_FULL;
    }

    tinyshell::Command& command = commands_[command_count_];
    constexpr bool fits_inline = sizeof(Fn) <= sizeof(command.storage) &&
                                 alignof(Fn) <= alignof(std::max_align_t);
    if constexpr (fits_inline) {
        new (static_cast<void*>(command.storage)) Fn(std::forward<F>(func));
        command.heap = nullptr;
    } else {
        command.heap = new (std::nothrow) Fn(std::forward<F>(func));
        if (command.heap == nullptr) return RESULT_ERROR;
    }
    command.destroy = [](void* callable, bool on_heap) {
        if (on_heap) {
            delete static_cast<Fn*>(callable);
        } else {
            static_cast<Fn*>(callable)->~Fn();
        }
    };
    command.invoke = [](void* callable, char* const* argv, tinyshell::ConvError* error) {
        return tinyshell::detail::invoke<Fn>(callable, argv, error, static_cast<Args*>(nullptr));
    };
    command.name = func_name;
    command.description = func_description != nullptr ? func_description : "";
    command.types = tinyshell::detail::TypeCodesOf<Args>::value;
    command.arity = static_cast<std::uint8_t>(std::tuple_size<Args>::value);
    command.module = static_cast<std::uint8_t>(module);
    ++command_count_;
    return RESULT_OK;
}

template <typename F>
std::uint8_t TableLinker::add_func_to_module(const std::string& module_name, F&& func,
                                             const std::string& func_name,
                                             const std::string& func_description) {
    const std::size_t module = find_module(module_name);
    if (module == kNotFound) return MODULE_NOT_FOUND;
    const char* name = intern(func_name);
    const char* description = intern(func_description);
    if (name == nullptr || description == nullptr) {
        ++dropped_;
        return TABLE_FULL;
    }
    return add_func_to_module(modules_[module].name, std::forward<F>(func), name, description);
}

#endif  // TABLE_LINKER_H
