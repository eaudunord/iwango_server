#pragma once
#include <boost/asio.hpp>
namespace asio = boost::asio;
#include <dcserver/shared_this.hpp>
#include <array>

class GateConnection;

class GateServer : public SharedThis<GateServer>
{
public:
	void start();

private:
	GateServer(asio::io_context& io_context, uint16_t port);
	void handleAccept(std::shared_ptr<GateConnection> newConnection, const asio::error_code& error);
	void acceptNext();
	void receiveUdp();

	asio::io_context& io_context;
	asio::ip::tcp::acceptor acceptor;
	asio::ip::udp::socket udpSocket;
	std::array<uint8_t, 1510> recvbuf;
	asio::ip::udp::endpoint source;	// source endpoint when receiving packets

	friend super;
};
