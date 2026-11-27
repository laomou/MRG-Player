// A tiny libretro core used by the runtime test.
//
// It exposes the whole front-end contract the runtime relies on: an XRGB8888
// frame, a batch of audio samples, battery RAM, serialization and the input
// bitmask query. The rendered colour encodes the frame counter so a snapshot
// taken at frame N can be recognised after a load.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "libretro.h"

namespace {

constexpr unsigned kWidth = 160;
constexpr unsigned kHeight = 144;
constexpr std::size_t kSaveRamSize = 64;
constexpr const char *kStateMagic = "STUB";

retro_environment_t environment = nullptr;
retro_video_refresh_t video = nullptr;
retro_audio_sample_batch_t audioBatch = nullptr;
retro_input_state_t inputState = nullptr;
retro_input_poll_t inputPoll = nullptr;
retro_core_option_v2_definition optionDefinitions[3] = {};
retro_core_option_v2_category optionCategories[2] = {};
std::vector<std::uint32_t> framebuffer;
std::vector<std::uint8_t> rom;
std::uint16_t saveRam[kSaveRamSize] = {};
unsigned frameCounter = 0;
unsigned lastInput = 0;
bool loaded = false;

void pokeEnvironment() {
  if (environment == nullptr)
    return;
  retro_pixel_format format = RETRO_PIXEL_FORMAT_XRGB8888;
  environment(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &format);
}

// Declares two options the way a real core does, so the front end can be
// tested for collecting them, answering GET_VARIABLE and flagging updates.
void declareOptions() {
  if (environment == nullptr)
    return;
  std::memset(optionDefinitions, 0, sizeof(optionDefinitions));
  std::memset(optionCategories, 0, sizeof(optionCategories));
  optionCategories[0].key = "audio";
  optionCategories[0].desc = "Audio";
  optionCategories[0].info = "Sound related choices.";
  optionDefinitions[0].key = "stub_brightness";
  optionDefinitions[0].desc = "Brightness";
  optionDefinitions[0].info = "How bright the test frame is.";
  optionDefinitions[0].values[0].value = "low";
  optionDefinitions[0].values[0].label = "Low";
  optionDefinitions[0].values[1].value = "medium";
  optionDefinitions[0].values[1].label = "Medium";
  optionDefinitions[0].values[2].value = "high";
  optionDefinitions[0].values[2].label = "High";
  optionDefinitions[0].default_value = "medium";
  optionDefinitions[1].key = "stub_audio";
  optionDefinitions[1].desc = "Audio tone";
  optionDefinitions[1].info = "Pitch of the generated tone.";
  optionDefinitions[1].category_key = "audio";
  optionDefinitions[1].values[0].value = "low";
  optionDefinitions[1].values[0].label = "Low";
  optionDefinitions[1].values[1].value = "high";
  optionDefinitions[1].values[1].label = "High";
  optionDefinitions[1].default_value = "high";
  retro_core_options_v2 options = {};
  options.categories = optionCategories;
  options.definitions = optionDefinitions;
  environment(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, &options);
}

// Reads the option the front end reports, and whether it was just changed, so
// the runtime test can see the values reach the core.
void observeOptions() {
  if (environment == nullptr)
    return;
  retro_variable variable = {"stub_brightness", nullptr};
  if (environment(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) &&
      variable.value != nullptr)
    saveRam[0] = static_cast<std::uint16_t>(
        static_cast<unsigned char>(variable.value[0]));
  bool updated = false;
  if (environment(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
    saveRam[1] = 1;
}

} // namespace

extern "C" {

unsigned retro_api_version(void) { return RETRO_API_VERSION; }

void retro_set_environment(retro_environment_t cb) { environment = cb; }
void retro_set_video_refresh(retro_video_refresh_t cb) { video = cb; }
void retro_set_audio_sample(retro_audio_sample_t) {}
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {
  audioBatch = cb;
}
void retro_set_input_poll(retro_input_poll_t cb) { inputPoll = cb; }
void retro_set_input_state(retro_input_state_t cb) { inputState = cb; }

void retro_init(void) {}
void retro_deinit(void) {}

void retro_get_system_info(struct retro_system_info *info) {
  info->library_name = "Stub GBA";
  info->library_version = "1.0";
  info->valid_extensions = "gba";
  info->need_fullpath = false;
  info->block_extract = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info) {
  info->geometry.base_width = kWidth;
  info->geometry.base_height = kHeight;
  info->geometry.max_width = kWidth;
  info->geometry.max_height = kHeight;
  info->geometry.aspect_ratio = 4.0f / 3.0f;
  info->timing.fps = 60.0;
  info->timing.sample_rate = 48000.0;
}

void retro_set_controller_port_device(unsigned, unsigned) {}

void retro_reset(void) {
  frameCounter = 0;
  lastInput = 0;
}

bool retro_load_game(const struct retro_game_info *game) {
  pokeEnvironment();
  declareOptions();
  // Real cores read retro_game_info::path, so a front end that leaves it null
  // crashes them; failing here keeps that regression visible to the tests.
  if (game == nullptr || game->path == nullptr)
    return false;
  framebuffer.assign(static_cast<std::size_t>(kWidth) * kHeight, 0xFF000000U);
  rom.clear();
  if (game != nullptr && game->data != nullptr && game->size != 0) {
    const auto *bytes = static_cast<const std::uint8_t *>(game->data);
    rom.assign(bytes, bytes + game->size);
  }
  std::memset(saveRam, 0, sizeof(saveRam));
  loaded = true;
  return true;
}

void retro_unload_game(void) { loaded = false; }

unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }

void retro_run(void) {
  if (!loaded)
    return;
  observeOptions();
  // A core may retime itself mid-game. The test asks for that through the
  // environment so the runtime's retiming path is covered without a core that
  // happens to switch between PAL and NTSC.
  if (frameCounter == 2) {
    const char *retime = std::getenv("MRG_STUB_RETIME_FPS");
    if (retime != nullptr && environment != nullptr) {
      retro_system_av_info info = {};
      info.geometry.base_width = kWidth;
      info.geometry.base_height = kHeight;
      info.geometry.max_width = kWidth;
      info.geometry.max_height = kHeight;
      info.geometry.aspect_ratio = static_cast<float>(kWidth) / static_cast<float>(kHeight);
      info.timing.fps = std::atof(retime);
      info.timing.sample_rate = 48000.0;
      environment(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &info);
    }
  }
  if (inputPoll != nullptr)
    inputPoll();
  lastInput = 0;
  if (inputState != nullptr) {
    const std::int16_t mask = inputState(0, RETRO_DEVICE_JOYPAD, 0,
                                         RETRO_DEVICE_ID_JOYPAD_MASK);
    lastInput = static_cast<unsigned>(static_cast<std::uint16_t>(mask));
    saveRam[8] = static_cast<std::uint16_t>(mask);
    saveRam[9] = static_cast<std::uint16_t>(inputState(
        0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT,
        RETRO_DEVICE_ID_ANALOG_X));
    saveRam[10] = static_cast<std::uint16_t>(inputState(
        0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT,
        RETRO_DEVICE_ID_ANALOG_Y));
  }
  // Bright enough to see in a screenshot: green, with the frame counter and the
  // input bitmask in the low channels.
  const std::uint32_t colour = 0xFF000000U | (0xC0U << 8) | (frameCounter & 0xFFU) | ((lastInput & 0xFFU) << 16);
  for (std::uint32_t &pixel : framebuffer)
    pixel = colour;
  if (video != nullptr)
    video(framebuffer.data(), kWidth, kHeight, kWidth * sizeof(std::uint32_t));
  if (audioBatch != nullptr) {
    std::int16_t samples[64 * 2];
    for (std::size_t index = 0; index < 128; ++index)
      samples[index] = static_cast<std::int16_t>((frameCounter * 3U + index) & 0x3FFU);
    audioBatch(samples, 64);
  }
  ++frameCounter;
}

std::size_t retro_serialize_size(void) {
  return std::strlen(kStateMagic) + sizeof(frameCounter) + sizeof(lastInput) + sizeof(saveRam) + sizeof(std::size_t) +
         rom.size();
}

bool retro_serialize(void *data, std::size_t size) {
  if (data == nullptr || size < retro_serialize_size())
    return false;
  auto *cursor = static_cast<std::uint8_t *>(data);
  const std::size_t magicSize = std::strlen(kStateMagic);
  std::memcpy(cursor, kStateMagic, magicSize);
  cursor += magicSize;
  std::memcpy(cursor, &frameCounter, sizeof(frameCounter));
  cursor += sizeof(frameCounter);
  std::memcpy(cursor, &lastInput, sizeof(lastInput));
  cursor += sizeof(lastInput);
  std::memcpy(cursor, saveRam, sizeof(saveRam));
  cursor += sizeof(saveRam);
  const std::size_t romSize = rom.size();
  std::memcpy(cursor, &romSize, sizeof(romSize));
  cursor += sizeof(romSize);
  if (romSize != 0)
    std::memcpy(cursor, rom.data(), romSize);
  return true;
}

bool retro_unserialize(const void *data, std::size_t size) {
  if (data == nullptr || size < retro_serialize_size())
    return false;
  auto *cursor = static_cast<const std::uint8_t *>(data);
  const std::size_t magicSize = std::strlen(kStateMagic);
  if (std::memcmp(cursor, kStateMagic, magicSize) != 0)
    return false;
  cursor += magicSize;
  std::memcpy(&frameCounter, cursor, sizeof(frameCounter));
  cursor += sizeof(frameCounter);
  std::memcpy(&lastInput, cursor, sizeof(lastInput));
  cursor += sizeof(lastInput);
  std::memcpy(saveRam, cursor, sizeof(saveRam));
  cursor += sizeof(saveRam);
  std::size_t romSize = 0;
  std::memcpy(&romSize, cursor, sizeof(romSize));
  if (romSize > rom.size())
    return false;
  return true;
}

void *retro_get_memory_data(unsigned id) {
  return id == RETRO_MEMORY_SAVE_RAM ? saveRam : nullptr;
}

std::size_t retro_get_memory_size(unsigned id) {
  return id == RETRO_MEMORY_SAVE_RAM ? sizeof(saveRam) : 0;
}

} // extern "C"
