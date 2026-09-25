#include "TableLinker.h"

namespace {

bool name_equals(const char* stored, std::string_view name) {
    const std::size_t length = std::strlen(stored);
    return length == name.size() && std::memcmp(stored, name.data(), length) == 0;
}

}  // namespace

TableLinker::~TableLinker() {
    for (std::size_t i = 0; i < command_count_; ++i) {
        tinyshell::Command& command = commands_[i];
        if (command.destroy != nullptr) {
            command.destroy(command.callable(), command.heap != nullptr);
        }
    }
}

std::uint8_t TableLinker::create_module(const char* mod_name, const char* mod_description) {
    if (mod_name == nullptr) return RESULT_ERROR;
    if (find_module(mod_name) != kNotFound) return MODULE_NOT_FOUND;  // already exists (1.x code)
    if (module_count_ >= TINYSHELL_MAX_MODULES) {
        ++dropped_;
        return TABLE_FULL;
    }
    modules_[module_count_].name = mod_name;
    modules_[module_count_].description = mod_description != nullptr ? mod_description : "";
    ++module_count_;
    return RESULT_OK;
}

std::uint8_t TableLinker::create_module(const std::string& mod_name,
                                        const std::string& mod_description) {
    if (find_module(mod_name) != kNotFound) return MODULE_NOT_FOUND;
    const char* name = intern(mod_name);
    const char* description = intern(mod_description);
    if (name == nullptr || description == nullptr) {
        ++dropped_;
        return TABLE_FULL;
    }
    return create_module(name, description);
}

std::size_t TableLinker::find_module(std::string_view name) const noexcept {
    for (std::size_t i = 0; i < module_count_; ++i) {
        if (name_equals(modules_[i].name, name)) return i;
    }
    return kNotFound;
}

std::size_t TableLinker::find_command(std::size_t module, std::string_view name) const noexcept {
    for (std::size_t i = 0; i < command_count_; ++i) {
        if (commands_[i].module == module && name_equals(commands_[i].name, name)) return i;
    }
    return kNotFound;
}

std::uint8_t TableLinker::call(std::size_t idx, char* const* argv, tinyshell::ConvError* error) {
    if (idx >= command_count_) return FUNCTION_NOT_FOUND;
    tinyshell::Command& command = commands_[idx];
    return command.invoke(command.callable(), argv, error);
}

const char* TableLinker::intern(const std::string& text) {
    const std::size_t needed = text.size() + 1U;
    if (names_used_ + needed > sizeof(names_)) return nullptr;
    char* slot = names_ + names_used_;
    std::memcpy(slot, text.c_str(), needed);
    names_used_ += needed;
    return slot;
}
