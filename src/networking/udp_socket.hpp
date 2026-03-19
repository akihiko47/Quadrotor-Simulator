#pragma once

#include <string>
#include <optional>
#include <cstdint>
#include <chrono>
#include <stdexcept>

#ifdef _WIN32
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#pragma comment(lib, "ws2_32.lib")
#else
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>
	#include <unistd.h>
	#include <fcntl.h>
	#include <errno.h>
#endif

class UDPSocket final {
public:
	explicit UDPSocket(std::uint16_t port) {
		m_socket = socket(AF_INET, SOCK_DGRAM, 0);
		if (m_socket == invalidHandle()) {
			throw std::runtime_error("Failed to create socket");
		}

		setNonBlocking(true);

		// Bind to local address
		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = INADDR_ANY;
		addr.sin_port = htons(port);

		if (bind(m_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
			closeNativeSocket(m_socket);
			m_socket = invalidHandle();
			throw std::runtime_error("Failed to bind socket");
		}
	}

	~UDPSocket() noexcept {
		cleanup();
	}

	UDPSocket(const UDPSocket&) = delete;
	UDPSocket& operator=(const UDPSocket&) = delete;
	UDPSocket(UDPSocket&& other) = delete;
	UDPSocket& operator=(UDPSocket&& other) = delete;

	void setNonBlocking(bool enable) {
		if (m_socket == invalidHandle()) {
			throw std::runtime_error("set_non_blocking called on invalid socket");
		}

		#ifdef _WIN32
			u_long mode = enable ? 1 : 0;
			if (ioctlsocket(m_socket, FIONBIO, &mode) != 0) {
				throw std::runtime_error("Failed to set non‑blocking mode");
			}
		#else
			int flags = fcntl(m_socket, F_GETFL, 0);
			if (flags == -1) {
				throw std::runtime_error("Failed to get socket flags");
			}
			if (enable) {
				flags |= O_NONBLOCK;
			} else {
				flags &= ~O_NONBLOCK;
			}
			if (fcntl(m_socket, F_SETFL, flags) == -1) {
				throw std::runtime_error("Failed to set socket flags");
			}
		#endif

		m_nonBlocking = enable;
	}

	// Structure returned by receive method
	struct ReceivedMessage {
		std::uint32_t senderIp;      // Network byte order (as in sockaddr_in::sin_addr.s_addr)
		std::uint16_t senderPort;    // Network byte order
		std::string data;
	};

	// Receive a message. In non‑blocking mode returns std::nullopt if no data available
	// In blocking mode blocks until data arrives
	// Throws std::runtime_error on unrecoverable errors
	std::optional<ReceivedMessage> receive() {
		if (m_socket == invalidHandle()) {
			throw std::runtime_error("receive called on invalid socket");
		}

		char buffer[1024];
		sockaddr_in sender{};
		socklen_t sender_len = sizeof(sender);

		int bytes = recvfrom(m_socket, buffer, sizeof(buffer), 0,
							 reinterpret_cast<sockaddr*>(&sender), &sender_len);

		if (bytes > 0) {
			ReceivedMessage msg;
			msg.data.assign(buffer, bytes);
			msg.senderIp = sender.sin_addr.s_addr;      // already network byte order
			msg.senderPort = sender.sin_port;           // network byte order
			return msg;
		}

		if (bytes == 0) {
			// Not possible for UDP
			return std::nullopt;
		}

		// Error or no data in non‑blocking mode
		if (m_nonBlocking && wouldBlock()) {
			return std::nullopt;
		}

		// Real error
		throw std::runtime_error("recvfrom failed");
	}

	// Send a message to a given IPv4 address
	bool sendTo(const std::string& data, const std::uint32_t& ip, std::uint16_t port) noexcept {
		if (m_socket == invalidHandle()) {
			return false;
		}

		sockaddr_in dest{};
		dest.sin_family = AF_INET;
		dest.sin_addr.s_addr = ip; // already network byte order
		dest.sin_port = port;      // network byte order

		int sent = sendto(m_socket, data.data(), data.size(), 0,
						  reinterpret_cast<sockaddr*>(&dest), sizeof(dest));

		if (sent == static_cast<int>(data.size())) {
			return true;
		}

		return false;
	}

	// Close socket
	void cleanup() noexcept {
		if (m_socket != invalidHandle()) {
			closeNativeSocket(m_socket);
			m_socket = invalidHandle();
		}
	}

private:
	#ifdef _WIN32
		using NativeHandle = SOCKET;
		static constexpr NativeHandle invalidHandle() noexcept { return INVALID_SOCKET; }
		static void closeNativeSocket(NativeHandle s) noexcept { closesocket(s); }
		static bool wouldBlock() noexcept {
			int err = WSAGetLastError();
			return err == WSAEWOULDBLOCK;
		}
		static std::string getLastErrorString() {
			return std::to_string(WSAGetLastError());
		}
	#else
		using NativeHandle = int;
		static constexpr NativeHandle invalid_handle() noexcept { return -1; }
		static void closeNativeSocket(NativeHandle s) noexcept { ::close(s); }
		static bool wouldBlock() noexcept {
			int err = errno;
			return err == EAGAIN || err == EWOULDBLOCK;
		}
		static std::string getLastErrorString() {
			return std::string(strerror(errno));
		}
	#endif

	NativeHandle m_socket = invalidHandle();
	bool m_nonBlocking = false;
};