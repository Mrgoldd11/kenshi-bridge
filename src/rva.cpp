// Kenshi Bridge — game functions that the installed KenshiLib does not export.
// Copyright (C) 2026 Mrgoldd11. GPLv3 or later, see LICENSE.
#include <Debug.h>
#include <core/Functions.h>
#include <kenshi/Dialogue.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vector>
#include <string>
#include <cstdio>

#include "rva.h"

namespace kb {

namespace {

std::vector<unsigned> g_gog, g_steam;
const std::vector<unsigned>* g_target = 0;
const char* g_build = "unknown";
char* g_base = 0;

bool readTable(const std::wstring& path, std::vector<unsigned>& out) {
	FILE* f = 0;
	if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return false;
	unsigned v;
	while (fread(&v, sizeof(v), 1, f) == 1) out.push_back(v);
	fclose(f);
	return !out.empty();
}

int indexOf(unsigned gogRva) {
	for (size_t k = 0; k < g_gog.size(); ++k)
		if (g_gog[k] == gogRva) return (int)k;
	return -1;
}

} // namespace

bool initRva() {
	wchar_t exe[MAX_PATH];
	DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);
	if (n == 0 || n >= MAX_PATH) return false;
	std::wstring exeDir(exe);
	exeDir = exeDir.substr(0, exeDir.find_last_of(L"\\/") + 1);
	// RE_Kenshi starts the game from <Kenshi>\RE_Kenshi\kenshi_x64.exe; a plain start is <Kenshi>\Kenshi_x64.exe.
	const wchar_t* dirs[] = { L"RVAs\\", L"RE_Kenshi\\RVAs\\", L"..\\RE_Kenshi\\RVAs\\" };
	bool found = false;
	for (int k = 0; k < 3 && !found; ++k) {
		g_gog.clear(); g_steam.clear();
		std::wstring dir = exeDir + dirs[k];
		found = readTable(dir + L"GOG_1.0.65.br", g_gog) && readTable(dir + L"Steam_1.0.65.br", g_steam)
			&& g_gog.size() == g_steam.size();
	}
	if (!found) {
		ErrorLog("[KenshiBridge] RVA tables for 1.0.65 not found in RE_Kenshi\\RVAs");
		return false;
	}
	g_base = (char*)GetModuleHandleW(NULL);
	// DialogLineData::checkConditions is exported by KenshiLib: its real address tells the build.
	const unsigned probeGog = 0x6787B0;
	int idx = indexOf(probeGog);
	intptr_t real = KenshiLib::GetRealAddress(&DialogLineData::checkConditions);
	unsigned rva = (unsigned)(real - (intptr_t)g_base);
	if (idx < 0 || real == 0) { ErrorLog("[KenshiBridge] build probe failed"); return false; }
	if (g_steam[idx] == rva) { g_target = &g_steam; g_build = "Steam"; }
	else if (g_gog[idx] == rva) { g_target = &g_gog; g_build = "GOG"; }
	else { ErrorLog("[KenshiBridge] unknown game build (not 1.0.65?)"); return false; }
	return true;
}

void* gameFunction(unsigned gogRva) {
	if (!g_target) return 0;
	int idx = indexOf(gogRva);
	if (idx < 0) return 0;
	return g_base + (*g_target)[idx];
}

const char* gameBuild() { return g_build; }

} // namespace kb
