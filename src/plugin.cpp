// Kenshi Bridge — RE_Kenshi plugin: a local JSON server that exposes game state,
// a few commands and a dialogue-selection trace to external tools.
// Copyright (C) 2026 Mrgoldd11.
//
// This program is free software: you can redistribute it and/or modify it under the terms of
// the GNU General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See LICENSE.
#include <Debug.h>
#include <core/Functions.h>
#include <kenshi/Globals.h>
#include <kenshi/GameWorld.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Character.h>
#include <kenshi/Faction.h>
#include <kenshi/GameData.h>
#include <kenshi/CameraClass.h>
#include <kenshi/Dialogue.h>
#include <kenshi/util/hand.h>
#include <kenshi/util/TimeOfDay.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdlib>
#include <sstream>

#include "server.h"
#include "trace.h"
#include "rva.h"
#include "borders.h"

using namespace kb;

namespace {

void writeVec(JWriter& w, const Ogre::Vector3& v) {
	w.beginArr().num(v.x).num(v.y).num(v.z).endArr();
}

void writeData(JWriter& w, GameData* d) {
	if (!d) { w.null(); return; }
	w.beginObj().key("stringId").str(d->stringID).key("name").str(d->name).endObj();
}

void writeFaction(JWriter& w, Faction* f) {
	if (!f) { w.null(); return; }
	w.beginObj().key("name").str(f->getName());
	w.key("stringId").str(f->data ? f->data->stringID : std::string());
	w.endObj();
}

void writeCharacterBrief(JWriter& w, Character* c) {
	if (!c) { w.null(); return; }
	w.beginObj()
		.key("name").str(c->getName())
		.key("template"); writeData(w, c->getGameData());
	w.key("faction"); writeFaction(w, c->getFaction());
	w.key("pos"); writeVec(w, c->getPosition());
	w.endObj();
}

void writeDialogueState(JWriter& w, Character* c) {
	Dialogue* d = c ? c->dialogue : 0;
	if (!d) { w.null(); return; }
	w.beginObj()
		.key("active").boolean(!d->_hasEnded)
		.key("event").integer((int)d->currentConversationType)
		.key("dialogue"); writeData(w, d->currentConversation ? d->currentConversation->data : 0);
	w.key("line"); writeData(w, d->currentLine ? d->currentLine->data : 0);
	Character* t = d->conversationTarget.getCharacter();
	w.key("target");
	if (t) {
		w.beginObj().key("name").str(t->getName()).key("template");
		writeData(w, t->getGameData());
		w.endObj();
	} else w.null();
	w.endObj();
}

Character* selected() {
	if (!ou || !ou->player) return 0;
	return ou->player->selectedCharacter.getCharacter();
}

// Functions the installed KenshiLib does not export (see rva.h). GOG RVAs from KenshiLib headers.
typedef void (*CharacterTeleport)(Character*, const Ogre::Vector3&);
typedef void (*CameraTeleport)(CameraClass*, const Ogre::Vector3&);
typedef void (*CameraStop)(CameraClass*);
typedef void (*WorldSpeed)(GameWorld*, float, bool);
typedef void (*WorldPause)(GameWorld*, bool);
CharacterTeleport fnCharacterTeleport = 0;
CameraTeleport fnCameraTeleport = 0;
CameraStop fnCameraStop = 0;
WorldSpeed fnSetGameSpeed = 0;
WorldPause fnUserPause = 0;

void resolveFunctions() {
	fnCharacterTeleport = (CharacterTeleport)gameFunction(0x5C9F00);
	fnCameraTeleport = (CameraTeleport)gameFunction(0x6AF190);
	fnCameraStop = (CameraStop)gameFunction(0x6ADDE0);
	fnSetGameSpeed = (WorldSpeed)gameFunction(0x786C30);
	fnUserPause = (WorldPause)gameFunction(0x786DA0);
}

bool unsupported(void* fn, std::string& code, std::string& message) {
	if (fn) return false;
	code = "unsupported"; message = "This command is not available for this game build";
	return true;
}

bool readVec(const JVal& args, const char* key, Ogre::Vector3& out) {
	const JVal& v = args.get(key);
	if (v.type != JVal::ARR || v.a.size() != 3) return false;
	for (int k = 0; k < 3; ++k) if (v.a[k].type != JVal::NUM) return false;
	out = Ogre::Vector3((float)v.a[0].n, (float)v.a[1].n, (float)v.a[2].n);
	return true;
}

// Runs one command on the game thread. Returns false and sets code/message on error.
bool runCommand(const Request& r, JWriter& w, std::string& code, std::string& message) {
	if (r.cmd == "state") {
		Character* c = selected();
		w.beginObj();
		w.key("paused").boolean(ou->paused);
		w.key("speed").num(ou->frameSpeedMult);
		TimeOfDay t = ou->getTimeStamp_inGameHours();
		w.key("time").beginObj().key("days").num(t.getTotalDays()).key("hours").num(t.getTotalHours()).endObj();
		w.key("selected"); writeCharacterBrief(w, c);
		w.key("selectedDialogue"); writeDialogueState(w, c);
		w.key("selectedIsPlayer").boolean(c ? c->isPlayerCharacter() : false);
		CameraClass* cam = ou->player->getCamera();
		w.key("camera");
		if (cam) { w.beginObj().key("center"); writeVec(w, cam->getCenter()); w.endObj(); }
		else w.null();
		w.key("playerCharacters").integer((long long)ou->player->playerCharacters.size());
		w.key("trace").boolean(traceEnabled());
		w.key("build").str(gameBuild());
		w.endObj();
		return true;
	}
	if (r.cmd == "teleport") {
		Ogre::Vector3 to;
		if (!readVec(r.args, "pos", to)) { code = "bad_args"; message = "args.pos must be [x, y, z]"; return false; }
		if (unsupported(fnCharacterTeleport, code, message)) return false;
		Character* c = selected();
		if (!c) { code = "no_selection"; message = "No character is selected"; return false; }
		fnCharacterTeleport(c, to);
		w.beginObj().key("name").str(c->getName()).key("pos"); writeVec(w, to); w.endObj();
		return true;
	}
	if (r.cmd == "camera") {
		Ogre::Vector3 to;
		if (!readVec(r.args, "pos", to)) { code = "bad_args"; message = "args.pos must be [x, y, z]"; return false; }
		if (unsupported(fnCameraTeleport, code, message) || unsupported(fnCameraStop, code, message)) return false;
		CameraClass* cam = ou->player->getCamera();
		if (!cam) { code = "no_camera"; message = "Camera is not ready"; return false; }
		fnCameraStop(cam);
		fnCameraTeleport(cam, to);
		w.beginObj().key("pos"); writeVec(w, to); w.endObj();
		return true;
	}
	if (r.cmd == "speed") {
		const JVal& v = r.args.get("value");
		if (v.type != JVal::NUM || v.n < 0 || v.n > 20) { code = "bad_args"; message = "args.value must be a number 0..20"; return false; }
		if (unsupported(fnUserPause, code, message) || unsupported(fnSetGameSpeed, code, message)) return false;
		if (v.n == 0) fnUserPause(ou, true);
		else { fnUserPause(ou, false); fnSetGameSpeed(ou, (float)v.n, false); }
		w.beginObj().key("value").num(v.n).endObj();
		return true;
	}
	if (r.cmd == "pause") {
		if (unsupported(fnUserPause, code, message)) return false;
		bool on = r.args.boolean("on", true);
		fnUserPause(ou, on);
		w.beginObj().key("paused").boolean(on).endObj();
		return true;
	}
	if (r.cmd == "trace") {
		if (r.args.has("on")) setTraceEnabled(r.args.boolean("on", false), r.args.str("filter", "player") == "all");
		w.beginObj().key("on").boolean(traceEnabled()).key("all").boolean(traceAll()).endObj();
		return true;
	}
	if (r.cmd == "trace_get") {
		long long since = (long long)r.args.num("since", 0);
		w.raw(traceSince(since));
		return true;
	}
	if (r.cmd == "towns") return cmdTowns(r.args, w, code, message);
	code = "unknown_cmd";
	message = "Unknown command: " + r.cmd;
	return false;
}

void serveRequests() {
	Request r;
	int budget = 16;
	while (budget-- > 0 && popRequest(r)) {
		JWriter w;
		std::string code, message;
		bool ok = false;
		try {
			ok = runCommand(r, w, code, message);
		} catch (...) {
			ok = false; code = "crash"; message = "The command failed inside the game (exception caught)";
		}
		sendLine(r.client, ok ? replyOk(r.idJson, w.text()) : replyError(r.idJson, code, message));
	}
}

void (*mainLoop_orig)(GameWorld* thisptr, float time) = 0;
void mainLoop_hook(GameWorld* thisptr, float time) {
	mainLoop_orig(thisptr, time);
	markFrame();
	serveRequests();
}

} // namespace

__declspec(dllexport) void startPlugin() {
	unsigned short port = kDefaultPort;
	const char* env = getenv("KENSHI_BRIDGE_PORT");
	if (env && atoi(env) > 0 && atoi(env) < 65536) port = (unsigned short)atoi(env);

	if (KenshiLib::SUCCESS != KenshiLib::AddHook(KenshiLib::GetRealAddress(&GameWorld::_NV_mainLoop_GPUSensitiveStuff), &mainLoop_hook, &mainLoop_orig)) {
		ErrorLog("[KenshiBridge] could not hook the main loop; bridge disabled");
		return;
	}
	if (initRva()) { resolveFunctions(); initBorders(); }
	else ErrorLog("[KenshiBridge] teleport/camera/speed and part of the trace are unavailable");
	installTraceHooks();
	if (startServer(port)) {
		std::ostringstream s;
		s << "[KenshiBridge] " << kPluginVersion << " listening on 127.0.0.1:" << port;
		DebugLog(s.str());
	}
}
