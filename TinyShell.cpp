#include <TinyShell.h>

#include <ShellStyle.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
#include <exception>
#define TINYSHELL_CATCH_EXCEPTIONS 1
#endif

// Collects text in a fixed stack buffer and hands it to the output callback in
// chunks of whole lines -- a buffer that fills up flushes through its last
// newline, so one line is never split across two callback calls unless it is
// longer than the whole buffer on its own.
class TinyShell::Writer {
public:
    Writer(TinyShell& shell, ShellMsgClass msg_class, bool discard = false)
        : shell_(shell), class_(msg_class), discard_(discard) {}
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    ~Writer() { flush(); }

    void put(char c) {
        if (discard_) return;
        if (length_ == sizeof(buffer_)) drain();
        buffer_[length_++] = c;
    }

    void put(std::string_view text) {
        for (char c : text) put(c);
    }

    void put(const char* text) { put(std::string_view(text != nullptr ? text : "")); }

    void put_number(unsigned long value) {
        char digits[24];
        const int n = std::snprintf(digits, sizeof(digits), "%lu", value);
        if (n > 0) put(std::string_view(digits, static_cast<std::size_t>(n)));
    }

    void flush() {
        if (length_ != 0) shell_.emit(std::string_view(buffer_, length_), class_);
        length_ = 0;
    }

private:
    // Buffer full: emit up to and including its last newline, keep the rest.
    void drain() {
        std::size_t cut = length_;
        while (cut > 0 && buffer_[cut - 1] != '\n') --cut;
        if (cut == 0) cut = length_;  // one line longer than the buffer
        shell_.emit(std::string_view(buffer_, cut), class_);
        std::memmove(buffer_, buffer_ + cut, length_ - cut);
        length_ -= cut;
    }

    TinyShell& shell_;
    ShellMsgClass class_;
    bool discard_;
    char buffer_[TINYSHELL_OUTPUT_CAPACITY];
    std::size_t length_ = 0;
};

namespace {

// The status line / verbosity rules every response goes through. A non-zero
// status makes an otherwise-plain line read as an error; a line that already
// carries a keyword/prefix keeps its own class (ShellStyle).
[[maybe_unused]] ShellMsgClass response_class(std::uint8_t code) {
    return code == RESULT_OK ? ShellMsgClass::Output : ShellMsgClass::Error;
}

constexpr bool response_text_enabled() {
#if defined(HARD_VERBOSE_OFF) || defined(VERBOSE_OFF)
    return false;
#else
    return true;
#endif
}

// `\` escapes the next character, so `\,` is not a separator.
std::size_t find_next_unescaped_comma(const char* text, std::size_t length, std::size_t start) {
    bool escaped = false;
    for (std::size_t i = start; i < length; ++i) {
        if (escaped) {
            escaped = false;
        } else if (text[i] == '\\') {
            escaped = true;
        } else if (text[i] == ',') {
            return i;
        }
    }
    return length;
}

std::size_t count_unescaped_commas(const char* text, std::size_t length) {
    std::size_t count = 0;
    for (std::size_t pos = find_next_unescaped_comma(text, length, 0); pos < length;
         pos = find_next_unescaped_comma(text, length, pos + 1)) {
        ++count;
    }
    return count;
}

// `\,` -> `,` and `\\` -> `\`; any other escape stays as written. In place,
// returns the new length.
std::size_t unescape_in_place(char* text, std::size_t length) {
    std::size_t out = 0;
    for (std::size_t i = 0; i < length; ++i) {
        if (text[i] == '\\' && i + 1 < length && (text[i + 1] == ',' || text[i + 1] == '\\')) {
            text[out++] = text[++i];
        } else {
            text[out++] = text[i];
        }
    }
    return out;
}

std::string_view trim_view(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

bool starts_with(std::string_view text, std::string_view prefix) {
    return text.substr(0, prefix.size()) == prefix;
}

// "module -command: " -- the bash-style prefix of every message about one
// command ("bash: foo: command not found"). A template so it can take the
// private TinyShell::Writer without naming it.
template <typename Out>
void put_command_prefix(Out& out, std::string_view module_name, std::string_view command_name) {
    out.put(module_name);
    if (!command_name.empty()) {
        out.put(" -");
        out.put(command_name);
    }
    out.put(": ");
}

template <typename Out>
void put_spaces(Out& out, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) out.put(' ');
}

std::size_t c_length(const char* text) { return text != nullptr ? std::strlen(text) : 0; }

// Columns of the help listings: two-space indent, then each column padded to
// its widest entry plus this gap.
constexpr std::size_t kHelpIndent = 2;
constexpr std::size_t kHelpGap = 2;

// Width write_types() prints: "(" + codes joined by ", " + ")".
std::size_t types_width(const tinyshell::Command& command) {
    std::size_t width = 2;
    for (std::size_t i = 0; i < command.arity; ++i) {
        width += c_length(command.types[i]) + (i != 0 ? 2 : 0);
    }
    return width;
}

}  // namespace

void TinyShell::begin() {
    create_module("help", "Help module for listing available commands");
    add([this]() { return wrapper_help(); }, "modules", "List every module", "help");
    add([this]() { return wrapper_help_types(); }, "types", "List the argument type codes", "help");
}

void TinyShell::emit(std::string_view chunk, ShellMsgClass msg_class) {
    if (chunk.empty() || !output_callback) return;
    if constexpr (shell_color_enabled()) {
        const std::string colored = shell_colorize_block(chunk, msg_class);
        output_callback(colored);
    } else {
        (void)msg_class;
        output_callback(chunk);
    }
}

void TinyShell::write_module_list(Writer& out) {
    if (table_linker.module_count() == 0) {
        out.put("no modules available.\n");
        return;
    }
    std::size_t name_width = 0;
    for (std::size_t i = 0; i < table_linker.module_count(); ++i) {
        const std::size_t width = c_length(table_linker.module_at(i).name);
        if (width > name_width) name_width = width;
    }
    for (std::size_t i = 0; i < table_linker.module_count(); ++i) {
        const tinyshell::Module& module = table_linker.module_at(i);
        put_spaces(out, kHelpIndent);
        out.put(module.name);
        put_spaces(out, name_width - c_length(module.name) + kHelpGap);
        out.put(module.description);
        out.put('\n');
    }
}

void TinyShell::write_types(Writer& out, const tinyshell::Command& command) {
    out.put('(');
    for (std::size_t i = 0; i < command.arity; ++i) {
        if (i != 0) out.put(", ");
        out.put(command.types[i]);
    }
    out.put(')');
}

void TinyShell::write_module_help(Writer& out, std::size_t module) {
    const tinyshell::Module& entry = table_linker.module_at(module);
    out.put(entry.name);
    out.put(" - ");
    out.put(entry.description);
    out.put("\nusage: ");
    out.put(entry.name);
    out.put(" -<command> [arg, ...]\n\n");

    std::size_t name_width = 0;
    std::size_t type_width = 0;
    for (std::size_t i = 0; i < table_linker.command_count(); ++i) {
        const tinyshell::Command& command = table_linker.command_at(i);
        if (command.module != module) continue;
        const std::size_t name = c_length(command.name);
        const std::size_t types = types_width(command);
        if (name > name_width) name_width = name;
        if (types > type_width) type_width = types;
    }
    for (std::size_t i = 0; i < table_linker.command_count(); ++i) {
        const tinyshell::Command& command = table_linker.command_at(i);
        if (command.module != module) continue;
        put_spaces(out, kHelpIndent);
        out.put('-');
        out.put(command.name);
        put_spaces(out, name_width - c_length(command.name) + kHelpGap);
        write_types(out, command);
        put_spaces(out, type_width - types_width(command) + kHelpGap);
        out.put(command.description);
        out.put('\n');
    }
}

std::uint8_t TinyShell::wrapper_help() {
    Writer out(*this, ShellMsgClass::Help);
    write_module_list(out);
    if (table_linker.dropped_registrations() != 0) {
        out.put("warning: ");
        out.put_number(static_cast<unsigned long>(table_linker.dropped_registrations()));
        out.put(" registration(s) dropped -- raise TINYSHELL_MAX_MODULES / TINYSHELL_MAX_COMMANDS\n");
    }
    return RESULT_OK;
}

std::uint8_t TinyShell::wrapper_help_types() {
    Writer out(*this, ShellMsgClass::Help);
    out.put(get_types());
    return RESULT_OK;
}

std::uint8_t TinyShell::run_command_line(std::string_view command) {
    std::uint8_t status = RESULT_OK;

    // Every exit goes through here: the status line, verbosity rules applied.
    const auto respond = [this, &status](auto&& write_text) {
#if !defined(HARD_VERBOSE_OFF)
        Writer out(*this, response_class(status), !response_text_enabled());
        write_text(out);
        out.flush();
#if defined(VERBOSE_OFF)
        Writer code(*this, response_class(status));
        code.put_number(status);
        code.put('\n');
#endif
#else
        (void)write_text;
#endif
        return status;
    };

    if (command.size() > TINYSHELL_MAX_LINE) {
        status = RESULT_ERROR;
        return respond([&](Writer& out) {
            out.put("error: command line too long (");
            out.put_number(static_cast<unsigned long>(command.size()));
            out.put(" characters, max ");
            out.put_number(static_cast<unsigned long>(TINYSHELL_MAX_LINE));
            out.put(")\n");
        });
    }

    // Tokenized in place, in this call's own copy: arguments become
    // NUL-terminated slices of it, which is what lets a string_view / const
    // char* parameter point straight at its text.
    char line[TINYSHELL_MAX_LINE + 1];
    std::memcpy(line, command.data(), command.size());
    line[command.size()] = '\0';
    const std::string_view text(line, command.size());

    // Split the same way 1.x did: the module is everything up to the first
    // space; the command name starts after the first '-' and runs to the next
    // space; whatever follows that space is the comma-separated argument list.
    const std::size_t first_space = text.find(' ');
    const std::string_view module_name = text.substr(0, first_space);
    std::string_view command_name;
    char* args = nullptr;
    std::size_t args_length = 0;
    std::size_t args_count = 0;
    const std::size_t dash = text.find('-');
    if (dash != std::string_view::npos) {
        std::size_t name_end = text.find(' ', dash);
        if (name_end == std::string_view::npos) name_end = text.size();
        command_name = text.substr(dash + 1, name_end - dash - 1);
        if (name_end < text.size()) {
            args = line + name_end + 1;
            args_length = text.size() - name_end - 1;
            args_count = args_length == 0 ? 0 : 1 + count_unescaped_commas(args, args_length);
        }
    }

    const std::size_t module = table_linker.find_module(module_name);
    if (module == TableLinker::kNotFound) {
        status = MODULE_NOT_FOUND;
        return respond([&](Writer& out) {
            out.put(module_name);
            out.put(": module not found\n\n");
            write_module_list(out);
        });
    }

    const std::size_t index = table_linker.find_command(module, command_name);
    if (index == TableLinker::kNotFound) {
        status = FUNCTION_NOT_FOUND;
        return respond([&](Writer& out) {
            put_command_prefix(out, module_name, command_name);
            // A bare module name asks for its help; anything else was a typo.
            out.put(command_name.empty() ? "missing command\n\n" : "command not found\n\n");
            write_module_help(out, module);
        });
    }

    const tinyshell::Command& entry = table_linker.command_at(index);
    if (args_count != entry.arity) {
        status = RESULT_ERROR;
        return respond([&](Writer& out) {
            put_command_prefix(out, module_name, command_name);
            out.put("invalid number of arguments, expected ");
            write_types(out, entry);
            out.put('\n');
        });
    }

    char* argv[TINYSHELL_MAX_ARGS > 0 ? TINYSHELL_MAX_ARGS : 1] = {};
    std::size_t pos = 0;
    for (std::size_t i = 0; i < args_count; ++i) {
        const std::size_t next = find_next_unescaped_comma(args, args_length, pos);
        char* token = args + pos;
        std::size_t length = next - pos;
        while (length != 0 && token[0] == ' ') {
            ++token;
            --length;
        }
        while (length != 0 && token[length - 1] == ' ') --length;
        length = unescape_in_place(token, length);
        token[length] = '\0';  // at most the separator's own slot
        argv[i] = token;
        pos = next + 1;
    }

    tinyshell::ConvError conversion;
#if defined(TINYSHELL_CATCH_EXCEPTIONS)
    const char* thrown = nullptr;
    try {
        status = table_linker.call(index, argv, &conversion);
    } catch (const std::exception& e) {
        status = RESULT_ERROR;
        thrown = e.what();
    } catch (...) {
        status = RESULT_ERROR;
        thrown = "unknown exception";
    }
    if (thrown != nullptr) {
        return respond([&](Writer& out) {
            put_command_prefix(out, module_name, command_name);
            out.put("error: ");
            out.put(thrown);
            out.put('\n');
        });
    }
#else
    status = table_linker.call(index, argv, &conversion);
#endif

    if (conversion.detail != nullptr) {
        status = RESULT_ERROR;
        const char* code = entry.types[conversion.index];
        return respond([&](Writer& out) {
            put_command_prefix(out, module_name, command_name);
            out.put("invalid argument '");
            out.put(argv[conversion.index]);
            out.put("' for type '");
            out.put(code);
            out.put("': ");
            out.put(conversion.detail);
            out.put('\n');
        });
    }

#if defined(TINYSHELL_QUIET_SUCCESS)
    // Unix convention: a command that worked says nothing more than its own
    // output. Failures are still reported below.
    if (status == RESULT_OK) return status;
#endif
    return respond([&](Writer& out) {
        put_command_prefix(out, module_name, command_name);
        if (status != RESULT_OK) {
            out.put("failed with status ");
            out.put_number(status);
            out.put('\n');
        } else {
            out.put("success\n");
        }
    });
}

std::vector<std::string> TinyShell::complete_line(std::string_view partial_command,
                                                  std::size_t max_results) {
    std::vector<std::string> completions;
    if (max_results == 0) {
        return completions;
    }

    const auto push_completion = [&](std::string candidate) {
        for (const std::string& existing : completions) {
            if (existing == candidate) return;
        }
        if (completions.size() < max_results) completions.push_back(std::move(candidate));
    };

    const auto complete_modules = [&](std::string_view prefix) {
        for (std::size_t i = 0; i < table_linker.module_count() && completions.size() < max_results;
             ++i) {
            const std::string_view name = table_linker.module_at(i).name;
            if (!name.empty() && starts_with(name, prefix)) {
                push_completion(std::string(name) + " ");
            }
        }
    };

    const auto complete_commands = [&](std::size_t module, std::string_view prefix) {
        const std::string_view module_name = table_linker.module_at(module).name;
        for (std::size_t i = 0;
             i < table_linker.command_count() && completions.size() < max_results; ++i) {
            const tinyshell::Command& command = table_linker.command_at(i);
            if (command.module != module) continue;
            const std::string_view name = command.name;
            if (!name.empty() && starts_with(name, prefix)) {
                std::string candidate(module_name);
                candidate += " -";
                candidate += name;
                candidate += ' ';
                push_completion(std::move(candidate));
            }
        }
    };

    const std::string_view input = trim_view(partial_command);
    if (input.empty()) {
        complete_modules("");
        return completions;
    }

    const std::size_t first_space = input.find(' ');
    if (first_space == std::string_view::npos) {
        complete_modules(input);
        return completions;
    }

    const std::string_view module_part = trim_view(input.substr(0, first_space));
    const std::string_view rest = trim_view(input.substr(first_space + 1));
    if (module_part.empty()) {
        complete_modules("");
        return completions;
    }

    const std::size_t module = table_linker.find_module(module_part);
    if (module == TableLinker::kNotFound) {
        complete_modules(module_part);
        return completions;
    }
    if (rest.empty()) {
        complete_commands(module, "");
        return completions;
    }
    if (rest[0] != '-') {
        complete_commands(module, rest);
        return completions;
    }
    if (rest.find(' ', 1) != std::string_view::npos) {
        return completions;
    }
    complete_commands(module, rest.substr(1));
    return completions;
}
