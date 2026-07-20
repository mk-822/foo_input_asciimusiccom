#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace musiccom {
struct Operator {
  int ar = 31, dr = 0, sr = 0, rr = 15, sl = 0, tl = 0, ks = 0, ml = 1, dt = 0,
      dt2 = 0;
};
struct Tone {
  int waveform = 0, speed = 0, depth = 0, algorithm = 7, feedback = 0;
  std::array<Operator, 4> op{};
};
struct SsgEnvelope {
  int period = 1;
  std::vector<int> levels;
};
enum class EventType {
  NoteOn,
  NoteOff,
  Rest,
  Tone,
  Volume,
  Detune,
  Register,
  Portamento,
  Vibrato,
  Tremolo,
  SsgEnvelopeShape,
  SsgEnvelopePeriod
};
struct Event {
  double time = 0;
  EventType type = EventType::NoteOff;
  int channel = 0, a = 0, b = 0;
  double value = 0;
  int c = 0;
};
struct Song {
  std::vector<Event> events;
  std::map<int, Tone> tones;
  std::map<int, SsgEnvelope> envelopes;
  double duration = 0;
  // The untrimmed end is retained so shortening an inaudible tail does not
  // move the configured fade earlier and alter audible samples.
  double fade_end = 0;
  std::string title;
};
struct ParseOptions {
  unsigned infinite_loop_count = 2;
  double fade_seconds = 8.0;
};
Song parse_mml(const std::string &bytes, const ParseOptions &options = {});
} // namespace musiccom
