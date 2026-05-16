#ifndef TINY_SHELL_H
#define TINY_SHELL_H

#include <TableLinker/TableLinker.h>
#include <string>
#include <vector>

using namespace std;

// Output modes:
// - Default: human-readable messages
// - VERBOSE: returns only numeric status codes ("<code>\n")
// - HARD_VERBOSE: returns no text (empty string)

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
// you can create custom argument converters in TableLinker/UserTypeConverters.h

/**
 * @brief TinyShell class provides a shell-like interface for managing modules and commands.
 */
class TinyShell {
    public:   
        /**
         * @brief Default constructor for TinyShell.
         */
        TinyShell() {}

        /** 
         * @brief initialize the shell, put the helper command in the help module
         */
        template<typename F>
        void begin(F&& callback) {
            // Converte o F para o seu OutCallback interno automaticamente
            output_callback = function<void(const string&)>(forward<F>(callback));
            create_module("help", "Help module for listing available commands");
            add(help_callback, "help", "Print this help message", "help");
            add(type_callback, "types", "Print all available types", "help");
            add(form_callback, "format", "Print the command format", "help");
        }

        void set_output_callback(function<void(const string&)> callback) {
            output_callback = callback;
        }

        /**
         * @brief run a command line
         * @param command: the command line to run
         * @return return the result of the function
         */
        uint8_t run_command_line(string command);

        /**
         * @brief Returns line-completion candidates for a partial shell input.
         *
         * Examples:
         * - "do" -> ["dongle "]
         * - "dongle -l" -> ["dongle -lcd ", "dongle -led ", ...]
         *
         * @param partial_command Partial command line being typed by the user.
         * @param max_results Maximum number of candidates to return.
         * @return List of full-line suggestions.
         */
        vector<string> complete_line(const string& partial_command, size_t max_results = 16);

        /**
         * @brief add a function to a module
         * @param func: the function to add
         * @param name: the name of the function
         * @param description: the description of the function
         * @param module_name: the name of the module
         * @return return the result of the function
         */

        template<typename F>
        uint8_t add(F&& func, string name, string description, string module_name) {
            return add(function(forward<F>(func)), name, description, module_name);
        }

        /**
         * @brief create a module
         * @param mod_name: the name of the module
         * @param mod_description: the description of the module
         * @return return the result of the function
         */
        uint8_t create_module(string mod_name, string mod_description);
    private:
        TableLinker table_linker;
        struct ParsedCommand {
            string module_name;
            string command_name;
            string args_str;
            size_t args_count;
        };

        /**
         * @brief Retrieve module information.
         * @param module_name: name of the module to get help, if empty return all modules
         * @return return all items inside the module
         */
        string get_help(string module_name = "");

        /**
         * @brief Parses a command string into its components.
         * @param command The command string to parse.
         * @return ParsedCommand structure containing parsed data.
         */
        ParsedCommand parse_command(const string& command);

        /**
         * @brief Validates a parsed command.
         * @param cmd The parsed command to validate.
         * @param error_code Numeric error code generated during validation.
         * @return Error message if invalid, empty string if valid.
         */
        string validate_command(const ParsedCommand& cmd, uint8_t& error_code);

        /**
         * @brief Converts command arguments to appropriate types.
         * @param cmd The parsed command containing arguments.
         * @param types Array of expected argument types.
         * @param error_msg Reference to error message string.
         * @return Array of converted arguments as void pointers.
         */
        void** convert_args(const ParsedCommand& cmd, const char** types, string& error_msg);

        /**
         * @brief Checks if the received argument types match the expected types for a function.
         * @param module_name Name of the module.
         * @param func_name Name of the function.
         * @param receive Number of received arguments.
         * @return True if types match, false otherwise.
         */
        bool check_expected_types(string module_name, string func_name, size_t receive);

        /**
         * @brief Checks if a module name exists.
         * @param module_name Name of the module to check.
         * @return True if module exists, false otherwise.
         */
        bool check_module_name(string module_name);

        /**
         * @brief Checks if a function name exists within a module.
         * @param module_name Name of the module.
         * @param func_name Name of the function to check.
         * @return True if function exists, false otherwise.
         */
        bool check_function_name(string module_name, string func_name);

        /**
         * @brief Gets the expected argument types for a function in a module.
         * @param module_name Name of the module.
         * @param func_name Name of the function.
         * @return String describing expected types.
         */
        string get_expected_types(string module_name, string func_name);

        /**
         * @brief Calls a function within a module with given arguments.
         * @param module_name Name of the module.
         * @param func_name Name of the function.
         * @param args Arguments to pass to the function.
         * @return Result of the function call.
         */
        uint8_t call(string module_name, string func_name, void** args = nullptr);

        // lamdas to creates generic callbacks for the help command
        function<void(const string&)> output_callback = [this](const string& output) { (void) output; /* no-op */ };
        function<uint8_t(const string&)> help_callback = [this](const string& module_name) { 
            output_callback(get_help(module_name));
            return RESULT_OK; 
        };
        function<uint8_t(const string&)> type_callback = [this](const string& none) { 
            output_callback(get_types());
            return RESULT_OK; 
        };
        function<uint8_t(const string&)> form_callback = [this](const string& none) { 
            output_callback("module -command arg0, arg1, arg2, ... argN \n to use commas in arguments, escape them with a backslash (\\,)\n");
            return RESULT_OK; 
        };
    };

#endif