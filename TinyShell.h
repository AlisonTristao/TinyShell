#ifndef TINY_SHELL_H
#define TINY_SHELL_H

#include <TableLinker/TableLinker.h>
#include <string>

using namespace std;

// **********************************
// *       Class of TinyShell       *
// **********************************

// The class receives a string type module -command args0, arg1, arg2, ... argsN
// and run the command with the args

// The command is a function pointer that is registered in the TableLinker
// The args are converted to the correct type and passed to the function pointer
// The args are passed as a void** pointer, so the function can receive any type of args
// The function pointer is registered in the TableLinker with the name and description
// The module is registered in the TableLinker with the name and description
// The module can have multiple functions registered in it  
// The module can be called with the call function
// you can create your type to convert the args to the correct type in the TableLinker.h 

class TinyShell {
    public:
        TinyShell() {}

        // helper
        string get_help(string module_name = "");

        // calls
        string run_line_command(string command);

        // adds
        template<typename... param>
        uint8_t add(uint8_t(*func)(param...), string name, string description, string module_name) {
            return table_linker.add_func_to_module(module_name, func, name, description);
        }
        uint8_t create_module(string mod_name, string mod_description);
    private:
        TableLinker table_linker;
        struct ParsedCommand {
            string module_name;
            string command_name;
            string args_str;
            size_t args_count;
        };

        // parsing
        ParsedCommand parse_command(const string& command);
        string validate_command(const ParsedCommand& cmd);
        void** convert_args(const ParsedCommand& cmd, const char** types, string& error_msg);

        // checks
        bool check_expected_types(string module_name, string func_name, size_t receive);
        bool check_module_name(string module_name);
        bool check_function_name(string module_name, string func_name);
        
        // gets
        string get_expected_types(string module_name, string func_name);

        // call
        uint8_t call(string module_name, string func_name, void** args = nullptr);
    };

#endif