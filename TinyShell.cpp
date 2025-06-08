#include <TinyShell.h>

string TinyShell::run_line_command(string command) {
    ParsedCommand cmd = parse_command(command);

    string validation_error = validate_command(cmd);
    if (!validation_error.empty())
        return validation_error;

    const char** types = table_linker.get_param_types(cmd.module_name, cmd.command_name);

    string conversion_error;
    void** args = convert_args(cmd, types, conversion_error);
    if (!conversion_error.empty())
        return conversion_error;

    uint8_t result = call(cmd.module_name, cmd.command_name, args);

    if (args) delete[] args;

    if (result != 0)
        return "Error executing command '" + cmd.command_name + "' in module '" + cmd.module_name + "': " + to_string(result);

    return "Command '" + cmd.command_name + "' executed successfully in module '" + cmd.module_name + "'";
}

size_t count_commas(const string& s) {
    size_t count = 0;
    for (char c : s) if (c == ',') ++count;
    return count;
}

TinyShell::ParsedCommand TinyShell::parse_command(const string& command) {
    ParsedCommand result;

    size_t space_pos = command.find(' ');
    result.module_name = (space_pos == string::npos) ? command : command.substr(0, space_pos);

    size_t command_start = command.find('-');
    if (command_start == string::npos) {
        result.command_name = "";
        result.args_str = "";
        result.args_count = 0;
        return result;
    }

    size_t command_end = command.find(' ', command_start);
    if (command_end == string::npos) command_end = command.length();

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

string TinyShell::validate_command(const ParsedCommand& cmd) {
    if (!check_module_name(cmd.module_name))
        return "Module '" + cmd.module_name + "' not found.\n\n" + get_help();

    if (!check_function_name(cmd.module_name, cmd.command_name))
        return "Command '" + cmd.command_name + "' not found in module '" + cmd.module_name + "'\n\n" +
               get_help(cmd.module_name);

    if (!check_expected_types(cmd.module_name, cmd.command_name, cmd.args_count))
        return get_expected_types(cmd.module_name, cmd.command_name);

    return "";
}

void** TinyShell::convert_args(const ParsedCommand& cmd, const char** types, string& error_msg) {
    if (cmd.args_count == 0) return nullptr;

    void** args = new void*[cmd.args_count];
    size_t pos = 0;

    for (size_t i = 0; i < cmd.args_count; ++i) {
        size_t next_pos = cmd.args_str.find(',', pos);
        if (next_pos == string::npos) next_pos = cmd.args_str.length();

        string arg = cmd.args_str.substr(pos, next_pos - pos);
        void* ptr = convert_type_char(arg.c_str(), types[i]);

        if (ptr == nullptr) {
            delete[] args;
            error_msg = "Error converting argument '" + arg + "' to type '" + types[i] + "'";
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