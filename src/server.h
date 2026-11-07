// Kenshi Bridge — local TCP server (127.0.0.1 only), one JSON object per line.
// Copyright (C) 2026 Mrgoldd11. GPLv3 or later, see LICENSE.
#pragma once
#include <string>
#include "json.h"

namespace kb {

const unsigned short kDefaultPort = 47801;
const char* const kPluginVersion = "0.1.0";
const int kProtocolVersion = 1;

struct Request {
	unsigned client;     // connection id
	std::string idJson;  // the request "id", copied back as raw JSON
	std::string cmd;
	JVal args;
};

bool startServer(unsigned short port);
unsigned short serverPort();

// Main (game) thread: take the next queued request.
bool popRequest(Request& out);
// Any thread: queue a reply line for a client.
void sendLine(unsigned client, const std::string& json);

// Main loop heartbeat: the game world is running.
void markFrame();
bool gameRunning();

std::string replyOk(const std::string& idJson, const std::string& resultJson);
std::string replyError(const std::string& idJson, const std::string& code, const std::string& message);

} // namespace kb
