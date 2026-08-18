#pragma once
#include <vector>
#include "roblox.hpp"

namespace menu {

void init();
void set_visible(bool v);
bool is_visible();

bool is_injected();

void set_scan_ok(bool ok);

void render(const std::vector<PlayerInfo>* players, uintptr_t render_view);

}
