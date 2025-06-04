#include <TinyShell.h>

// return string
#define VERBOSE

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

string TinyShell::get_expected_types(string module_name, string func_name) {
    // check if the module exists
    if (!check_module_name(module_name))
        return "Module '" + module_name + "' not found.";

    // check if the function exists
    if (!check_function_name(module_name, func_name))
        return "Function '" + func_name + "' not found in module '" + module_name + "'.";

    // get the expected types for the function in the module
    string text = "Expected types for '" + func_name + "' in module '" + module_name + "': ";

    return text + table_linker.get_expected_types_str(module_name, func_name);
}

string TinyShell::run_line_command(string command) {
    // module -command args0, args1, args2, ... argsN
    // if command is not formatted correctly, return an error message
    //if (command.empty() || command.find('-') == string::npos)
    //    return "Invalid command format. Use 'module -command args0, args1, ...'";

    // module_name
    string module_name = command.substr(0, command.find(' '));

    // command_name (init with - and end with space)
    size_t command_start = command.find('-');
    size_t command_end = command.find(' ', command_start);
    string command_name = command.substr(command_start + 1, command_end - command_start - 1);

    // args
    string args_str = command.substr(command_end + 1);
    size_t args_count = 0;
    if (!args_str.empty())
        args_count = 1 + count(args_str.begin(), args_str.end(), ',');

    // check if module exists
    if (!check_module_name(module_name)){
        return "Module '" + module_name + "' not found." + "\n\n" +
        get_help();
    }

    // check if command exists
    if (!check_function_name(module_name, command_name)){
        return "Command '" + command_name + "' not found in module '" + module_name + "'" + "\n\n" +
        get_help(module_name);
    }

    // expect args count
    if (!check_expected_types(module_name, command_name, args_count))
        return get_expected_types(module_name, command_name);

    // get the char type array
    const char** types = table_linker.get_param_types(module_name, command_name);

    // separate args
    void** args = nullptr;
    if (args_count > 0) {
        args = new void*[args_count];
        size_t pos = 0;
        // split args_str by commas
        for (size_t i = 0; i < args_count; ++i) {
            // find the next comma or end of string
            size_t next_pos = args_str.find(',', pos);
            if (next_pos == string::npos) next_pos = args_str.length();
            string arg = args_str.substr(pos, next_pos - pos);
            // add the argument to the args array
            void* ptr = convert_type_char(arg.c_str(), types[i]);
            if (ptr == nullptr) {
                delete[] args;
                return "Error converting argument '" + arg + "' to type '" + types[i] + "'";
            }
            args[i] = ptr;
            pos = next_pos + 1;
        }
    }

    // call the command
    uint8_t result = call(module_name, command_name, args);
    if (result != 0)
        return "Error executing command '" + command_name + "' in module '" + module_name + "': " + to_string(result);

    else 
        return "Command '" + command_name + "' executed successfully in module '" + module_name + "'";
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