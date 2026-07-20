#include "musiccom/mml.hpp"
#include "musiccom/player.hpp"
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
static void u32(std::ofstream &o, uint32_t v) { o.write((char *)&v, 4); }
static void u16(std::ofstream &o, uint16_t v) { o.write((char *)&v, 2); }
int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "usage: musiccom_render input.mml output.wav\n";
    return 2;
  }
  std::ifstream in(argv[1], std::ios::binary);
  std::string text((std::istreambuf_iterator<char>(in)), {});
  auto song = musiccom::parse_mml(text);
  musiccom::trim_trailing_silence(song);
  musiccom::Player player(std::move(song));
  std::vector<float> pcm;
  std::vector<float> block(2048);
  for (;;) {
    auto n = player.render(block.data(), 1024);
    if (!n)
      break;
    pcm.insert(pcm.end(), block.begin(), block.begin() + n * 2);
  }
  std::ofstream o(argv[2], std::ios::binary);
  uint32_t bytes = (uint32_t)(pcm.size() * 4);
  o.write("RIFF", 4);
  u32(o, 36 + bytes);
  o.write("WAVEfmt ", 8);
  u32(o, 16);
  u16(o, 3);
  u16(o, 2);
  u32(o, 48000);
  u32(o, 48000 * 8);
  u16(o, 8);
  u16(o, 32);
  o.write("data", 4);
  u32(o, bytes);
  o.write((char *)pcm.data(), bytes);
  std::cout << pcm.size() / 2 << " frames\n";
}
