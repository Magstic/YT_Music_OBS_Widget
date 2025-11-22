#include "audio_ws_source.hpp"
#include "websocket_server.hpp"

#include <util/platform.h>
#include <cmath>

#define blog(level, msg, ...) blog(level, "audio-ws: " msg, ##__VA_ARGS__)

static const char *P_AUDIO_SRC = "audio_source";
static const char *P_OUTPUT_BUS = "output_bus";
static const char *P_GAIN = "gain";
static const char *P_NOISE_FLOOR = "noise_floor";
static const char *P_ATTACK = "attack";
static const char *P_RELEASE = "release";

// === AudioWsSource implementation ===

AudioWsSource::AudioWsSource(obs_source_t *source)
	: m_source(source)
{
	memset(&m_audio_info, 0, sizeof(m_audio_info));
	if (obs_get_audio_info(&m_audio_info)) {
		m_channels = get_audio_channels(m_audio_info.speakers);
	} else {
		m_channels = 0;
	}
	init_bands();
}

AudioWsSource::~AudioWsSource()
{
	release_audio_capture();
}

void AudioWsSource::get_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, P_AUDIO_SRC, P_OUTPUT_BUS);
	obs_data_set_default_double(settings, P_GAIN, 3.0);
	obs_data_set_default_double(settings, P_NOISE_FLOOR, 0.0005);
	obs_data_set_default_double(settings, P_ATTACK, 0.7);
	obs_data_set_default_double(settings, P_RELEASE, 0.3);
}

obs_properties_t *AudioWsSource::get_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();

	obs_property_t *list = obs_properties_add_list(props, P_AUDIO_SRC, "Audio Source",
						     OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	// special value: output bus (整體混音輸出)
	obs_property_list_add_string(list, "(Output Bus)", P_OUTPUT_BUS);

	obs_properties_add_float_slider(props, P_GAIN, "Waveform Gain", 0.5, 16.0, 0.5);
	obs_properties_add_float_slider(props, P_NOISE_FLOOR, "Noise Floor", 0.0, 0.01, 0.0001);
	obs_properties_add_float_slider(props, P_ATTACK, "Attack (0-1)", 0.0, 1.0, 0.05);
	obs_properties_add_float_slider(props, P_RELEASE, "Release (0-1)", 0.0, 1.0, 0.05);

	// 枚舉所有帶音訊的來源
	obs_enum_sources([](void *param, obs_source_t *src) {
		obs_property_t *list = (obs_property_t *)param;
		if (obs_source_get_output_flags(src) & OBS_SOURCE_AUDIO) {
			const char *name = obs_source_get_name(src);
			obs_property_list_add_string(list, name, name);
		}
		return true;
	}, list);

	return props;
}

void *AudioWsSource::create(obs_data_t *settings, obs_source_t *source)
{
	AudioWsSource *ctx = new AudioWsSource(source);
	ctx->update(settings);
	return ctx;
}

void AudioWsSource::destroy(void *data)
{
	delete static_cast<AudioWsSource *>(data);
}

void AudioWsSource::update(obs_data_t *settings)
{
	const char *src_name = obs_data_get_string(settings, P_AUDIO_SRC);
	if (!src_name || strcmp(src_name, "") == 0) {
		src_name = P_OUTPUT_BUS;
	}

	m_use_output_bus = (strcmp(src_name, P_OUTPUT_BUS) == 0);
	m_audio_source_name = m_use_output_bus ? std::string() : std::string(src_name);

	double gain = obs_data_get_double(settings, P_GAIN);
	double noise_floor = obs_data_get_double(settings, P_NOISE_FLOOR);
	double attack = obs_data_get_double(settings, P_ATTACK);
	double release = obs_data_get_double(settings, P_RELEASE);

	if (gain < 0.1)
		gain = 0.1;
	if (gain > 16.0)
		gain = 16.0;
	if (noise_floor < 0.0)
		noise_floor = 0.0;
	if (noise_floor > 0.1)
		noise_floor = 0.1;
	if (attack < 0.0)
		attack = 0.0;
	if (attack > 1.0)
		attack = 1.0;
	if (release < 0.0)
		release = 0.0;
	if (release > 1.0)
		release = 1.0;

	{
		std::lock_guard<std::mutex> lock(m_level_mutex);
		m_gain = (float)gain;
		m_noise_floor = (float)noise_floor;
		m_attack = (float)attack;
		m_release = (float)release;
	}

	release_audio_capture();
	recapture_audio();
}

void AudioWsSource::tick(float seconds)
{
	if (!m_capture_ok) {
		m_retry_accum += seconds;
		if (m_retry_accum >= 1.0f) {
			m_retry_accum = 0.0f;
			recapture_audio();
		}
	} else {
		m_retry_accum = 0.0f;
	}
	update_websocket();
}

void AudioWsSource::recapture_audio()
{
	release_audio_capture();

	if (m_use_output_bus) {
		// 捕獲整個輸出總線
		audio_t *audio = obs_get_audio();
		if (!audio)
		{
			m_capture_ok = false;
			return;
		}

		audio_convert_info cvt = {};
		cvt.format = AUDIO_FORMAT_FLOAT_PLANAR;
		cvt.samples_per_sec = m_audio_info.samples_per_sec;
		cvt.speakers = m_audio_info.speakers;

		m_output_bus_captured = audio_output_connect(audio, 0, &cvt, &AudioWsSource::capture_output_bus, this);
		m_capture_ok = m_output_bus_captured;
	} else {
		// 捕獲特定來源
		if (m_audio_source_name.empty())
		{
			m_capture_ok = false;
			return;
		}
		obs_source_t *src = obs_get_source_by_name(m_audio_source_name.c_str());
		if (!src)
		{
			m_capture_ok = false;
			return;
		}
		obs_source_add_audio_capture_callback(src, &AudioWsSource::capture_audio, this);
		m_audio_source = obs_source_get_weak_source(src);
		obs_source_release(src);
		m_capture_ok = true;
	}
}

void AudioWsSource::release_audio_capture()
{
	if (m_audio_source) {
		obs_source_t *src = obs_weak_source_get_source(m_audio_source);
		obs_weak_source_release(m_audio_source);
		m_audio_source = nullptr;
		if (src) {
			obs_source_remove_audio_capture_callback(src, &AudioWsSource::capture_audio, this);
			obs_source_release(src);
		}
	}

	if (m_output_bus_captured) {
		m_output_bus_captured = false;
		audio_output_disconnect(obs_get_audio(), 0, &AudioWsSource::capture_output_bus, this);
	}
	m_capture_ok = false;
}

void AudioWsSource::capture_audio(void *param, obs_source_t *source, const audio_data *audio, bool muted)
{
	UNUSED_PARAMETER(source);
	AudioWsSource *self = static_cast<AudioWsSource *>(param);
	self->process_audio(audio, muted);
}

void AudioWsSource::capture_output_bus(void *param, size_t mix_idx, audio_data *audio)
{
	UNUSED_PARAMETER(mix_idx);
	AudioWsSource *self = static_cast<AudioWsSource *>(param);
	self->process_audio(audio, false);
}

void AudioWsSource::process_audio(const audio_data *audio, bool muted)
{
	if (!audio || muted)
		return;

	const size_t frames = (size_t)audio->frames;
	if (frames == 0)
		return;

	// 取得單聲道資料（取第一個有資料的聲道）
	const float *mono = nullptr;
	size_t planes = m_channels ? m_channels : MAX_AV_PLANES;
	for (size_t ch = 0; ch < planes; ++ch) {
		if (audio->data[ch]) {
			mono = (const float *)audio->data[ch];
			break;
		}
	}
	if (!mono)
		return;

	// 全局 RMS（可用於附加用途）
	float sum_sq = 0.0f;
	for (size_t i = 0; i < frames; ++i) {
		float v = mono[i];
		sum_sq += v * v;
	}
	float rms = frames ? sqrtf(sum_sq / (float)frames) : 0.0f;

	// 12 頻段能量（簡化 Goertzel）
	float band_values[12] = {};
	for (size_t b = 0; b < 12; ++b) {
		float coeff = m_band_coef[b];
		float s_prev = 0.0f;
		float s_prev2 = 0.0f;
		for (size_t n = 0; n < frames; ++n) {
			float s = mono[n] + coeff * s_prev - s_prev2;
			s_prev2 = s_prev;
			s_prev = s;
		}
		float power = s_prev2 * s_prev2 + s_prev * s_prev - coeff * s_prev * s_prev2;
		if (power < 0.0f)
			power = 0.0f;
		band_values[b] = power / (float)frames;
	}

	std::lock_guard<std::mutex> lock(m_level_mutex);

	// 更新全局 m_level（保留原有行為）
	{
		float level_lin = rms * m_gain;
		if (level_lin < m_noise_floor)
			level_lin = 0.0f;
		if (level_lin > 1.0f)
			level_lin = 1.0f;
		float level = std::sqrt(level_lin);
		if (level > m_level)
			m_level = m_level * (1.0f - m_attack) + level * m_attack;
		else
			m_level = m_level * (1.0f - m_release) + level * m_release;
	}

	// 依照頻段能量更新每條 bar 的值
	for (size_t b = 0; b < 12; ++b) {
		float v = band_values[b] * m_gain;
		if (v < m_noise_floor)
			v = 0.0f;
		if (v > 1.0f)
			v = 1.0f;
		v = std::sqrt(v);
		float &cur = m_bar_levels[b];
		if (v > cur)
			cur = cur * (1.0f - m_attack) + v * m_attack;
		else
			cur = cur * (1.0f - m_release) + v * m_release;
	}
}

void AudioWsSource::init_bands()
{
	// 以常見 12-band EQ 的中心頻率為參考，採用對數分佈
	// 單位: Hz
	const float default_freqs[12] = {
		60.0f, 100.0f, 160.0f, 250.0f,
		400.0f, 630.0f, 1000.0f, 1600.0f,
		2500.0f, 4000.0f, 6300.0f, 10000.0f
	};

	float sr = (float)(m_audio_info.samples_per_sec ? m_audio_info.samples_per_sec : 48000);
	if (sr <= 0.0f)
		sr = 48000.0f;

	for (size_t i = 0; i < 12; ++i) {
		float f = default_freqs[i];
		// 確保不超過 Nyquist 頻率
		float nyquist = sr * 0.5f;
		if (f > nyquist)
			f = nyquist;
		m_band_freqs[i] = f;
		// Goertzel 係數: 2*cos(2*pi*f/Fs)
		const float pi = 3.14159265358979323846f;
		float omega = 2.0f * pi * f / sr;
		m_band_coef[i] = 2.0f * cosf(omega);
	}
}

void AudioWsSource::update_websocket()
{
	std::array<float, 12> bars{};
	{
		std::lock_guard<std::mutex> lock(m_level_mutex);
		for (size_t i = 0; i < bars.size(); ++i)
			bars[i] = m_bar_levels[i];
	}

	WebSocketServer *server = GetGlobalWebSocketServer();
	if (server)
		server->setBars(bars);
}

// === obs_source_info ===

obs_source_info audio_ws_source_info = {};

static void init_audio_ws_source_info()
{
	static bool inited = false;
	if (inited)
		return;
	inited = true;

	audio_ws_source_info.id = "audio-ws-source";
	audio_ws_source_info.type = OBS_SOURCE_TYPE_INPUT;
	audio_ws_source_info.output_flags = OBS_SOURCE_AUDIO; // 其實不輸出影像，只當工具使用

	audio_ws_source_info.get_name = [](void *) {
		return "Audio WebSocket Analyzer";
	};
	audio_ws_source_info.create = AudioWsSource::create;
	audio_ws_source_info.destroy = AudioWsSource::destroy;
	audio_ws_source_info.update = [](void *data, obs_data_t *settings) {
		static_cast<AudioWsSource *>(data)->update(settings);
	};
	audio_ws_source_info.get_defaults = AudioWsSource::get_defaults;
	audio_ws_source_info.get_properties = AudioWsSource::get_properties;
	audio_ws_source_info.video_render = nullptr; // 不渲染畫面
	audio_ws_source_info.audio_render = nullptr;
	audio_ws_source_info.video_tick = [](void *data, float seconds) {
		static_cast<AudioWsSource *>(data)->tick(seconds);
	};
}

// 在模組載入前確保 info 已填好
struct AudioWsSourceInfoInitializer {
	AudioWsSourceInfoInitializer() { init_audio_ws_source_info(); }
} g_audioWsSourceInfoInitializer;
