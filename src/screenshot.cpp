// Kenshi Bridge — screenshot command (the game window to a PNG file).
// Copyright (C) 2026 Mrgoldd11. GPLv3 or later, see LICENSE.
#include <Debug.h>
#include <kenshi/Globals.h>
#include <kenshi/KingOfRenderThread.h>
#include <kenshi/Renderer.h>
#include <ogre/OgreRenderWindow.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdlib>

#include "screenshot.h"

namespace kb {

bool cmdScreenshot(const JVal& args, JWriter& w, std::string& code, std::string& message) {
	if (!au || !au->render || !au->render->window) { code = "unsupported"; message = "The game window is not ready"; return false; }
	Ogre::RenderWindow* win = au->render->window;
	std::string path = args.str("path", "");
	if (path.empty()) {
		const char* local = getenv("LOCALAPPDATA");
		if (!local || !*local) { code = "failed"; message = "LOCALAPPDATA is not set"; return false; }
		std::string base = std::string(local) + "\\KenshiBridge";
		CreateDirectoryA(base.c_str(), 0);
		base += "\\shots";
		CreateDirectoryA(base.c_str(), 0);
		char name[64];
		sprintf(name, "\\shot-%lu.png", (unsigned long)GetTickCount());
		path = base + name;
	}
	win->writeContentsToFile(path);
	DWORD attr = GetFileAttributesA(path.c_str());
	if (attr == INVALID_FILE_ATTRIBUTES) { code = "failed"; message = "The picture was not written: " + path; return false; }
	w.beginObj().key("path").str(path).key("width").integer((long long)win->getWidth()).key("height").integer((long long)win->getHeight()).endObj();
	return true;
}

}
