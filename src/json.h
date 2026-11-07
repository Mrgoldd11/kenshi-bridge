// Kenshi Bridge — minimal JSON for the bridge protocol (C++ for the VS2010 toolset).
// Copyright (C) 2026 Mrgoldd11. GPLv3 or later, see LICENSE.
#pragma once
#include <string>
#include <vector>
#include <map>

namespace kb {

struct JVal {
	enum Type { NUL, BOOL, NUM, STR, ARR, OBJ };
	Type type;
	bool b;
	double n;
	std::string s;
	std::vector<JVal> a;
	std::map<std::string, JVal> o;
	JVal() : type(NUL), b(false), n(0) {}
	bool has(const std::string& k) const { return type == OBJ && o.find(k) != o.end(); }
	const JVal& get(const std::string& k) const;
	double num(const std::string& k, double def) const;
	std::string str(const std::string& k, const std::string& def) const;
	bool boolean(const std::string& k, bool def) const;
};

// Parses one JSON value. Returns false on syntax error.
bool parseJson(const std::string& text, JVal& out);

// Writer: builds compact JSON text.
class JWriter {
public:
	JWriter() : needComma(false) {}
	JWriter& beginObj() { sep(); buf += '{'; needComma = false; return *this; }
	JWriter& endObj() { buf += '}'; needComma = true; return *this; }
	JWriter& beginArr() { sep(); buf += '['; needComma = false; return *this; }
	JWriter& endArr() { buf += ']'; needComma = true; return *this; }
	JWriter& key(const char* k) { sep(); quote(k); buf += ':'; needComma = false; return *this; }
	JWriter& str(const std::string& v) { sep(); quote(v); needComma = true; return *this; }
	JWriter& num(double v);
	JWriter& integer(long long v);
	JWriter& boolean(bool v) { sep(); buf += v ? "true" : "false"; needComma = true; return *this; }
	JWriter& null() { sep(); buf += "null"; needComma = true; return *this; }
	JWriter& raw(const std::string& json) { sep(); buf += json; needComma = true; return *this; }
	const std::string& text() const { return buf; }
private:
	void sep() { if (needComma) buf += ','; }
	void quote(const std::string& v);
	std::string buf;
	bool needComma;
};

} // namespace kb
