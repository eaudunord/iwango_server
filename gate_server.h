#pragma once
#include "asio_compat.h"
#include <dcserver/shared_this.hpp>
#include <array>
#include <string>

class GateConnection;

class GateServer : public SharedThis<GateServer>
{
public:
	void start();
	void setAdvertiseAddress(const std::string& address) {
		advertiseAddress = address;
	}

private:
	GateServer(asio::io_context& io_context, uint16_t port);
	void handleAccept(std::shared_ptr<GateConnection> newConnection, const boost::system::error_code& error);
	void acceptNext();
	void receiveUdp();

	asio::io_context& io_context;
	asio::ip::tcp::acceptor acceptor;
	asio::ip::udp::socket udpSocket;
	std::array<uint8_t, 1510> recvbuf;
	asio::ip::udp::endpoint source;	// source endpoint when receiving packets
	std::string advertiseAddress;

	friend super;
};
