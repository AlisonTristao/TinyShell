// Standalone test for TinyShell / TableLinker -- plain asserts, no framework.
// To run it (both with and without colour, like test_shell_style.cpp):
//   g++ -std=gnu++17 -I. -Wall -Wextra TinyShell.cpp TableLinker/TableLinker.cpp ShellStyle.cpp test/test_tinyshell.cpp -o ts && ./ts
//   g++ -std=gnu++17 -I. -Wall -Wextra -DTINYSHELL_COLOR TinyShell.cpp TableLinker/TableLinker.cpp ShellStyle.cpp test/test_tinyshell.cpp -o tsc && ./tsc
//
// The global operator new below counts every allocation, which is how the
// "running a command allocates nothing" promise is checked, not assumed.

#define TINYSHELL_NO_USING_NAMESPACE_STD
#include "TinyShell.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace {
std::size_t g_allocations = 0;
}

void* operator new(std::size_t size) {
    ++g_allocations;
    if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
#if defined(__cpp_exceptions)
    throw std::bad_alloc();
#else
    std::abort();
#endif
}
void* operator new[](std::size_t size) { return operator new(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    ++g_allocations;
    return std::malloc(size == 0 ? 1 : size);
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {

// VERBOSE_OFF / HARD_VERBOSE_OFF replace every message with a status code or
// nothing, so the message checks below only run in the default mode.
#if defined(VERBOSE_OFF) || defined(HARD_VERBOSE_OFF)
constexpr bool kMessages = false;
#else
constexpr bool kMessages = true;
#endif

// Collects what the shell prints, colour stripped; a string_view callback, so the shell
// itself allocates nothing to deliver it (the capture's own std::string is
// the test's cost, measured apart below).
struct Capture {
    std::string text;
    int calls = 0;
    void operator()(std::string_view chunk) {
        if (shell_color_enabled()) {
            text += shell_strip_sgr(chunk);
        } else {
            text.append(chunk.data(), chunk.size());
        }
        ++calls;
    }
};

// True when `needle` was printed -- or when this build prints no messages at
// all, so the message checks below read the same in every mode.
bool contains(const std::string& haystack, std::string_view needle) {
    return !kMessages || haystack.find(needle) != std::string::npos;
}

// The "<module> -<command>: success" line, which TINYSHELL_QUIET_SUCCESS leaves
// out by design.
bool reported_success(const std::string& haystack, std::string_view needle) {
#if defined(TINYSHELL_QUIET_SUCCESS)
    (void)haystack;
    (void)needle;
    return true;
#else
    return contains(haystack, needle);
#endif
}

void test_basic_call_and_types() {
    TinyShell shell;
    Capture out;
    shell.set_output_callback([&out](std::string_view s) { out(s); });
    shell.begin();
    assert(shell.create_module("math", "numbers") == RESULT_OK);

    int32_t got_i = 0;
    uint8_t got_u = 0;
    float got_f = 0.0f;
    bool got_b = false;
    std::string got_s;
    assert(shell.add(
               [&](int32_t i, uint8_t u, float f, bool b, std::string s) -> uint8_t {
                   got_i = i;
                   got_u = u;
                   got_f = f;
                   got_b = b;
                   got_s = s;
                   return 0;
               },
               "mix", "all kinds", "math") == RESULT_OK);

    assert(shell.run_command_line("math -mix -12, 0x10, 2.5, ON, hello\\, world") == RESULT_OK);
    assert(got_i == -12);
    assert(got_u == 16);
    assert(got_f == 2.5f);
    assert(got_b);
    assert(got_s == "hello, world");
    assert(reported_success(out.text, "math -mix: success\n"));

    // `long` / `int` / `int32_t` are all "i4" by size, not by exact type.
    assert(shell.add([](long v) -> uint8_t { return v == 7 ? 0 : 1; }, "l", "long", "math") ==
           RESULT_OK);
    assert(shell.run_command_line("math -l 7") == RESULT_OK);
}

void test_error_messages() {
    TinyShell shell;
    Capture out;
    shell.set_output_callback([&out](std::string_view s) { out(s); });
    shell.begin();
    shell.create_module("robot", "the robot");
    shell.add([](uint8_t, uint8_t) -> uint8_t { return 0; }, "pwm", "set pwm", "robot");
    shell.add([]() -> uint8_t { return 3; }, "fail", "always fails", "robot");

    out.text.clear();
    assert(shell.run_command_line("nope -x") == MODULE_NOT_FOUND);
    assert(contains(out.text, "nope: module not found\n\n"));
    // Module list: names padded to the widest one ("robot").
    assert(contains(out.text,
                    "  help   Help module for listing available commands\n"
                    "  robot  the robot\n"));

    out.text.clear();
    assert(shell.run_command_line("robot") == FUNCTION_NOT_FOUND);  // bare module
    assert(contains(out.text, "robot: missing command\n\n"));
    // Command list: name and type columns aligned.
    assert(contains(out.text,
                    "robot - the robot\n"
                    "usage: robot -<command> [arg, ...]\n"
                    "\n"
                    "  -pwm   (u1, u1)  set pwm\n"
                    "  -fail  ()        always fails\n"));

    out.text.clear();
    assert(shell.run_command_line("robot -nope") == FUNCTION_NOT_FOUND);
    assert(contains(out.text, "robot -nope: command not found\n"));

    out.text.clear();
    assert(shell.run_command_line("robot -pwm 1") == RESULT_ERROR);
    assert(contains(out.text, "robot -pwm: invalid number of arguments, expected (u1, u1)\n"));

    out.text.clear();
    assert(shell.run_command_line("robot -pwm 1, 300") == RESULT_ERROR);
    assert(contains(out.text,
                    "robot -pwm: invalid argument '300' for type 'u1': unsigned integer out of range\n"));

    out.text.clear();
    assert(shell.run_command_line("robot -pwm -1, 2") == RESULT_ERROR);
    assert(contains(out.text, "negative value for unsigned type"));

    out.text.clear();
    assert(shell.run_command_line("robot -fail") == 3);
    assert(contains(out.text, "robot -fail: failed with status 3\n"));

    // Duplicate module keeps the 1.x code; duplicate command is refused.
    assert(shell.create_module("robot", "again") == MODULE_NOT_FOUND);
    assert(shell.add([]() -> uint8_t { return 0; }, "fail", "dup", "robot") == RESULT_ERROR);
    assert(shell.add([]() -> uint8_t { return 0; }, "x", "no module", "ghost") == MODULE_NOT_FOUND);
}

void test_help_and_completion() {
    TinyShell shell;
    Capture out;
    shell.set_output_callback([&out](std::string_view s) { out(s); });
    shell.begin();
    shell.create_module("dongle", "the dongle");
    shell.add([]() -> uint8_t { return 0; }, "led", "led", "dongle");
    shell.add([]() -> uint8_t { return 0; }, "lcd", "lcd", "dongle");

    assert(shell.run_command_line("help -modules") == RESULT_OK);
    assert(contains(out.text, "  help    Help module for listing available commands\n"));
    assert(contains(out.text, "  dongle  the dongle\n"));
    out.text.clear();
    assert(shell.run_command_line("help -types") == RESULT_OK);
    assert(contains(out.text, "u1 u2 u4 u8"));

    const std::vector<std::string> modules = shell.complete_line("do");
    assert(modules.size() == 1 && modules[0] == "dongle ");
    const std::vector<std::string> commands = shell.complete_line("dongle -l");
    assert(commands.size() == 2 && commands[0] == "dongle -led " && commands[1] == "dongle -lcd ");
    assert(shell.complete_line("dongle -led x").empty());
}

void test_string_view_args_are_zero_copy() {
    TinyShell shell;
    shell.set_output_callback([](std::string_view) {});
    shell.create_module("t", "text");
    std::string seen;
    const char* seen_c = nullptr;
    shell.add(
        [&](std::string_view a, const char* b) -> uint8_t {
            seen.assign(a.data(), a.size());
            seen_c = b;
            return 0;
        },
        "two", "views", "t");
    assert(shell.run_command_line("t -two   spaced out  , second") == RESULT_OK);
    assert(seen == "spaced out");
    assert(seen_c != nullptr);  // pointed into the (now gone) line buffer
}

// The headline: once registered, running commands allocates nothing -- not for
// the line, the arguments, nor the response (string_view output callback, no
// std::string parameter, colour off).
void test_running_a_command_allocates_nothing() {
    TinyShell shell;
    std::size_t bytes = 0;
    shell.set_output_callback([&bytes](std::string_view s) { bytes += s.size(); });
    shell.begin();
    shell.create_module("motion", "move");
    uint32_t last = 0;
    shell.add([&last](int8_t a, int8_t b, uint32_t ms) -> uint8_t {
        last = static_cast<uint32_t>(a + b) + ms;
        return 0;
    }, "drive", "drive", "motion");

    const std::size_t before = g_allocations;
    for (int i = 0; i < 100; ++i) {
        assert(shell.run_command_line("motion -drive 10, -5, 1000") == RESULT_OK);
        assert(shell.run_command_line("motion -drive 1, 300, 5") == RESULT_ERROR);  // range error
        assert(shell.run_command_line("nosuch -x") == MODULE_NOT_FOUND);           // help text
        assert(shell.run_command_line("help -modules") == RESULT_OK);
    }
    if (!shell_color_enabled()) {
        assert(g_allocations == before);
    }
    assert(last == 1005U);
    assert(bytes > 0);
}

// Registering N commands costs no allocation per command for the usual small
// capture -- the 1.x registry cloned every existing command on each add().
void test_registration_does_not_allocate() {
    const std::size_t before = g_allocations;
    TinyShell* shell = new TinyShell();  // one allocation: the shell object itself
    shell->create_module("m", "many");
    static const char* names[] = {"a", "b", "c", "d", "e", "f", "g", "h", "i", "j"};
    int counter = 0;
    for (const char* name : names) {
        assert(shell->add([&counter, shell]() -> uint8_t { ++counter; return 0; }, name, "n", "m") ==
               RESULT_OK);
    }
    assert(g_allocations == before + 1);
    delete shell;
}

// std::function / a big capture still works; it costs one allocation, once.
void test_big_callable_goes_to_the_heap_once() {
    TinyShell shell;
    shell.set_output_callback([](std::string_view) {});
    shell.create_module("big", "big");
    struct Payload {
        char bytes[128];
    } payload{};
    payload.bytes[0] = 42;
    const std::size_t before = g_allocations;
    assert(shell.add([payload]() -> uint8_t { return payload.bytes[0] == 42 ? 0 : 1; }, "p", "p",
                     "big") == RESULT_OK);
    assert(g_allocations == before + 1);
    std::function<uint8_t(uint16_t)> fn = [](uint16_t v) -> uint8_t { return v == 9 ? 0 : 1; };
    shell.add(fn, "fn", "std::function", "big");
    assert(shell.run_command_line("big -p") == RESULT_OK);
    assert(shell.run_command_line("big -fn 9") == RESULT_OK);
}

void test_capacity_and_dropped_registrations() {
    TinyShell shell;
    Capture out;
    shell.set_output_callback([&out](std::string_view s) { out(s); });
    shell.begin();
    std::size_t created = 0;
    for (std::size_t i = 0; i < TINYSHELL_MAX_MODULES + 3; ++i) {
        const std::string name = "mod" + std::to_string(i);  // std::string -> interned
        const std::uint8_t result = shell.create_module(name, "d");
        if (result == RESULT_OK) ++created;
    }
    assert(created < TINYSHELL_MAX_MODULES + 3);
    assert(shell.dropped_registrations() > 0);
    shell.run_command_line("help -modules");
    assert(contains(out.text, "registration(s) dropped"));
}

void test_line_too_long_and_nesting() {
    TinyShell shell;
    Capture out;
    shell.set_output_callback([&out](std::string_view s) { out(s); });
    shell.create_module("n", "nest");
    shell.add([&shell](std::string_view inner) -> uint8_t {
        return shell.run_command_line(inner);  // a command that runs another
    }, "run", "run", "n");
    shell.add([]() -> uint8_t { return 0; }, "ok", "ok", "n");
    assert(shell.run_command_line("n -run n -ok") == RESULT_OK);
    assert(reported_success(out.text, "n -ok: success\n"));

    const std::string huge(TINYSHELL_MAX_LINE + 1, 'x');
    assert(shell.run_command_line(huge) == RESULT_ERROR);
    assert(contains(out.text, "error: command line too long"));
}

// The 1.x callback shape (const std::string&) keeps working.
void test_legacy_string_callback() {
    TinyShell shell;
    std::string seen;
    shell.set_output_callback([&seen](const std::string& s) { seen += s; });
    shell.create_module("x", "x");
    shell.add([]() -> uint8_t { return 0; }, "y", "y", "x");
    shell.run_command_line(std::string("x -y"));
    assert(reported_success(shell_strip_sgr(seen), "x -y: success"));
}

// A response bigger than the output buffer arrives in several whole-line
// chunks, never with a line cut in half.
void test_long_output_is_chunked_on_line_boundaries() {
    TinyShell shell;
    std::vector<std::string> chunks;
    shell.set_output_callback([&chunks](std::string_view s) { chunks.emplace_back(s); });
    shell.begin();
    static std::string names[40];
    for (int i = 0; i < 40; ++i) {
        names[i] = "module_with_a_fairly_long_name_" + std::to_string(i);
        shell.create_module(names[i].c_str(), "and a description that takes some room");
    }
    chunks.clear();
    shell.run_command_line("help -modules");
    assert(chunks.size() >= 3);  // several help chunks (+ the status line)
    for (const std::string& chunk : chunks) {
        if (shell_color_enabled()) break;  // SGR resets land after the newline
        assert(!chunk.empty() && chunk.back() == '\n');
    }
}

// VERBOSE_OFF: every response is just the status code and a newline;
// HARD_VERBOSE_OFF: nothing at all.
void test_verbosity_modes() {
    TinyShell shell;
    Capture out;
    shell.set_output_callback([&out](std::string_view s) { out(s); });
    shell.create_module("v", "v");
    shell.add([]() -> uint8_t { return 7; }, "seven", "7", "v");
    assert(shell.run_command_line("v -seven") == 7);
#if defined(HARD_VERBOSE_OFF)
    assert(out.text.empty());
#elif defined(VERBOSE_OFF)
    assert(out.text == std::string("7") + '\n');
#else
    assert(out.text.find("v -seven: failed with status 7\n") != std::string::npos);
#endif
}

// TINYSHELL_QUIET_SUCCESS: a command that worked adds nothing to its own
// output; a failure is still reported.
void test_quiet_success() {
    TinyShell shell;
    Capture out;
    shell.set_output_callback([&out](std::string_view s) { out(s); });
    shell.create_module("q", "q");
    shell.add([]() -> uint8_t { return 0; }, "ok", "ok", "q");
    shell.add([]() -> uint8_t { return 2; }, "bad", "bad", "q");
    assert(shell.run_command_line("q -ok") == RESULT_OK);
#if defined(TINYSHELL_QUIET_SUCCESS)
    assert(out.text.empty());
#else
    assert(contains(out.text, "q -ok: success\n"));
#endif
    assert(shell.run_command_line("q -bad") == 2);
    assert(contains(out.text, "q -bad: failed with status 2\n"));
}

}  // namespace

int main() {
    test_basic_call_and_types();
    test_error_messages();
    test_help_and_completion();
    test_string_view_args_are_zero_copy();
    test_running_a_command_allocates_nothing();
    test_registration_does_not_allocate();
    test_big_callable_goes_to_the_heap_once();
    test_capacity_and_dropped_registrations();
    test_line_too_long_and_nesting();
    test_legacy_string_callback();
    test_long_output_is_chunked_on_line_boundaries();
    test_verbosity_modes();
    test_quiet_success();
    std::printf("test_tinyshell: all checks passed%s\n",
                shell_color_enabled() ? " (TINYSHELL_COLOR)" : "");
    return 0;
}
