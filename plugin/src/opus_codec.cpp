#include "opus_codec.h"
#include "plugin_debug.h"
#include <iostream>
#include <vector>

#ifdef HAVE_OPUS
#include <opus/opus.h>
#endif

static void* g_opus_encoder = nullptr;
static int g_sampleRate = 48000;
static int g_channels = 1;

int opus_encoder_init(int sampleRate, int channels) {
#ifdef HAVE_OPUS
  int err = 0;
  OpusEncoder* enc = opus_encoder_create(sampleRate, channels, OPUS_APPLICATION_VOIP, &err);
  if (err != OPUS_OK) {
    std::cerr << "Opus encoder create failed: " << opus_strerror(err) << std::endl;
    return -1;
  }
  g_opus_encoder = enc;
  g_sampleRate = sampleRate;
  g_channels = channels;
  if (g_plugin_debug.load()) std::cout << "opus: encoder initialized" << std::endl;
  return 0;
#else
  std::cerr << "opus: not available at build time" << std::endl;
  return -1;
#endif
}

std::vector<unsigned char> opus_encode_frame(const int16_t* pcm, size_t samples) {
  std::vector<unsigned char> out;
#ifdef HAVE_OPUS
  if (!g_opus_encoder) return out;
  // Opus maximum packet size (a safe upper bound)
  const int MAX_PACKET = 1500;
  out.resize(MAX_PACKET);
  int nbBytes = opus_encode((OpusEncoder*)g_opus_encoder, pcm, (int)samples, out.data(), MAX_PACKET);
  if (nbBytes < 0) {
    std::cerr << "opus_encode failed: " << opus_strerror(nbBytes) << std::endl;
    out.clear();
  } else {
    out.resize(nbBytes);
  }
#else
  (void)pcm; (void)samples;
#endif
  return out;
}

void opus_encoder_destroy() {
#ifdef HAVE_OPUS
  if (g_opus_encoder) opus_encoder_destroy((OpusEncoder*)g_opus_encoder);
  g_opus_encoder = nullptr;
#endif
}
