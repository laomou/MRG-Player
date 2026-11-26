#include "app/RenderProbe.hpp"

#include <cstddef>
#include <cstdlib>
#include <cstring>

#if defined(__linux__)
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace launcher {
namespace {

const char *describeFlags(Uint32 flags) {
  static char text[64];
  text[0] = '\0';
  std::size_t used = 0;
  const auto append = [&used](char *buffer, std::size_t size,
                              const char *name) {
    const int written = std::snprintf(buffer + used, size - used, "%s%s",
                                      used != 0 ? "|" : "", name);
    if (written > 0 && static_cast<std::size_t>(written) < size - used)
      used += static_cast<std::size_t>(written);
  };
  if ((flags & SDL_RENDERER_SOFTWARE) != 0)
    append(text, sizeof text, "software");
  if ((flags & SDL_RENDERER_ACCELERATED) != 0)
    append(text, sizeof text, "accelerated");
  if ((flags & SDL_RENDERER_PRESENTVSYNC) != 0)
    append(text, sizeof text, "vsync");
  if ((flags & SDL_RENDERER_TARGETTEXTURE) != 0)
    append(text, sizeof text, "target-texture");
  return text[0] != '\0' ? text : "none";
}

void printGlDetails(SDL_Renderer *renderer, std::FILE *out) {
  using GlGetString = const unsigned char *(SDLCALL *)(unsigned int);
  const auto getString = reinterpret_cast<GlGetString>(
      SDL_GL_GetProcAddress("glGetString"));
  if (getString == nullptr) {
    std::fprintf(out, "  gl strings: unavailable (%s)\n", SDL_GetError());
    return;
  }
  constexpr unsigned int kGLVendor = 0x1F00;
  constexpr unsigned int kGLRenderer = 0x1F01;
  constexpr unsigned int kGLVersion = 0x1F02;
  const char *vendor = reinterpret_cast<const char *>(getString(kGLVendor));
  const char *device = reinterpret_cast<const char *>(getString(kGLRenderer));
  const char *version = reinterpret_cast<const char *>(getString(kGLVersion));
  std::fprintf(out, "  gl vendor: %s\n", vendor != nullptr ? vendor : "?");
  std::fprintf(out, "  gl renderer: %s\n", device != nullptr ? device : "?");
  std::fprintf(out, "  gl version: %s\n", version != nullptr ? version : "?");
  (void)renderer;
}

#if defined(__linux__)
void printFramebuffer(std::FILE *out) {
  const int fd = ::open("/dev/fb0", O_RDONLY);
  if (fd < 0) {
    std::fprintf(out, "  /dev/fb0: cannot open\n");
    return;
  }
  fb_var_screeninfo info{};
  if (::ioctl(fd, FBIOGET_VSCREENINFO, &info) != 0) {
    std::fprintf(out, "  /dev/fb0: FBIOGET_VSCREENINFO failed\n");
    ::close(fd);
    return;
  }
  std::fprintf(out,
               "  /dev/fb0: %ux%u visible, %ux%u virtual, %u bpp, "
               "%u bits/color\n",
               info.xres, info.yres, info.xres_virtual, info.yres_virtual,
               info.bits_per_pixel, info.red.length + info.green.length +
                                        info.blue.length);
  ::close(fd);
}
#else
void printFramebuffer(std::FILE *out) { (void)out; }
#endif

} // namespace

void printRendererReport(SDL_Window *window, SDL_Renderer *renderer,
                         std::FILE *out) {
  if (out == nullptr)
    return;
  std::fprintf(out, "MRG renderer report\n");
  const char *videoDriver = SDL_GetCurrentVideoDriver();
  std::fprintf(out, "  video driver: %s\n",
               videoDriver != nullptr ? videoDriver : "?");

  const int driverCount = SDL_GetNumRenderDrivers();
  std::fprintf(out, "  render drivers (%d):", driverCount);
  for (int index = 0; index < driverCount; ++index) {
    SDL_RendererInfo info{};
    if (SDL_GetRenderDriverInfo(index, &info) == 0)
      std::fprintf(out, " %s", info.name);
  }
  std::fprintf(out, "\n");

  if (renderer != nullptr) {
    SDL_RendererInfo info{};
    if (SDL_GetRendererInfo(renderer, &info) == 0) {
      std::fprintf(out, "  renderer: %s (%s)\n", info.name,
                   describeFlags(info.flags));
      std::fprintf(out, "  texture formats (%u):", info.num_texture_formats);
      for (Uint32 index = 0; index < info.num_texture_formats; ++index)
        std::fprintf(out, " %s", SDL_GetPixelFormatName(
                                     info.texture_formats[index]));
      std::fprintf(out, "\n");
    }
    int outputWidth = 0;
    int outputHeight = 0;
    if (SDL_GetRendererOutputSize(renderer, &outputWidth, &outputHeight) == 0)
      std::fprintf(out, "  renderer output: %dx%d\n", outputWidth,
                   outputHeight);
    if (info.name != nullptr &&
        std::strstr(info.name, "opengl") != nullptr) {
      int drawableWidth = 0;
      int drawableHeight = 0;
      SDL_GL_GetDrawableSize(window, &drawableWidth, &drawableHeight);
      std::fprintf(out, "  gl drawable: %dx%d\n", drawableWidth,
                   drawableHeight);
      std::fprintf(out, "  gl swap interval: %d\n", SDL_GL_GetSwapInterval());
      printGlDetails(renderer, out);
    }
  }

  if (window != nullptr) {
    int windowWidth = 0;
    int windowHeight = 0;
    SDL_GetWindowSize(window, &windowWidth, &windowHeight);
    std::fprintf(out, "  window: %dx%d\n", windowWidth, windowHeight);
#if SDL_VERSION_ATLEAST(2, 26, 0)
    int pixelWidth = 0;
    int pixelHeight = 0;
    SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight);
    std::fprintf(out, "  window pixels: %dx%d\n", pixelWidth, pixelHeight);
#endif
    const int display = SDL_GetWindowDisplayIndex(window);
    SDL_DisplayMode mode{};
    if (display >= 0 && SDL_GetDesktopDisplayMode(display, &mode) == 0)
      std::fprintf(out, "  desktop mode: %dx%d @ %d Hz, %s\n", mode.w, mode.h,
                   mode.refresh_rate, SDL_GetPixelFormatName(mode.format));
  }

  std::fprintf(out, "  hints: scale-quality=%s vsync=%s render-driver=%s\n",
               SDL_GetHint(SDL_HINT_RENDER_SCALE_QUALITY),
               SDL_GetHint(SDL_HINT_RENDER_VSYNC),
               SDL_GetHint(SDL_HINT_RENDER_DRIVER));
  const char *outputOverride = std::getenv("MRG_OUTPUT_SIZE");
  std::fprintf(out, "  MRG_OUTPUT_SIZE: %s\n",
               outputOverride != nullptr ? outputOverride : "(unset)");
  printFramebuffer(out);
  std::fflush(out);
}

FrameStats::FrameStats() {
  const char *flag = std::getenv("MRG_FRAME_STATS");
  enabled_ = flag != nullptr && flag[0] != '\0' && std::strcmp(flag, "0") != 0;
}

void FrameStats::frame(Uint64 frameTicks, Uint64 presentTicks) {
  if (!enabled_)
    return;
  ++frames_;
  frameTicks_ += frameTicks;
  presentTicks_ += presentTicks;
  if (frames_ % 300 != 0)
    return;
  const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());
  const double frameMs = static_cast<double>(frameTicks_) * 1000.0 /
                         frequency / static_cast<double>(frames_);
  const double presentMs = static_cast<double>(presentTicks_) * 1000.0 /
                           frequency / static_cast<double>(frames_);
  std::fprintf(stderr,
               "frame stats: %.2f ms/frame (%.1f fps), present %.2f ms "
               "over %llu frames\n",
               frameMs, frameMs > 0.0 ? 1000.0 / frameMs : 0.0, presentMs,
               frames_);
  std::fflush(stderr);
}

} // namespace launcher
