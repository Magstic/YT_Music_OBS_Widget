#include <obs-module.h>
#include "audio_ws_source.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-audio-ws-plugin", "en-US")

MODULE_EXPORT const char *obs_module_name(void)
{
	return "Audio WebSocket Analyzer";
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Expose audio levels over a local WebSocket for browser widgets.";
}

extern struct obs_source_info audio_ws_source_info;

MODULE_EXPORT bool obs_module_load(void)
{
	obs_register_source(&audio_ws_source_info);
	return true;
}

MODULE_EXPORT void obs_module_unload(void)
{
	/* nothing to clean up globally for now */
}
