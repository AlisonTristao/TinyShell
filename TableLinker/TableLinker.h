#ifndef TABLE_LINKER_H
#define TABLE_LINKER_H

// errors
#define RESULT_OK  0
#define RESULT_ERROR 255
#define FUNCTION_NOT_FOUND 254
#define MODULE_NOT_FOUND 253

#include <memory>
#include <functional>
#include <typeindex>
#include <typeinfo>
#include <utility>
#include <string>
#include <cstring>

using namespace std;

// ****************************************
// * Templates to converte typeid to char *
// ****************************************

template<typename T>
constexpr const char* type_code() {
    if constexpr (is_same<T, uint8_t>::value)   return "u1";
    if constexpr (is_same<T, int8_t>::value)    return "i1";
    if constexpr (is_same<T, int32_t>::value)   return "i4";
    if constexpr (is_same<T, uint32_t>::value)  return "u4";
    if constexpr (is_same<T, float>::value)     return "f4";
    if constexpr (is_same<T, double>::value)    return "f8";
    if constexpr (is_same<T, char>::value)      return "c1";
    if constexpr (is_same<T, string>::value)    return "s0";
    return "??";  // Unknown type
}

inline void* convert_type_char(const char* data, const char* type_code) {
    if (strcmp(type_code, "u1") == 0) return new uint8_t(static_cast<uint8_t>(atoi(data)));
    if (strcmp(type_code, "i4") == 0) return new int32_t(atoi(data));
    if (strcmp(type_code, "f4") == 0) return new float(atof(data));
    if (strcmp(type_code, "f8") == 0) return new double(atof(data));
    if (strcmp(type_code, "c1") == 0) return new char(data[0]);
    if (strcmp(type_code, "s0") == 0) return new string(data);
    throw invalid_argument("Unknown type code");
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
        const char** get_param_types() { return param_types; };
        size_t get_size() const { return size; }
        string get_name() { return name; }
        string get_description() { return description; }
    protected:
        const char** param_types = nullptr;
        string name;
        string description;
        size_t size;
};

// template class to save functions with variable parameters
template<typename... param>
class class_function : public base_function {
    public:
        class_function(function<uint8_t(param...)> func_ptr, string func_name, string func_description) : func(func_ptr) {
            size = sizeof...(param);
            param_types = new const char*[size];
            name = func_name;
            description = func_description;

            // Convert each parameter type to a single identifying character
            size_t i = 0;
            using expander = int[];
            (void)expander{0, ((param_types[i++] = type_code<param>()), 0)...};
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
        function_manager() : func_array(nullptr), size(0) {}
        function_manager(size_t size);
        ~function_manager();

        // gets
        const char** get_param_types(string name);
        size_t get_param_size(size_t idx);
        string get_name(size_t idx);
        string get_description(size_t idx);
        string get_all();
        string get_expected_types_str(string name);

        // checks
        bool check_name(string name);
        bool check_expected_types(string name, size_t receive);

        // calls
        uint8_t call(string name, void** args = nullptr);

        template<typename... param>
        uint8_t add(uint8_t(*func)(param...), string name, string description) {
            return add(size, func, name, description);
        }

    private:
        // function pointers
        unique_ptr<base_function>* func_array;
        size_t size;

        size_t select(string name);
        void resize(size_t size);
        bool check_index(size_t idx);
        uint8_t call(size_t idx, void** args);
        const char** get_param_types(size_t idx);
        string get_expected_types_str(size_t idx);
        uint8_t call(size_t idx);

        template<typename... param>
        uint8_t add(size_t idx, uint8_t(*func)(param...), string name, string description) {
            // resize the array if necessary
            if (idx == size) resize(size + 1);

            // check vector limit
            if (check_index(idx)) return RESULT_ERROR;

            // add into vector
            func_array[idx] = make_unique<class_function<param...>>(function<uint8_t(param...)>(func), name, description);
            return RESULT_OK;
        }

};

// **********************************
// *      Class of TableLinker      *
// **********************************

class TableLinker {
    public:
        TableLinker() : commands_array(nullptr), module_name(nullptr), module_description(nullptr), size(0) {}
        TableLinker(size_t table_size);
        ~TableLinker();

        // gets
        string get_all();
        uint8_t create_module(string mod_name, string mod_description);
        string get_all_module(string name);
        string get_expected_types_str(string module_name, string func_name);

        // checks
        bool check_module_name(string name);
        bool check_function_name(string module_name, string func_name);
        bool check_expected_types(string module_name, string func_name, size_t receive);
        const char** get_param_types(string module_name, string func_name);

        // calls
        uint8_t call(string module_name, string func_name);
        uint8_t call(string module_name, string func_name, void** args);

        template<typename... param>
        uint8_t add_func_to_module(string name, uint8_t(*func)(param...), string func_name, string func_description) {
            size_t mod_idx = select_module(name);
            if (check_index(mod_idx)) return MODULE_NOT_FOUND; // Module not found
            return commands_array[mod_idx].add(func, func_name, func_description);
        }
    private:
        function_manager* commands_array;
        string* module_name;
        string* module_description;
        size_t size;
    
        bool check_index(size_t idx);
        void resize(size_t new_size);
        size_t select(string name);
        size_t select_module(string name);
        string get_all_module(size_t idx);
        uint8_t create_module(size_t idx, string mod_name, string mod_description);

        template<typename... param>
        uint8_t add_func_to_module(size_t mod_idx, uint8_t(*func)(param...), string func_name, string func_description) {
            return commands_array[mod_idx].add(func, func_name, func_description);
        }
};

# endif