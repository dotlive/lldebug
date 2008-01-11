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

#ifndef __LLDEBUG_EVENT_H__
#define __LLDEBUG_EVENT_H__

namespace lldebug {

class Context;

class wxContextEvent : public wxEvent {
public:
	explicit wxContextEvent(wxEventType type, int winid, Context *ctx);
	virtual ~wxContextEvent();

	virtual Context *GetContext() const {
		return m_ctx;
	}

	virtual wxEvent *Clone() const {
		return new wxContextEvent(*this);
	}

private:
	Context *m_ctx;
};

class wxChangedStateEvent : public wxEvent {
public:
	explicit wxChangedStateEvent(wxEventType type, int winid, bool value);
	virtual ~wxChangedStateEvent();

	bool GetValue() const {
		return m_value;
	}

	void SetValue(bool value) {
		m_value = value;
	}

	virtual wxEvent *Clone() const {
		return new wxChangedStateEvent(*this);
	}

private:
	bool m_value;
};

class wxSourceLineEvent : public wxContextEvent {
public:
	explicit wxSourceLineEvent(wxEventType type, int winid,
							   Context *ctx, lua_State *L, bool isCurrentRunning,
							   const std::string &key, const std::string &title,
							   int line, int level);
	virtual ~wxSourceLineEvent();

	/// Get lua_State* object.
	lua_State *GetLua() const {
		return m_lua;
	}

	/// Get identifier key of the source file.
	const std::string &GetKey() const {
		return m_key;
	}

	/// Get title of the source file.
	const std::string &GetTitle() const {
		return m_title;
	}

	/// Get number of line.
	int GetLine() const {
		return m_line;
	}

	/// Are this source and line current running ?
	/// (considering backtrace)
	bool IsCurrentRunning() const {
		return m_isCurrentRunning;
	}

	/// Get the level of stack frame of the function.
	int GetLevel() const {
		return m_level;
	}

	virtual wxEvent *Clone() const {
		return new wxSourceLineEvent(*this);
	}

private:
	lua_State *m_lua;
	std::string m_key;
	std::string m_title;
	int m_line;
	bool m_isCurrentRunning;
	int m_level;
};

void SendCreateFrameEvent(Context *ctx);
void SendDestroyedFrameEvent(Context *ctx);

BEGIN_DECLARE_EVENT_TYPES()
#if !wxCHECK_VERSION(2, 5, 0)
	DECLARE_EXPORTED_LOCAL_EVENT_TYPE(WXDLLIMPEXP_LLDEBUG, wxEVT_CREATE_FRAME, 2650)
	DECLARE_EXPORTED_LOCAL_EVENT_TYPE(WXDLLIMPEXP_LLDEBUG, wxEVT_DESTROY_FRAME, 2651)
	DECLARE_EXPORTED_LOCAL_EVENT_TYPE(WXDLLIMPEXP_LLDEBUG, wxEVT_CHANGED_STATE, 2652)
	DECLARE_EXPORTED_LOCAL_EVENT_TYPE(WXDLLIMPEXP_LLDEBUG, wxEVT_UPDATE_SOURCE, 2653)
#else
	DECLARE_EXPORTED_EVENT_TYPE(WXDLLIMPEXP_LLDEBUG, wxEVT_CREATE_FRAME, 2650)
	DECLARE_EXPORTED_EVENT_TYPE(WXDLLIMPEXP_LLDEBUG, wxEVT_DESTROY_FRAME, 2651)
	DECLARE_EXPORTED_EVENT_TYPE(WXDLLIMPEXP_LLDEBUG, wxEVT_CHANGED_STATE, 2652)
	DECLARE_EXPORTED_EVENT_TYPE(WXDLLIMPEXP_LLDEBUG, wxEVT_UPDATE_SOURCE, 2653)
#endif
END_DECLARE_EVENT_TYPES()

typedef void (wxEvtHandler::*wxContextEventFunction)(wxContextEvent &);
typedef void (wxEvtHandler::*wxChangedStateEventFunction)(wxChangedStateEvent &);
typedef void (wxEvtHandler::*wxSourceLineEventFunction)(wxSourceLineEvent &);

#if !wxCHECK_VERSION(2, 5, 0)
#define EVT_LLDEBUG_CREATE_FRAME (id, fn) DECLARE_EVENT_TABLE_ENTRY(wxEVT_CREATE_FRAME , id, -1, (wxObjectEventFunction)(wxEventFunction)(wxContextEventFunction)&fn, (wxObject *)NULL),
#define EVT_LLDEBUG_DESTROY_FRAME(id, fn) DECLARE_EVENT_TABLE_ENTRY(wxEVT_DESTROY_FRAME, id, -1, (wxObjectEventFunction)(wxEventFunction)(wxContextEventFunction)&fn, (wxObject *)NULL),
#define EVT_LLDEBUG_CHANGED_STATE(id, fn) DECLARE_EVENT_TABLE_ENTRY(wxEVT_CHANGED_STATE, id, wxCONCAT(id, _END), (wxObjectEventFunction)(wxEventFunction)(wxChangedStateEventFunction)&fn, (wxObject *)NULL),
#define EVT_LLDEBUG_UPDATE_SOURCE(id, fn) DECLARE_EVENT_TABLE_ENTRY(wxEVT_UPDATE_SOURCE, id, wxCONCAT(id, _END), (wxObjectEventFunction)(wxEventFunction)(wxSourceLineEventFunction)&fn, (wxObject *)NULL),
#else
#define EVT_LLDEBUG_CREATE_FRAME (id, fn) DECLARE_EVENT_TABLE_ENTRY(wxEVT_CREATE_FRAME,  id, wxID_ANY, (wxObjectEventFunction)(wxEventFunction)wxStaticCastEvent(wxContextEventFunction, &fn), (wxObject *)NULL),
#define EVT_LLDEBUG_DESTROY_FRAME(id, fn) DECLARE_EVENT_TABLE_ENTRY(wxEVT_DESTROY_FRAME, id, wxID_ANY, (wxObjectEventFunction)(wxEventFunction)wxStaticCastEvent(wxContextEventFunction, &fn), (wxObject *)NULL),
#define EVT_LLDEBUG_CHANGED_STATE(id, fn) DECLARE_EVENT_TABLE_ENTRY(wxEVT_CHANGED_STATE, id, wxCONCAT(id, _END), (wxObjectEventFunction)(wxEventFunction)wxStaticCastEvent(wxChangedStateEventFunction, &fn), (wxObject *)NULL),
#define EVT_LLDEBUG_UPDATE_SOURCE(id, fn) DECLARE_EVENT_TABLE_ENTRY(wxEVT_UPDATE_SOURCE, id, wxCONCAT(id, _END), (wxObjectEventFunction)(wxEventFunction)wxStaticCastEvent(wxSourceLineEventFunction, &fn), (wxObject *)NULL),
#endif

}

#endif
