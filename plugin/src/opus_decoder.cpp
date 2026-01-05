#include "opus_decoder.h"
#include "plugin_debug.h"
#include <iostream>

#ifdef HAVE_OPUS
#include <opus/opus.h>
#endif

static void* g_opus_decoder = nullptr;
static int g_dec_sampleRate = 48000;
static int g_dec_channels = 1;

int opus_decoder_init(int sampleRate, int channels) {
#ifdef HAVE_OPUS
  int err = 0;
  OpusDecoder* dec = opus_decoder_create(sampleRate, channels, &err);
  if (err != OPUS_OK) {
    std::cerr << "Opus decoder create failed: " << opus_strerror(err) << std::endl;
    return -1;
  }
  g_opus_decoder = dec;
  g_dec_sampleRate = sampleRate;
  g_dec_channels = channels;
  if (g_plugin_debug.load()) std::cout << "opus: decoder initialized" << std::endl;
  return 0;
#else
  (void)sampleRate; (void)channels;
  std::cerr << "opus: not available at build time" << std::endl;
  return -1;
#endif
}

std::vector<int16_t> opus_decode_packet(const unsigned char* data, size_t len) {
  std::vector<int16_t> out;
#ifdef HAVE_OPUS
  if (!g_opus_decoder || !data || len == 0) return out;
  // Maximum frame size: 120ms @ 48kHz = 5760 samples per channel
  const int MAX_SAMPLES = 5760;
  out.resize(MAX_SAMPLES * g_dec_channels);
  int nsamples = opus_decode((OpusDecoder*)g_opus_decoder, data, (int)len, out.data(), MAX_SAMPLES, 0);
  if (nsamples < 0) {
    std::cerr << "opus_decode failed: " << opus_strerror(nsamples) << std::endl;
    out.clear();
  } else {
    out.resize(nsamples * g_dec_channels);
  }
#else
  (void)data; (void)len;
#endif
  return out;
}

void opus_decoder_destroy() {
#ifdef HAVE_OPUS
  if (g_opus_decoder) opus_decoder_destroy((OpusDecoder*)g_opus_decoder);
  g_opus_decoder = nullptr;
#endif
}