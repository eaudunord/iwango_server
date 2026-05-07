#pragma once
#include <dcserver/asio.hpp>
#include <dcserver/shared_this.hpp>
#include <array>

class GateConnection;

class GateServer : public SharedThis<GateServer>
{
public:
	void start();

private:
	GateServer(asio::io_context& io_context, uint16_t port);
	void handleAccept(std::shared_ptr<GateConnection> newConnection, const std::error_code& error);
	void acceptNext();
	void receiveUdp();

	asio::io_context& io_context;
	asio::ip::tcp::acceptor acceptor;
	asio::ip::udp::socket udpSocket;
	std::array<uint8_t, 1510> recvbuf;
	asio::ip::udp::endpoint source;	// source endpoint when receiving packets

	friend super;
};
