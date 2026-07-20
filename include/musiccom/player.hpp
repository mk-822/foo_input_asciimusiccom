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
}
