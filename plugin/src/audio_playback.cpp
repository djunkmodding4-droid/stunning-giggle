#include "audio_playback.h"
#include "plugin_debug.h"
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <vector>
#include <atomic>

#ifdef HAVE_PORTAUDIO
#include <portaudio.h>
#endif

static std::deque<std::vector<int16_t>> g_queue;
static std::mutex g_queue_mutex;
static std::condition_variable g_queue_cv;
static std::atomic<bool> g_playback_running(false);
static std::thread g_playback_thread;
static int g_play_sampleRate = 48000;
static int g_play_channels = 1;

static void playback_thread_func() {
#ifdef HAVE_PORTAUDIO
  PaError err = Pa_Initialize();
  if (err != paNoError) {
    std::cerr << "audio_playback: PortAudio init failed: " << Pa_GetErrorText(err) << std::endl;
    return;
  }

  PaStreamParameters outParams;
  outParams.device = Pa_GetDefaultOutputDevice();
  if (outParams.device == paNoDevice) {
    std::cerr << "audio_playback: No default output device." << std::endl;
    Pa_Terminate();
    return;
  }
  const PaDeviceInfo* di = Pa_GetDeviceInfo(outParams.device);
  outParams.channelCount = g_play_channels;
  outParams.sampleFormat = paInt16;
  outParams.suggestedLatency = di->defaultLowOutputLatency;
  outParams.hostApiSpecificStreamInfo = nullptr;

  PaStream* stream = nullptr;
  err = Pa_OpenStream(&stream, nullptr, &outParams, g_play_sampleRate, paFramesPerBufferUnspecified, paNoFlag, nullptr, nullptr);
  if (err != paNoError) {
    std::cerr << "audio_playback: Pa_OpenStream failed: " << Pa_GetErrorText(err) << std::endl;
    Pa_Terminate();
    return;
  }
  err = Pa_StartStream(stream);
  if (err != paNoError) {
    std::cerr << "audio_playback: Pa_StartStream failed: " << Pa_GetErrorText(err) << std::endl;
    Pa_CloseStream(stream);
    Pa_Terminate();
    return;
  }

  while (g_playback_running.load()) {
    std::unique_lock<std::mutex> lk(g_queue_mutex);
    g_queue_cv.wait(lk, [](){ return !g_queue.empty() || !g_playback_running.load(); });
    if (!g_playback_running.load()) break;
    auto buf = std::move(g_queue.front());
    g_queue.pop_front();
    lk.unlock();

    if (!buf.empty()) {
      PaError wr = Pa_WriteStream(stream, buf.data(), (unsigned long)(buf.size() / g_play_channels));
      if (wr != paNoError) {
        std::cerr << "audio_playback: Pa_WriteStream failed: " << Pa_GetErrorText(wr) << std::endl;
      }
    }
  }

  Pa_StopStream(stream);
  Pa_CloseStream(stream);
  Pa_Terminate();
#else
  (void)g_queue; (void)g_queue_cv; (void)g_queue_mutex; (void)g_playback_running;
#endif
}

int audio_playback_start() {
#ifdef HAVE_PORTAUDIO
  if (g_playback_running.load()) return 0;
  g_playback_running.store(true);
  g_playback_thread = std::thread(playback_thread_func);
  if (g_plugin_debug.load()) std::cout << "audio_playback: started" << std::endl;
  return 0;
#else
  std::cerr << "audio_playback: PortAudio not available at build time" << std::endl;
  return -1;
#endif
}

void audio_playback_stop() {
#ifdef HAVE_PORTAUDIO
  if (!g_playback_running.load()) return;
  g_playback_running.store(false);
  g_queue_cv.notify_all();
  if (g_playback_thread.joinable()) g_playback_thread.join();
  if (g_plugin_debug.load()) std::cout << "audio_playback: stopped" << std::endl;
#else
  (void)g_playback_running;
#endif
}

void audio_playback_enqueue(const int16_t* pcm, size_t samples, int sampleRate, int channels) {
#ifdef HAVE_PORTAUDIO
  if (!g_playback_running.load()) return;
  std::vector<int16_t> v;
  v.assign(pcm, pcm + samples);
  {
    std::lock_guard<std::mutex> lk(g_queue_mutex);
    g_queue.push_back(std::move(v));
  }
  g_queue_cv.notify_one();
#else
  (void)pcm; (void)samples; (void)sampleRate; (void)channels;
#endif
}