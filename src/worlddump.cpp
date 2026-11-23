// Kenshi Bridge — world dump: foliage and buildings of the loaded world, as the game placed them.
// Copyright (C) 2026 Mrgoldd11. GPLv3 or later, see LICENSE.
//
// Foliage (trees, rocks, bushes) has no positions in the game files: the game scatters it when a
// zone loads. Every placed instance goes through FoliageSystem::finalMakeEnt, so that function is
// hooked and the instances are remembered per zone. Buildings are read from the content of the
// loaded zones. Both are written to one small file per zone (see README, "World dump").
//
// Game structures are read by offset (KenshiLib headers, game 1.0.65) to keep this file free of
// the heavy zone headers.
#include <Debug.h>
#include <core/Functions.h>
#include <kenshi/Globals.h>
#include <kenshi/GameWorld.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/CameraClass.h>
#include <kenshi/RootObject.h>
#include <kenshi/Town.h>
#include <kenshi/GameData.h>
#include <kenshi/Faction.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "worlddump.h"
#include "server.h"
#include "rva.h"

namespace kb {

namespace {

const int kZones = 64;
const float kZoneSize = 4608.0f;

// ---- offsets (KenshiLib headers) ----
const size_t kOffZoneMgr = 0x8B0;        // GameWorld::zoneMgr
const size_t kOffWorldMap = 0xC8;        // ZoneManager::worldMap[64][64]
const size_t kSizeZoneMap = 0x168;       // sizeof(ZoneMap)
const size_t kOffZmContent = 0x0;        // ZoneMap::mapContent
const size_t kOffZmCoords = 0x18;        // ZoneMap::coordinates (iVector2)
const size_t kOffContentThings = 0x50;   // RootObjectContainer::things (lektor<RootObject*>)
const size_t kOffContentLoaded = 0xD8;   // ZoneMapContent::loaded
const size_t kOffContentFoliage = 0x100; // ZoneMapContent::foliageSystem
const size_t kOffFolLoadComplete = 0x78; // FoliageSystem::loadComplete
const size_t kOffFolZoneMap = 0x80;      // FoliageSystem::zoneMap
const size_t kOffBldIsFoliage = 0xC0;
const size_t kOffBldInterior = 0xF8;
const size_t kOffBldComplete = 0x160;    // Building::_buildState.isComplete
const size_t kOffBldIsDoor = 0x1A0;
const size_t kOffBldDestroyed = 0x1A1;
const size_t kOffBldFurnitureOf = 0x238;
const size_t kOffBldOutsideFurniture = 0x240;

// Building flags in the dump.
const int kFlagInterior = 1, kFlagFoliage = 2, kFlagDestroyed = 4, kFlagUnfinished = 8, kFlagDoor = 16,
	kFlagFurniture = 32, kFlagOutsideFurniture = 64;

template <class T> T& at(void* base, size_t off) { return *(T*)((char*)base + off); }

typedef bool (*ZoneBool)(void* zoneMap);
typedef float (*ZoneHeight)(void* zoneMap, float x, float z);
typedef TownBase* (*BuildingTown)(void* building);
typedef void (*CameraTeleport)(CameraClass*, const Ogre::Vector3&);
typedef void (*CameraStop)(CameraClass*);
ZoneBool fnZoneLoaded = 0;
ZoneHeight fnZoneHeight = 0;
BuildingTown fnBuildingTown = 0;
CameraTeleport fnCameraTeleport = 0;
CameraStop fnCameraStop = 0;

// ---- foliage captured by the hooks ----
#pragma pack(push, 1)
struct FolInst {
	float x, y, z, yaw, scale;
	unsigned short mesh;
	unsigned short flags; // 1 = carries a building (ore rocks)
};
#pragma pack(pop)

struct ZoneFoliage {
	std::vector<std::string> meshes;
	std::map<std::string, unsigned short> meshIndex;
	std::vector<FolInst> inst;
	bool dirty;      // changed since the last write
	DWORD touched;   // tick count of the last change
	ZoneFoliage() : dirty(false), touched(0) {}
};

CRITICAL_SECTION g_lock;                 // guards g_fol (the hooks may run off the main thread)
std::map<int, ZoneFoliage> g_fol;        // by zone key
bool g_hookEnt = false, g_hookCtor = false;
__declspec(thread) int t_ctorZone = -1;  // zone whose FoliageSystem is being built on this thread

// ---- files ----
std::string g_dir;
std::set<int> g_folFiles, g_bldFiles;    // zones that have a file on disk
std::map<int, unsigned> g_bldHash;       // last written building list, by zone
std::map<int, int> g_bldCount, g_folCount; // sizes of what was written this session
long long g_written = 0;
std::string g_lastError;

int zoneKey(int x, int y) { return x * kZones + y; }
bool validZone(int x, int y) { return x >= 0 && x < kZones && y >= 0 && y < kZones; }

int zoneKeyOfPos(float x, float z) {
	int zx = (int)std::floor(x / kZoneSize) + kZones / 2;
	int zy = (int)std::floor(z / kZoneSize) + kZones / 2;
	return validZone(zx, zy) ? zoneKey(zx, zy) : -1;
}

void* zoneManager() { return ou ? at<void*>(ou, kOffZoneMgr) : 0; }

void* zoneMapAt(void* mgr, int index) { return (char*)mgr + kOffWorldMap + (size_t)index * kSizeZoneMap; }

int keyOfZoneMap(void* zm) {
	int x = at<int>(zm, kOffZmCoords), y = at<int>(zm, kOffZmCoords + 4);
	return validZone(x, y) ? zoneKey(x, y) : -1;
}

// Zone map with these coordinates, or 0 (the array order is not relied on).
void* findZoneMap(int key) {
	void* mgr = zoneManager();
	if (!mgr) return 0;
	for (int k = 0; k < kZones * kZones; ++k) {
		void* zm = zoneMapAt(mgr, k);
		if (keyOfZoneMap(zm) == key) return zm;
	}
	return 0;
}

// Content of a zone that is fully loaded on the main thread, or 0.
void* loadedContent(void* zm) {
	void* content = at<void*>(zm, kOffZmContent);
	if (!content || !at<bool>(content, kOffContentLoaded)) return 0;
	if (fnZoneLoaded && !fnZoneLoaded(zm)) return 0;
	return content;
}

std::string zoneFile(const char* kind, int key, const char* ext) {
	char b[64];
	sprintf(b, "%s.%d.%d.%s", kind, key / kZones, key % kZones, ext);
	return g_dir + "\\" + b;
}

bool writeFileAtomic(const std::string& path, const std::string& bytes) {
	std::string tmp = path + ".tmp";
	FILE* f = fopen(tmp.c_str(), "wb");
	if (!f) { g_lastError = "cannot write " + tmp; return false; }
	bool ok = bytes.empty() || fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
	fclose(f);
	if (!ok || !MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
		g_lastError = "cannot write " + path;
		DeleteFileA(tmp.c_str());
		return false;
	}
	++g_written;
	return true;
}

void putU16(std::string& s, unsigned v) { s += (char)(v & 0xFF); s += (char)((v >> 8) & 0xFF); }
void putU32(std::string& s, unsigned v) { putU16(s, v & 0xFFFF); putU16(s, v >> 16); }

// foliage.X.Y.bin: "KBF1", u32 zoneX, u32 zoneY, u32 meshCount, meshCount x (u16 length, string id),
// u32 instanceCount, instanceCount x 24 bytes (f32 x, y, z, yaw degrees, scale; u16 mesh; u16 flags).
bool writeFoliage(int key, const ZoneFoliage& z) {
	std::string s("KBF1");
	putU32(s, key / kZones); putU32(s, key % kZones);
	putU32(s, (unsigned)z.meshes.size());
	for (size_t k = 0; k < z.meshes.size(); ++k) { putU16(s, (unsigned)z.meshes[k].size()); s += z.meshes[k]; }
	putU32(s, (unsigned)z.inst.size());
	if (!z.inst.empty()) s.append((const char*)&z.inst[0], z.inst.size() * sizeof(FolInst));
	if (!writeFileAtomic(zoneFile("foliage", key, "bin"), s)) return false;
	g_folFiles.insert(key);
	g_folCount[key] = (int)z.inst.size();
	return true;
}

// Writes the foliage of the zones that changed and have been quiet for `quietMs` (0 = all dirty).
void flushFoliage(DWORD quietMs) {
	DWORD now = GetTickCount();
	for (;;) {
		int key = -1;
		ZoneFoliage copy;
		EnterCriticalSection(&g_lock);
		for (std::map<int, ZoneFoliage>::iterator it = g_fol.begin(); it != g_fol.end(); ++it) {
			if (!it->second.dirty || now - it->second.touched < quietMs) continue;
			key = it->first;
			copy = it->second;
			it->second.dirty = false;
			break;
		}
		LeaveCriticalSection(&g_lock);
		if (key < 0) break;
		writeFoliage(key, copy);
	}
}

// Frees the foliage of zones that are written and no longer loaded.
void purgeFoliage() {
	EnterCriticalSection(&g_lock);
	for (std::map<int, ZoneFoliage>::iterator it = g_fol.begin(); it != g_fol.end();) {
		void* zm = it->second.dirty ? 0 : findZoneMap(it->first);
		if (zm && !at<void*>(zm, kOffZmContent)) g_fol.erase(it++);
		else ++it;
	}
	LeaveCriticalSection(&g_lock);
}

// ---- foliage hooks ----
ZoneFoliage& zoneForAdd(int key, DWORD now) {
	ZoneFoliage& z = g_fol[key];
	// Without the constructor hook a reload is recognised by a long pause between instances.
	if (!g_hookCtor && !z.inst.empty() && now - z.touched > 20000) z = ZoneFoliage();
	return z;
}

typedef bool (*FinalMakeEnt)(void* self, void* ent, const Ogre::Vector3& pos, const Ogre::Degree& yaw, const float& scale,
	void* treeLoader, void* building, const Ogre::Matrix4& transform);
FinalMakeEnt finalMakeEnt_orig = 0;
bool finalMakeEnt_hook(void* self, void* ent, const Ogre::Vector3& pos, const Ogre::Degree& yaw, const float& scale,
	void* treeLoader, void* building, const Ogre::Matrix4& transform) {
	bool made = finalMakeEnt_orig(self, ent, pos, yaw, scale, treeLoader, building, transform);
	if (!made || !ent) return made;
	GameData* data = at<GameData*>(ent, 0); // EntData::data
	if (!data) return made;
	int key = t_ctorZone;
	if (key < 0 && self) {
		void* zm = at<void*>(self, kOffFolZoneMap);
		if (zm) key = keyOfZoneMap(zm);
	}
	if (key < 0) key = zoneKeyOfPos(pos.x, pos.z);
	if (key < 0) return made;
	DWORD now = GetTickCount();
	EnterCriticalSection(&g_lock);
	ZoneFoliage& z = zoneForAdd(key, now);
	std::map<std::string, unsigned short>::iterator it = z.meshIndex.find(data->stringID);
	unsigned short mesh;
	if (it != z.meshIndex.end()) mesh = it->second;
	else if (z.meshes.size() < 0xFFFF) {
		mesh = (unsigned short)z.meshes.size();
		z.meshes.push_back(data->stringID);
		z.meshIndex[data->stringID] = mesh;
	} else { LeaveCriticalSection(&g_lock); return made; }
	FolInst i;
	i.x = pos.x; i.y = pos.y; i.z = pos.z;
	i.yaw = yaw.valueDegrees();
	i.scale = scale;
	i.mesh = mesh;
	i.flags = building ? 1 : 0;
	z.inst.push_back(i);
	z.dirty = true;
	z.touched = now;
	LeaveCriticalSection(&g_lock);
	return made;
}

typedef void* (*FoliageCtor)(void* self, void* zoneMap, bool firstTimeLoad, bool buildOverlay, bool reloadData);
FoliageCtor foliageCtor_orig = 0;
void* foliageCtor_hook(void* self, void* zoneMap, bool firstTimeLoad, bool buildOverlay, bool reloadData) {
	int key = zoneMap ? keyOfZoneMap(zoneMap) : -1;
	if (key >= 0) {
		// A zone is being (re)loaded: its foliage starts from scratch, so no duplicates.
		EnterCriticalSection(&g_lock);
		ZoneFoliage& z = g_fol[key];
		z = ZoneFoliage();
		z.dirty = true;
		z.touched = GetTickCount();
		LeaveCriticalSection(&g_lock);
	}
	int before = t_ctorZone;
	t_ctorZone = key;
	void* r = foliageCtor_orig(self, zoneMap, firstTimeLoad, buildOverlay, reloadData);
	t_ctorZone = before;
	if (key >= 0) {
		EnterCriticalSection(&g_lock);
		ZoneFoliage& z = g_fol[key];
		z.dirty = true;
		z.touched = GetTickCount();
		LeaveCriticalSection(&g_lock);
	}
	return r;
}

// ---- buildings ----
struct BuildingRow {
	std::string id, name, town, faction;
	Ogre::Vector3 pos;
	Ogre::Quaternion rot;
	int flags;
};

void collectBuildings(void* content, std::vector<BuildingRow>& rows) {
	lektor<RootObject*>& things = at<lektor<RootObject*> >(content, kOffContentThings);
	for (uint32_t k = 0; k < things.size(); ++k) {
		RootObject* o = things[k];
		if (!o || o->getDataType() != BUILDING) continue;
		GameData* d = o->getGameData();
		if (!d) continue;
		BuildingRow r;
		r.id = d->stringID;
		r.name = d->name;
		r.pos = o->getPosition();
		r.rot = o->getOrientation();
		r.flags = 0;
		if (at<bool>(o, kOffBldInterior)) r.flags |= kFlagInterior;
		if (at<bool>(o, kOffBldIsFoliage)) r.flags |= kFlagFoliage;
		if (at<bool>(o, kOffBldDestroyed)) r.flags |= kFlagDestroyed;
		if (!at<bool>(o, kOffBldComplete)) r.flags |= kFlagUnfinished;
		if (at<bool>(o, kOffBldIsDoor)) r.flags |= kFlagDoor;
		if (at<void*>(o, kOffBldFurnitureOf)) r.flags |= kFlagFurniture;
		if (at<bool>(o, kOffBldOutsideFurniture)) r.flags |= kFlagOutsideFurniture;
		TownBase* town = fnBuildingTown ? fnBuildingTown(o) : 0;
		GameData* td = town ? town->getGameData() : 0;
		if (td) r.town = td->stringID;
		Faction* f = o->getFaction();
		if (f && f->data) r.faction = f->data->stringID;
		rows.push_back(r);
	}
}

unsigned hashBytes(unsigned h, const void* p, size_t n) {
	const unsigned char* b = (const unsigned char*)p;
	for (size_t k = 0; k < n; ++k) { h ^= b[k]; h *= 16777619u; }
	return h;
}

unsigned hashRows(const std::vector<BuildingRow>& rows) {
	unsigned h = 2166136261u;
	for (size_t k = 0; k < rows.size(); ++k) {
		const BuildingRow& r = rows[k];
		h = hashBytes(h, r.id.data(), r.id.size());
		h = hashBytes(h, r.town.data(), r.town.size());
		h = hashBytes(h, r.faction.data(), r.faction.size());
		h = hashBytes(h, &r.pos, sizeof(r.pos));
		h = hashBytes(h, &r.rot, sizeof(r.rot));
		h = hashBytes(h, &r.flags, sizeof(r.flags));
	}
	return h ^ (unsigned)rows.size();
}

// buildings.X.Y.json: {"v":1,"zone":[x,y],"buildings":[{"id","name","p":[x,y,z],"q":[w,x,y,z],"f":flags,"town","faction"}]}
bool writeBuildings(int key, const std::vector<BuildingRow>& rows) {
	JWriter w;
	w.beginObj().key("v").integer(1).key("zone").beginArr().integer(key / kZones).integer(key % kZones).endArr();
	w.key("buildings").beginArr();
	for (size_t k = 0; k < rows.size(); ++k) {
		const BuildingRow& r = rows[k];
		w.beginObj().key("id").str(r.id).key("name").str(r.name)
			.key("p").beginArr().num(r.pos.x).num(r.pos.y).num(r.pos.z).endArr()
			.key("q").beginArr().num(r.rot.w).num(r.rot.x).num(r.rot.y).num(r.rot.z).endArr()
			.key("f").integer(r.flags);
		if (!r.town.empty()) w.key("town").str(r.town);
		if (!r.faction.empty()) w.key("faction").str(r.faction);
		w.endObj();
	}
	w.endArr().endObj();
	if (!writeFileAtomic(zoneFile("buildings", key, "json"), w.text())) return false;
	g_bldFiles.insert(key);
	g_bldCount[key] = (int)rows.size();
	return true;
}

// Reads the buildings of one loaded zone and writes the file when the list changed.
void dumpZoneBuildings(void* zm, bool force) {
	int key = keyOfZoneMap(zm);
	void* content = key >= 0 ? loadedContent(zm) : 0;
	if (!content) return;
	std::vector<BuildingRow> rows;
	collectBuildings(content, rows);
	unsigned h = hashRows(rows);
	std::map<int, unsigned>::iterator it = g_bldHash.find(key);
	if (!force && it != g_bldHash.end() && it->second == h) return;
	if (writeBuildings(key, rows)) g_bldHash[key] = h;
}

void dumpAllBuildings(bool force) {
	void* mgr = zoneManager();
	if (!mgr) return;
	for (int k = 0; k < kZones * kZones; ++k) dumpZoneBuildings(zoneMapAt(mgr, k), force);
}

// ---- fly-over ----
struct Flyover {
	bool running;
	int x0, y0, x1, y1;
	int index;          // next cell of the rectangle, row by row (serpentine)
	bool force;
	DWORD dwellMs, timeoutMs;
	bool waiting;       // the camera stands over `zone` and waits for it to load
	int zone;
	DWORD since;
	int visited, skipped, timeouts;
	bool haveHome;
	Ogre::Vector3 home; // camera position before the fly-over
	std::string stopReason;
	Flyover() : running(false), x0(0), y0(0), x1(kZones - 1), y1(kZones - 1), index(0), force(false), dwellMs(1500),
		timeoutMs(40000), waiting(false), zone(-1), since(0), visited(0), skipped(0), timeouts(0), haveHome(false) {}
};
Flyover g_fly;

int flyTotal() { return (g_fly.x1 - g_fly.x0 + 1) * (g_fly.y1 - g_fly.y0 + 1); }

int flyZone(int index) {
	int w = g_fly.x1 - g_fly.x0 + 1;
	int row = index / w, col = index % w;
	if (row & 1) col = w - 1 - col;
	return zoneKey(g_fly.x0 + col, g_fly.y0 + row);
}

void saveFlyover() {
	JWriter w;
	w.beginObj().key("rect").beginArr().integer(g_fly.x0).integer(g_fly.y0).integer(g_fly.x1).integer(g_fly.y1).endArr()
		.key("index").integer(g_fly.index).key("force").boolean(g_fly.force).endObj();
	writeFileAtomic(g_dir + "\\flyover.json", w.text());
	--g_written; // not a zone file
}

bool loadFlyover() {
	FILE* f = fopen((g_dir + "\\flyover.json").c_str(), "rb");
	if (!f) return false;
	char buf[512];
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	JVal v;
	if (!parseJson(std::string(buf, n), v)) return false;
	const JVal& r = v.get("rect");
	if (r.type != JVal::ARR || r.a.size() != 4) return false;
	g_fly.x0 = (int)r.a[0].n; g_fly.y0 = (int)r.a[1].n; g_fly.x1 = (int)r.a[2].n; g_fly.y1 = (int)r.a[3].n;
	if (!validZone(g_fly.x0, g_fly.y0) || !validZone(g_fly.x1, g_fly.y1) || g_fly.x1 < g_fly.x0 || g_fly.y1 < g_fly.y0) return false;
	g_fly.index = (int)v.num("index", 0);
	g_fly.force = v.boolean("force", false);
	return true;
}

CameraClass* camera() { return ou && ou->player ? ou->player->getCamera() : 0; }

void moveCamera(const Ogre::Vector3& to) {
	CameraClass* cam = camera();
	if (!cam || !fnCameraTeleport) return;
	if (fnCameraStop) fnCameraStop(cam);
	fnCameraTeleport(cam, to);
}

void stopFlyover(const char* reason) {
	if (!g_fly.running) return;
	g_fly.running = false;
	g_fly.waiting = false;
	g_fly.stopReason = reason;
	saveFlyover();
	if (g_fly.haveHome) moveCamera(g_fly.home);
	flushFoliage(0);
	dumpAllBuildings(false);
}

void flyoverTick() {
	if (!g_fly.running) return;
	CameraClass* cam = camera();
	if (!cam || !zoneManager()) { stopFlyover("the world was closed"); return; }
	DWORD now = GetTickCount();
	if (g_fly.waiting) {
		void* zm = findZoneMap(g_fly.zone);
		void* content = zm ? loadedContent(zm) : 0;
		void* fol = content ? at<void*>(content, kOffContentFoliage) : 0;
		// loadComplete is the normal signal; ten seconds over a loaded zone is the fallback.
		bool ready = fol && (at<bool>(fol, kOffFolLoadComplete) || now - g_fly.since > 10000);
		if (ready) {
			// Wait until the hooks have been quiet for the dwell time.
			EnterCriticalSection(&g_lock);
			std::map<int, ZoneFoliage>::iterator it = g_fol.find(g_fly.zone);
			if (it != g_fol.end() && now - it->second.touched < g_fly.dwellMs) ready = false;
			LeaveCriticalSection(&g_lock);
		}
		bool timeout = now - g_fly.since > g_fly.timeoutMs;
		if (!(ready && now - g_fly.since >= g_fly.dwellMs) && !timeout) return;
		if (ready) {
			++g_fly.visited;
			// Zones around the camera got loaded too: write everything that is ready.
			flushFoliage(0);
			dumpAllBuildings(false);
			// A zone without a single instance still gets a file, so it is not visited again.
			if (!g_folFiles.count(g_fly.zone) && g_hookEnt) writeFoliage(g_fly.zone, ZoneFoliage());
		} else ++g_fly.timeouts;
		g_fly.waiting = false;
		++g_fly.index;
		purgeFoliage();
		if ((g_fly.index & 7) == 0) saveFlyover();
	}
	// Next zone that still needs a visit.
	int total = flyTotal();
	while (g_fly.index < total) {
		int key = flyZone(g_fly.index);
		if (g_fly.force || !g_folFiles.count(key) || !g_bldFiles.count(key)) break;
		++g_fly.skipped;
		++g_fly.index;
	}
	if (g_fly.index >= total) { stopFlyover("finished"); return; }
	int key = flyZone(g_fly.index);
	float cx = (key / kZones - kZones / 2 + 0.5f) * kZoneSize;
	float cz = (key % kZones - kZones / 2 + 0.5f) * kZoneSize;
	Ogre::Vector3 to(cx, cam->getCenter().y, cz);
	void* zm = findZoneMap(key);
	if (zm && fnZoneHeight && loadedContent(zm)) {
		float h = fnZoneHeight(zm, cx, cz);
		if (h > -90.0f) to.y = h; // -99 = no terrain loaded
	}
	moveCamera(to);
	g_fly.zone = key;
	g_fly.waiting = true;
	g_fly.since = now;
}

void writeFlyStatus(JWriter& w) {
	int total = flyTotal();
	w.beginObj().key("running").boolean(g_fly.running)
		.key("rect").beginArr().integer(g_fly.x0).integer(g_fly.y0).integer(g_fly.x1).integer(g_fly.y1).endArr()
		.key("index").integer(g_fly.index).key("total").integer(total)
		.key("visited").integer(g_fly.visited).key("skipped").integer(g_fly.skipped).key("timeouts").integer(g_fly.timeouts);
	if (g_fly.running && g_fly.zone >= 0) w.key("zone").beginArr().integer(g_fly.zone / kZones).integer(g_fly.zone % kZones).endArr();
	if (!g_fly.running && !g_fly.stopReason.empty()) w.key("stopped").str(g_fly.stopReason);
	w.key("foliageFiles").integer((long long)g_folFiles.size()).key("buildingFiles").integer((long long)g_bldFiles.size());
	w.endObj();
}

// ---- start-up ----
void scanDir() {
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((g_dir + "\\*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		int x = -1, y = -1;
		if (sscanf(fd.cFileName, "foliage.%d.%d.bin", &x, &y) == 2 && validZone(x, y) && strstr(fd.cFileName, ".tmp") == 0) g_folFiles.insert(zoneKey(x, y));
		else if (sscanf(fd.cFileName, "buildings.%d.%d.json", &x, &y) == 2 && validZone(x, y) && strstr(fd.cFileName, ".tmp") == 0) g_bldFiles.insert(zoneKey(x, y));
	} while (FindNextFileA(h, &fd));
	FindClose(h);
}

void initDir() {
	const char* env = getenv("KENSHI_BRIDGE_DUMP");
	if (env && *env) g_dir = env;
	else {
		const char* local = getenv("LOCALAPPDATA");
		if (!local || !*local) return;
		std::string base = std::string(local) + "\\KenshiBridge";
		CreateDirectoryA(base.c_str(), 0);
		g_dir = base + "\\world";
	}
	CreateDirectoryA(g_dir.c_str(), 0);
	DWORD attr = GetFileAttributesA(g_dir.c_str());
	if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) { g_lastError = "cannot create " + g_dir; g_dir.clear(); return; }
	scanDir();
}

} // namespace

void initWorldDump() {
	InitializeCriticalSection(&g_lock);
	initDir();
	fnZoneLoaded = (ZoneBool)gameFunction(0xA07900);
	fnZoneHeight = (ZoneHeight)gameFunction(0xA072C0);
	fnBuildingTown = (BuildingTown)gameFunction(0xF6BE0);
	fnCameraTeleport = (CameraTeleport)gameFunction(0x6AF190);
	fnCameraStop = (CameraStop)gameFunction(0x6ADDE0);
	if (g_dir.empty()) { ErrorLog("[KenshiBridge] world dump: no folder to write to; dump disabled"); return; }
	void* make = gameFunction(0x6D1610);
	void* ctor = gameFunction(0x6D33F0);
	// The constructor hook goes first: finalMakeEnt relies on it to tell a zone reload.
	if (make && ctor) g_hookCtor = KenshiLib::SUCCESS == KenshiLib::AddHook(ctor, &foliageCtor_hook, &foliageCtor_orig);
	if (make) g_hookEnt = KenshiLib::SUCCESS == KenshiLib::AddHook(make, &finalMakeEnt_hook, &finalMakeEnt_orig);
	if (!g_hookEnt) ErrorLog("[KenshiBridge] world dump: foliage hook failed; only buildings are dumped");
}

void worldDumpTick() {
	if (g_dir.empty()) return;
	static DWORD lastFlush = 0, lastScan = 0;
	static int scanAt = 0;
	DWORD now = GetTickCount();
	if (now - lastFlush > 500) {
		lastFlush = now;
		flushFoliage(2000);
	}
	// Buildings: a slice of the zone table per step, the whole table in about three seconds.
	void* mgr = zoneManager();
	if (mgr && now - lastScan > 100) {
		lastScan = now;
		for (int k = 0; k < 128; ++k) {
			dumpZoneBuildings(zoneMapAt(mgr, scanAt), false);
			scanAt = (scanAt + 1) % (kZones * kZones);
		}
		if (scanAt == 0) purgeFoliage();
	}
	flyoverTick();
}

bool cmdDump(const JVal& args, JWriter& w, std::string& code, std::string& message) {
	if (g_dir.empty()) { code = "unsupported"; message = "World dump is disabled: " + g_lastError; return false; }
	void* mgr = zoneManager();
	if (!mgr) { code = "no_world"; message = "The world is not loaded yet"; return false; }
	if (args.boolean("now", false)) {
		flushFoliage(0);
		dumpAllBuildings(true);
	}
	w.beginObj().key("dir").str(g_dir)
		.key("foliageHook").boolean(g_hookEnt).key("zoneReloadHook").boolean(g_hookCtor)
		.key("foliageFiles").integer((long long)g_folFiles.size())
		.key("buildingFiles").integer((long long)g_bldFiles.size())
		.key("filesWritten").integer(g_written);
	if (!g_lastError.empty()) w.key("lastError").str(g_lastError);

	// Loaded zones with what the dump holds for them.
	w.key("loaded").beginArr();
	long long folTotal = 0, bldTotal = 0;
	for (int k = 0; k < kZones * kZones; ++k) {
		void* zm = zoneMapAt(mgr, k);
		int key = keyOfZoneMap(zm);
		if (key < 0 || !loadedContent(zm)) continue;
		int fol = -1;
		bool pending = false;
		EnterCriticalSection(&g_lock);
		std::map<int, ZoneFoliage>::iterator it = g_fol.find(key);
		if (it != g_fol.end()) { fol = (int)it->second.inst.size(); pending = it->second.dirty; }
		LeaveCriticalSection(&g_lock);
		if (fol < 0 && g_folCount.count(key)) fol = g_folCount[key];
		int bld = g_bldCount.count(key) ? g_bldCount[key] : -1;
		w.beginObj().key("zone").beginArr().integer(key / kZones).integer(key % kZones).endArr();
		if (fol >= 0) { w.key("foliage").integer(fol); folTotal += fol; }
		else w.key("foliage").null(); // loaded before the plugin could see it, or not on disk
		if (bld >= 0) { w.key("buildings").integer(bld); bldTotal += bld; }
		else w.key("buildings").null();
		if (pending) w.key("pending").boolean(true);
		w.key("foliageFile").boolean(g_folFiles.count(key) != 0).endObj();
	}
	w.endArr().key("foliage").integer(folTotal).key("buildings").integer(bldTotal);

	// Optional check list around a point.
	const JVal& near_ = args.get("near");
	if (near_.type == JVal::ARR && near_.a.size() == 3) {
		float nx = (float)near_.a[0].n, nz = (float)near_.a[2].n;
		float range = (float)args.num("range", 300);
		int limit = (int)args.num("limit", 60);
		if (range <= 0 || range > 5000) { code = "bad_args"; message = "args.range must be in (0, 5000]"; return false; }
		w.key("near").beginObj().key("foliage").beginArr();
		int n = 0;
		EnterCriticalSection(&g_lock);
		for (std::map<int, ZoneFoliage>::iterator it = g_fol.begin(); it != g_fol.end(); ++it) {
			const ZoneFoliage& z = it->second;
			for (size_t k = 0; k < z.inst.size() && n < limit; ++k) {
				const FolInst& i = z.inst[k];
				float dx = i.x - nx, dz = i.z - nz;
				if (dx * dx + dz * dz > range * range) continue;
				w.beginObj().key("id").str(z.meshes[i.mesh])
					.key("p").beginArr().num(i.x).num(i.y).num(i.z).endArr()
					.key("yaw").num(i.yaw).key("scale").num(i.scale).endObj();
				++n;
			}
		}
		LeaveCriticalSection(&g_lock);
		w.endArr().key("buildings").beginArr();
		n = 0;
		for (int k = 0; k < kZones * kZones && n < limit; ++k) {
			void* content = loadedContent(zoneMapAt(mgr, k));
			if (!content) continue;
			std::vector<BuildingRow> rows;
			collectBuildings(content, rows);
			for (size_t j = 0; j < rows.size() && n < limit; ++j) {
				const BuildingRow& r = rows[j];
				float dx = r.pos.x - nx, dz = r.pos.z - nz;
				if (dx * dx + dz * dz > range * range) continue;
				w.beginObj().key("id").str(r.id).key("name").str(r.name)
					.key("p").beginArr().num(r.pos.x).num(r.pos.y).num(r.pos.z).endArr()
					.key("q").beginArr().num(r.rot.w).num(r.rot.x).num(r.rot.y).num(r.rot.z).endArr()
					.key("f").integer(r.flags).endObj();
				++n;
			}
		}
		w.endArr().endObj();
	}
	w.endObj();
	return true;
}

bool cmdFlyover(const JVal& args, JWriter& w, std::string& code, std::string& message) {
	if (g_dir.empty()) { code = "unsupported"; message = "World dump is disabled: " + g_lastError; return false; }
	std::string action = args.str("action", "status");
	if (action == "status") { writeFlyStatus(w); return true; }
	if (action == "stop") { stopFlyover("stopped by request"); writeFlyStatus(w); return true; }
	if (action != "start" && action != "resume") { code = "bad_args"; message = "args.action must be start, stop, resume or status"; return false; }
	if (!fnCameraTeleport) { code = "unsupported"; message = "The camera cannot be moved in this game build"; return false; }
	CameraClass* cam = camera();
	if (!cam || !zoneManager()) { code = "no_world"; message = "The world is not loaded yet"; return false; }
	if (g_fly.running) { code = "busy"; message = "The fly-over is already running"; return false; }

	Flyover next;
	if (action == "resume") {
		Flyover keep = g_fly;
		if (!loadFlyover()) { g_fly = keep; code = "nothing_to_resume"; message = "No stopped fly-over to resume; use action start"; return false; }
		next = g_fly;
		next.visited = next.skipped = next.timeouts = 0;
		g_fly = keep;
	} else {
		const JVal& r = args.get("rect");
		if (r.type == JVal::ARR) {
			if (r.a.size() != 4) { code = "bad_args"; message = "args.rect must be [x0, y0, x1, y1]"; return false; }
			next.x0 = (int)r.a[0].n; next.y0 = (int)r.a[1].n; next.x1 = (int)r.a[2].n; next.y1 = (int)r.a[3].n;
			if (!validZone(next.x0, next.y0) || !validZone(next.x1, next.y1) || next.x1 < next.x0 || next.y1 < next.y0) {
				code = "bad_args"; message = "args.rect: zone indices 0..63, x0 <= x1, y0 <= y1"; return false;
			}
		}
		next.force = args.boolean("force", false);
	}
	double dwell = args.num("dwell", 1500), timeout = args.num("timeout", 40);
	if (dwell < 200 || dwell > 60000 || timeout < 5 || timeout > 600) { code = "bad_args"; message = "args.dwell 200..60000 ms, args.timeout 5..600 s"; return false; }
	next.dwellMs = (DWORD)dwell;
	next.timeoutMs = (DWORD)(timeout * 1000);
	next.home = cam->getCenter();
	next.haveHome = true;
	next.running = true;
	g_fly = next;
	saveFlyover();
	writeFlyStatus(w);
	return true;
}

} // namespace kb
