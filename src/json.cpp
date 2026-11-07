// Kenshi Bridge — minimal JSON. Copyright (C) 2026 Mrgoldd11. GPLv3 or later.
#include "json.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>

namespace kb {

static const JVal kNull;

const JVal& JVal::get(const std::string& k) const {
	if (type != OBJ) return kNull;
	std::map<std::string, JVal>::const_iterator it = o.find(k);
	return it == o.end() ? kNull : it->second;
}
double JVal::num(const std::string& k, double def) const {
	const JVal& v = get(k); return v.type == NUM ? v.n : def;
}
std::string JVal::str(const std::string& k, const std::string& def) const {
	const JVal& v = get(k); return v.type == STR ? v.s : def;
}
bool JVal::boolean(const std::string& k, bool def) const {
	const JVal& v = get(k); return v.type == BOOL ? v.b : def;
}

namespace {
struct Parser {
	const std::string& t; size_t i; int depth;
	Parser(const std::string& text) : t(text), i(0), depth(0) {}
	void ws() { while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\r' || t[i] == '\n')) ++i; }
	bool lit(const char* w) {
		size_t n = strlen(w);
		if (t.compare(i, n, w) != 0) return false;
		i += n; return true;
	}
	static void utf8(unsigned cp, std::string& out) {
		if (cp < 0x80) out += (char)cp;
		else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
		else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
		else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
	}
	bool hex4(unsigned& v) {
		if (i + 4 > t.size()) return false;
		v = 0;
		for (int k = 0; k < 4; ++k) {
			char c = t[i++]; v <<= 4;
			if (c >= '0' && c <= '9') v |= c - '0';
			else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
			else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
			else return false;
		}
		return true;
	}
	bool string(std::string& out) {
		if (t[i] != '"') return false;
		++i;
		while (i < t.size()) {
			char c = t[i++];
			if (c == '"') return true;
			if (c != '\\') { out += c; continue; }
			if (i >= t.size()) return false;
			char e = t[i++];
			switch (e) {
			case '"': out += '"'; break; case '\\': out += '\\'; break; case '/': out += '/'; break;
			case 'b': out += '\b'; break; case 'f': out += '\f'; break; case 'n': out += '\n'; break;
			case 'r': out += '\r'; break; case 't': out += '\t'; break;
			case 'u': {
				unsigned cp; if (!hex4(cp)) return false;
				if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= t.size() && t[i] == '\\' && t[i + 1] == 'u') {
					i += 2; unsigned lo; if (!hex4(lo)) return false;
					cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
				}
				utf8(cp, out); break;
			}
			default: return false;
			}
		}
		return false;
	}
	bool value(JVal& v) {
		if (++depth > 32) return false;
		ws();
		if (i >= t.size()) return false;
		char c = t[i];
		bool ok = false;
		if (c == '{') {
			v.type = JVal::OBJ; ++i; ws();
			if (i < t.size() && t[i] == '}') { ++i; ok = true; }
			else for (;;) {
				ws(); std::string k;
				if (i >= t.size() || !string(k)) break;
				ws(); if (i >= t.size() || t[i] != ':') break; ++i;
				JVal item; if (!value(item)) break;
				v.o[k] = item; ws();
				if (i < t.size() && t[i] == ',') { ++i; continue; }
				if (i < t.size() && t[i] == '}') { ++i; ok = true; }
				break;
			}
		} else if (c == '[') {
			v.type = JVal::ARR; ++i; ws();
			if (i < t.size() && t[i] == ']') { ++i; ok = true; }
			else for (;;) {
				JVal item; if (!value(item)) break;
				v.a.push_back(item); ws();
				if (i < t.size() && t[i] == ',') { ++i; continue; }
				if (i < t.size() && t[i] == ']') { ++i; ok = true; }
				break;
			}
		} else if (c == '"') { v.type = JVal::STR; ok = string(v.s); }
		else if (lit("true")) { v.type = JVal::BOOL; v.b = true; ok = true; }
		else if (lit("false")) { v.type = JVal::BOOL; v.b = false; ok = true; }
		else if (lit("null")) { v.type = JVal::NUL; ok = true; }
		else {
			const char* start = t.c_str() + i; char* end = 0;
			double d = strtod(start, &end);
			if (end != start) { v.type = JVal::NUM; v.n = d; i += end - start; ok = true; }
		}
		--depth;
		return ok;
	}
};
} // namespace

bool parseJson(const std::string& text, JVal& out) {
	Parser p(text);
	if (!p.value(out)) return false;
	p.ws();
	return p.i == text.size();
}

JWriter& JWriter::num(double v) {
	sep();
	if (v != v || v > 1e300 || v < -1e300) buf += "null";
	else { char tmp[64]; sprintf_s(tmp, "%.6g", v); buf += tmp; }
	needComma = true; return *this;
}
JWriter& JWriter::integer(long long v) {
	sep(); char tmp[32]; sprintf_s(tmp, "%lld", v); buf += tmp; needComma = true; return *this;
}
void JWriter::quote(const std::string& v) {
	buf += '"';
	for (size_t k = 0; k < v.size(); ++k) {
		unsigned char c = (unsigned char)v[k];
		if (c == '"') buf += "\\\"";
		else if (c == '\\') buf += "\\\\";
		else if (c == '\n') buf += "\\n";
		else if (c == '\r') buf += "\\r";
		else if (c == '\t') buf += "\\t";
		else if (c < 0x20) { char tmp[8]; sprintf_s(tmp, "\\u%04x", c); buf += tmp; }
		else buf += (char)c;
	}
	buf += '"';
}

} // namespace kb
