#include "i18n/CoreOptionText.hpp"

#include <cstddef>

namespace launcher::i18n {
namespace {

struct LabelEntry {
  const char *key;
  const char *text;
};

struct ValueEntry {
  const char *value;  // matched without regard to case
  const char *text;
};

// Cores ship these tables in English; the keys are stable, the labels are not,
// so we look up by key and leave anything we do not know in the core's words.
constexpr LabelEntry kLabels[] = {
    {"mesen_overclock", "超频"},
    {"mesen_overclock_type", "超频方式"},
    {"mesen_region", "区域"},
    {"mesen_ramstate", "RAM 开机初始值"},
    {"mesen_fdsautoinsertdisk", "FDS：自动插入磁盘"},
    {"mesen_fdsfastforwardload", "FDS：加载时快进"},
    {"mesen_ntsc_filter", "NTSC 滤镜"},
    {"mesen_palette", "调色板"},
    {"mesen_nospritelimit", "取消精灵数量限制"},
    {"mesen_overscan_left", "左侧过扫描"},
    {"mesen_overscan_right", "右侧过扫描"},
    {"mesen_overscan_up", "上侧过扫描"},
    {"mesen_overscan_down", "下侧过扫描"},
    {"mesen_aspect_ratio", "画面比例"},
    {"mesen_hdpacks", "启用高清材质包"},
    {"mesen_screenrotation", "屏幕旋转"},
    {"mesen_fake_stereo", "启用伪立体声"},
    {"mesen_mute_triangle_ultrasonic", "降低三角波爆音"},
    {"mesen_reduce_dmc_popping", "降低 DMC 爆音"},
    {"mesen_swap_duty_cycle", "交换方波占空比"},
    {"mesen_disable_noise_mode_flag", "关闭噪声模式标志"},
    {"mesen_audio_sample_rate", "音频采样率"},
    {"mesen_controllerturbospeed", "手柄连发速度"},
    {"mesen_shift_buttons_clockwise", "A/B/X/Y 顺时针轮换"},
    {"mgba_gb_model", "Game Boy 机型（重启生效）"},
    {"mgba_use_bios", "存在 BIOS 时使用（重启生效）"},
    {"mgba_skip_bios", "跳过 BIOS 开场（重启生效）"},
    {"mgba_gb_colors", "默认 Game Boy 调色板"},
    {"mgba_gb_colors_preset", "硬件预设调色板（重启生效）"},
    {"mgba_sgb_borders", "使用 Super Game Boy 边框（重启生效）"},
    {"mgba_color_correction", "色彩校正"},
    {"mgba_interframe_blending", "帧间混合"},
    {"mgba_audio_low_pass_filter", "音频低通滤镜"},
    {"mgba_audio_low_pass_range", "低通滤镜强度"},
    {"mgba_allow_opposing_directions", "允许同时按相反方向"},
    {"mgba_solar_sensor_level", "光照传感器强度"},
    {"mgba_force_gbp", "Game Boy Player 震动（重启生效）"},
    {"mgba_idle_optimization", "空闲循环优化"},
    {"mgba_frameskip", "跳帧"},
    {"mgba_frameskip_threshold", "跳帧阈值（%）"},
    {"mgba_frameskip_interval", "跳帧间隔"},
    {"gambatte_gb_colorization", "GB 着色"},
    {"gambatte_gb_internal_palette", "内置调色板"},
    {"gambatte_gb_palette_twb64_1", "TWB64 第 1 套调色板"},
    {"gambatte_gb_palette_twb64_2", "TWB64 第 2 套调色板"},
    {"gambatte_gb_palette_twb64_3", "TWB64 第 3 套调色板"},
    {"gambatte_gb_palette_pixelshift_1", "PixelShift 第 1 套调色板"},
    {"gambatte_gbc_color_correction", "色彩校正"},
    {"gambatte_gbc_color_correction_mode", "色彩校正模式"},
    {"gambatte_gbc_frontlight_position", "色彩校正 - 前光位置"},
    {"gambatte_dark_filter_level", "暗色滤镜强度（%）"},
    {"gambatte_mix_frames", "帧间混合"},
    {"gambatte_audio_resampler", "音频重采样"},
    {"gambatte_gb_hwmode", "模拟硬件（需重启）"},
    {"gambatte_gb_bootloader", "使用官方引导程序（需重启）"},
    {"gambatte_up_down_allowed", "允许同时按相反方向"},
    {"gambatte_turbo_period", "连发间隔"},
    {"gambatte_rumble_level", "手柄震动强度"},
    {"gambatte_show_gb_link_settings", "显示联机设置"},
    {"gambatte_gb_link_mode", "联机模式"},
    {"gambatte_gb_link_network_port", "联机端口"},
    {"gambatte_gb_link_network_server_ip_1", "联机服务器地址 第 01 位"},
    {"gambatte_gb_link_network_server_ip_2", "联机服务器地址 第 02 位"},
    {"gambatte_gb_link_network_server_ip_3", "联机服务器地址 第 03 位"},
    {"gambatte_gb_link_network_server_ip_4", "联机服务器地址 第 04 位"},
    {"gambatte_gb_link_network_server_ip_5", "联机服务器地址 第 05 位"},
    {"gambatte_gb_link_network_server_ip_6", "联机服务器地址 第 06 位"},
    {"gambatte_gb_link_network_server_ip_7", "联机服务器地址 第 07 位"},
    {"gambatte_gb_link_network_server_ip_8", "联机服务器地址 第 08 位"},
    {"gambatte_gb_link_network_server_ip_9", "联机服务器地址 第 09 位"},
    {"gambatte_gb_link_network_server_ip_10", "联机服务器地址 第 10 位"},
    {"gambatte_gb_link_network_server_ip_11", "联机服务器地址 第 11 位"},
    {"gambatte_gb_link_network_server_ip_12", "联机服务器地址 第 12 位"},
    {"snes9x_region", "主机区域（重载核心）"},
    {"snes9x_aspect", "首选画面比例"},
    {"snes9x_overscan", "裁掉过扫描"},
    {"snes9x_mode7_hires", "HD Mode 7 倍数"},
    {"snes9x_mode7_hires_bilinear", "HD Mode 7 滤镜"},
    {"snes9x_hires_blend", "高分辨率混合"},
    {"snes9x_blargg", "Blargg NTSC 滤镜"},
    {"snes9x_audio_interpolation", "音频插值"},
    {"snes9x_up_down_allowed", "允许同时按相反方向"},
    {"snes9x_overclock_superfx", "SuperFX 超频"},
    {"snes9x_superfx_timing", "SuperFX 时序模型（实验）"},
    {"snes9x_overclock_cycles", "减少降速（Hack，不安全）"},
    {"snes9x_reduce_sprite_flicker", "减少闪烁（Hack，不安全）"},
    {"snes9x_randomize_memory", "内存随机化（不安全）"},
    {"snes9x_block_invalid_vram_access", "拦截非法 VRAM 访问"},
    {"snes9x_echo_buffer_hack", "回声缓冲 Hack（不安全，仅老 addmusic hack）"},
    {"snes9x_show_lightgun_settings", "显示光枪设置"},
    {"snes9x_lightgun_mode", "光枪模式"},
    {"snes9x_superscope_reverse_buttons", "Super Scope 扳机反转"},
    {"snes9x_superscope_crosshair", "Super Scope 准星"},
    {"snes9x_superscope_color", "Super Scope 准星颜色"},
    {"snes9x_justifier1_crosshair", "Justifier 1 准星"},
    {"snes9x_justifier1_color", "Justifier 1 准星颜色"},
    {"snes9x_justifier2_crosshair", "Justifier 2 准星"},
    {"snes9x_justifier2_color", "Justifier 2 准星颜色"},
    {"snes9x_rifle_crosshair", "M.A.C.S. Rifle 准星"},
    {"snes9x_rifle_color", "M.A.C.S. Rifle 准星颜色"},
    {"snes9x_msu1_enhanced_audio", "MSU-1 增强音频"},
    {"snes9x_show_advanced_av_settings", "显示高级音视频设置"},
    {"snes9x_layer_1", "显示图层 1"},
    {"snes9x_layer_2", "显示图层 2"},
    {"snes9x_layer_3", "显示图层 3"},
    {"snes9x_layer_4", "显示图层 4"},
    {"snes9x_layer_5", "显示精灵层"},
    {"snes9x_gfx_transp", "启用透明效果"},
    {"snes9x_sndchan_volume_1", "声道 1 音量 %"},
    {"snes9x_sndchan_volume_2", "声道 2 音量 %"},
    {"snes9x_sndchan_volume_3", "声道 3 音量 %"},
    {"snes9x_sndchan_volume_4", "声道 4 音量 %"},
    {"snes9x_sndchan_volume_5", "声道 5 音量 %"},
    {"snes9x_sndchan_volume_6", "声道 6 音量 %"},
    {"snes9x_sndchan_volume_7", "声道 7 音量 %"},
    {"snes9x_sndchan_volume_8", "声道 8 音量 %"},
    {"genesis_plus_gx_system_hw", "主机硬件"},
    {"genesis_plus_gx_region_detect", "主机区域"},
    {"genesis_plus_gx_vdp_mode", "强制 VDP 模式"},
    {"genesis_plus_gx_bios", "系统引导 ROM"},
    {"genesis_plus_gx_system_bram", "CD 系统 BRAM（需重启）"},
    {"genesis_plus_gx_cart_bram", "CD 备份卡 BRAM（需重启）"},
    {"genesis_plus_gx_cart_size", "CD 备份卡 BRAM 容量（需重启）"},
    {"genesis_plus_gx_add_on", "CD 附加设备（MD 模式）（需重启）"},
    {"genesis_plus_gx_lock_on", "卡带 Lock-On"},
    {"genesis_plus_gx_aspect_ratio", "核心提供的画面比例"},
    {"genesis_plus_gx_overscan", "边缘"},
    {"genesis_plus_gx_left_border", "隐藏 Master System 侧边框"},
    {"genesis_plus_gx_gg_extra", "Game Gear 扩展画面"},
    {"genesis_plus_gx_blargg_ntsc_filter", "Blargg NTSC 滤镜"},
    {"genesis_plus_gx_lcd_filter", "LCD 拖影滤镜"},
    {"genesis_plus_gx_render", "隔行模式 2 输出"},
    {"genesis_plus_gx_frameskip", "跳帧"},
    {"genesis_plus_gx_frameskip_threshold", "跳帧阈值（%）"},
    {"genesis_plus_gx_ym2413", "Master System FM（YM2413）"},
    {"genesis_plus_gx_ym2413_core", "Master System FM（YM2413）内核"},
    {"genesis_plus_gx_ym2612", "Mega Drive/Genesis FM"},
    {"genesis_plus_gx_sound_output", "声音输出"},
    {"genesis_plus_gx_audio_filter", "音频滤镜"},
    {"genesis_plus_gx_lowpass_range", "低通滤镜 %"},
    {"genesis_plus_gx_psg_preamp", "PSG 前置增益"},
    {"genesis_plus_gx_fm_preamp", "FM 前置增益"},
    {"genesis_plus_gx_cdda_volume", "CD-DA 音量"},
    {"genesis_plus_gx_pcm_volume", "PCM 音量"},
    {"genesis_plus_gx_audio_eq_low", "均衡 低频"},
    {"genesis_plus_gx_audio_eq_mid", "均衡 中频"},
    {"genesis_plus_gx_audio_eq_high", "均衡 高频"},
    {"genesis_plus_gx_gun_input", "光枪输入"},
    {"genesis_plus_gx_gun_cursor", "显示光枪准星"},
    {"genesis_plus_gx_invert_mouse", "反转鼠标 Y 轴"},
    {"genesis_plus_gx_no_sprite_limit", "取消每行精灵限制"},
    {"genesis_plus_gx_enhanced_vscroll", "增强逐格垂直滚动"},
    {"genesis_plus_gx_enhanced_vscroll_limit", "增强逐格垂直滚动上限"},
    {"genesis_plus_gx_overclock", "CPU 速度"},
    {"genesis_plus_gx_force_dtack", "系统卡死"},
    {"genesis_plus_gx_addr_error", "68K 地址错误"},
    {"genesis_plus_gx_cd_latency", "CD 读取延时"},
    {"genesis_plus_gx_cd_precache", "CD 镜像缓存"},
    {"genesis_plus_gx_show_advanced_audio_settings", "显示高级音量设置（重开菜单生效）"},
    {"genesis_plus_gx_psg_channel_0_volume", "PSG 音调通道 0 音量 %"},
    {"genesis_plus_gx_psg_channel_1_volume", "PSG 音调通道 1 音量 %"},
    {"genesis_plus_gx_psg_channel_2_volume", "PSG 音调通道 2 音量 %"},
    {"genesis_plus_gx_psg_channel_3_volume", "PSG 噪声通道 3 音量 %"},
    {"genesis_plus_gx_md_channel_0_volume", "MD/Gen FM 通道 0 音量 %"},
    {"genesis_plus_gx_md_channel_1_volume", "MD/Gen FM 通道 1 音量 %"},
    {"genesis_plus_gx_md_channel_2_volume", "MD/Gen FM 通道 2 音量 %"},
    {"genesis_plus_gx_md_channel_3_volume", "MD/Gen FM 通道 3 音量 %"},
    {"genesis_plus_gx_md_channel_4_volume", "MD/Gen FM 通道 4 音量 %"},
    {"genesis_plus_gx_md_channel_5_volume", "MD/Gen FM 通道 5 音量 %"},
    {"genesis_plus_gx_sms_fm_channel_0_volume", "SMS FM 通道 0 音量 %"},
    {"genesis_plus_gx_sms_fm_channel_1_volume", "SMS FM 通道 1 音量 %"},
    {"genesis_plus_gx_sms_fm_channel_2_volume", "SMS FM 通道 2 音量 %"},
    {"genesis_plus_gx_sms_fm_channel_3_volume", "SMS FM 通道 3 音量 %"},
    {"genesis_plus_gx_sms_fm_channel_4_volume", "SMS FM 通道 4 音量 %"},
    {"genesis_plus_gx_sms_fm_channel_5_volume", "SMS FM 通道 5 音量 %"},
    {"genesis_plus_gx_sms_fm_channel_6_volume", "SMS FM 通道 6 音量 %"},
    {"genesis_plus_gx_sms_fm_channel_7_volume", "SMS FM 通道 7 音量 %"},
    {"genesis_plus_gx_sms_fm_channel_8_volume", "SMS FM 通道 8 音量 %"},
};

// Only the words that carry meaning are listed. Numbers, palette names, and
// hardware jargon are left as the core wrote them.
constexpr ValueEntry kValues[] = {
    {"enabled", "开启"},
    {"disabled", "关闭"},
    {"on", "开"},
    {"off", "关"},
    {"yes", "是"},
    {"no", "否"},
    {"auto", "自动"},
    {"auto-detect", "自动检测"},
    {"none", "无"},
    {"default", "默认"},
    {"custom", "自定义"},
    {"raw", "原始"},
    {"per-game", "按游戏"},
    {"uncorrected", "不校正"},
    {"low", "低"},
    {"medium", "中"},
    {"high", "高"},
    {"very high", "很高"},
    {"light", "轻"},
    {"normal", "标准"},
    {"heavy", "重"},
    {"fast", "快"},
    {"accurate", "精确"},
    {"standard", "标准"},
    {"monochrome", "单色"},
    {"composite", "复合"},
    {"s-video", "S 端子"},
    {"white", "白"},
    {"black", "黑"},
    {"grey", "灰"},
    {"gray", "灰"},
    {"red", "红"},
    {"green", "绿"},
    {"blue", "蓝"},
    {"cyan", "青"},
    {"orange", "橙"},
    {"yellow", "黄"},
    {"purple", "紫"},
    {"pink", "粉"},
    {"violet", "紫罗兰"},
    {"sky", "天蓝"},
    {"all 0s (default)", "全 0（默认）"},
    {"all 1s", "全 1"},
    {"random values", "随机值"},
    {"before nmi (recommended)", "在 NMI 之前（推荐）"},
    {"after nmi", "在 NMI 之后"},
    {"25% grey", "25% 灰"},
    {"50% grey", "50% 灰"},
    {"75% grey", "75% 灰"},
};

constexpr std::size_t kBlendSuffixLength = 8;  // " (blend)"
constexpr const char kBlendSuffix[] = " (blend)";
constexpr const char kBlendText[] = "（混合）";

bool equalsIgnoreCase(std::string_view left, std::string_view right) noexcept {
  if (left.size() != right.size())
    return false;
  for (std::size_t index = 0; index < left.size(); ++index) {
    const unsigned char a = static_cast<unsigned char>(left[index]);
    const unsigned char b = static_cast<unsigned char>(right[index]);
    const unsigned char lowerA = (a >= 'A' && a <= 'Z') ? a + ('a' - 'A') : a;
    const unsigned char lowerB = (b >= 'A' && b <= 'Z') ? b + ('a' - 'A') : b;
    if (lowerA != lowerB)
      return false;
  }
  return true;
}

const char *findLabel(std::string_view key) noexcept {
  for (const LabelEntry &entry : kLabels) {
    if (key == entry.key)
      return entry.text;
  }
  return nullptr;
}

const char *findValue(std::string_view value) noexcept {
  for (const ValueEntry &entry : kValues) {
    if (equalsIgnoreCase(value, entry.value))
      return entry.text;
  }
  return nullptr;
}

}  // namespace

std::string coreOptionLabel(std::string_view key, std::string_view fallback,
                            Language language) {
  if (language != Language::SimplifiedChinese)
    return std::string(fallback);
  const char *known = findLabel(key);
  return known != nullptr ? std::string(known) : std::string(fallback);
}

std::string coreOptionValue(std::string_view value, std::string_view fallback,
                            Language language) {
  if (language != Language::SimplifiedChinese)
    return std::string(fallback);
  if (const char *known = findValue(value); known != nullptr)
    return std::string(known);
  if (value.size() > kBlendSuffixLength &&
      value.compare(value.size() - kBlendSuffixLength, kBlendSuffixLength, kBlendSuffix) == 0) {
    const std::string_view base = value.substr(0, value.size() - kBlendSuffixLength);
    if (const char *blendBase = findValue(base); blendBase != nullptr)
      return std::string(blendBase) + kBlendText;
  }
  return std::string(fallback);
}

}  // namespace launcher::i18n
