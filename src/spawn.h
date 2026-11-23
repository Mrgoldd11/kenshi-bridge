// Kenshi Bridge — spawn command (put a character/animal into the world).
// Copyright (C) 2026 Mrgoldd11. GPLv3 or later, see LICENSE.
#pragma once
#include "json.h"

namespace kb {

// Resolves the game functions the command needs; false if the build is not supported.
bool initSpawn();
// spawn: args.id (CHARACTER record string id), args.pos [x,y,z] (default: camera centre),
// args.age (animals: 0 pup .. 1, default 0.5), args.faction ("player" = the selected character's faction; a FACTION string id; default: the
// faction named in the record, else the empty faction).
bool cmdSpawn(const JVal& args, JWriter& w, std::string& code, std::string& message);

}
