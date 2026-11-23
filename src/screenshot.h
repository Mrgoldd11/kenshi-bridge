// Kenshi Bridge — screenshot command (the game window to a PNG file).
// Copyright (C) 2026 Mrgoldd11. GPLv3 or later, see LICENSE.
#pragma once
#include "json.h"

namespace kb {

// screenshot: writes the game window to %LOCALAPPDATA%\KenshiBridge\shots\shot-N.png
// (or args.path) and returns {path, width, height}. Runs on the game thread.
bool cmdScreenshot(const JVal& args, JWriter& w, std::string& code, std::string& message);

}
