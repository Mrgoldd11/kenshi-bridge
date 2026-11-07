// Kenshi Bridge — town list and towns with the radius of their border.
// Copyright (C) 2026 Mrgoldd11. GPLv3 or later, see LICENSE.
#pragma once
#include <string>
#include "json.h"

namespace kb {

// Resolves game functions used here. Call after initRva().
void initBorders();
// "towns": every town/outpost with centre, radius and type. args: near [x,y,z], range, mult.
bool cmdTowns(const JVal& args, JWriter& w, std::string& code, std::string& message);
}
