#pragma once
#include <obs.h>
#include <util/base.h>
#include <stdexcept>
#include <string>
#include "obs_compat.hpp"

// Compile against GPL-licensed OBS 32.1.2 headers for the baseline API.
// Stable patch releases in the representative-tested minor series are accepted.
// Resolve the installed runtime explicitly; no OBS binaries are redistributed.
#define OBS_FUNCTIONS(X) \
 X(obs_get_version_string) X(obs_startup) X(obs_shutdown) X(obs_add_data_path) \
 X(obs_reset_video) X(obs_open_module) X(obs_init_module) X(obs_post_load_modules) \
 X(obs_data_create) X(obs_data_release) X(obs_data_set_string) X(obs_data_set_bool) X(obs_data_set_int) \
 X(obs_source_create) X(obs_source_release) X(obs_source_inc_showing) X(obs_source_dec_showing) \
 X(obs_source_get_width) X(obs_source_get_height) X(obs_source_video_render) \
 X(obs_add_main_render_callback) X(obs_remove_main_render_callback) \
 X(obs_enter_graphics) X(obs_leave_graphics) \
 X(gs_texrender_create) X(gs_texrender_destroy) X(gs_texrender_begin) \
 X(gs_texrender_reset) X(gs_texrender_end) X(gs_texrender_get_texture) \
 X(gs_get_device_obj) X(gs_texture_get_obj) X(gs_clear) X(gs_ortho) \
 X(gs_matrix_push) X(gs_matrix_pop) X(gs_matrix_identity) X(gs_matrix_scale3f) \
 X(gs_matrix_translate3f) X(gs_blend_state_push) X(gs_blend_state_pop) X(gs_reset_blend_state) \
 X(gs_texture_open_shared) X(gs_texture_destroy) X(gs_texture_create) X(gs_texture_set_image) \
 X(gs_texture_get_color_format) X(gs_copy_texture) \
 X(gs_texture_get_width) X(gs_texture_get_height) X(obs_get_base_effect) \
 X(gs_effect_get_param_by_name) X(gs_effect_set_texture_srgb) X(gs_effect_loop) X(gs_draw_sprite) \
 X(gs_framebuffer_srgb_enabled) X(gs_enable_framebuffer_srgb) \
 X(base_set_log_handler)

struct ObsApi {
 HMODULE dll = nullptr;
#define DECLARE(name) decltype(&::name) name = nullptr;
 OBS_FUNCTIONS(DECLARE)
#undef DECLARE
 void Load(const std::wstring& bin) {
  if (dll) return;
  SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS);
  AddDllDirectory(bin.c_str());
  HMODULE candidate = LoadLibraryExW((bin + L"\\obs.dll").c_str(), nullptr,
    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS);
  if (!candidate) throw std::runtime_error("Cannot load installed obs.dll. Check obs_dir in viewer.ini.");
  try {
#define LOAD(name) name = reinterpret_cast<decltype(name)>(GetProcAddress(candidate, #name)); \
  if (!name) throw std::runtime_error("Missing OBS runtime function: " #name);
  OBS_FUNCTIONS(LOAD)
#undef LOAD
  std::string version = obs_get_version_string();
  if (!SupportsObsRuntime(version))
   throw std::runtime_error("Supported OBS series: 32.0.x, 32.1.x, 32.2.x and 33.0.x (stable releases). Installed: " + version);
  dll = candidate;
  } catch (...) { FreeLibrary(candidate); throw; }
 }
};
