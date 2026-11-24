#include "runtime/libretro/LibretroCore.hpp"

#include <cstring>
#include <utility>

#include "SDL_loadso.h"

namespace launcher::runtime::libretro {
namespace {

// libretro callbacks have no user pointer, so the core that is currently
// running is reached through one static slot. Only one core plays at a time.
LibretroCore::Host *g_host = nullptr;

bool environmentThunk(unsigned command, void *data) {
  if (g_host == nullptr || !g_host->environment)
    return false;
  return g_host->environment(command, data);
}

void videoThunk(const void *data, unsigned width, unsigned height, std::size_t pitch) {
  if (g_host != nullptr && g_host->video)
    g_host->video(data, width, height, pitch);
}

void audioSampleThunk(std::int16_t left, std::int16_t right) {
  if (g_host == nullptr || !g_host->audio)
    return;
  const std::int16_t samples[2] = {left, right};
  (void)g_host->audio(samples, 1);
}

std::size_t audioBatchThunk(const std::int16_t *data, std::size_t frames) {
  if (g_host == nullptr || !g_host->audio || data == nullptr)
    return frames;
  return g_host->audio(data, frames);
}

void inputPollThunk() {}

std::int16_t inputStateThunk(unsigned port, unsigned device, unsigned index, unsigned id) {
  if (g_host == nullptr || !g_host->input)
    return 0;
  return g_host->input(port, device, index, id);
}

// SDL_LoadFunction returns a void pointer. Copying it into the typed pointer
// avoids the implementation-defined cast between object and function pointers.
// SDL2 wraps dlopen and LoadLibrary, so one loader serves every platform.
template <typename T> T resolveSymbol(void *handle, const char *name) {
  void *address = SDL_LoadFunction(handle, name);
  T target{};
  static_assert(sizeof(target) == sizeof(address), "unexpected function pointer size");
  std::memcpy(&target, &address, sizeof(target));
  return target;
}

} // namespace

LibretroCore::~LibretroCore() { unload(); }

bool LibretroCore::load(const std::string &path, std::string &error) {
  error.clear();
  unload();
  handle_ = SDL_LoadObject(path.c_str());
  if (handle_ == nullptr) {
    const char *reason = SDL_GetError();
    error = "Cannot open core " + path + ": " + (reason[0] != '\0' ? reason : "unknown error");
    return false;
  }
  path_ = path;
  setEnvironment_ = resolveSymbol<SetEnvironmentFn>(handle_, "retro_set_environment");
  setVideoRefresh_ = resolveSymbol<SetVideoRefreshFn>(handle_, "retro_set_video_refresh");
  setAudioSample_ = resolveSymbol<SetAudioSampleFn>(handle_, "retro_set_audio_sample");
  setAudioSampleBatch_ = resolveSymbol<SetAudioSampleBatchFn>(handle_, "retro_set_audio_sample_batch");
  setInputPoll_ = resolveSymbol<SetInputPollFn>(handle_, "retro_set_input_poll");
  setInputState_ = resolveSymbol<SetInputStateFn>(handle_, "retro_set_input_state");
  init_ = resolveSymbol<InitFn>(handle_, "retro_init");
  deinit_ = resolveSymbol<DeinitFn>(handle_, "retro_deinit");
  apiVersion_ = resolveSymbol<ApiVersionFn>(handle_, "retro_api_version");
  getSystemInfo_ = resolveSymbol<GetSystemInfoFn>(handle_, "retro_get_system_info");
  getSystemAvInfo_ = resolveSymbol<GetSystemAvInfoFn>(handle_, "retro_get_system_av_info");
  setControllerPortDevice_ = resolveSymbol<SetControllerPortDeviceFn>(handle_, "retro_set_controller_port_device");
  reset_ = resolveSymbol<ResetFn>(handle_, "retro_reset");
  run_ = resolveSymbol<RunFn>(handle_, "retro_run");
  serializeSize_ = resolveSymbol<SerializeSizeFn>(handle_, "retro_serialize_size");
  serialize_ = resolveSymbol<SerializeFn>(handle_, "retro_serialize");
  unserialize_ = resolveSymbol<UnserializeFn>(handle_, "retro_unserialize");
  loadGame_ = resolveSymbol<LoadGameFn>(handle_, "retro_load_game");
  unloadGame_ = resolveSymbol<UnloadGameFn>(handle_, "retro_unload_game");
  getRegion_ = resolveSymbol<GetRegionFn>(handle_, "retro_get_region");
  getMemoryData_ = resolveSymbol<GetMemoryDataFn>(handle_, "retro_get_memory_data");
  getMemorySize_ = resolveSymbol<GetMemorySizeFn>(handle_, "retro_get_memory_size");
  if (setEnvironment_ == nullptr || setVideoRefresh_ == nullptr || setAudioSample_ == nullptr ||
      setAudioSampleBatch_ == nullptr || setInputPoll_ == nullptr || setInputState_ == nullptr || init_ == nullptr ||
      deinit_ == nullptr || apiVersion_ == nullptr || getSystemInfo_ == nullptr || getSystemAvInfo_ == nullptr ||
      setControllerPortDevice_ == nullptr || reset_ == nullptr || run_ == nullptr || serializeSize_ == nullptr ||
      serialize_ == nullptr || unserialize_ == nullptr || loadGame_ == nullptr || unloadGame_ == nullptr ||
      getRegion_ == nullptr || getMemoryData_ == nullptr || getMemorySize_ == nullptr) {
    error = "Core is missing required libretro entry points: " + path;
    unload();
    return false;
  }
  if (apiVersion_() != RETRO_API_VERSION) {
    error = "Core reports an unsupported libretro API version: " + path;
    unload();
    return false;
  }
  retro_system_info info{};
  getSystemInfo_(&info);
  libraryName_ = info.library_name != nullptr ? info.library_name : "libretro core";
  libraryVersion_ = info.library_version != nullptr ? info.library_version : "";
  validExtensions_ = info.valid_extensions != nullptr ? info.valid_extensions : "";
  needFullpath_ = info.need_fullpath;
  return true;
}

void LibretroCore::unload() {
  if (g_host == &host_)
    g_host = nullptr;
  host_ = Host{};
  if (handle_ != nullptr)
    SDL_UnloadObject(handle_);
  handle_ = nullptr;
  path_.clear();
  libraryName_.clear();
  libraryVersion_.clear();
  validExtensions_.clear();
  needFullpath_ = false;
  setEnvironment_ = nullptr;
  setVideoRefresh_ = nullptr;
  setAudioSample_ = nullptr;
  setAudioSampleBatch_ = nullptr;
  setInputPoll_ = nullptr;
  setInputState_ = nullptr;
  init_ = nullptr;
  deinit_ = nullptr;
  apiVersion_ = nullptr;
  getSystemInfo_ = nullptr;
  getSystemAvInfo_ = nullptr;
  setControllerPortDevice_ = nullptr;
  reset_ = nullptr;
  run_ = nullptr;
  serializeSize_ = nullptr;
  serialize_ = nullptr;
  unserialize_ = nullptr;
  loadGame_ = nullptr;
  unloadGame_ = nullptr;
  getRegion_ = nullptr;
  getMemoryData_ = nullptr;
  getMemorySize_ = nullptr;
}

void LibretroCore::attachEnvironment(Host host) {
  host_ = std::move(host);
  g_host = &host_;
  setEnvironment_(&environmentThunk);
}

void LibretroCore::attachCallbacks() {
  setVideoRefresh_(&videoThunk);
  setAudioSample_(&audioSampleThunk);
  setAudioSampleBatch_(&audioBatchThunk);
  setInputPoll_(&inputPollThunk);
  setInputState_(&inputStateThunk);
}

void LibretroCore::init() {
  if (init_ != nullptr)
    init_();
}

void LibretroCore::deinit() {
  if (deinit_ != nullptr)
    deinit_();
}

retro_system_av_info LibretroCore::avInfo() const {
  retro_system_av_info info{};
  if (getSystemAvInfo_ != nullptr)
    getSystemAvInfo_(&info);
  return info;
}

bool LibretroCore::loadGame(const char *path, const void *data, std::size_t size) {
  if (loadGame_ == nullptr)
    return false;
  retro_game_info info{};
  info.path = path;
  info.data = data;
  info.size = size;
  info.meta = nullptr;
  return loadGame_(&info);
}

void LibretroCore::unloadGame() {
  if (unloadGame_ != nullptr)
    unloadGame_();
}

void LibretroCore::setControllerPortDevice(unsigned port, unsigned device) {
  if (setControllerPortDevice_ != nullptr)
    setControllerPortDevice_(port, device);
}

void LibretroCore::reset() {
  if (reset_ != nullptr)
    reset_();
}

void LibretroCore::run() {
  if (run_ != nullptr)
    run_();
}

unsigned LibretroCore::region() const { return getRegion_ != nullptr ? getRegion_() : RETRO_REGION_NTSC; }

std::size_t LibretroCore::serializeSize() const { return serializeSize_ != nullptr ? serializeSize_() : 0; }

bool LibretroCore::serialize(void *data, std::size_t size) const {
  return serialize_ != nullptr && serialize_(data, size);
}

bool LibretroCore::unserialize(const void *data, std::size_t size) const {
  return unserialize_ != nullptr && unserialize_(data, size);
}

void *LibretroCore::memoryData(unsigned id) const { return getMemoryData_ != nullptr ? getMemoryData_(id) : nullptr; }

std::size_t LibretroCore::memorySize(unsigned id) const {
  return getMemorySize_ != nullptr ? getMemorySize_(id) : 0;
}

} // namespace launcher::runtime::libretro
