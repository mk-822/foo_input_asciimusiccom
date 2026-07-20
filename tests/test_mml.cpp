#include "musiccom/mml.hpp"
#include "musiccom/player.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
int main() {
  musiccom::ParseOptions nofade;
  nofade.infinite_loop_count = 1;
  nofade.fade_seconds = 0;
  musiccom::ParseOptions loop2_nofade;
  loop2_nofade.fade_seconds = 0;
  auto s = musiccom::parse_mml(
      "1:T120 O4 L4 {0 C D}\nSTR:A$=E4\n2:T120 $A$ "
      "R4\nSOUND:@1\nLFO:0,0,0,7,3\nOP1:31,1,2,3,4,5,1,2,-1,0\n",
      loop2_nofade);
  assert(s.tones.count(1));
  assert(std::abs(s.duration - 2.0 / 1.1) < 1e-6);
  int on = 0;
  for (auto &e : s.events)
    if (e.type == musiccom::EventType::NoteOn)
      on++;
  assert(on == 5);
  auto tie = musiccom::parse_mml("1:T120 O4 C4 & C4 &D4\n", nofade);
  std::vector<musiccom::Event> notes;
  for (auto &e : tie.events)
    if (e.type == musiccom::EventType::NoteOn)
      notes.push_back(e);
  assert(notes.size() == 3);
  assert(notes[0].b == 0);
  assert(notes[1].b == 1);
  assert(notes[2].b == 1);
  int offs = 0;
  for (auto &e : tie.events)
    if (e.type == musiccom::EventType::NoteOff)
      offs++;
  assert(offs == 1);
  auto boundary_tie =
      musiccom::parse_mml("1:T120 O4 G4 P0 & G2\n", nofade);
  std::vector<musiccom::Event> boundary_notes;
  int boundary_offs = 0;
  for (auto &e : boundary_tie.events) {
    if (e.type == musiccom::EventType::NoteOn)
      boundary_notes.push_back(e);
    else if (e.type == musiccom::EventType::NoteOff)
      ++boundary_offs;
  }
  assert(boundary_notes.size() == 2);
  assert(boundary_notes[0].b == 0 && boundary_notes[1].b == 1);
  assert(boundary_offs == 1);
  auto porta = musiccom::parse_mml("1:T128 O4 C+ P4 D+4&D+ P0\n", nofade);
  int porta_on = 0, porta_off = 0;
  double porta_seconds = 0;
  for (auto &e : porta.events)
    if (e.type == musiccom::EventType::Portamento) {
      if (e.a == 4) {
        ++porta_on;
        porta_seconds = e.value;
      } else if (e.a == 0)
        ++porta_off;
    }
  assert(porta_on == 1 && porta_off == 1 && porta_seconds > 0);
  auto legacy_loop = musiccom::parse_mml("1:T120 {99 C4}\n", loop2_nofade);
  assert(std::abs(legacy_loop.duration - 1.0 / 1.1) < 1e-6);
  auto omitted_loop = musiccom::parse_mml("1:T120 { C4}\n", loop2_nofade);
  assert(std::abs(omitted_loop.duration - 1.0 / 1.1) < 1e-6);
  int omitted_notes = 0;
  for (auto &e : omitted_loop.events)
    if (e.type == musiccom::EventType::NoteOn)
      ++omitted_notes;
  assert(omitted_notes == 2);
  auto omitted_nested = musiccom::parse_mml(
      "1:T120 { C1}\n2:T120 {{2 C1}}\n", loop2_nofade);
  int nested_notes[2]{};
  for (auto &e : omitted_nested.events)
    if (e.type == musiccom::EventType::NoteOn && e.channel < 2)
      ++nested_notes[e.channel];
  assert(nested_notes[0] == 4 && nested_notes[1] == 4);
  auto default_dot = musiccom::parse_mml("1:T120 L4. C D4 E\n", nofade);
  assert(std::abs(default_dot.duration - 2.0 / 1.1) < 1e-6);
  auto independent_loops = musiccom::parse_mml(
      "1:T120 {0 C1}\n2:T120 {0 C2}\n3:T120 {0 C4}\n", loop2_nofade);
  double last[3]{};
  for (auto &e : independent_loops.events)
    if (e.channel < 3)
      last[e.channel] = std::max(last[e.channel], e.time);
  assert(std::abs(last[0] - last[1]) < 1e-6);
  assert(std::abs(last[0] - last[2]) < 1e-6);
  auto inherited = musiccom::parse_mml("1:T150 C4\n2:C4\n", nofade);
  assert(std::abs(inherited.duration - (0.4 / 1.1)) < 1e-6);
  auto ssg_env =
      musiccom::parse_mml("4:T120 @1 C4&C4\nSSGENV:@1,2,15,12,8,4,0\n", nofade);
  bool timed_envelope = false;
  for (auto &e : ssg_env.events)
    if (e.type == musiccom::EventType::Tone && e.channel == 3 && e.a == 1 &&
        e.b > 0)
      timed_envelope = true;
  assert(timed_envelope);

  // COP26 uses a roughly 97 ms SSG envelope step at T70.  Multiplying that
  // microsecond value by 48 kHz must not overflow a 32-bit int and accelerate
  // the envelope until the held note becomes silent.
  auto held_ssg = musiccom::parse_mml(
      "6:T70 Q8 V13 @1 O5 B2.\n"
      "SSGENV:@1,2,7,8,9,10,11,11,11,11,11,11,11,11,11,11,11,11,11,"
      "11,11,11,11,11,11,11,11,11,11,10,9,8,7,6,5,4,3,2,1\n",
      nofade);
  musiccom::Player held_player(std::move(held_ssg));
  std::vector<float> held_pcm(48000 * 2 * 2);
  auto held_frames = held_player.render(held_pcm.data(), 48000 * 2);
  assert(held_frames == 48000 * 2);
  double held_energy = 0;
  for (size_t frame = 48000; frame < held_frames; ++frame)
    held_energy += std::abs(held_pcm[frame * 2]);
  assert(held_energy > 1.0);

  // A raw Y register write immediately before a note must survive NoteOn.
  // COP03 uses Y66,n this way to shape the opening @3 synth sound.
  auto render_y = [&](int tl) {
    auto y_song = musiccom::parse_mml(
        "3:T150 V15 @1 Y66," + std::to_string(tl) + " O4 C2\n"
        "SOUND:@1\n"
        "LFO:0,0,0,7,0\n"
        "OP1:31,0,0,15,0,0,0,1,0,0\n"
        "OP2:0,0,0,15,0,127,0,1,0,0\n"
        "OP3:0,0,0,15,0,127,0,1,0,0\n"
        "OP4:0,0,0,15,0,127,0,1,0,0\n",
        nofade);
    musiccom::Player y_player(std::move(y_song));
    std::vector<float> pcm(12000 * 2);
    y_player.render(pcm.data(), 12000);
    double energy = 0;
    for (float sample : pcm)
      energy += std::abs(sample);
    return energy;
  };
  assert(render_y(0) > render_y(127) * 10.0);
  std::cout << "ok\n";
}
