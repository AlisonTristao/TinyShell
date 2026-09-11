#include <TinyShell.h>

#include <ShellStyle.h>

#include <algorithm>
#include <cctype>

namespace {

string format_shell_response(uint8_t code, const string& verbose_text) {
#if defined(HARD_VERBOSE_OFF)
    (void)code;
    (void)verbose_text;
    return "";
#elif defined(VERBOSE_OFF)
    return to_string(code) + "\n";
#else
    // A non-zero status makes an otherwise-plain line read as an error; a line
    // that already carries a keyword/prefix keeps its own class. With
    // TINYSHELL_COLOR undefined this returns verbose_text byte-for-byte.
    return shell_colorize_block(verbose_text,
                                code == RESULT_OK ? ShellMsgClass::Output : ShellMsgClass::Error);
#endif
}

string trim_copy(const string& text) {
    const auto first = find_if_not(text.begin(), text.end(), [](unsigned char ch) {
        return isspace(ch) != 0;
    });

    if (first == text.end()) {
        return "";
    }

    const auto last = find_if_not(text.rbegin(), text.rend(), [](unsigned char ch) {
        return isspace(ch) != 0;
    }).base();

    return string(first, last);
}

bool starts_with(const string& text, const string& prefix) {
    if (prefix.length() > text.length()) {
        return false;
    }

    return equal(prefix.begin(), prefix.end(), text.begin());
}

} // namespace

uint8_t TinyShell::run_command_line(string command) {
    // remove leading and trailing whitespace
    ParsedCommand cmd = parse_command(command);
    uint8_t status_code = RESULT_OK;

    // verify if the command is valid. A bad/incomplete command (e.g. a bare
    // module name) falls through to get_help(), which concatenates a
    // std::string per registered command in that module -- on a module with
    // many commands, under a thin/fragmented heap, that allocation can fail.
    // Uncaught, that used to abort() the whole process; caught here, it is
    // just another RESULT_ERROR response, same as a failed call() below.
    string validation_text;
    try {
        validation_text = validate_command(cmd, status_code);
    } catch (const exception& e) {
        status_code = RESULT_ERROR;
        validation_text = string(e.what()) + " " + string(__FUNCTION__) + " " + string(__FILE__) + ":" + to_string(__LINE__);
    } catch (...) {
        status_code = RESULT_ERROR;
        validation_text = "Unknown error occurred in " + string(__FUNCTION__) + " " + string(__FILE__) + ":" + to_string(__LINE__);
    }

    if (status_code != RESULT_OK) {
        output_callback(format_shell_response(status_code, validation_text));
        return status_code;
    }

    const char** types = table_linker.get_param_types(cmd.module_name, cmd.command_name);
    string result_text;
    void** args = nullptr;

    args = convert_args(cmd, types, result_text);

    // if the args conversion failed, keep RESULT_ERROR and skip execution
    if (!result_text.empty()) {
        status_code = RESULT_ERROR;
    } else {
        try {
            status_code = call(cmd.module_name, cmd.command_name, args);

            if (status_code != RESULT_OK) {
                result_text = "Comando '" + cmd.command_name + "' do modulo '" + cmd.module_name + "' falhou com codigo de erro: " + to_string(status_code) + ".\n";
            } else {
                result_text = "Comando '" + cmd.command_name + "' do modulo '" + cmd.module_name + "' executado com sucesso.\n";
            }
        } catch (const exception& e) {
            status_code = RESULT_ERROR;
            result_text = string(e.what()) + " " + string(__FUNCTION__) + " " + string(__FILE__) + ":" + to_string(__LINE__);
        } catch (...) {
            status_code = RESULT_ERROR;
            result_text = "Unknown error occurred in " + string(__FUNCTION__) + " " + string(__FILE__) + ":" + to_string(__LINE__);
        }
    }

    // clean up converted arguments
    if (args != nullptr && types != nullptr) {
        for (size_t i = 0; i < cmd.args_count; ++i) {
            destroy_type_char(args[i], types[i]);
        }
    }

    // clean up the pointer array
    delete[] args;

    // return the result of the command execution
    output_callback(format_shell_response(status_code, result_text));
    return status_code;
}

vector<string> TinyShell::complete_line(const string& partial_command, size_t max_results) {
    vector<string> completions;
    if (max_results == 0) {
        return completions;
    }

    const auto push_completion = [&](const string& candidate) {
        if (candidate.empty()) {
            return;
        }

        const bool already_added = find(completions.begin(), completions.end(), candidate) != completions.end();
        if (!already_added && completions.size() < max_results) {
            completions.push_back(candidate);
        }
    };

    const auto complete_modules = [&](const string& module_prefix) {
        const size_t module_total = table_linker.module_count();
        for (size_t i = 0; i < module_total && completions.size() < max_results; ++i) {
            const string module_name = table_linker.module_name_at(i);
            if (module_name.empty()) {
                continue;
            }

            if (starts_with(module_name, module_prefix)) {
                push_completion(module_name + " ");
            }
        }
    };

    const auto complete_commands = [&](const string& module_name, const string& command_prefix) {
        const size_t command_total = table_linker.function_count(module_name);
        for (size_t i = 0; i < command_total && completions.size() < max_results; ++i) {
            const string command_name = table_linker.function_name_at(module_name, i);
            if (command_name.empty()) {
                continue;
            }

            if (starts_with(command_name, command_prefix)) {
                push_completion(module_name + " -" + command_name + " ");
            }
        }
    };

    const string input = trim_copy(partial_command);
    if (input.empty()) {
        complete_modules("");
        return completions;
    }

    const size_t first_space = input.find(' ');
    if (first_space == string::npos) {
        complete_modules(input);
        return completions;
    }

    const string module_part = trim_copy(input.substr(0, first_space));
    const string rest = trim_copy(input.substr(first_space + 1));

    if (module_part.empty()) {
        complete_modules("");
        return completions;
    }

    if (!check_module_name(module_part)) {
        complete_modules(module_part);
        return completions;
    }

    if (rest.empty()) {
        complete_commands(module_part, "");
        return completions;
    }

    if (rest[0] != '-') {
        complete_commands(module_part, rest);
        return completions;
    }

    const size_t command_end = rest.find(' ', 1);
    if (command_end != string::npos) {
        return completions;
    }

    const string command_part = rest.substr(1);
    complete_commands(module_part, command_part);
    return completions;
}

size_t count_commas(const string& s) {
    size_t count = 0;
    bool escaped = false;
    for (char c : s) {
        if (escaped) {
            escaped = false;
            continue;
        }

        if (c == '\\') {
            escaped = true;
            continue;
        }

        if (c == ',') {
            ++count;
        }
    }
    return count;
}

size_t find_next_unescaped_comma(const string& s, size_t start_pos) {
    bool escaped = false;
    for (size_t i = start_pos; i < s.length(); ++i) {
        const char c = s[i];
        if (escaped) {
            escaped = false;
            continue;
        }

        if (c == '\\') {
            escaped = true;
            continue;
        }

        if (c == ',') {
            return i;
        }
    }

    return string::npos;
}

string unescape_commas(const string& text) {
    string out;
    out.reserve(text.length());

    bool escaped = false;
    for (char c : text) {
        if (escaped) {
            if (c == ',' || c == '\\') {
                out.push_back(c);
            } else {
                out.push_back('\\');
                out.push_back(c);
            }
            escaped = false;
            continue;
        }

        if (c == '\\') {
            escaped = true;
            continue;
        }

        out.push_back(c);
    }

    if (escaped) {
        out.push_back('\\');
    }

    return out;
}

TinyShell::ParsedCommand TinyShell::parse_command(const string& command) {
    ParsedCommand result;

    // get the module name
    size_t space_pos = command.find(' ');
    result.module_name = (space_pos == string::npos) ? command : command.substr(0, space_pos);

    // verify if the module name is empty
    size_t command_start = command.find('-');
    if (command_start == string::npos) {
        result.command_name = "";
        result.args_str = "";
        result.args_count = 0;
        return result;
    }

    // find the command name and arguments
    size_t command_end = command.find(' ', command_start);
    if (command_end == string::npos) command_end = command.length();

    // extract the command name
    result.command_name = command.substr(command_start + 1, command_end - command_start - 1);

    if (command_end < command.length()) {
        result.args_str = command.substr(command_end + 1);
        result.args_count = result.args_str.empty() ? 0 : 1 + count_commas(result.args_str);
    } else {
        result.args_str = "";
        result.args_count = 0;
    }

    return result;
}

string TinyShell::validate_command(const ParsedCommand& cmd, uint8_t& error_code) {
    error_code = RESULT_OK;

    if (!check_module_name(cmd.module_name)) {
        error_code = MODULE_NOT_FOUND;
        return "Module '" + cmd.module_name + "' not found.\n\n" + get_help();
    }

    if (!check_function_name(cmd.module_name, cmd.command_name)) {
        error_code = FUNCTION_NOT_FOUND;
        return "Command '" + cmd.command_name + "' not found in module '" + cmd.module_name + "'\n\n" +
               get_help(cmd.module_name);
    }

    if (!check_expected_types(cmd.module_name, cmd.command_name, cmd.args_count)) {
        error_code = RESULT_ERROR;
        return "Falha ao converter os tipos, a funcao espera => " +
               get_expected_types(cmd.module_name, cmd.command_name) + "\n";
    }

    return "";
}

void** TinyShell::convert_args(const ParsedCommand& cmd, const char** types, string& error_msg) {
    if (cmd.args_count == 0 || types == nullptr) return nullptr;

    void** args = new void*[cmd.args_count]{};
    size_t pos = 0;

    // split the args_str by commas and convert each argument to the corresponding type
    for (size_t i = 0; i < cmd.args_count; ++i) {
        size_t next_pos = find_next_unescaped_comma(cmd.args_str, pos);
        if (next_pos == string::npos) next_pos = cmd.args_str.length();

        // get the argument substring
        string arg = cmd.args_str.substr(pos, next_pos - pos);

        // remove leading and trailing whitespace from the argument
        const size_t first = arg.find_first_not_of(' ');
        if (first == string::npos) {
            arg.clear();
        } else {
            arg.erase(0, first);
            arg.erase(arg.find_last_not_of(' ') + 1);
        }

        // decode escaped commas (\,) and escaped backslash (\\)
        arg = unescape_commas(arg);

        // convert the argument to the corresponding type
        void* ptr = nullptr;
        try {
            ptr = convert_type_char(arg.c_str(), types[i]);
            if (ptr == nullptr) {
                error_msg = "Error converting argument '" + arg + "' to type '" + types[i] + "'";
            }
        } catch (const exception& e) {
            error_msg = "Error converting argument '" + arg + "' to type '" + types[i] + "': " + string(e.what());
        } catch (...) {
            error_msg = "Unknown error converting argument '" + arg + "' to type '" + types[i] + "'";
        }

        if (!error_msg.empty()) {
            for (size_t j = 0; j < i; ++j) {
                destroy_type_char(args[j], types[j]);
            }
            delete[] args;
            return nullptr;
        }

        args[i] = ptr;
        pos = next_pos + 1;
    }

    return args;
}

string TinyShell::get_help(string module_name) {
    if (module_name.empty()) return table_linker.get_all();
    else return table_linker.get_all_module(module_name);
}

bool TinyShell::check_module_name(string module_name) {
    return table_linker.check_module_name(module_name);
}

bool TinyShell::check_function_name(string module_name, string func_name) {
    return table_linker.check_function_name(module_name, func_name);
}

string TinyShell::get_expected_types(string module_name, string func_name){
    return table_linker.get_expected_types_str(module_name, func_name);
}

uint8_t TinyShell::create_module(string mod_name, string mod_description) {
    return table_linker.create_module(mod_name, mod_description);
}

bool TinyShell::check_expected_types(string module_name, string command_name, size_t args_count) {
    // Check if the module and command exist
    return table_linker.check_expected_types(module_name, command_name, args_count);
}

uint8_t TinyShell::call(string module_name, string func_name, void** args) {
    return table_linker.call(module_name, func_name, args);
}
