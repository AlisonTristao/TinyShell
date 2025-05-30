#ifndef TABLE_LINKER_H
#define TABLE_LINKER_H

#include <vector>
#include <memory>
#include <functional>
#include <typeindex>
#include <typeinfo>
#include <utility>

using namespace std;

// *************************************
// * class to create generic functions *
// *************************************

// abstract class to create the generics
class base_function {
public:
    virtual ~base_function() {}
    virtual uint8_t call(void** args) = 0;
    virtual const vector<type_index>& getParamTypes() const = 0;
};

// template class to save functions with variable parameters
template<typename... params>
class class_function : public base_function {
    function<uint8_t(params...)> func;
    vector<type_index> paramTypes;

public:
    class_function(function<uint8_t(params...)> f) : func(f) {
        paramTypes = { type_index(typeid(params))... };
    }

    uint8_t call(void** args) override {
        return callImpl(args, index_sequence_for<params...>{});
    }

    const vector<type_index>& getParamTypes() const override {
        return paramTypes;
    }

private:
    template<size_t... Is>
    uint8_t callImpl(void** args, index_sequence<Is...>) {
        return func(*reinterpret_cast<typename remove_reference<params>::type*>(args[Is])...);
    }
};

// class to save the pointers
class function_manager {
    // limit to functions pointers
    uint8_t limit = 255;
    vector<unique_ptr<base_function>> func_vector;
    public:
        template<typename... params>
        void add(uint8_t(*f)(params...)) {
            // check vector limit
            if (func_vector.size() == limit) return 253;

            // add into vector
            func_vector.push_back(make_unique<class_function<params...>>(function<uint8_t(params...)>(f)));
        }

        uint8_t call(int index, void** args) {
            // check the intem index
            if (index < 0 || index >= (int)func_vector.size()) return 255;

            // call the function and return the result
            return func_vector[index]->call(args);
        }

        const vector<type_index>& getParamTypes(int index) const {
            return func_vector[index]->getParamTypes();
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