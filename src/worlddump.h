// Kenshi Bridge — world dump: foliage instances and buildings of loaded zones, written to files
// an external editor can read; plus a camera fly-over that visits zones to get them dumped.
// Copyright (C) 2026 Mrgoldd11. GPLv3 or later, see LICENSE.
#pragma once
#include <string>
#include "json.h"

namespace kb {

// Resolves game functions and installs the foliage hooks. Call after initRva().
void initWorldDump();
// Called every frame on the game main thread: writes changed zones, drives the fly-over.
void worldDumpTick();
// "dump": what is dumped. args: now (write everything loaded right away), near [x,y,z] + range
// (list the dumped foliage and buildings around a point, for checks).
bool cmdDump(const JVal& args, JWriter& w, std::string& code, std::string& message);
// "flyover": args.action = start | stop | resume | status; rect [x0,y0,x1,y1] (zone indices,
// default the whole map), force (visit zones that already have files), dwell (ms), timeout (s).
bool cmdFlyover(const JVal& args, JWriter& w, std::string& code, std::string& message);

}
