// Kenshi Bridge — game functions that the installed KenshiLib does not export.
// Copyright (C) 2026 Mrgoldd11. GPLv3 or later, see LICENSE.
//
// KenshiLib 0.5.0 (shipped with RE_Kenshi 0.3.5) exports only part of the functions declared in
// its headers. For the rest the plugin looks the address up itself: the header gives the GOG RVA,
// RE_Kenshi/RVAs/{GOG,Steam}_1.0.65.br map it to the running build.
#pragma once

namespace kb {

// Loads the RVA tables and detects the build (Steam or GOG). Returns false if not possible.
bool initRva();
// Address of a game function by its GOG 1.0.65 RVA (as in KenshiLib headers), or 0.
void* gameFunction(unsigned gogRva);
// "Steam", "GOG" or "unknown".
const char* gameBuild();

}
