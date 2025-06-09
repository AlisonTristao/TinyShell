#include "TableLinker.h"

function_manager::function_manager(size_t size) : size(size) {
    func_array = (size > 0) ? new unique_ptr<base_function>[size] : nullptr;
}

function_manager::~function_manager() {
    delete[] func_array;
}

void function_manager::resize(size_t new_size) {
    // allocate new array
    unique_ptr<base_function>* new_func_array = new unique_ptr<base_function>[new_size];

    // copy the old data
    size_t copy_size = (new_size < size) ? new_size : size;
    for (size_t i = 0; i < copy_size; ++i)
        new_func_array[i] = move(func_array[i]);

    // delete the old array
    delete[] func_array;

    // update the pointer and size
    func_array = new_func_array;
    size = new_size;
}

const char** function_manager::get_param_types(string name) {
    size_t idx = select(name);
    return get_param_types(idx);
}

const char** function_manager::get_param_types(size_t idx) {
    if (check_index(idx)) return nullptr;
    return func_array[idx]->get_param_types();
}

size_t function_manager::get_param_size(size_t idx) {
    if (check_index(idx)) return FUNCTION_NOT_FOUND;
    return func_array[idx]->get_size();
}

string function_manager::get_name(size_t idx) {
    if (check_index(idx)) return "";
    return func_array[idx]->get_name();
}

string function_manager::get_description(size_t idx) {
    if (check_index(idx)) return "";
    return func_array[idx]->get_description();
}

string function_manager::get_expected_types_str(string name) {
    size_t idx = select(name);
    if (check_index(idx)) return "";
    return get_expected_types_str(idx);
}

string function_manager::get_expected_types_str(size_t idx) {
    if (check_index(idx)) return "";
    
    // get the expected types from the function
    const char** types = func_array[idx]->get_param_types();
    size_t size_param = func_array[idx]->get_size();

    // concatenate the types into a string
    string expected_types = "(";
    for (size_t i = 0; i < size_param; i++) {
        expected_types += types[i];
        if (i < (size_param - 1))
            expected_types += ", ";
    }
    expected_types += ")";

    return expected_types;
}

string function_manager::get_all() {
    string text = "";
    for (size_t i = 0; i < size; i++)
        text += "-" + func_array[i]->get_name() + " " + get_expected_types_str(i) + " => " + func_array[i]->get_description() + "\n";
    return text.empty() ? "no functions available.\n" : text;
}

bool function_manager::check_index(size_t idx) {
    return (idx >= size);
}

bool function_manager::check_name(string name) {
    size_t idx = select(name);
    return !check_index(idx);
}

uint8_t function_manager::call(size_t idx, void** args) {
    // check the item index
    if (check_index(idx)) return FUNCTION_NOT_FOUND;

    // call the function and return the result
    return func_array[idx]->call(args);
}

uint8_t function_manager::call(size_t idx) {
    return call(idx, nullptr);
}

uint8_t function_manager::call(string name, void** args) {
    size_t idx = select(name);
    if (check_index(idx)) return MODULE_NOT_FOUND;
    return call(idx, args);
}

size_t function_manager::select(string name) {
    for (size_t i = 0; i < size; i++)
        if (func_array[i]->get_name() == name)
            return i;
    return -1;
}

bool function_manager::check_expected_types(string name, size_t receive) {
    size_t idx = select(name);
    if (check_index(idx)) return false; // Function not found
    return (receive == func_array[idx]->get_size());
}

TableLinker::TableLinker(size_t table_size) : size(table_size) {
    if (size == 0) {
        commands_array = nullptr;
        module_name = nullptr;
        module_description = nullptr;
        return;
    }
    commands_array = new function_manager[size];
    module_name = new string[size];
    module_description = new string[size];
}

TableLinker::~TableLinker() {
    delete[] commands_array;
    delete[] module_name;
    delete[] module_description;
}

void TableLinker::resize(size_t new_size) {
    // alocate new arrays
    function_manager* new_commands = new function_manager[new_size];
    string* new_names = new string[new_size];
    string* new_descriptions = new string[new_size];

    // copy the old data
    size_t copy_size = (new_size < size) ? new_size : size;
    for (size_t i = 0; i < copy_size; ++i) {
        new_commands[i] = commands_array[i];
        new_names[i] = module_name[i];
        new_descriptions[i] = module_description[i];
    }

    // liberate the memory of the old arrays
    delete[] commands_array;
    delete[] module_name;
    delete[] module_description;

    // update the pointers and the new size
    commands_array = new_commands;
    module_name = new_names;
    module_description = new_descriptions;
    size = new_size;
}

uint8_t TableLinker::create_module(string mod_name, string mod_description) {
    // Check if the module already exists
    if (check_module_name(mod_name)) return MODULE_NOT_FOUND; // Module already exists
    // Find the next available index
    return create_module(size, mod_name, mod_description);
}

string TableLinker::get_all_module(string name) {
    size_t idx = select_module(name);
    return get_all_module(idx);
}

string TableLinker::get_all() {
    string text = "";
    for (size_t i = 0; i < size; i++)
        text += module_name[i] + " => " + module_description[i] + "\n";
    return text.empty() ? "no modules available.\n" : text;
}

uint8_t TableLinker::call(string module_name, string func_name) {
    size_t mod_idx = select_module(module_name);
    if (check_index(mod_idx)) return MODULE_NOT_FOUND; // Module not found
    return commands_array[mod_idx].call(func_name);
}

uint8_t TableLinker::create_module(size_t idx, string mod_name, string mod_description) {
    // Resize the array if necessary
    if (idx == size) resize(size + 1);

    if (check_index(idx)) return MODULE_NOT_FOUND; // Index out of bounds

    // Set the module name and description
    module_name[idx] = mod_name;
    module_description[idx] = mod_description;
    return RESULT_OK;
}

uint8_t TableLinker::call(string module_name, string func_name, void** args) {
    size_t mod_idx = select_module(module_name);
    if (check_index(mod_idx)) return MODULE_NOT_FOUND; // Module not found
    return commands_array[mod_idx].call(func_name, args);
}

bool TableLinker::check_module_name(string name) {
    size_t idx = select_module(name);
    return !check_index(idx);
}

bool TableLinker::check_index(size_t idx) {
    return (idx >= size);
}

size_t TableLinker::select(string name) {
    for (size_t i = 0; i < size; i++)
        if (commands_array[i].get_name(i) == name)
            return i;
    return RESULT_ERROR;
}

size_t TableLinker::select_module(string name) {
    for (size_t i = 0; i < size; i++)
        if (module_name[i] == name)
            return i;
    return RESULT_ERROR;
}

string TableLinker::get_all_module(size_t idx) {
    if (check_index(idx)) return "module not found.\n";
    string text =   module_name[idx] + ": " +
                    module_description[idx] + "\n" + 
                    commands_array[idx].get_all();
    return text;
}

bool TableLinker::check_function_name(string module_name, string func_name) {
    size_t mod_idx = select_module(module_name);
    if (check_index(mod_idx)) return false; // Module not found
    return commands_array[mod_idx].check_name(func_name);
}

string TableLinker::get_expected_types_str(string module_name, string func_name) {
    size_t mod_idx = select_module(module_name);
    if (check_index(mod_idx)) return ""; // Module not found
    return commands_array[mod_idx].get_expected_types_str(func_name);
}

const char** TableLinker::get_param_types(string module_name, string func_name) {
    size_t mod_idx = select_module(module_name);
    if (check_index(mod_idx)) return nullptr; // Module not found
    return commands_array[mod_idx].get_param_types(func_name);
}

bool TableLinker::check_expected_types(string module_name, string func_name, size_t receive) {
    size_t mod_idx = select_module(module_name);
    if (check_index(mod_idx)) return false; // Module not found
    return commands_array[mod_idx].check_expected_types(func_name, receive);
}