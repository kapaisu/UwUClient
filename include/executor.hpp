#pragma once
#include <string>
#include <cstdint>

namespace executor {

bool ping(const std::string& msg);

bool execute_source(const std::string& source);

bool ready();

uintptr_t lua_state();

void refresh_state();

}
