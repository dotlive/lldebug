/*
 */

#include "lldebug_prec.h"
#include "lldebug_remoteengine.h"
#include "lldebug_serialization.h"

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <sstream>

namespace lldebug {

using boost::asio::ip::tcp;

class SocketBase {
private:
	boost::asio::ip::tcp::socket m_socket;
	const CommandCallback m_callback;
	bool m_isConnected;

	typedef std::queue<Command> WriteCommandQueue;
	/// This object is handled in one specific thread.
	WriteCommandQueue m_writeCommandQueue;
	
public:
	/// Write command data.
	void Write(const CommandHeader &header, const CommandData &data) {
		Command command(header, data);

		GetService().post(boost::bind(&SocketBase::doWrite, this, command));
	}

	/// Close this socket.
	void Close() {
		GetService().post(boost::bind(&SocketBase::doClose, this));
	}

	void Connected() {
		GetService().post(boost::bind(&SocketBase::connected, this));
	}

	/// Is this socket connected ?
	bool IsConnected() {
		return m_isConnected;
	}

	/// Get the io_service object.
	boost::asio::io_service &GetService() {
		return m_socket.io_service();
	}

	/// Get the socket object.
	boost::asio::ip::tcp::socket &GetSocket() {
		return m_socket;
	}

	/// Get the callback.
	CommandCallback GetCallback() {
		return m_callback;
	}

protected:
	SocketBase(boost::asio::io_service &ioService,
			   const CommandCallback &callback)
		: m_socket(ioService), m_callback(callback)
		, m_isConnected(false) {
	}

	virtual ~SocketBase() {
	}

private:
	void connected() {
		if (!m_isConnected) {
			m_isConnected = true;
			asyncReadCommand();
		}
	}

	void asyncReadCommand() {
		shared_ptr<Command> command(new Command);

		boost::asio::async_read(m_socket,
			boost::asio::buffer(&command->GetHeader(), sizeof(CommandHeader)),
			boost::bind(
				&SocketBase::handleReadCommand, this, command,
				boost::asio::placeholders::error));
	}

	void handleReadCommand(shared_ptr<Command> command,
						   const boost::system::error_code &error) {
		if (!error) {
			// Check that response is OK.
			if (command->GetCtxId() < 0) {
				doClose();
			}

			// Read the command data if exists.
			if (command->GetDataSize() > 0) {
				std::vector<char> &data = command->GetData().GetImplData();
				data.resize(command->GetDataSize());

				boost::asio::async_read(m_socket,
					boost::asio::buffer(data, command->GetDataSize()),
					boost::bind(
						&SocketBase::handleReadData, this, command,
						boost::asio::placeholders::error));
			}
			else {
				handleReadData(command, error);
			}
		}
		else {
			doClose();
		}
	}

	/// It's called after the end of the command reading.
	void handleReadData(shared_ptr<Command> command,
						const boost::system::error_code &error) {
		if (!error) {
			assert(command != NULL);

			// m_callback may change.
			CommandCallback callback = m_callback;
			callback(*command);

			// Prepare the new command.
			asyncReadCommand();
		}
		else {
			doClose();
		}
	}

	/// Send the asynchronous write order.
	/// The memory of the command must be kept somewhere.
	void asyncWrite(const Command &command) {
		if (command.GetDataSize() == 0) {
			// Delete the command memory.
			boost::asio::async_write(m_socket,
				boost::asio::buffer(&command.GetHeader(), sizeof(CommandHeader)),
				boost::bind(
					&SocketBase::handleWrite, this, true,
					boost::asio::placeholders::error));
		}
		else {
			// Don't delete the command memory.
			boost::asio::async_write(m_socket,
				boost::asio::buffer(&command.GetHeader(), sizeof(CommandHeader)),
				boost::bind(
					&SocketBase::handleWrite, this, false,
					boost::asio::placeholders::error));

			// Write command data.
			boost::asio::async_write(m_socket,
				boost::asio::buffer(command.GetData().GetImplData(), command.GetDataSize()),
				boost::bind(
					&SocketBase::handleWrite, this, true,
					boost::asio::placeholders::error));
		}
	}

	/// Do the asynchronous writing of the command.
	/// The command memory must be kept until the end of the writing,
	/// so there is a write command queue.
	void doWrite(const Command &command) {
		bool isProgress = !m_writeCommandQueue.empty();
		m_writeCommandQueue.push(command);

		if (!isProgress) {
			asyncWrite(m_writeCommandQueue.front());
		}
	}

	/// It's called after the end of writing command.
	/// The command memory is deleted if possible.
	void handleWrite(bool deleteCommand,
					 const boost::system::error_code& error) {
		if (!error) {
			if (deleteCommand) {
				m_writeCommandQueue.pop();

				// Begin the new write order.
				if (!m_writeCommandQueue.empty()) {
					asyncWrite(m_writeCommandQueue.front());
				}
			}
		}
		else {
			doClose();
		}
	}

	void doClose() {
		m_socket.close();
		m_isConnected = false;
	}
};


/*-----------------------------------------------------------------*/
class ContextSocket : public SocketBase {
private:
	tcp::acceptor m_acceptor;

public:
	explicit ContextSocket(boost::asio::io_service &ioService,
						 const CommandCallback &callback,
						 tcp::endpoint endpoint)
		: SocketBase(ioService, callback)
		, m_acceptor(ioService, endpoint) {
	}

	/// Start connection.
	int Start(int waitSeconds) {
		m_acceptor.async_accept(GetSocket(),
			boost::bind(
				&ContextSocket::handleAccept, this,
				boost::asio::placeholders::error));

		// Wait for connection if need.
		if (waitSeconds >= 0) {
			boost::xtime current, end;
			boost::xtime_get(&end, boost::TIME_UTC);
			end.sec += waitSeconds;

			// IsConnected become true in handleConnect.
			while (!IsConnected()) {
				boost::xtime_get(&current, boost::TIME_UTC);
				if (boost::xtime_cmp(current, end) >= 0) {
					return -1;
				}

				// Do async operation.
				GetService().poll_one();
				GetService().reset();

				current.nsec += 100 * 1000 * 1000;
				boost::thread::sleep(current);
			}
		}
		
		return 0;
	}

	virtual ~ContextSocket() {
	}

protected:
	void handleAccept(const boost::system::error_code &error) {
		if (!error) {
			this->Connected();
		}
		else {
			m_acceptor.async_accept(GetSocket(),
				boost::bind(
					&ContextSocket::handleAccept, this,
					boost::asio::placeholders::error));
		}
	}
};


/*-----------------------------------------------------------------*/
class FrameSocket : public SocketBase {
private:
	tcp::resolver::iterator m_endpointIterator;

public:
	explicit FrameSocket(boost::asio::io_service &ioService,
						   const CommandCallback &callback,
						   tcp::resolver::query query)
		: SocketBase(ioService, callback) {
		tcp::resolver resolver(ioService);
		m_endpointIterator = resolver.resolve(query);
	}

	virtual ~FrameSocket() {
	}

	/// Start connection.
	int Start(int waitSeconds) {
		tcp::resolver::iterator endpoint_iterator = m_endpointIterator;
		tcp::endpoint endpoint = *endpoint_iterator;

		GetSocket().async_connect(endpoint,
			boost::bind(
				&FrameSocket::handleConnect, this,
				boost::asio::placeholders::error, endpoint_iterator));

		// Wait for connection if need.
		if (waitSeconds >= 0) {
			boost::xtime current, end;
			boost::xtime_get(&end, boost::TIME_UTC);
			end.sec += waitSeconds;

			// IsOpen become true in handleConnect.
			while (!IsConnected()) {
				boost::xtime_get(&current, boost::TIME_UTC);
				if (boost::xtime_cmp(current, end) >= 0) {
					return -1;
				}

				// Do async operation.
				GetService().poll_one();
				GetService().reset();

				current.nsec += 100 * 1000 * 1000;
				boost::thread::sleep(current);
			}
		}
		
		return 0;
	}

protected:
	virtual void handleConnect(const boost::system::error_code &error,
							   tcp::resolver::iterator endpointIterator) {
		if (!error) {
			// The connection was successful.
			this->Connected();
		}
		else {
			//if (endpointIterator != tcp::resolver::iterator()) {
			// The connection failed. Try the next endpoint in the list.
			tcp::endpoint endpoint = *endpointIterator;

			GetSocket().close();
			GetSocket().async_connect(endpoint,
				boost::bind(
					&FrameSocket::handleConnect, this,
					boost::asio::placeholders::error, endpointIterator));
		}
		/*else {
			std::cout << "Error: " << error << "\n";
		}*/
	}
};


/*-----------------------------------------------------------------*/
RemoteEngine::RemoteEngine()
	: m_isThreadActive(false), m_commandIdCounter(0)
	, m_ctxId(-1) {
}

RemoteEngine::~RemoteEngine() {
	StopThread();
}

int RemoteEngine::StartContext(int portNum, int ctxId, int waitSeconds) {
	scoped_lock lock(m_mutex);
	shared_ptr<ContextSocket> socket;

	// limit time
	boost::xtime xt;
	boost::xtime_get(&xt, boost::TIME_UTC);
	xt.sec += waitSeconds;

	socket.reset(new ContextSocket(m_ioService,
		boost::bind(
			&RemoteEngine::HandleReadCommand, this, _1),
			tcp::endpoint(tcp::v4(), portNum)));

	// Start connection.
	if (socket->Start(waitSeconds) != 0) {
		return -1;
	}

	StartThread();
	m_commandIdCounter = 1;
	m_socket = boost::shared_static_cast<SocketBase>(socket);
	StartConnection(ctxId);

	// Wait for START_CONNECTION and ctxId value.
	if (m_ctxId < 0) {
		m_ctxCond.timed_wait(lock, xt);

		if (m_ctxId < 0) {
			return -1;
		}
	}

	return 0;
}

int RemoteEngine::StartFrame(const std::string &hostName,
							 const std::string &portName,
							 int waitSeconds) {
	scoped_lock lock(m_mutex);
	shared_ptr<FrameSocket> socket;

	// limit time
	boost::xtime xt;
	boost::xtime_get(&xt, boost::TIME_UTC);
	xt.sec += waitSeconds;

	// Create socket object.
	socket.reset(new FrameSocket(m_ioService,
		boost::bind(
			&RemoteEngine::HandleReadCommand, this, _1),
			tcp::resolver::query(hostName, portName)));

	// Start connection.
	if (socket->Start(waitSeconds) != 0) {
		return -1;
	}

	StartThread();
	m_commandIdCounter = 2;
	m_socket = boost::shared_static_cast<SocketBase>(socket);

	// Wait for START_CONNECTION and ctxId value.
	if (m_ctxId < 0) {
		m_ctxCond.timed_wait(lock, xt);

		if (m_ctxId < 0) {
			return -1;
		}
	}

	return 0;
}

void RemoteEngine::SetCtxId(int ctxId) {
	scoped_lock lock(m_mutex);

	if (m_ctxId != ctxId) {
		m_ctxId = ctxId;
		m_ctxCond.notify_all();
	}
}

bool RemoteEngine::IsConnected() {
	scoped_lock lock(m_mutex);

	if (m_socket == NULL) {
		return false;
	}

	return m_socket->IsConnected();
}

void RemoteEngine::SetReadCommandCallback(const CommandCallback &callback) {
	scoped_lock lock(m_mutex);

	m_readCommandCallback = callback;
}

bool RemoteEngine::IsThreadActive() {
	scoped_lock lock(m_mutex);

	return m_isThreadActive;
}

void RemoteEngine::SetThreadActive(bool is) {
	scoped_lock lock(m_mutex);

	if (m_isThreadActive == is) {
		return;
	}

	m_isThreadActive = is;

	if (!m_isThreadActive) {
		/*Command command;
		command.header.type = REMOTECOMMANDTYPE_END_CONNECTION;
		command.header.ctxId = m_ctxId;
		command.header.commandId = 0;
		command.header.dataSize = 0;
		m_readCommandCallback(command);*/
	}
}

void RemoteEngine::StartThread() {
	scoped_lock lock(m_mutex);

	if (!IsThreadActive()) {
		boost::function0<void> fn
			= boost::bind(&RemoteEngine::ServiceThread, this);
		m_thread.reset(new boost::thread(fn));
	}
}

void RemoteEngine::StopThread() {
	if (IsThreadActive()) {
		SetThreadActive(false);
	}

	if (m_thread != NULL) {
		m_thread->join();
		m_thread.reset();
		m_socket.reset();
	}
}

void RemoteEngine::ServiceThread() {
	// If IsThreadActive is true, the thread has already started.
	if (IsThreadActive()) {
		return;
	}

	SetThreadActive(true);
	try {
		for (;;) {
			// 10 works are set.
			for (int i = 0; i < 10; ++i) {
				m_ioService.poll_one();
			}

			if (!IsThreadActive() && m_ioService.poll_one() == 0) {
				break;
			}

			m_ioService.reset();
			boost::xtime xt;
			boost::xtime_get(&xt, boost::TIME_UTC);
			xt.nsec += 10 * 1000 * 1000; // 1ms = 1000 * 1000nsec
			boost::thread::sleep(xt);
		}
	}
	catch (std::exception &ex) {
		printf("%s\n", ex.what());
	}

	SetThreadActive(false);
}

void RemoteEngine::HandleReadCommand(const Command &command) {
	scoped_lock lock(m_mutex);
	bool isResponseCommand = false;

	// First, find a response command.
	WaitResponseCommandList::iterator it = m_waitResponseCommandList.begin();
	while (it != m_waitResponseCommandList.end()) {
		const CommandHeader &header_ = (*it).header;

		if (command.GetCtxId() == header_.ctxId
			&& command.GetCommandId() == header_.commandId) {
			isResponseCommand = true;
			if (IsThreadActive()) {
				(*it).response(command);
			}
			it = m_waitResponseCommandList.erase(it);
		}
		else {
			++it;
		}
	}

	switch (command.GetType()) {
	case REMOTECOMMANDTYPE_START_CONNECTION:
		SetCtxId(command.GetCtxId());
		return;
	case REMOTECOMMANDTYPE_END_CONNECTION:
		SetCtxId(-1);
		return;
	default:
		if (m_ctxId < 0) {
			SetCtxId(command.GetCtxId());
		}
		break;
	}

	if (!isResponseCommand) {
		if (m_readCommandCallback) {
			CommandCallback callback = m_readCommandCallback;

			lock.unlock();
			callback(command);
			lock.lock();
		}
	}
}

CommandHeader RemoteEngine::InitCommandHeader(RemoteCommandType type,
											  size_t dataSize,
											  int commandId) {
	scoped_lock lock(m_mutex);
	CommandHeader header;

	header.type = type;
	header.ctxId = m_ctxId;
	header.dataSize = (boost::uint32_t)dataSize;

	// Set a new value to header.commandId if commandId == 0
	if (commandId == 0) {
		header.commandId = m_commandIdCounter;
		m_commandIdCounter += 2;
	}
	else {
		header.commandId = commandId;
	}

	return header;
}

void RemoteEngine::WriteCommand(RemoteCommandType type,
								const CommandData &data) {
	scoped_lock lock(m_mutex);
	CommandHeader header;

	header = InitCommandHeader(type, data.GetSize());
	m_socket->Write(header, data);
}

void RemoteEngine::WriteCommand(RemoteCommandType type,
								const CommandData &data,
								const CommandCallback &response) {
	scoped_lock lock(m_mutex);
	WaitResponseCommand wcommand;

	wcommand.header = InitCommandHeader(type, data.GetSize());
	wcommand.response = response;
	m_socket->Write(wcommand.header, data);
	m_waitResponseCommandList.push_back(wcommand);
}

void RemoteEngine::WriteResponse(const Command &readCommand,
								 RemoteCommandType type,
								 const CommandData &data) {
	scoped_lock lock(m_mutex);
	CommandHeader header;

	header = InitCommandHeader(
		type,
		data.GetSize(),
		readCommand.GetCommandId());
	m_socket->Write(header, data);
}

void RemoteEngine::ResponseSuccessed(const Command &command) {
	scoped_lock lock(m_mutex);

	WriteResponse(command, REMOTECOMMANDTYPE_SUCCESSED, CommandData());
}

void RemoteEngine::ResponseFailed(const Command &command) {
	scoped_lock lock(m_mutex);

	WriteResponse(command, REMOTECOMMANDTYPE_FAILED, CommandData());
}

void RemoteEngine::StartConnection(int ctxId) {
	scoped_lock lock(m_mutex);
	CommandHeader header;

	header = InitCommandHeader(
		REMOTECOMMANDTYPE_START_CONNECTION,
		0, 0);
	header.ctxId = ctxId;
	m_socket->Write(header, CommandData());
}

void RemoteEngine::EndConnection() {
	scoped_lock lock(m_mutex);

	WriteCommand(
		REMOTECOMMANDTYPE_END_CONNECTION,
		CommandData());
}

void RemoteEngine::ChangedState(bool isBreak) {
	scoped_lock lock(m_mutex);
	CommandData data;

	data.Set_ChangedState(isBreak);
	WriteCommand(
		REMOTECOMMANDTYPE_CHANGED_STATE,
		data);
}

void RemoteEngine::UpdateSource(const std::string &key, int line, int updateSourceCount, const CommandCallback &response) {
	scoped_lock lock(m_mutex);
	CommandData data;

	data.Set_UpdateSource(key, line, updateSourceCount);
	WriteCommand(
		REMOTECOMMANDTYPE_UPDATE_SOURCE,
		data,
		response);
}

void RemoteEngine::AddedSource(const Source &source) {
	scoped_lock lock(m_mutex);
	CommandData data;

	data.Set_AddedSource(source);
	WriteCommand(
		REMOTECOMMANDTYPE_ADDED_SOURCE,
		data);
}

/// Notify that the breakpoint was set.
void RemoteEngine::SetBreakpoint(const Breakpoint &bp) {
	scoped_lock lock(m_mutex);
	CommandData data;

	data.Set_SetBreakpoint(bp);
	WriteCommand(
		REMOTECOMMANDTYPE_SET_BREAKPOINT,
		data);
}

void RemoteEngine::RemoveBreakpoint(const Breakpoint &bp) {
	scoped_lock lock(m_mutex);
	CommandData data;

	data.Set_RemoveBreakpoint(bp);
	WriteCommand(
		REMOTECOMMANDTYPE_REMOVE_BREAKPOINT,
		data);
}

void RemoteEngine::ChangedBreakpointList(const BreakpointList &bps) {
	scoped_lock lock(m_mutex);
	CommandData data;

	data.Set_ChangedBreakpointList(bps);
	WriteCommand(
		REMOTECOMMANDTYPE_CHANGED_BREAKPOINTLIST,
		data);
}

void RemoteEngine::Break() {
	scoped_lock lock(m_mutex);

	WriteCommand(
		REMOTECOMMANDTYPE_BREAK,
		CommandData());
}

void RemoteEngine::Resume() {
	scoped_lock lock(m_mutex);

	WriteCommand(
		REMOTECOMMANDTYPE_RESUME,
		CommandData());
}

void RemoteEngine::StepInto() {
	scoped_lock lock(m_mutex);

	WriteCommand(
		REMOTECOMMANDTYPE_STEPINTO,
		CommandData());
}

void RemoteEngine::StepOver() {
	scoped_lock lock(m_mutex);

	WriteCommand(
		REMOTECOMMANDTYPE_STEPOVER,
		CommandData());
}

void RemoteEngine::StepReturn() {
	scoped_lock lock(m_mutex);

	WriteCommand(
		REMOTECOMMANDTYPE_STEPRETURN,
		CommandData());
}

struct VarListResponseHandler {
	LuaVarListCallback m_callback;

	explicit VarListResponseHandler(const LuaVarListCallback &callback)
		: m_callback(callback) {
	}

	void operator()(const Command &command) {
		LuaVarList vars;
		command.GetData().Get_ValueVarList(vars);
		m_callback(command, vars);
	}
};

void RemoteEngine::RequestFieldsVarList(const LuaVar &var, const LuaVarListCallback &callback) {
	scoped_lock lock(m_mutex);
	CommandData data;

	data.Set_RequestFieldVarList(var);
	WriteCommand(
		REMOTECOMMANDTYPE_REQUEST_FIELDSVARLIST,
		data,
		VarListResponseHandler(callback));
}

void RemoteEngine::RequestLocalVarList(const LuaStackFrame &stackFrame, const LuaVarListCallback &callback) {
	scoped_lock lock(m_mutex);
	CommandData data;

	data.Set_RequestLocalVarList(stackFrame);
	WriteCommand(
		REMOTECOMMANDTYPE_REQUEST_LOCALVARLIST,
		data,
		VarListResponseHandler(callback));
}

void RemoteEngine::RequestGlobalVarList(const LuaVarListCallback &callback) {
	scoped_lock lock(m_mutex);

	WriteCommand(
		REMOTECOMMANDTYPE_REQUEST_GLOBALVARLIST,
		CommandData(),
		VarListResponseHandler(callback));
}

void RemoteEngine::RequestRegistryVarList(const LuaVarListCallback &callback) {
	scoped_lock lock(m_mutex);

	WriteCommand(
		REMOTECOMMANDTYPE_REQUEST_REGISTRYVARLIST,
		CommandData(),
		VarListResponseHandler(callback));
}

void RemoteEngine::RequestEnvironVarList(const LuaVarListCallback &callback) {
	scoped_lock lock(m_mutex);

	WriteCommand(
		REMOTECOMMANDTYPE_REQUEST_ENVIRONVARLIST,
		CommandData(),
		VarListResponseHandler(callback));
}

void RemoteEngine::RequestStackList(const LuaVarListCallback &callback) {
	scoped_lock lock(m_mutex);

	WriteCommand(
		REMOTECOMMANDTYPE_REQUEST_STACKLIST,
		CommandData(),
		VarListResponseHandler(callback));
}

void RemoteEngine::ResponseVarList(const Command &command, const LuaVarList &vars) {
	scoped_lock lock(m_mutex);
	CommandData data;

	data.Set_ValueVarList(vars);
	WriteResponse(
		command,
		REMOTECOMMANDTYPE_VALUE_VARLIST,
		data);
}


/*-----------------------------------------------------------------*/
CommandData::CommandData() {
}

CommandData::CommandData(const std::vector<char> &data)
	: m_data(data) {
}

CommandData::~CommandData() {
}
Command::Command() {
}

Command::Command(const CommandHeader &header, const CommandData &data)
	: m_header(header), m_data(data) {
}

Command::~Command() {
}

void CommandData::Get_ChangedState(bool &isBreak) const {
	Serializer::ToValue(m_data, isBreak);
}

void CommandData::Set_ChangedState(bool isBreak) {
	m_data = Serializer::ToData(isBreak);
}

void CommandData::Get_UpdateSource(std::string &key, int &line,
								   int &updateSourceCount) const {
	Serializer::ToValue(m_data, key, line, updateSourceCount);
}
void CommandData::Set_UpdateSource(const std::string &key, int line,
								   int updateSourceCount) {
	m_data = Serializer::ToData(key, line, updateSourceCount);
}

void CommandData::Get_AddedSource(Source &source) const {
	Serializer::ToValue(m_data, source);
}
void CommandData::Set_AddedSource(const Source &source) {
	m_data = Serializer::ToData(source);
}

void CommandData::Get_SetBreakpoint(Breakpoint &bp) const {
	Serializer::ToValue(m_data, bp);
}
void CommandData::Set_SetBreakpoint(const Breakpoint &bp) {
	m_data = Serializer::ToData(bp);
}

void CommandData::Get_RemoveBreakpoint(Breakpoint &bp) const {
	Serializer::ToValue(m_data, bp);
}
void CommandData::Set_RemoveBreakpoint(const Breakpoint &bp) {
	m_data = Serializer::ToData(bp);
}

void CommandData::Get_ChangedBreakpointList(BreakpointList &bps) const {
	Serializer::ToValue(m_data, bps);
}
void CommandData::Set_ChangedBreakpointList(const BreakpointList &bps) {
	m_data = Serializer::ToData(bps);
}

void CommandData::Get_RequestFieldVarList(LuaVar &var) const {
	Serializer::ToValue(m_data, var);
}
void CommandData::Set_RequestFieldVarList(const LuaVar &var) {
	m_data = Serializer::ToData(var);
}

void CommandData::Get_RequestLocalVarList(LuaStackFrame &stackFrame) const {
	Serializer::ToValue(m_data, stackFrame);
}
void CommandData::Set_RequestLocalVarList(const LuaStackFrame &stackFrame) {
	m_data = Serializer::ToData(stackFrame);
}

void CommandData::Get_ValueVarList(LuaVarList &vars) const {
	Serializer::ToValue(m_data, vars);
}
void CommandData::Set_ValueVarList(const LuaVarList &vars) {
	m_data = Serializer::ToData(vars);
}

void CommandData::Get_ValueBacktraceList(LuaBacktraceList &backtraces) const {
	Serializer::ToValue(m_data, backtraces);
}
void CommandData::Set_ValueBacktraceList(const LuaBacktraceList &backtraces) {
	m_data = Serializer::ToData(backtraces);
}

}