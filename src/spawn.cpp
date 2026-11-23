// Kenshi Bridge — spawn command (put a character/animal into the world).
// Copyright (C) 2026 Mrgoldd11. GPLv3 or later, see LICENSE.
#include <Debug.h>
#include <kenshi/Globals.h>
#include <kenshi/GameWorld.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Character.h>
#include <kenshi/Faction.h>
#include <kenshi/GameData.h>
#include <kenshi/CameraClass.h>
#include <kenshi/RootObjectFactory.h>

#include "spawn.h"
#include "rva.h"

namespace kb {
namespace {

// Member functions the installed KenshiLib does not export; `this` is the first argument.
typedef GameData* (*GetData)(GameDataManager*, const std::string&);
typedef Faction* (*FactionById)(FactionManager*, const std::string&);
typedef RootObject* (*CreateRandomCharacter)(RootObjectFactory*, Faction*, Ogre::Vector3, RootObjectContainer*, GameData*, Building*, float);
GetData fnGetData = 0;
FactionById fnFactionById = 0;
CreateRandomCharacter fnCreateCharacter = 0;

bool readPos(const JVal& args, Ogre::Vector3& out) {
	const JVal& v = args.get("pos");
	if (v.type != JVal::ARR || v.a.size() != 3) return false;
	for (int k = 0; k < 3; ++k) if (v.a[k].type != JVal::NUM) return false;
	out = Ogre::Vector3((float)v.a[0].n, (float)v.a[1].n, (float)v.a[2].n);
	return true;
}

} // namespace

bool initSpawn() {
	fnGetData = (GetData)gameFunction(0x6BCAA0);
	fnFactionById = (FactionById)gameFunction(0x2E7570);
	fnCreateCharacter = (CreateRandomCharacter)gameFunction(0x582F60);
	return fnGetData && fnFactionById && fnCreateCharacter;
}

bool cmdSpawn(const JVal& args, JWriter& w, std::string& code, std::string& message) {
	if (!fnGetData || !fnFactionById || !fnCreateCharacter) {
		code = "unsupported"; message = "This command is not available for this game build"; return false;
	}
	std::string id = args.str("id", "");
	if (id.empty()) { code = "bad_args"; message = "args.id must be the string id of a CHARACTER record"; return false; }
	Ogre::Vector3 pos;
	if (args.has("pos")) {
		if (!readPos(args, pos)) { code = "bad_args"; message = "args.pos must be [x, y, z]"; return false; }
	} else {
		CameraClass* cam = ou->player ? ou->player->getCamera() : 0;
		if (!cam) { code = "no_camera"; message = "Camera is not ready; pass args.pos"; return false; }
		pos = cam->getCenter();
	}
	GameData* data = fnGetData(&ou->gamedata, id);
	if (!data) { code = "not_found"; message = "No record with string id " + id; return false; }
	if (data->type != CHARACTER && data->type != ANIMAL_CHARACTER) {
		code = "bad_type"; message = "Record " + id + " is not a character record"; return false;
	}

	// Animals take age as 0..1 (0 = pup); default: grown.
	float age = (float)args.num("age", 1.0);
	if (age < 0) age = 0;
	if (age > 1) age = 1;
	Faction* faction = 0;
	std::string fname = args.str("faction", "");
	if (fname == "player") {
		Character* c = ou->player ? ou->player->selectedCharacter.getCharacter() : 0;
		faction = c ? c->getFaction() : 0;
	} else if (!fname.empty()) {
		faction = fnFactionById(ou->factionMgr, fname);
		if (!faction) { code = "not_found"; message = "No faction with string id " + fname; return false; }
	} else {
		const Ogre::vector<GameDataReference>::type* refs = data->getReferenceListIfExists("faction");
		if (refs && !refs->empty()) faction = fnFactionById(ou->factionMgr, (*refs)[0].sid);
	}
	if (!faction) faction = ou->factionMgr->getEmptyFaction();

	RootObject* o = fnCreateCharacter(ou->theFactory, faction, pos, 0, data, 0, age);
	if (!o) { code = "failed"; message = "The game did not create the character"; return false; }
	w.beginObj().key("id").str(id).key("name").str(data->name)
		.key("pos").beginArr().num(pos.x).num(pos.y).num(pos.z).endArr().endObj();
	return true;
}

}
