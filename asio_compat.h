// Compatibility shim that replaces <dcserver/asio.hpp>.
//
// Provides:
//   - `namespace asio = boost::asio;` so the rest of the code can use
//     bare `asio::` symbols.
//   - `DynamicBuffer`, a thin extension of asio::streambuf that exposes
//     a `bytes()` accessor for byte-level indexing of received data.
//     The original lived in libdcserver's <dcserver/asio.hpp>; we keep
//     the same shape so call sites don't change.
#pragma once
#include <boost/asio.hpp>
#include <cstdint>

namespace asio = boost::asio;

class DynamicBuffer : public asio::streambuf
{
public:
	const uint8_t *bytes() const {
		return asio::buffer_cast<const uint8_t *>(this->data());
	}
};
