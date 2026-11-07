// Kenshi Bridge — town list and town border rings.
// Copyright (C) 2026 Mrgoldd11. GPLv3 or later, see LICENSE.
//
// A town border is a circle around the town centre: Town::withinBordersRange(p, mult) is
// "horizontal distance < getRadius() * mult" (decoded from the exe, GOG 0x9255A0).
#include <Debug.h>
#include <core/Functions.h>
#include <kenshi/Globals.h>
#include <kenshi/SharedKing.h>
#include <kenshi/KingOfRenderThread.h>
#include <kenshi/Renderer.h>
#include <kenshi/Town.h>
#include <kenshi/GameData.h>
#include <kenshi/Faction.h>
#include <ogre/OgreSceneManager.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cmath>
#include <vector>

#include "borders.h"
#include "rva.h"

namespace kb {

namespace {

typedef lektor<RootObject*>& (*GetAllTowns)(TownList*);
GetAllTowns fnGetAllTowns = 0;

const char* typeName(int t) {
	switch (t) {
		case 0: return "nest";
		case 1: return "outpost";
		case 2: return "town";
		case 3: return "village";
		case 4: return "ruins";
		case 5: return "slave camp";
		case 6: return "military";
		case 7: return "prison";
		case 8: return "nest marker";
		default: return "other";
	}
}

struct TownRow {
	TownBase* t;
	Ogre::Vector3 pos;
	float radius;
	int type;
};

bool readVec3(const JVal& args, const char* key, Ogre::Vector3& out) {
	const JVal& v = args.get(key);
	if (v.type != JVal::ARR || v.a.size() != 3) return false;
	for (int k = 0; k < 3; ++k) if (v.a[k].type != JVal::NUM) return false;
	out = Ogre::Vector3((float)v.a[0].n, (float)v.a[1].n, (float)v.a[2].n);
	return true;
}

// Collects towns (not nests) filtered by args.near/range. Returns false on error.
bool collect(const JVal& args, std::vector<TownRow>& rows, std::string& code, std::string& message) {
	if (!fnGetAllTowns) { code = "unsupported"; message = "Town list is not available for this game build"; return false; }
	if (!shou || !shou->townList) { code = "no_world"; message = "The world is not loaded yet"; return false; }
	double mult = args.num("mult", 1.0);
	if (mult <= 0 || mult > 20) { code = "bad_args"; message = "args.mult must be a number in (0, 20]"; return false; }
	Ogre::Vector3 near_(0, 0, 0);
	bool hasNear = args.has("near");
	if (hasNear && !readVec3(args, "near", near_)) { code = "bad_args"; message = "args.near must be [x, y, z]"; return false; }
	double range = args.num("range", 0);

	lektor<RootObject*>& all = fnGetAllTowns(shou->townList);
	for (uint32_t k = 0; k < all.size(); ++k) {
		TownBase* tb = static_cast<TownBase*>(all[k]);
		if (!tb || !tb->isTown()) continue;
		TownRow r;
		r.t = tb;
		r.pos = tb->getPosition();
		r.radius = (float)(tb->getRadius() * mult);
		r.type = (int)static_cast<Town*>(tb)->townType;
		if (hasNear && range > 0) {
			double dx = r.pos.x - near_.x, dz = r.pos.z - near_.z;
			if (std::sqrt(dx * dx + dz * dz) > range + r.radius) continue;
		}
		rows.push_back(r);
	}
	return true;
}

} // namespace

void initBorders() {
	fnGetAllTowns = (GetAllTowns)gameFunction(0xB85F0);
}

bool cmdTowns(const JVal& args, JWriter& w, std::string& code, std::string& message) {
	std::vector<TownRow> rows;
	if (!collect(args, rows, code, message)) return false;
	w.beginObj().key("towns").beginArr();
	for (size_t k = 0; k < rows.size(); ++k) {
		const TownRow& r = rows[k];
		GameData* d = r.t->getGameData();
		w.beginObj()
			.key("stringId").str(d ? d->stringID : std::string())
			.key("name").str(r.t->getName())
			.key("type").integer(r.type)
			.key("typeName").str(typeName(r.type))
			.key("radius").num(r.radius)
			.key("pos").beginArr().num(r.pos.x).num(r.pos.y).num(r.pos.z).endArr()
			.endObj();
	}
	w.endArr().key("count").integer((long long)rows.size()).endObj();
	return true;
}

} // namespace kb
