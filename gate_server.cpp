#include "common.h"
#include "database.h"
#include "gate_server.h"
#include "models.h"
#include <boost/bind/bind.hpp>
#include <stdio.h>
#include <vector>
#include <algorithm>
#include <signal.h>

using sstream = std::stringstream;

static std::string toSjis(const std::string& str, GameId gameId) {
	return utf8ToSjis(str, gameId == GameId::GolfShiyouyo || gameId == GameId::CuldCept || gameId == GameId::RuneJade);
}

class GateProcessor
{
public:
	virtual ~GateProcessor() = default;

	//What is 0x3F6 and 0x3FF for?
	void processRequest(const std::string& request)
	{
		std::vector<std::string> split = splitString(request, ' ');
		if (split[0] == "REQUEST_FILTER")
		{
			if (split.size() < 2) {
				sendPacket(ERROR1);
				return;
			}
			GameId gameId = identifyGame(split[1]);
			// Lobby servers list
			sendPacket(0x3E8);
			LobbyServer *server = LobbyServer::getServer(gameId);
			if (server != nullptr)
			{
				sstream ss;
				ss << server->getName() << ' ' << localAddress
				   << ' ' << server->getIpPort() << " 1";
				sendPacket(0x3E9, ss.str());
			}
			sendPacket(0x3EA);
		}
		else if (split[0] == "HANDLE_LIST_GET")
		{
			if (split.size() < 4) {
				sendPacket(ERROR1);
				return;
			}
			GameId gameId = identifyGame(split[2]);
			std::string userName = split[1];
			std::string handleName;
			if (gameId != GameId::RuneJade)
			{
				if (isAnonymous(userName))
				{
						// Forcibly assign a 'PlayerN' handle
						std::string handleName;
						LobbyServer *server = LobbyServer::getServer(gameId);
						for (int i = 1; i < 100 && server != nullptr; i++)
						{
							handleName =  "Player" + std::to_string(i);
							if (server->getPlayer(handleName) == nullptr)
								break;
							handleName = "";
						}
						if (!handleName.empty())
							sendPacket(0x3F2, "1" + toSjis(handleName, gameId));
						else
							sendPacket(0x3F2);
						return;
				}
				handleName = userName;
				std::transform(handleName.begin(), handleName.end(), handleName.begin(), [](char c) {
					if (c == ' ' || c == '#' || c == '&' || c == '*' || c == '=')
						return '_';
					else
						return c;
				});
				// IWANGO max handle length is 19 chars. But Golf Shiyou 2 only accepts
				// full-width shift-JIS chars which take up 2 bytes each.
				// Hundred Swords UI only has space for 6 chars but no other issue.
				unsigned maxLength = (gameId == GameId::GolfShiyouyo || gameId == GameId::CuldCept) ? 9 : 19;
				if (gameId == GameId::Daytona)
					handleName = handleName.substr(0, maxLength - 3) + ".us";
				else
					handleName = handleName.substr(0, maxLength);
			}
			std::vector<std::string> handles = getHandles(gameId, userName, handleName);
			sstream ss;
			for (unsigned i = 0; i < handles.size(); i++)
			{
				if (i > 0)
					ss << ' ';
				ss << (i + 1) << toSjis(handles[i], gameId);
			}
			sendPacket(0x3F2, ss.str());
		}
		else if (split[0] == "HANDLE_ADD")
		{
			if (split.size() < 5) {
				sendPacket(ERROR1);
				return;
			}

			std::string userName = split[1];
			if (isAnonymous(userName)) {
				sendPacket(NAME_IN_USE1);
				return;
			}
			GameId gameId = identifyGame(split[2]);
			int handleIndx = atoi(split[3].c_str());
			std::string handlename = sjisToUtf8(split[4]);

			try {
				if (createHandle(gameId, userName, handleIndx, handlename))
					sendPacket(0x3F3, "1 " + toSjis(handlename, gameId));
				else
					sendPacket(ERROR1);
			} catch (const UniqueConstraintViolation&) {
				sendPacket(NAME_IN_USE1);
			}
		}
		else if (split[0] == "HANDLE_REPLACE")
		{
			if (split.size() < 5) {
				sendPacket(ERROR1);
				return;
			}

			std::string userName = split[1];
			if (isAnonymous(userName)) {
				sendPacket(NAME_IN_USE1);
				return;
			}
			GameId gameId = identifyGame(split[2]);
			int handleIndx = atoi(split[3].c_str());
			std::string newHandleName = sjisToUtf8(split[4]);

			try {
				if (replaceHandle(gameId, userName, handleIndx, newHandleName))
					sendPacket(0x3F4, "1 " + toSjis(newHandleName, gameId));
				else
					sendPacket(ERROR1);
			} catch (const UniqueConstraintViolation&) {
				sendPacket(NAME_IN_USE1);
			}
		}
		else if (split[0] == "HANDLE_DELETE")
		{
			if (split.size() < 5) {
				sendPacket(ERROR1);
				return;
			}

			std::string userName = split[1];
			GameId gameId = identifyGame(split[2]);
			int handleIndx = atoi(split[3].c_str());

			if (deleteHandle(gameId, userName, handleIndx))
				sendPacket(0x3F5);
			else
				sendPacket(ERROR1);
		}
	}

protected:
	enum Errors {
		ERROR1 = 0x3FC,
		NAME_IN_USE1 = 0x3FD,
		NAME_IN_USE2 = 0x3FE,
		ERROR2 = 0x3FF,
	};

	virtual void sendPacket(uint16_t opcode, const std::string& payload = {}) = 0;

	bool isAnonymous(const std::string& userName) {
		return userName == "flycast1" || userName == "flycast2" || userName == "dream";
	}

	std::string localAddress;
};

class GateConnection : public SharedThis<GateConnection>, public GateProcessor
{
public:
	asio::ip::tcp::socket& getSocket() {
		return socket;
	}

	void receive()
	{
		if (localAddress.empty())
			localAddress = socket.local_endpoint().address().to_string();
		timer.expires_at(asio::chrono::steady_clock::now() + asio::chrono::seconds(60));
		timer.async_wait(boost::bind(&GateConnection::onTimeOut, shared_from_this(), asio::placeholders::error()));
		asio::async_read_until(socket, recvBuffer, packetMatcher,
				boost::bind(&GateConnection::onReceive, shared_from_this(), asio::placeholders::error(), asio::placeholders::bytes_transferred()));
	}

	void send(const std::vector<uint8_t>& data)
	{
		memcpy(&sendBuffer[sendIdx], data.data(), data.size());
		sendIdx += data.size();
		send();
	}

private:
	GateConnection(asio::io_context& io_context)
		: socket(io_context), timer(io_context)
	{
	}

	void send()
	{
		if (sending)
			return;
		sending = true;
		uint16_t packetSize = *(uint16_t *)&sendBuffer[0] + 2;
		asio::async_write(socket, asio::buffer(sendBuffer, packetSize),
			boost::bind(&GateConnection::onSent, shared_from_this(),
					asio::placeholders::error(),
					asio::placeholders::bytes_transferred()));
	}
	void onSent(const boost::system::error_code& ec, size_t len)
	{
		if (ec)
		{
			if (ec != asio::error::eof && ec != asio::error::bad_descriptor)
				ERROR_LOG(GameId::Unknown, "gate: onSent: %s", ec.message().c_str());
			close();
			return;
		}
		sending = false;
		assert(len <= sendIdx);
		sendIdx -= len;
		if (sendIdx != 0) {
			memmove(&sendBuffer[0], &sendBuffer[len], sendIdx);
			send();
		}
	}

	using iterator = asio::buffers_iterator<asio::const_buffers_1>;

	std::pair<iterator, bool>
	static packetMatcher(iterator begin, iterator end)
	{
		if (end - begin < 3)
			return std::make_pair(begin, false);
		iterator i = begin;
		uint16_t len = (uint8_t)*i++;
		len |= uint8_t(*i++) << 8;
		len += 2;
		if (end - begin < len)
			return std::make_pair(begin, false);
		return std::make_pair(begin + len, true);
	}

	void onReceive(const boost::system::error_code& ec, size_t len)
	{
		if (ec || len < 2)
		{
			if (ec && ec != asio::error::eof && ec != asio::error::bad_descriptor && ec != asio::error::operation_aborted)
				ERROR_LOG(GameId::Unknown, "gate: onReceive: %s", ec.message().c_str());
			else if (len != 0)
				ERROR_LOG(GameId::Unknown, "gate: onReceive: small packet: %zd", len);
			else
				DEBUG_LOG(GameId::Unknown, "gate: Connection closed");
			close();
			return;
		}
		// Grab data and process if correct.
		std::string payload = std::string(&recvBuffer.bytes()[2], &recvBuffer.bytes()[len]);
		INFO_LOG(GameId::Unknown, "gate: [%s] Request [%s]", socket.remote_endpoint().address().to_string().c_str(), payload.c_str());
		processRequest(payload);
		recvBuffer.consume(len);
		receive();
	}

	void sendPacket(uint16_t opcode, const std::string& payload = {}) override
	{
		*(uint16_t *)&sendBuffer[sendIdx] = payload.size() + 2;
		*(uint16_t *)&sendBuffer[sendIdx + 2] = opcode;
		memcpy(&sendBuffer[sendIdx + 4], payload.data(), payload.length());
		sendIdx += 4 + payload.length();
		send();
	}

	void onTimeOut(const boost::system::error_code& ec)
	{
		if (ec)
			return;
		if (socket.is_open())
			try {
				ERROR_LOG(GameId::Unknown, "gate: connection timeout with %s",
						socket.remote_endpoint().address().to_string().c_str());
			} catch (...) {}
		close();
	}

	void close()
	{
		boost::system::error_code ignore;
		socket.shutdown(asio::socket_base::shutdown_both, ignore);
		socket.close(ignore);
		timer.cancel(ignore);
	}

	asio::ip::tcp::socket socket;
	asio::steady_timer timer;
	DynamicBuffer recvBuffer;
	std::array<uint8_t, 1024> sendBuffer;
	size_t sendIdx = 0;
	bool sending = false;

	friend super;
};

GateServer::GateServer(asio::io_context& io_context, uint16_t port)
	: io_context(io_context),
	  acceptor(asio::ip::tcp::acceptor(io_context,
			asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port))),
	  udpSocket(io_context, asio::ip::udp::endpoint(asio::ip::udp::v4(), port))
{
	asio::socket_base::reuse_address option(true);
	acceptor.set_option(option);
	udpSocket.set_option(option);
}

void GateServer::start()
{
	acceptNext();
	receiveUdp();
}

void GateServer::acceptNext()
{
	GateConnection::Ptr newConnection = GateConnection::create(io_context);

	acceptor.async_accept(newConnection->getSocket(),
			boost::bind(&GateServer::handleAccept, shared_from_this(), newConnection, asio::placeholders::error()));
}

void GateServer::handleAccept(GateConnection::Ptr newConnection, const boost::system::error_code& error)
{
	if (!error) {
		INFO_LOG(GameId::Unknown, "gate: New connection from %s", newConnection->getSocket().remote_endpoint().address().to_string().c_str());
		newConnection->receive();
	}
	acceptNext();
}

class UdpGateProcessor : public GateProcessor
{
public:
	UdpGateProcessor(asio::ip::udp::socket& socket, const asio::ip::udp::endpoint& remote)
		: socket(socket), remote(remote)
	{
		// FIXME no easy way to get the local address with UDP
		localAddress = "172.20.0.1";
	}

private:
	void sendPacket(uint16_t opcode, const std::string& payload = {}) override
	{
		std::vector<uint8_t> buf;
		buf.resize(payload.size() + 2);
		*(uint16_t *)buf.data() = opcode;
		memcpy(buf.data() + 2, payload.data(), payload.size());
		boost::system::error_code ec;
		socket.send_to(asio::buffer(buf), remote, 0, ec);
	}

	asio::ip::udp::socket& socket;
	const asio::ip::udp::endpoint& remote;
};

void GateServer::receiveUdp()
{
	udpSocket.async_receive_from(asio::buffer(recvbuf), source,
		[this](const boost::system::error_code& ec, size_t len)
		{
			if (ec) {
				ERROR_LOG(GameId::Unknown, "receive_from failed: %s", ec.message().c_str());
				return;
			}
			//dump(recvbuf.data(), len);
			std::string payload((const char *)recvbuf.data(), len);
			DEBUG_LOG(GameId::Unknown, "gate: [%s] Request [%s]", source.address().to_string().c_str(), payload.c_str());
			UdpGateProcessor processor(udpSocket, source);
			processor.processRequest(payload);
			receiveUdp();
		});
}
