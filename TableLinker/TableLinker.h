#ifndef TABLE_LINKER_H
#define TABLE_LINKER_H

#include <memory>
#include <functional>
#include <typeindex>
#include <typeinfo>
#include <utility>

using namespace std;

// ****************************************
// * Templates to converte typeid to char *
// ****************************************

template<typename T>
constexpr char char_type() {
    if      constexpr (std::is_same<T, int>::value)     return 'i';
    else if constexpr (std::is_same<T, float>::value)   return 'f';
    else if constexpr (std::is_same<T, double>::value)  return 'd';
    else if constexpr (std::is_same<T, char>::value)    return 'c';
    else if constexpr (std::is_same<T, String>::value)  return 'S';
    else                                                return '?';  // UNKNOWN
}

// *************************************
// * Class to create generic functions *
// *************************************

// abstract class to create the generics
class base_function {
    public:
        virtual ~base_function() {
            delete[] param_types;
        }
        virtual uint8_t call(void** args) = 0;
        const char* get_param_types() { return param_types; };
        size_t get_size() const { return size; }
        String get_name() { return name; }
        String get_description() { return description; }
    protected:
        char* param_types;
        String name;
        String description;
        size_t size;
};

// template class to save functions with variable parameters
template<typename... param>
class class_function : public base_function {
    public:
        class_function(function<uint8_t(param...)> func_ptr, String func_name, String func_description) : func(func_ptr) {
            size = sizeof...(param);
            param_types = new char[size];
            name = func_name;
            description = func_description;

            // Convert each parameter type to a single identifying character
            size_t i = 0;
            using expander = int[];
            (void)expander{0, ((param_types[i++] = char_type<param>()), 0)...};
        }

        // This function is called to invoke the stored function
        uint8_t call(void** args) override {
            return callDispatch(args, index_sequence_for<param...>{});
        }

    private:
        // Specialization for functions with NO parameters
        uint8_t callDispatch(void**, index_sequence<>) {
            return func();  // Safe: function expects no arguments
        }

        // General case for functions with one or more parameters
        template<size_t... Is>
        uint8_t callDispatch(void** args, index_sequence<Is...>) {
            return func(this->template getArg<typename remove_reference<param>::type>(args[Is])...);
        }

        // Converts void* to the expected argument type
        // Returns default value if ptr is nullptr
        template<typename T>
        T getArg(void* ptr) {
            if (ptr == nullptr)
                return T{};  // Default-initialized value (e.g., 0, "", false)
            return *reinterpret_cast<T*>(ptr);
        }

        function<uint8_t(param...)> func;
};

// class to save the pointers
class function_manager {
    public:
        function_manager() {}
        function_manager(size_t size) : size(size) {
            func_array = new unique_ptr<base_function>[size];
        }

        ~function_manager(){
            delete[] func_array;
        }

        void set_size(size_t size) {
            this->size = size;
            func_array = new unique_ptr<base_function>[size];
        }

        template<typename... param>
        uint8_t add(uint8_t idx, uint8_t(*func)(param...), String name, String description) {
            // check vector limit
            if (check_index(idx)) return 255;

            // add into vector
            func_array[idx] = make_unique<class_function<param...>>(function<uint8_t(param...)>(func), name, description);
            return 0;
        }

        const char* get_param_types(uint8_t idx) const {
            return func_array[idx]->get_param_types();
        }

        size_t get_param_size(uint8_t idx) const {
            return func_array[idx]->get_size();
        }

        String get_name(uint8_t idx) {
            return func_array[idx]->get_name();
        }

        String get_description(uint8_t idx) {
            return func_array[idx]->get_description();
        }

        String get_expected_types_string(uint8_t idx){
            String text = "";
            const char* type_parameters = get_param_types(idx);
            for(uint8_t i; i < get_param_size(idx); i++)
                text += type_parameters[i];
            return text;
        }

        String get_all() {
            String text = "";
            for (uint8_t i = 0; i < size; i++)
                text += func_array[i]->get_name() + " => " + func_array[i]->get_description() + "\n";
            return text;
        }

        uint8_t call(int idx) {
            // call the function and return the result
            return call(idx, nullptr);
        }

        uint8_t call(String name) {
            return call(select(name));
        }

        uint8_t call(String name, void** args) {
            return call(select(name), args);
        }

        uint8_t select(String name) {
            for (uint8_t i = 0; i < size; i++)
            if (func_array[i]->get_name() == name)
                return i;
            return -1;
        }

        bool check_name(String name) {
            for (uint8_t i = 0; i < size; i++)
                if (func_array[i]->get_name() == name)
                    return true;
            return false;
        }
    private:
        // limit to functions pointers
        unique_ptr<base_function>* func_array;
        size_t size;

        bool check_index(uint8_t index) {
            return (index < 0 || index >= size);
        }

        uint8_t call(int idx, void** args) {
            // check the intem index
            if (check_index(idx)) return 255;

            // call the function and return the result
            try {
                return func_array[idx]->call(args);
            } catch (const std::exception& e) {
                return 202;
            } catch (...) {
                return 203;
            }
        }
};

// **********************************
// *      Class of TableLinker      *
// **********************************

class TableLinker {
    public:
        TableLinker(size_t size) : size(size) {
            commands_array = new function_manager[size];
            module_name = new String[size];
            module_description = new String[size];
        }

        ~TableLinker() {
            delete[] commands_array, module_name, module_description;
        }

        uint8_t create_module(uint8_t idx, size_t mod_size, String mod_name, String mod_description) {
            if (check_index(idx)) return 255;
            commands_array[idx].set_size(mod_size);
            module_name[idx] = mod_name;
            module_description[idx] = mod_description;
        }

        template<typename... param>
        uint8_t add_func_to_module(size_t mod_idx, uint8_t(*func)(param...), size_t func_idx, String func_name, String func_description) {
            return commands_array[mod_idx].add(func_idx, func, func_name, func_description);
        }

        String get_all() {
            String text = "";
            for (uint8_t i = 0; i < size; i++) 
                text += module_name[i] + " => " + module_description[i] + "\n";
            return text;
        }

        String get_all_module(uint8_t idx) {
            if (check_index(idx)) return "255";
                return commands_array[idx].get_all();
        }

    private:
        bool check_index(uint8_t index) {
            return (index < 0 || index >= size);
        }
        function_manager* commands_array;
        String* module_name;
        String* module_description;
        size_t size;
};

# endif