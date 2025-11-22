#pragma once

#include <obs-module.h>
#include <string>
#include <array>
#include <mutex>

class WebSocketServer;

class AudioWsSource {
public:
	AudioWsSource(obs_source_t *source);
	~AudioWsSource();

	void update(obs_data_t *settings);
	void tick(float seconds);

	static void get_defaults(obs_data_t *settings);
	static obs_properties_t *get_properties(void *data);

	static void *create(obs_data_t *settings, obs_source_t *source);
	static void destroy(void *data);

	static void capture_audio(void *param, obs_source_t *source, const audio_data *audio, bool muted);
	static void capture_output_bus(void *param, size_t mix_idx, audio_data *audio);

private:
	obs_source_t *m_source = nullptr;
	obs_weak_source_t *m_audio_source = nullptr;
	std::string m_audio_source_name;
	bool m_use_output_bus = false;
	bool m_output_bus_captured = false;

	obs_audio_info m_audio_info{};
	size_t m_channels = 0;

	float m_level = 0.0f; // 0..1 之間的音量估計
	std::mutex m_level_mutex;
	bool m_capture_ok = false;
	float m_retry_accum = 0.0f;

	float m_gain = 3.0f;
	float m_noise_floor = 0.0005f;
	float m_attack = 0.7f;
	float m_release = 0.3f;
	std::array<float, 12> m_bar_levels{};
	std::array<float, 12> m_band_freqs{};
	std::array<float, 12> m_band_coef{};

	void recapture_audio();
	void release_audio_capture();
	void process_audio(const audio_data *audio, bool muted);
	void update_websocket();
	void init_bands();
};

extern obs_source_info audio_ws_source_info;
