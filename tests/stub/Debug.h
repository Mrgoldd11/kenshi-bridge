// Test stub for KenshiLib's Debug.h: the harness prints to stderr instead of RE_Kenshi_log.txt.
#pragma once
#include <cstdio>
#include <string>
inline void DebugLog(const std::string& m) { fprintf(stderr, "%s\n", m.c_str()); }
inline void DebugLog(const char* m) { fprintf(stderr, "%s\n", m); }
inline void ErrorLog(const std::string& m) { fprintf(stderr, "ERROR %s\n", m.c_str()); }
inline void ErrorLog(const char* m) { fprintf(stderr, "ERROR %s\n", m); }
