/*
 * Copyright (c) 2005-2008  cielacanth <cielacanth AT s60.xrea.com>
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *    1. Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *    2. Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "lldebug_prec.h"
#include "lldebug_codeconv.h"
#include "lldebug_luainfo.h"

#include <sstream>

namespace lldebug {

std::string LuaToString(lua_State *L, int idx) {
	int type = lua_type(L, idx);
	bool ascii = true;
	char buffer[512];
	std::string str;

	switch (type) {
	case LUA_TNONE:
		str = "none";
		break;
	case LUA_TNIL:
		str = "nil";
		break;
	case LUA_TBOOLEAN:
		str = (lua_toboolean(L, idx) ? "true" : "false");
		break;
	case LUA_TNUMBER: {
		lua_Number d = lua_tonumber(L, idx);
		lua_Integer i;
		lua_number2int(i, d);
		if ((lua_Number)i == d) {
			snprintf(buffer, sizeof(buffer), "%d", i);
		}
		else {
			snprintf(buffer, sizeof(buffer), "%f", d);
		}
		str = buffer;
		}
		break;
	case LUA_TSTRING:
		snprintf(buffer, sizeof(buffer), "%s", lua_tostring(L, idx));
		str = buffer;
		ascii = false;
		break;
	case LUA_TFUNCTION:
	case LUA_TTHREAD:
	case LUA_TTABLE:
		snprintf(buffer, sizeof(buffer), "%p", lua_topointer(L, idx));
		str = buffer;
		break;
	case LUA_TUSERDATA:
	case LUA_TLIGHTUSERDATA:
		lua_pushvalue(L, idx);
		lua_pushliteral(L, "tostring");
		lua_gettable(L, LUA_GLOBALSINDEX);
		lua_insert(L, -2);
		lua_pcall(L, 1, 1, 0);
		snprintf(buffer, sizeof(buffer), "%s", lua_tostring(L, -1));
		str = buffer;
		lua_pop(L, 1);
		ascii = false;
		break;
	default:
		return std::string("");
	}

	return (ascii ? str : ConvToUTF8(str));
}


LuaVar::LuaVar()
	: m_L(NULL), m_valueType(LUA_TNONE) {
}

LuaVar::LuaVar(lua_State *L, VarRootType type, int level)
	: m_L(L), m_rootType(type), m_level(level), m_hasFields(false) {
	m_valueType = LUA_TTABLE;
	m_value = "root table";
}

LuaVar::LuaVar( shared_ptr<LuaVar> parent, const std::string &name, int valueIdx)
	: m_L(parent->m_L), m_rootType(parent->m_rootType), m_level(parent->m_level)
	, m_parent(parent), m_name(name), m_hasFields(false) {
	m_valueType = lua_type(m_L, valueIdx);
	m_value = LuaToString(m_L, valueIdx);
}

LuaVar::LuaVar(shared_ptr<LuaVar> parent, int keyIdx, int valueIdx)
	: m_L(parent->m_L), m_rootType(parent->m_rootType), m_level(parent->m_level)
	, m_parent(parent), m_hasFields(false) {
	m_name = LuaToString(m_L, keyIdx);
	m_valueType = lua_type(m_L, valueIdx);
	m_value = LuaToString(m_L, valueIdx);
}

LuaVar::~LuaVar() {
}


LuaBackTraceInfo::LuaBackTraceInfo(lua_State *L, int level,
								   lua_Debug *ar,
								   const std::string &sourceTitle) {
	std::string name;

	if (*ar->namewhat != '\0') { /* is there a name? */
		name = ConvToUTF8(ar->name);
	}
	else {
		if (*ar->what == 'm') { /* main? */
			name = "main chunk";
		}
		else if (*ar->what == 'C' || *ar->what == 't') {
			name = std::string("?");  /* C function or tail call */
		}
		else {
			std::stringstream stream;
			stream << "no name [defined <" << ar->short_src << ":" << ar->linedefined << ">]";
			stream.flush();
			name = stream.str();
		}
	}

	m_lua = L;
	m_key = ar->source;
	m_sourceTitle = sourceTitle;
	m_line = ar->currentline;
	m_funcName = name;
	m_level = level;
}

LuaBackTraceInfo::~LuaBackTraceInfo() {
}

}
