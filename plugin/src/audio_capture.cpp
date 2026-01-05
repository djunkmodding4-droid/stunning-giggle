#include "audio_capture.h"
#include "plugin_debug.h"
#include <iostream>
#include <atomic>
#include <mutex>
#include <vector>

#ifdef HAVE_PORTAUDIO
#include <portaudio.h>
#endif

static std::atomic<bool> g_capture_running(false);
static pcm_callback_t g_pcm_cb = nullptr;
static std::mutex g_cb_mutex;

int audio_capture_start() {
#ifdef HAVE_PORTAUDIO
  PaError err = Pa_Initialize();
  if (err != paNoError) {
    std::cerr << "PortAudio init failed: " << Pa_GetErrorText(err) << std::endl;
    return -1;
  }

  PaStreamParameters inputParams;
  inputParams.device = Pa_GetDefaultInputDevice();
  if (inputParams.device == paNoDevice) {
    std::cerr << "No default input device." << std::endl;
    Pa_Terminate();
    return -1;
  }
  const PaDeviceInfo* di = Pa_GetDeviceInfo(inputParams.device);
  inputParams.channelCount = 1; // mono for simplicity
  inputParams.sampleFormat = paInt16;
  inputParams.suggestedLatency = di->defaultLowInputLatency;
  inputParams.hostApiSpecificStreamInfo = nullptr;

  PaStream* stream = nullptr;
  err = Pa_OpenStream(&stream, &inputParams, nullptr, 48000, paFramesPerBufferUnspecified, paNoFlag, [](const void* input, void* /*output*/, unsigned long frameCount, const PaStreamCallbackTimeInfo* /*timeInfo*/, PaStreamCallbackFlags /*statusFlags*/, void* userData)->int {
    (void)userData;
    const int16_t* in = (const int16_t*)input;
    std::lock_guard<std::mutex> lk(g_cb_mutex);
    if (g_pcm_cb && in) {
      // call cb once per callback
      g_pcm_cb(in, frameCount, 48000, 1);
    }
    return paContinue;
  }, nullptr);
  if (err != paNoError) {
    std::cerr << "Pa_OpenStream failed: " << Pa_GetErrorText(err) << std::endl;
    Pa_Terminate();
    return -1;
  }

  err = Pa_StartStream(stream);
  if (err != paNoError) {
    std::cerr << "Pa_StartStream failed: " << Pa_GetErrorText(err) << std::endl;
    Pa_CloseStream(stream);
    Pa_Terminate();
    return -1;
  }

  // store stream pointer in a thread-local static or global if we need to stop, but for simplicity we keep running
  g_capture_running.store(true);
  if (g_plugin_debug.load()) std::cout << "audio_capture: started (PortAudio)" << std::endl;
  return 0;
#else
  (void)g_pcm_cb;
  std::cerr << "audio_capture: PortAudio not available at build time" << std::endl;
  return -1;
#endif
}

void audio_capture_stop() {
#ifdef HAVE_PORTAUDIO
  if (!g_capture_running.load()) return;
  Pa_Terminate();
  g_capture_running.store(false);
  if (g_plugin_debug.load()) std::cout << "audio_capture: stopped" << std::endl;
#else
  (void)g_capture_running;
  std::cerr << "audio_capture_stop: PortAudio not available" << std::endl;
#endif
}

void audio_capture_set_callback(pcm_callback_t cb) {
  std::lock_guard<std::mutex> lk(g_cb_mutex);
  g_pcm_cb = cb;
}
