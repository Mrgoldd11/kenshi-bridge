// Kenshi Bridge — dialogue selection trace.
// Copyright (C) 2026 Mrgoldd11. GPLv3 or later, see LICENSE.
//
// How it works (Kenshi 1.0.65): Dialogue::sendEvent -> Dialogue::_chooseDialog(list, target) walks
// the event's candidate list (sorted by score) and calls DialogLineData::checkConditions on the
// candidates it reaches. checkConditions has side effects (unique ownership, permanent chance
// rolls), so the trace never calls it itself: it hooks it and records what the game evaluated.
#include <Debug.h>
#include <core/Functions.h>
#include <kenshi/Character.h>
#include <kenshi/Dialogue.h>
#include <kenshi/GameData.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <deque>
#include <vector>
#include <sstream>

#include "json.h"
#include "trace.h"
#include "rva.h"

namespace kb {

namespace {

struct Check { DialogLineData* line; bool pass; int depth; };

struct Pending {
	std::vector<Check> checks;
};

CRITICAL_SECTION g_lock;
std::deque<std::pair<long long, std::string> > g_records;
long long g_seq = 0;
volatile LONG g_on = 0;
volatile LONG g_all = 0;
const size_t kMaxRecords = 300;

__declspec(thread) int t_event = -1;     // event of the innermost sendEvent on this thread
__declspec(thread) int t_depth = 0;      // _chooseDialog recursion depth
__declspec(thread) Pending* t_pending = 0;

std::string idOf(DialogLineData* d) { return d && d->data ? d->data->stringID : std::string(); }
std::string nameOf(DialogLineData* d) { return d && d->data ? d->data->name : std::string(); }

void writeChar(JWriter& w, Character* c) {
	if (!c) { w.null(); return; }
	GameData* g = c->getGameData();
	w.beginObj().key("name").str(c->getName())
		.key("templateId").str(g ? g->stringID : std::string())
		.key("player").boolean(c->isPlayerCharacter())
		.endObj();
}

bool inList(DialogChoiceList* list, DialogLineData* d) {
	if (!list) return false;
	for (uint32_t k = 0; k < list->conversationChoices.size(); ++k)
		if (list->conversationChoices[k] == d) return true;
	return false;
}

// The top-level entry of `list` that `result` belongs to (result may be a leaf of an empty node).
DialogLineData* topOf(DialogChoiceList* list, DialogLineData* result) {
	for (DialogLineData* d = result; d; d = d->parent)
		if (inList(list, d)) return d;
	return result;
}

void record(Dialogue* self, DialogChoiceList* list, Character* target, bool wordswap,
            DialogLineData* result, const Pending& p) {
	Character* me = self->me;
	DialogLineData* top = topOf(list, result);
	JWriter w;
	w.beginObj();
	w.key("seq").integer(0); // replaced below, under the lock
	w.key("event").integer(t_event);
	w.key("wordswap").boolean(wordswap);
	w.key("speaker"); writeChar(w, me);
	w.key("target"); writeChar(w, target);
	w.key("chosen");
	if (result) w.beginObj().key("dialogue").str(idOf(top)).key("dialogueName").str(nameOf(top))
		.key("line").str(idOf(result)).endObj();
	else w.null();
	w.key("candidates").beginArr();
	std::ostringstream log;
	log << "[KenshiBridge trace] ev=" << t_event << " speaker=" << (me ? me->getName() : std::string("?"))
	    << " chosen=" << (result ? idOf(top) : std::string("none")) << " |";
	for (uint32_t k = 0; k < list->conversationChoices.size(); ++k) {
		DialogLineData* d = list->conversationChoices[k];
		if (!d) continue;
		int status = -1; // -1 not reached, 0 failed, 1 passed
		for (size_t c = 0; c < p.checks.size(); ++c)
			if (p.checks[c].line == d && p.checks[c].depth == 1) status = p.checks[c].pass ? 1 : 0;
		int scoreFor = target ? d->getScore(target) : d->score;
		const char* st = status == 1 ? "pass" : status == 0 ? "fail" : "not_reached";
		w.beginObj()
			.key("id").str(idOf(d)).key("name").str(nameOf(d))
			.key("score").integer(d->score)
			.key("scoreForTarget").integer(scoreFor)
			.key("interjection").boolean(d->isInterjection)
			.key("chanceTemporary").num(d->chanceTemporary)
			.key("chancePermanent").num(d->chancePermanent)
			.key("conditions").str(st)
			.key("chosen").boolean(d == top)
			.endObj();
		log << " " << idOf(d) << "(" << scoreFor << "," << st << (d == top ? ",CHOSEN" : "") << ")";
	}
	w.endArr();
	w.key("checks").beginArr();
	for (size_t c = 0; c < p.checks.size(); ++c)
		w.beginObj().key("id").str(idOf(p.checks[c].line)).key("depth").integer(p.checks[c].depth)
			.key("pass").boolean(p.checks[c].pass).endObj();
	w.endArr();
	w.endObj();

	std::string text = w.text();
	EnterCriticalSection(&g_lock);
	long long seq = ++g_seq;
	std::ostringstream s; s << "\"seq\":" << seq;
	const std::string ph = "\"seq\":0";
	size_t at = text.find(ph);
	if (at != std::string::npos) text.replace(at, ph.size(), s.str());
	g_records.push_back(std::make_pair(seq, text));
	while (g_records.size() > kMaxRecords) g_records.pop_front();
	LeaveCriticalSection(&g_lock);
	DebugLog(log.str());
}

bool wanted(Dialogue* self, Character* target) {
	if (g_all) return true;
	if (t_event == 1) return true; // EV_PLAYER_TALK_TO_ME
	if (target && target->isPlayerCharacter()) return true;
	if (self->me && self->me->isPlayerCharacter()) return true;
	return false;
}

bool (*sendEvent_orig)(Dialogue*, Character*, EventTriggerEnum) = 0;
bool sendEvent_hook(Dialogue* self, Character* who, EventTriggerEnum what) {
	int saved = t_event;
	t_event = (int)what;
	bool r = sendEvent_orig(self, who, what);
	t_event = saved;
	return r;
}

bool (*sendEventOverride_orig)(Dialogue*, Character*, EventTriggerEnum, bool) = 0;
bool sendEventOverride_hook(Dialogue* self, Character* who, EventTriggerEnum what, bool forceRepeat) {
	int saved = t_event;
	t_event = (int)what;
	bool r = sendEventOverride_orig(self, who, what, forceRepeat);
	t_event = saved;
	return r;
}

bool (*checkConditions_orig)(DialogLineData*, Dialogue*, Character*, bool) = 0;
bool checkConditions_hook(DialogLineData* self, Dialogue* dialog, Character* target, bool isWordswap) {
	bool r = checkConditions_orig(self, dialog, target, isWordswap);
	if (t_pending && t_pending->checks.size() < 512) {
		Check c; c.line = self; c.pass = r; c.depth = t_depth;
		t_pending->checks.push_back(c);
	}
	return r;
}

DialogLineData* (*chooseDialog_orig)(Dialogue*, DialogChoiceList*, Character*, bool) = 0;
DialogLineData* chooseDialog_hook(Dialogue* self, DialogChoiceList* list, Character* target, bool isWordswap) {
	if (!g_on || t_depth > 0 || !list || !wanted(self, target)) {
		++t_depth;
		DialogLineData* r = chooseDialog_orig(self, list, target, isWordswap);
		--t_depth;
		return r;
	}
	Pending p;
	t_pending = &p;
	t_depth = 1;
	DialogLineData* r = chooseDialog_orig(self, list, target, isWordswap);
	t_depth = 0;
	t_pending = 0;
	try { record(self, list, target, isWordswap, r, p); }
	catch (...) { ErrorLog("[KenshiBridge] trace record failed"); }
	return r;
}

} // namespace

void installTraceHooks() {
	InitializeCriticalSection(&g_lock);
	bool ok = true;
	// sendEvent/sendEventOverride are not exported by KenshiLib 0.5.0: resolved via rva.h.
	// Without them the trace still works, only the "event" field stays -1.
	void* se = gameFunction(0x684210);
	void* seo = gameFunction(0x684EE0);
	if (se) ok &= KenshiLib::SUCCESS == KenshiLib::AddHook(se, &sendEvent_hook, &sendEvent_orig);
	if (seo) ok &= KenshiLib::SUCCESS == KenshiLib::AddHook(seo, &sendEventOverride_hook, &sendEventOverride_orig);
	if (!se || !seo) ok = false;
	ok &= KenshiLib::SUCCESS == KenshiLib::AddHook(KenshiLib::GetRealAddress(&DialogLineData::checkConditions), &checkConditions_hook, &checkConditions_orig);
	ok &= KenshiLib::SUCCESS == KenshiLib::AddHook(KenshiLib::GetRealAddress(&Dialogue::_chooseDialog), &chooseDialog_hook, &chooseDialog_orig);
	if (!ok) ErrorLog("[KenshiBridge] some dialogue trace hooks failed; trace may be incomplete");
}

bool traceEnabled() { return g_on != 0; }
bool traceAll() { return g_all != 0; }
void setTraceEnabled(bool on, bool all) {
	InterlockedExchange(&g_all, all ? 1 : 0);
	InterlockedExchange(&g_on, on ? 1 : 0);
	DebugLog(on ? "[KenshiBridge] dialogue trace ON" : "[KenshiBridge] dialogue trace OFF");
}

std::string traceSince(long long since) {
	JWriter w;
	EnterCriticalSection(&g_lock);
	w.beginObj().key("next").integer(g_seq).key("items").beginArr();
	for (size_t k = 0; k < g_records.size(); ++k)
		if (g_records[k].first > since) w.raw(g_records[k].second);
	w.endArr().endObj();
	LeaveCriticalSection(&g_lock);
	return w.text();
}

} // namespace kb
