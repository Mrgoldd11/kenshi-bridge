// Kenshi Bridge — dialogue selection trace. Copyright (C) 2026 Mrgoldd11. GPLv3 or later.
#pragma once
#include <string>

namespace kb {
void installTraceHooks();
bool traceEnabled();
bool traceAll();
void setTraceEnabled(bool on, bool all);
// JSON object {"next": <seq>, "items": [...]} with records whose seq > since.
std::string traceSince(long long since);
}
