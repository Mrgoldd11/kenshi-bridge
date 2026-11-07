// Kenshi Bridge — local TCP server. Copyright (C) 2026 Mrgoldd11. GPLv3 or later.
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <deque>
#include <map>
#include <Debug.h>
#include "server.h"

#pragma comment(lib, "ws2_32.lib")

namespace kb {

namespace {

struct Lock {
	CRITICAL_SECTION& cs;
	Lock(CRITICAL_SECTION& c) : cs(c) { EnterCriticalSection(&cs); }
	~Lock() { LeaveCriticalSection(&cs); }
private:
	Lock& operator=(const Lock&);
};

struct Client {
	SOCKET sock;
	std::string inbuf;
	std::string outbuf;
};

CRITICAL_SECTION g_lock;          // guards g_queue and every Client::outbuf
std::deque<Request> g_queue;
std::map<unsigned, Client> g_clients;
unsigned g_nextClient = 1;
SOCKET g_listen = INVALID_SOCKET;
unsigned short g_port = 0;
volatile LONG g_lastFrameTick = 0;

const size_t kMaxLine = 1 << 20;
const size_t kMaxClients = 8;
const size_t kMaxQueue = 256;

std::string idOf(const JVal& req) {
	const JVal& id = req.get("id");
	JWriter w;
	if (id.type == JVal::NUM) w.num(id.n);
	else if (id.type == JVal::STR) w.str(id.s);
	else w.null();
	return w.text();
}

void handleLine(unsigned client, const std::string& line) {
	if (line.empty()) return;
	JVal req;
	if (!parseJson(line, req) || req.type != JVal::OBJ) {
		sendLine(client, replyError("null", "bad_json", "Request is not a JSON object"));
		return;
	}
	Request r;
	r.client = client;
	r.idJson = idOf(req);
	r.cmd = req.str("cmd", "");
	r.args = req.get("args");
	if (r.cmd.empty()) {
		sendLine(client, replyError(r.idJson, "bad_request", "Request has no \"cmd\""));
		return;
	}
	if (r.cmd == "ping") {
		JWriter w;
		w.beginObj()
			.key("plugin").str("kenshi-bridge")
			.key("version").str(kPluginVersion)
			.key("protocol").integer(kProtocolVersion)
			.key("inGame").boolean(gameRunning())
			.endObj();
		sendLine(client, replyOk(r.idJson, w.text()));
		return;
	}
	if (!gameRunning()) {
		sendLine(client, replyError(r.idJson, "not_in_game", "The game world is not running (main menu or loading)"));
		return;
	}
	Lock l(g_lock);
	if (g_queue.size() >= kMaxQueue) {
		sendLine(client, replyError(r.idJson, "busy", "Too many queued requests"));
		return;
	}
	g_queue.push_back(r);
}

void closeClient(std::map<unsigned, Client>::iterator it) {
	closesocket(it->second.sock);
	g_clients.erase(it);
}

DWORD WINAPI serverThread(LPVOID) {
	char buf[8192];
	for (;;) {
		fd_set rd, wr;
		FD_ZERO(&rd); FD_ZERO(&wr);
		FD_SET(g_listen, &rd);
		{
			Lock l(g_lock);
			for (std::map<unsigned, Client>::iterator it = g_clients.begin(); it != g_clients.end(); ++it) {
				FD_SET(it->second.sock, &rd);
				if (!it->second.outbuf.empty()) FD_SET(it->second.sock, &wr);
			}
		}
		timeval tv; tv.tv_sec = 0; tv.tv_usec = 20000;
		int n = select(0, &rd, &wr, 0, &tv);
		if (n == SOCKET_ERROR) { Sleep(50); continue; }

		if (FD_ISSET(g_listen, &rd)) {
			SOCKET s = accept(g_listen, 0, 0);
			if (s != INVALID_SOCKET) {
				Lock l(g_lock);
				if (g_clients.size() >= kMaxClients) closesocket(s);
				else {
					u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);
					Client c; c.sock = s;
					g_clients[g_nextClient++] = c;
				}
			}
		}

		std::vector<std::pair<unsigned, std::string> > lines;
		{
			Lock l(g_lock);
			for (std::map<unsigned, Client>::iterator it = g_clients.begin(); it != g_clients.end();) {
				Client& c = it->second;
				bool dead = false;
				if (FD_ISSET(c.sock, &rd)) {
					int got = recv(c.sock, buf, sizeof(buf), 0);
					if (got <= 0) dead = true;
					else {
						c.inbuf.append(buf, got);
						size_t pos;
						while ((pos = c.inbuf.find('\n')) != std::string::npos) {
							std::string line = c.inbuf.substr(0, pos);
							if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
							lines.push_back(std::make_pair(it->first, line));
							c.inbuf.erase(0, pos + 1);
						}
						if (c.inbuf.size() > kMaxLine) dead = true;
					}
				}
				if (!dead && !c.outbuf.empty() && FD_ISSET(c.sock, &wr)) {
					int sent = send(c.sock, c.outbuf.data(), (int)c.outbuf.size(), 0);
					if (sent > 0) c.outbuf.erase(0, sent);
					else if (WSAGetLastError() != WSAEWOULDBLOCK) dead = true;
				}
				if (dead) { std::map<unsigned, Client>::iterator d = it++; closeClient(d); }
				else ++it;
			}
		}
		for (size_t k = 0; k < lines.size(); ++k) handleLine(lines[k].first, lines[k].second);
	}
	return 0;
}

} // namespace

bool startServer(unsigned short port) {
	InitializeCriticalSection(&g_lock);
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { ErrorLog("[KenshiBridge] WSAStartup failed"); return false; }
	g_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (g_listen == INVALID_SOCKET) { ErrorLog("[KenshiBridge] socket() failed"); return false; }
	BOOL excl = TRUE;
	setsockopt(g_listen, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&excl, sizeof(excl));
	sockaddr_in addr; ZeroMemory(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1 only
	if (bind(g_listen, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR || listen(g_listen, 4) == SOCKET_ERROR) {
		ErrorLog("[KenshiBridge] cannot listen on 127.0.0.1 (port busy?)");
		closesocket(g_listen); g_listen = INVALID_SOCKET;
		return false;
	}
	g_port = port;
	HANDLE th = CreateThread(0, 0, serverThread, 0, 0, 0);
	if (!th) { ErrorLog("[KenshiBridge] CreateThread failed"); return false; }
	CloseHandle(th);
	return true;
}

unsigned short serverPort() { return g_port; }

bool popRequest(Request& out) {
	Lock l(g_lock);
	if (g_queue.empty()) return false;
	out = g_queue.front();
	g_queue.pop_front();
	return true;
}

void sendLine(unsigned client, const std::string& json) {
	Lock l(g_lock);
	std::map<unsigned, Client>::iterator it = g_clients.find(client);
	if (it == g_clients.end()) return;
	it->second.outbuf += json;
	it->second.outbuf += '\n';
}

void markFrame() { InterlockedExchange(&g_lastFrameTick, (LONG)GetTickCount()); }

bool gameRunning() {
	LONG last = g_lastFrameTick;
	return last != 0 && (DWORD)GetTickCount() - (DWORD)last < 3000;
}

std::string replyOk(const std::string& idJson, const std::string& resultJson) {
	JWriter w;
	w.beginObj().key("id").raw(idJson).key("ok").boolean(true).key("result").raw(resultJson).endObj();
	return w.text();
}

std::string replyError(const std::string& idJson, const std::string& code, const std::string& message) {
	JWriter w;
	w.beginObj().key("id").raw(idJson).key("ok").boolean(false)
		.key("error").beginObj().key("code").str(code).key("message").str(message).endObj()
		.endObj();
	return w.text();
}

} // namespace kb
