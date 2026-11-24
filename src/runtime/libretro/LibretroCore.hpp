#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "libretro.h"

namespace launcher::runtime::libretro {

// One dlopen-ed libretro core plus the entry points the front end calls.
//
// libretro callbacks carry no user pointer, so the installed callbacks forward
// to the host of whichever core attached last. That is fine for a player that
// runs one game at a time, and the constraint lives here instead of leaking
// into every caller.
class LibretroCore {
public:
  // Front-end hooks a running core calls.
  struct Host {
    std::function<bool(unsigned command, void *data)> environment;
    std::function<void(const void *data, unsigned width, unsigned height, std::size_t pitch)> video;
    std::function<std::size_t(const std::int16_t *data, std::size_t frames)> audio;
    std::function<std::int16_t(unsigned port, unsigned device, unsigned index, unsigned id)> input;
  };

  LibretroCore() = default;
  ~LibretroCore();
  LibretroCore(const LibretroCore &) = delete;
  LibretroCore &operator=(const LibretroCore &) = delete;

  bool load(const std::string &path, std::string &error);
  void unload();
  // Installs the callbacks the core will call while it runs.
  // The environment callback has to be reachable during retro_init(), while the
  // video, audio, and input callbacks have to wait until the core has
  // initialised: cores allocate their display state in retro_init() and crash
  // if a callback setter is called before it.
  void attachEnvironment(Host host);
  void attachCallbacks();

  [[nodiscard]] bool valid() const noexcept { return handle_ != nullptr; }
  [[nodiscard]] const std::string &path() const noexcept { return path_; }
  [[nodiscard]] const std::string &libraryName() const noexcept { return libraryName_; }
  [[nodiscard]] const std::string &libraryVersion() const noexcept { return libraryVersion_; }
  [[nodiscard]] const std::string &validExtensions() const noexcept { return validExtensions_; }

  void init();
  void deinit();
  [[nodiscard]] retro_system_av_info avInfo() const;
  // Cores read retro_game_info::path even when the data is passed in memory, so
  // the path is always supplied; a core that sets need_fullpath reads the file
  // itself and must not be handed a copy.
  [[nodiscard]] bool needsFullPath() const noexcept { return needFullpath_; }
  [[nodiscard]] bool loadGame(const char *path, const void *data, std::size_t size);
  void unloadGame();
  void setControllerPortDevice(unsigned port, unsigned device);
  void reset();
  void run();
  [[nodiscard]] unsigned region() const;
  [[nodiscard]] std::size_t serializeSize() const;
  [[nodiscard]] bool serialize(void *data, std::size_t size) const;
  [[nodiscard]] bool unserialize(const void *data, std::size_t size) const;
  [[nodiscard]] void *memoryData(unsigned id) const;
  [[nodiscard]] std::size_t memorySize(unsigned id) const;

private:
  using SetEnvironmentFn = void (*)(retro_environment_t);
  using SetVideoRefreshFn = void (*)(retro_video_refresh_t);
  using SetAudioSampleFn = void (*)(retro_audio_sample_t);
  using SetAudioSampleBatchFn = void (*)(retro_audio_sample_batch_t);
  using SetInputPollFn = void (*)(retro_input_poll_t);
  using SetInputStateFn = void (*)(retro_input_state_t);
  using InitFn = void (*)();
  using DeinitFn = void (*)();
  using ApiVersionFn = unsigned (*)();
  using GetSystemInfoFn = void (*)(retro_system_info *);
  using GetSystemAvInfoFn = void (*)(retro_system_av_info *);
  using SetControllerPortDeviceFn = void (*)(unsigned, unsigned);
  using ResetFn = void (*)();
  using RunFn = void (*)();
  using SerializeSizeFn = std::size_t (*)();
  using SerializeFn = bool (*)(void *, std::size_t);
  using UnserializeFn = bool (*)(const void *, std::size_t);
  using LoadGameFn = bool (*)(const retro_game_info *);
  using UnloadGameFn = void (*)();
  using GetRegionFn = unsigned (*)();
  using GetMemoryDataFn = void *(*)(unsigned);
  using GetMemorySizeFn = std::size_t (*)(unsigned);

  void *handle_ = nullptr;
  std::string path_;
  std::string libraryName_;
  std::string libraryVersion_;
  std::string validExtensions_;
  bool needFullpath_ = false;
  Host host_;

  SetEnvironmentFn setEnvironment_ = nullptr;
  SetVideoRefreshFn setVideoRefresh_ = nullptr;
  SetAudioSampleFn setAudioSample_ = nullptr;
  SetAudioSampleBatchFn setAudioSampleBatch_ = nullptr;
  SetInputPollFn setInputPoll_ = nullptr;
  SetInputStateFn setInputState_ = nullptr;
  InitFn init_ = nullptr;
  DeinitFn deinit_ = nullptr;
  ApiVersionFn apiVersion_ = nullptr;
  GetSystemInfoFn getSystemInfo_ = nullptr;
  GetSystemAvInfoFn getSystemAvInfo_ = nullptr;
  SetControllerPortDeviceFn setControllerPortDevice_ = nullptr;
  ResetFn reset_ = nullptr;
  RunFn run_ = nullptr;
  SerializeSizeFn serializeSize_ = nullptr;
  SerializeFn serialize_ = nullptr;
  UnserializeFn unserialize_ = nullptr;
  LoadGameFn loadGame_ = nullptr;
  UnloadGameFn unloadGame_ = nullptr;
  GetRegionFn getRegion_ = nullptr;
  GetMemoryDataFn getMemoryData_ = nullptr;
  GetMemorySizeFn getMemorySize_ = nullptr;
};

} // namespace launcher::runtime::libretro
