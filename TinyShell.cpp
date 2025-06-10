#include <TinyShell.h>
#include <Arduino.h>

// define a macro to safely execute a command and catch exceptions
#define SAFE_EXEC(expr) [&]() -> string { \
    try { \
        return expr; \
    } catch (const exception& e) { \
        return string(e.what()) + " " + string(__FUNCTION__) + " " + string(__FILE__) + ":" + to_string(__LINE__); \
    } catch (...) { \
       return "Unknown error occurred in " + string(__FUNCTION__) + " " + string(__FILE__) + ":" + to_string(__LINE__); \
    } \
}();

string TinyShell::run_line_command(string command) {
    // remove leading and trailing whitespace
    ParsedCommand cmd = parse_command(command);

    // verify if the command is valid
    string validation_error = validate_command(cmd);
    if (!validation_error.empty())
        return validation_error;

    // check if the module exists
    const char** types = table_linker.get_param_types(cmd.module_name, cmd.command_name);
    string result_text;
    void** args = nullptr;

    args = convert_args(cmd, types, result_text);

    // if the args conversion failed, return the error message
    if (result_text.empty())

        // try to call the command with the converted arguments
        result_text = SAFE_EXEC([&]() -> string {
            // call the function with the converted arguments
            uint8_t result = call(cmd.module_name, cmd.command_name, args);

            // check the result of the command execution
            if (result != 0)
                return "Comando '" + cmd.command_name + "' do módulo '" + cmd.module_name + "' falhou com código de erro: " + to_string(result) + ".\n";

            return "Comando '" + cmd.command_name + "' do módulo '" + cmd.module_name + "' executado com sucesso.\n";
        }());

    // clean up the allocated memory
    delete[] args;
    delete[] types;
    
    // return the result of the command execution
    return result_text;
}

size_t count_commas(const string& s) {
    size_t count = 0;
    for (char c : s) if (c == ',') ++count;
    return count;
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
    if (cmd.args_count == 0 || types == nullptr) return nullptr;

    void** args = new void*[cmd.args_count];
    size_t pos = 0;

    // split the args_str by commas and convert each argument to the corresponding type
    for (size_t i = 0; i < cmd.args_count; ++i) {
        size_t next_pos = cmd.args_str.find(',', pos);
        if (next_pos == string::npos) next_pos = cmd.args_str.length();

        // get the argument substring
        string arg = cmd.args_str.substr(pos, next_pos - pos);

        // remove leading and trailing whitespace from the argument
        arg.erase(0, arg.find_first_not_of(' '));
        arg.erase(arg.find_last_not_of(' ') + 1);

        // convert the argument to the corresponding type using safe conversion
        void* ptr = nullptr;
        string result_text = SAFE_EXEC([&]() -> string {
            ptr = convert_type_char(arg.c_str(), types[i]);
            if (ptr == nullptr)
                error_msg = "Error converting argument '" + arg + "' to type '" + types[i] + "'";
            return "";
        }());

        if (!result_text.empty())
            // break the loop if an error occurred during conversion
            break;

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