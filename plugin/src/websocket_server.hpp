#pragma once

#include <array>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include <string>
#include <cstdint>

// 非高性能實作，只面向本機單用戶場景，足夠驅動 widget。

class WebSocketServer {
public:
	WebSocketServer();
	~WebSocketServer();

	bool start(uint16_t port);
	void stop();

	void setBars(const std::array<float, 12> &bars);

private:
	std::atomic<bool> m_running{false};
	std::thread m_thread;

	std::mutex m_data_mutex;
	std::array<float, 12> m_bars{};

	void run(uint16_t port);

	std::string build_frame();
};

// 提供一個全域單例，方便在來源內共用伺服器
WebSocketServer *GetGlobalWebSocketServer();
