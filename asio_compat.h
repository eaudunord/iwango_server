// Compatibility shim that replaces <dcserver/asio.hpp>.
//
// Provides:
//   - `namespace asio = boost::asio;` so the rest of the code can use
//     bare `asio::` symbols.
//   - `DynamicBuffer` as an alias for `asio::streambuf`. We do NOT
//     subclass streambuf because it is noncopyable/nonmovable, which
//     makes overload resolution against asio::async_read_until prefer
//     the DynamicBuffer-concept overload that requires movability and
//     fails to compile.
//   - A free `bytes()` helper for streambuf, replacing the member
//     accessor previously provided by libdcserver's wrapper.
#pragma once
#include <boost/asio.hpp>
#include <cstdint>

namespace asio = boost::asio;

using DynamicBuffer = asio::streambuf;

inline const uint8_t *bytes(const asio::streambuf& buf) {
	return asio::buffer_cast<const uint8_t *>(buf.data());
}
