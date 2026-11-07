// Kenshi Bridge — test harness: the real server.cpp/json.cpp without the game.
// A fake "main loop" answers a few commands so the protocol can be tested from outside.
// Copyright (C) 2026 Mrgoldd11. GPLv3 or later, see LICENSE.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include "../src/server.h"

using namespace kb;

int main(int argc, char** argv) {
	unsigned short port = argc > 1 ? (unsigned short)atoi(argv[1]) : kDefaultPort;
	if (!startServer(port)) { printf("START_FAILED\n"); return 1; }
	printf("LISTENING %u\n", (unsigned)port);
	fflush(stdout);
	bool inGame = true;
	for (;;) {
		if (inGame) markFrame();
		Request r;
		while (popRequest(r)) {
			JWriter w;
			if (r.cmd == "echo") {
				w.beginObj().key("text").str(r.args.str("text", "")).endObj();
				sendLine(r.client, replyOk(r.idJson, w.text()));
			} else if (r.cmd == "leave_game") {
				inGame = false; // stop the heartbeat: after 3 s commands get not_in_game
				sendLine(r.client, replyOk(r.idJson, "true"));
			} else if (r.cmd == "quit") {
				sendLine(r.client, replyOk(r.idJson, "true"));
				Sleep(200);
				return 0;
			} else {
				sendLine(r.client, replyError(r.idJson, "unknown_cmd", "Unknown command: " + r.cmd));
			}
		}
		Sleep(16);
	}
}
