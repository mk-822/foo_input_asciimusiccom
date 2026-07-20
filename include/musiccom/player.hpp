#pragma once
#include "musiccom/mml.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>

namespace musiccom {
class Player {
public:
  explicit Player(Song song, uint32_t rate=48000, double fade_seconds=8.0);
  ~Player();
  Player(Player&&) noexcept; Player& operator=(Player&&) noexcept;
  void seek(double seconds);
  size_t render(float* interleaved_stereo, size_t frames);
  double duration() const;
private: struct Impl; std::unique_ptr<Impl> p_;
};

// Shorten song.duration when rendered PCM ends in sustained silence.  The
// fade timing remains anchored to Song::fade_end.  Returns true when trimmed.
bool trim_trailing_silence(Song &song, uint32_t rate = 48000,
                           double fade_seconds = 8.0,
                           float silence_threshold = 1.0e-5f,
                           double minimum_silence_seconds = 0.25,
                           double padding_seconds = 0.02);
}
