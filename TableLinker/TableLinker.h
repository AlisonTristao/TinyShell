#ifndef TABLE_LINKER_H
#define TABLE_LINKER_H

#include <memory>
#include <functional>
#include <typeindex>
#include <typeinfo>
#include <utility>

using namespace std;

// ****************************************
// * templates to converte typeid to char *
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
// * class to create generic functions *
// *************************************

// abstract class to create the generics
class base_function {
    public:
        virtual ~base_function() {
            delete[] param_types;
        }
        virtual uint8_t call(void** args) = 0;
        virtual const char* get_param_types() const = 0;
        virtual size_t get_size() const = 0;
    protected:
        char* param_types;
        size_t size;
};

// template class to save functions with variable parameters
template<typename... param>
class class_function : public base_function {
    public:
        class_function(function<uint8_t(param...)> f) : func(f) {
            size = sizeof...(param);
            param_types = new char[size];
            
            // convert tipe_id to char - gnu++ 17
            size_t i = 0;
            using expander = int[];
            (void)expander{0, ((param_types[i++] = char_type<param>()), 0)...};
        }

        uint8_t call(void** args) override {
            return callImpl(args, index_sequence_for<param...>{});
        }

        const char* get_param_types() const override {
            return param_types;
        }

        size_t get_size() const override {
            return size;
        }

    private:
        template<size_t... Is>
        uint8_t callImpl(void** args, index_sequence<Is...>) {
            return func(*reinterpret_cast<typename remove_reference<param>::type*>(args[Is])...);
        }
        function<uint8_t(param...)> func;
};

// class to save the pointers
class function_manager {
    // limit to functions pointers
    unique_ptr<base_function>* func_array;
    size_t size;

    public:
        function_manager(size_t s) : size(s) {
            func_array = new unique_ptr<base_function>[size];
        }

        template<typename... param>
        uint8_t add(uint8_t idx, uint8_t(*f)(param...)) {
            // check vector limit
            if (check_index(idx)) return 255;

            // add into vector
            func_array[idx] = make_unique<class_function<param...>>(function<uint8_t(param...)>(f));
            return 0;
        }

        uint8_t call(int idx, void** args) {
            // check the intem index
            if (check_index(idx)) return 255;

            // call the function and return the result
            return func_array[idx]->call(args);
        }

        const char* get_param_types(uint8_t idx) const {
            return func_array[idx]->get_param_types();
        }

        size_t get_param_sizes(uint8_t idx) const {
            return func_array[idx]->get_size();
        }

        String get_expected_types_string(uint8_t idx){
            String text = "";
            const char* type_parameters = get_param_types(idx);
            for(uint8_t i; i < get_param_sizes(0); i++)
                text += type_parameters[i];
            return text;
        }

    private:
        bool check_index(uint8_t index) {
            return (index < 0 || index > size);
        }
};

// **********************************
// *      Class of TableLinker      *
// **********************************

class TableLinker {
    public:
        TableLinker();
        ~TableLinker();

        


    private:

};

# endif