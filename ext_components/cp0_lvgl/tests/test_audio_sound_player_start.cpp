/*
 * SPDX-License-Identifier: MIT
 *
 * Regression test (task c-7): the system sound player's worker thread must not run before the player's members exist.
 *
 * Launcher 0.8.7-c7.1 died with SIGSEGV 0.6 s after start on the FZ-M1 tablet ("segfault at c8" in
 * std::vector<float>::capacity() under Cp0SystemSoundPlayer::Impl::decode_sound()). The worker was started from the
 * member initializer list, before cache_ was constructed: when the scheduler ran the new thread first, it resized cache_
 * and began decoding, then the constructor reset cache_ to an empty vector under it, and decode_sound() wrote the
 * second sound into cache_[1] of a null vector (0 + sizeof(CachedSound) = 0xb8, its pcm's end of storage at 0xc8).
 *
 * The test makes that interleaving certain instead of likely: pthread_create (interposed) returns only once the new
 * thread is inside the first decode, and cp0_file_path() (the asset lookup the worker calls) holds the worker there
 * until the player's constructor has returned. Two real WAV files are decoded. Before the fix: SIGSEGV at 0xc8, as on
 * the tablet. After: the worker decodes the three sounds, then a reload decodes them again.
 */

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "cp0_audio_system_sound_player.hpp"

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <string>

#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>

namespace {

std::string g_dir;
std::mutex g_mutex;
std::condition_variable g_cv;
bool g_armed = false;       /* the next pthread_create waits for its thread to be in the first decode */
bool g_in_decode = false;   /* the worker asked for its first asset */
bool g_constructed = false; /* the player's constructor returned */
int g_lookups = 0;          /* cp0_file_path() calls */

void wait_flag(const bool &flag)
{
    std::unique_lock<std::mutex> lock(g_mutex);
    g_cv.wait_for(lock, std::chrono::seconds(3), [&flag] { return flag; });
}

void raise_flag(bool &flag)
{
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        flag = true;
    }
    g_cv.notify_all();
}

int lookups()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_lookups;
}

void put16(FILE *f, std::uint16_t v)
{
    const unsigned char b[2] = {static_cast<unsigned char>(v & 0xff), static_cast<unsigned char>(v >> 8)};
    std::fwrite(b, 1, 2, f);
}

void put32(FILE *f, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i) std::fputc(static_cast<int>((v >> (8 * i)) & 0xff), f);
}

/* a short 16-bit PCM WAV, 48 kHz stereo, a square wave */
bool write_wav(const std::string &path, std::uint32_t frames)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const std::uint32_t data = frames * 4;
    std::fwrite("RIFF", 1, 4, f);
    put32(f, 36 + data);
    std::fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);
    put16(f, 1);          /* PCM */
    put16(f, 2);          /* channels */
    put32(f, 48000);      /* rate */
    put32(f, 48000 * 4);  /* bytes per second */
    put16(f, 4);          /* block align */
    put16(f, 16);         /* bits */
    std::fwrite("data", 1, 4, f);
    put32(f, data);
    for (std::uint32_t i = 0; i < frames; ++i) {
        const std::uint16_t s = (i / 50) % 2 ? 0x2000 : 0xE000;
        put16(f, s);
        put16(f, s);
    }
    return std::fclose(f) == 0;
}

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                            \
        }                                                                            \
    } while (0)

} // namespace

/* A fresh object starts zeroed, as the launcher's player did at start-up (new memory from the kernel). */
void *operator new(std::size_t size)
{
    void *p = std::calloc(1, size ? size : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }

/* The scheduler runs the new thread first (as on the tablet): the creator gets control back only once that thread is in
 * its first decode. */
extern "C" int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start)(void *), void *arg) noexcept
{
    using real_t = int (*)(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
    static const real_t real = reinterpret_cast<real_t>(dlsym(RTLD_NEXT, "pthread_create"));
    if (!real) return EAGAIN;
    const int rc = real(thread, attr, start, arg);
    bool gate = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        gate = g_armed;
        g_armed = false;
    }
    if (rc == 0 && gate) wait_flag(g_in_decode);
    return rc;
}

/* The player resolves its sounds here, on its worker thread: the first lookup waits until the constructor returned. */
std::string cp0_file_path(std::string file)
{
    bool first = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        ++g_lookups;
        first = !g_in_decode;
    }
    if (first) {
        raise_flag(g_in_decode);
        wait_flag(g_constructed);
    }
    return g_dir + "/" + file;
}

int main()
{
    char tmpl[] = "/tmp/cp0_sound_start_XXXXXX";
    CHECK(::mkdtemp(tmpl) != nullptr);
    g_dir = tmpl;
    CHECK(write_wav(g_dir + "/Ding2.wav", 4800));
    CHECK(write_wav(g_dir + "/key_back.wav", 2400));
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_armed = true;
    }
    {
        Cp0SystemSoundPlayer player; /* 0.8.7-c7.1: SIGSEGV in decode_sound() once the constructor returned */
        raise_flag(g_constructed);
        CHECK(player.sound_count() == 3);
        CHECK(player.contains("Ding2.wav") && player.contains("key_back.wav"));
        /* the three platform sounds looked up: the second one was decoded (the crash was there) */
        for (int i = 0; i < 300 && lookups() < 3; ++i) ::usleep(10000);
        CHECK(lookups() == 3);
        /* the worker is alive past its first decode: a reload decodes the sounds again */
        CHECK(player.reload({"key_back.wav", "Ding2.wav", "key_back.wav"}) == 0);
        for (int i = 0; i < 300 && lookups() < 6; ++i) ::usleep(10000);
        CHECK(lookups() == 6);
    }
    const std::string cleanup = "rm -rf '" + g_dir + "'";
    CHECK(std::system(cleanup.c_str()) == 0);
    std::printf("test_audio_sound_player_start: PASS\n");
    return 0;
}
