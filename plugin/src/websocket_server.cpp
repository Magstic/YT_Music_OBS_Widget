#include "websocket_server.hpp"

#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET socket_t;
#define INVALID_SOCKET_VAL INVALID_SOCKET
#define CLOSESOCKET closesocket
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/select.h>
#include <errno.h>
typedef int socket_t;
#define INVALID_SOCKET_VAL (-1)
#define CLOSESOCKET ::close
#define SOCKET_ERROR (-1)
#endif

#include <sstream>
#include <cstring>
#include <chrono>
#include <cmath>
#include <obs-module.h>

#define ws_blog(level, msg, ...) blog(level, "audio-ws-ws: " msg, __VA_ARGS__)

static WebSocketServer *g_server = nullptr;

WebSocketServer *GetGlobalWebSocketServer()
{
	// lazy singleton，首次呼叫時建立並啟動在 127.0.0.1:9450
	if (!g_server) {
		g_server = new WebSocketServer();
		g_server->start(9450);
	}
	return g_server;
}

WebSocketServer::WebSocketServer() {}

WebSocketServer::~WebSocketServer()
{
	stop();
}

bool WebSocketServer::start(uint16_t port)
{
	if (m_running.load())
		return true;

	m_running = true;
	m_thread = std::thread([this, port]() { run(port); });
	if (!g_server)
		g_server = this;
	return true;
}

void WebSocketServer::stop()
{
	if (!m_running.load())
		return;
	m_running = false;
	if (m_thread.joinable())
		m_thread.join();
}

void WebSocketServer::setBars(const std::array<float, 12> &bars)
{
	std::lock_guard<std::mutex> lock(m_data_mutex);
	m_bars = bars;
}

// 簡化：此處實作一個非常基本的 WebSocket server，僅支援單連線、text frame、無分片

static bool recv_line(socket_t s, std::string &out)
{
	out.clear();
	char c = 0;
	while (true) {
		int r = recv(s, &c, 1, 0);
		if (r <= 0) return false;
		if (c == '\n') break;
		if (c != '\r') out.push_back(c);
	}
	return true;
}

// 極簡 base64 實作
static const char *B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string base64_encode(const unsigned char *data, size_t len)
{
	std::string out;
	out.reserve(((len + 2) / 3) * 4);
	for (size_t i = 0; i < len; i += 3) {
		uint32_t v = data[i] << 16;
		if (i + 1 < len) v |= data[i + 1] << 8;
		if (i + 2 < len) v |= data[i + 2];

		out.push_back(B64[(v >> 18) & 0x3F]);
		out.push_back(B64[(v >> 12) & 0x3F]);
		if (i + 1 < len)
			out.push_back(B64[(v >> 6) & 0x3F]);
		else
			out.push_back('=');
		if (i + 2 < len)
			out.push_back(B64[v & 0x3F]);
		else
			out.push_back('=');
	}
	return out;
}

// 極簡 SHA1 實作，用於 Sec-WebSocket-Accept
struct Sha1Ctx {
	uint32_t h[5];
	uint64_t length;
	unsigned char block[64];
	size_t block_len;
};

static void sha1_init(Sha1Ctx *ctx)
{
	ctx->h[0] = 0x67452301;
	ctx->h[1] = 0xEFCDAB89;
	ctx->h[2] = 0x98BADCFE;
	ctx->h[3] = 0x10325476;
	ctx->h[4] = 0xC3D2E1F0;
	ctx->length = 0;
	ctx->block_len = 0;
}

static void sha1_transform(Sha1Ctx *ctx, const unsigned char *block)
{
	uint32_t w[80];
	for (int i = 0; i < 16; ++i) {
		w[i] = (block[i * 4] << 24) | (block[i * 4 + 1] << 16) |
		       (block[i * 4 + 2] << 8) | (block[i * 4 + 3]);
	}
	for (int i = 16; i < 80; ++i) {
		uint32_t v = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
		w[i] = (v << 1) | (v >> 31);
	}

	uint32_t a = ctx->h[0];
	uint32_t b = ctx->h[1];
	uint32_t c = ctx->h[2];
	uint32_t d = ctx->h[3];
	uint32_t e = ctx->h[4];

	for (int i = 0; i < 80; ++i) {
		uint32_t f, k;
		if (i < 20) {
			f = (b & c) | ((~b) & d);
			k = 0x5A827999;
		} else if (i < 40) {
			f = b ^ c ^ d;
			k = 0x6ED9EBA1;
		} else if (i < 60) {
			f = (b & c) | (b & d) | (c & d);
			k = 0x8F1BBCDC;
		} else {
			f = b ^ c ^ d;
			k = 0xCA62C1D6;
		}
		uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[i];
		e = d;
		d = c;
		c = (b << 30) | (b >> 2);
		b = a;
		a = temp;
	}

	ctx->h[0] += a;
	ctx->h[1] += b;
	ctx->h[2] += c;
	ctx->h[3] += d;
	ctx->h[4] += e;
}

static void sha1_update(Sha1Ctx *ctx, const unsigned char *data, size_t len)
{
	ctx->length += len * 8;
	while (len > 0) {
		size_t to_copy = 64 - ctx->block_len;
		if (to_copy > len) to_copy = len;
		memcpy(ctx->block + ctx->block_len, data, to_copy);
		ctx->block_len += to_copy;
		data += to_copy;
		len -= to_copy;
		if (ctx->block_len == 64) {
			sha1_transform(ctx, ctx->block);
			ctx->block_len = 0;
		}
	}
}

static void sha1_final(Sha1Ctx *ctx, unsigned char out[20])
{
	ctx->block[ctx->block_len++] = 0x80;
	if (ctx->block_len > 56) {
		while (ctx->block_len < 64)
			ctx->block[ctx->block_len++] = 0;
		sha1_transform(ctx, ctx->block);
		ctx->block_len = 0;
	}
	while (ctx->block_len < 56)
		ctx->block[ctx->block_len++] = 0;
	for (int i = 7; i >= 0; --i)
		ctx->block[ctx->block_len++] = (unsigned char)((ctx->length >> (i * 8)) & 0xFF);
	sha1_transform(ctx, ctx->block);

	for (int i = 0; i < 5; ++i) {
		out[i * 4 + 0] = (ctx->h[i] >> 24) & 0xFF;
		out[i * 4 + 1] = (ctx->h[i] >> 16) & 0xFF;
		out[i * 4 + 2] = (ctx->h[i] >> 8) & 0xFF;
		out[i * 4 + 3] = (ctx->h[i]) & 0xFF;
	}
}

static std::string websocket_accept_key(const std::string &client_key)
{
	static const char *GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
	std::string src = client_key + GUID;
	Sha1Ctx ctx;
	sha1_init(&ctx);
	sha1_update(&ctx, (const unsigned char *)src.data(), src.size());
	unsigned char digest[20];
	sha1_final(&ctx, digest);
	return base64_encode(digest, 20);
}

std::string WebSocketServer::build_frame()
{
	// 構造簡單 text frame: FIN=1, opcode=1, 無 masking
	std::array<float, 12> bars_copy;
	{
		std::lock_guard<std::mutex> lock(m_data_mutex);
		bars_copy = m_bars;
	}

	std::ostringstream oss;
	oss << "{\"bars\":[";
	for (size_t i = 0; i < bars_copy.size(); ++i) {
		if (i) oss << ',';
		float v = bars_copy[i];
		if (v < 0.0f) v = 0.0f;
		if (v > 1.0f) v = 1.0f;
		oss << v;
	}
	oss << "]}";
	std::string payload = oss.str();

	std::string frame;
	frame.push_back((char)0x81); // FIN + text
	size_t len = payload.size();
	if (len < 126) {
		frame.push_back((char)len);
	} else if (len <= 0xFFFF) {
		frame.push_back(126);
		frame.push_back((char)((len >> 8) & 0xFF));
		frame.push_back((char)(len & 0xFF));
	} else {
		frame.push_back(127);
		for (int i = 7; i >= 0; --i)
			frame.push_back((char)((len >> (i * 8)) & 0xFF));
	}
	frame += payload;
	return frame;
}

void WebSocketServer::run(uint16_t port)
{
#ifdef _WIN32
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		ws_blog(LOG_ERROR, "WSAStartup failed (err=%d)", WSAGetLastError());
		m_running = false;
		return;
	}
#endif

	socket_t listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listen_sock == INVALID_SOCKET_VAL) {
#ifdef _WIN32
		ws_blog(LOG_ERROR, "socket() failed (err=%d)", WSAGetLastError());
		WSACleanup();
#endif
		m_running = false;
		return;
	}

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1
	addr.sin_port = htons(port);

	int yes = 1;
	setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));

	if (bind(listen_sock, (sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
#ifdef _WIN32
		ws_blog(LOG_ERROR, "bind() failed on 127.0.0.1:%d (err=%d)", (int)port, WSAGetLastError());
#else
		ws_blog(LOG_ERROR, "bind() failed on 127.0.0.1:%d", (int)port);
#endif
		CLOSESOCKET(listen_sock);
#ifdef _WIN32
		WSACleanup();
#endif
		m_running = false;
		return;
	}

	listen(listen_sock, 1);
	ws_blog(LOG_INFO, "WebSocket server listening on 127.0.0.1:%d", (int)port);

	while (m_running.load()) {
		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(listen_sock, &readfds);
		timeval tv{};
		tv.tv_sec = 0;
		tv.tv_usec = 200000; // 200ms
		int r = select((int)listen_sock + 1, &readfds, nullptr, nullptr, &tv);
		if (r <= 0) {
			continue;
		}
		if (!FD_ISSET(listen_sock, &readfds))
			continue;

		sockaddr_in client_addr{};
	#ifdef _WIN32
		int client_len = sizeof(client_addr);
	#else
		socklen_t client_len = sizeof(client_addr);
	#endif
		socket_t client = accept(listen_sock, (sockaddr *)&client_addr, &client_len);
		if (client == INVALID_SOCKET_VAL) {
#ifdef _WIN32
			ws_blog(LOG_WARNING, "accept() failed (err=%d)", WSAGetLastError());
#endif
			continue;
		}
		ws_blog(LOG_INFO, "%s", "Client accepted for WebSocket handshake");

		// WebSocket handshake
		std::string line;
		std::string key;
		while (recv_line(client, line)) {
			ws_blog(LOG_INFO, "Handshake line: %s", line.c_str());
			if (line.empty()) break; // headers end
			const std::string prefix = "Sec-WebSocket-Key:";
			if (line.rfind(prefix, 0) == 0) {
				key = line.substr(prefix.size());
				// trim
				size_t first = key.find_first_not_of(" \t");
				size_t last = key.find_last_not_of(" \t");
				if (first != std::string::npos)
					key = key.substr(first, last - first + 1);
			}
		}
		ws_blog(LOG_INFO, "Handshake header parse finished, key_len=%d", (int)key.size());
		if (key.empty()) {
			ws_blog(LOG_WARNING, "%s", "Handshake missing Sec-WebSocket-Key, closing client");
			CLOSESOCKET(client);
			continue;
		}

		std::string accept_key = websocket_accept_key(key);
		ws_blog(LOG_INFO, "Client Sec-WebSocket-Key: %s", key.c_str());
		ws_blog(LOG_INFO, "Computed Sec-WebSocket-Accept: %s", accept_key.c_str());
		ws_blog(LOG_DEBUG, "%s", "Sec-WebSocket-Key received, sending 101 response");
		std::ostringstream resp;
		resp << "HTTP/1.1 101 Switching Protocols\r\n";
		resp << "Upgrade: websocket\r\n";
		resp << "Connection: Upgrade\r\n";
		resp << "Sec-WebSocket-Accept: " << accept_key << "\r\n\r\n";
		std::string resp_str = resp.str();
		int hs_sent = send(client, resp_str.c_str(), (int)resp_str.size(), 0);
		if (hs_sent <= 0) {
#ifdef _WIN32
			ws_blog(LOG_ERROR, "Handshake send() failed (err=%d)", WSAGetLastError());
#endif
			CLOSESOCKET(client);
			continue;
		}

		// 簡單的推送 loop：直到連線關閉或伺服器停止
		while (m_running.load()) {
			std::string frame = build_frame();
			int sent = send(client, frame.data(), (int)frame.size(), 0);
			if (sent <= 0) {
#ifdef _WIN32
				ws_blog(LOG_INFO, "send() to client failed/closed (err=%d)", WSAGetLastError());
#endif
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(60)); // 約 16 FPS
		}

		CLOSESOCKET(client);
		ws_blog(LOG_INFO, "%s", "Client disconnected");
	}

	CLOSESOCKET(listen_sock);
#ifdef _WIN32
	WSACleanup();
#endif
}
