#include "musiccom/player.hpp"
#include "ymfm_opn.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace musiccom {
struct Player::Impl : ymfm::ymfm_interface {
  Song song;
  uint32_t rate;
  double fade;
  ymfm::ym2608 chip;
  size_t event = 0;
  uint64_t frame = 0;
  double chip_phase = 0;
  std::array<int, 6> tone{}, volume{};
  std::array<uint64_t, 6> envelope_step_frames{}, envelope_next_frame{};
  std::array<size_t, 6> envelope_index{};
  std::array<bool, 6> envelope_active{};
  struct PitchRamp {
    double current = 0, target = 0, step = 0;
    uint64_t step_frames = 1, next_frame = 0;
    unsigned remaining = 0;
  };
  struct SquareModulation {
    int amount = 0, offset = 0;
    uint64_t period_frames = 1, delay_frames = 0, next_frame = 0;
    bool waiting_for_delay = false;
  };
  std::array<int, 6> detune{}, previous_midi{};
  std::array<unsigned, 6> portamento_ticks{};
  std::array<double, 6> base_hz{}, portamento_tick_seconds{};
  std::array<PitchRamp, 6> ramp{};
  std::array<SquareModulation, 6> vibrato{}, tremolo{};
  std::array<bool, 6> ssg_hardware_envelope{};
  std::array<int, 6> ssg_envelope_shape{};
  uint8_t ssg_mixer = 0x3f;
  explicit Impl(Song s, uint32_t r, double f)
      : song(std::move(s)), rate(r), fade(f), chip(*this) {
    chip.set_fidelity(ymfm::OPN_FIDELITY_MIN);
    reset();
  }
  void reset() {
    chip.reset();
    event = 0;
    frame = 0;
    chip_phase = 0;
    tone.fill(0);
    volume.fill(12);
    envelope_step_frames.fill(0);
    envelope_next_frame.fill(0);
    envelope_index.fill(0);
    envelope_active.fill(false);
    detune.fill(0);
    previous_midi.fill(-1);
    portamento_ticks.fill(0);
    base_hz.fill(0);
    portamento_tick_seconds.fill(0);
    ramp.fill({});
    vibrato.fill({});
    tremolo.fill({});
    ssg_hardware_envelope.fill(false);
    ssg_envelope_shape.fill(0);
    ssg_mixer = 0x3f;
    write(0x29, 0x80);
    write(7, ssg_mixer);
  }
  void write(int reg, int val) {
    chip.write_address((uint8_t)reg);
    chip.write_data((uint8_t)val);
  }
  static bool is_carrier(int algorithm, int op) {
    static constexpr unsigned masks[8] = {0x8, 0x8, 0x8, 0x8,
                                          0xa, 0xe, 0xe, 0xf};
    return (masks[algorithm & 7] & (1u << op)) != 0;
  }
  static int volume_attenuation(int v) {
    v = std::clamp(v, 0, 15);
    return v == 0 ? 127 : (15 - v) * 2;
  }
  int effective_volume(int ch) const {
    return std::clamp(volume[ch] + tremolo[ch].offset, 0, 15);
  }
  int fm_volume_attenuation(int ch) const {
    if (volume[ch] == 0)
      return 127;
    // The FM driver applies U directly in TL units after scaling V; a positive
    // square-wave phase therefore reduces attenuation (111Eh--1167h).
    return std::clamp(volume_attenuation(volume[ch]) - tremolo[ch].offset, 0,
                      127);
  }
  void apply_tone(int ch, int id) {
    if (ch >= 3)
      return;
    auto it = song.tones.find(id);
    if (it == song.tones.end())
      return;
    auto &t = it->second;
    static const int slot[] = {0, 8, 4, 12};
    for (int op = 0; op < 4; op++) {
      auto &o = t.op[op];
      int r = ch + slot[op];
      int dt = o.dt < 0 ? std::max(0, 8 + o.dt) : o.dt;
      int tl =
          o.tl +
          (is_carrier(t.algorithm, op) ? fm_volume_attenuation(ch) : 0);
      write(0x30 + r, (dt & 7) << 4 | (o.ml & 15));
      write(0x40 + r, std::clamp(tl, 0, 127));
      write(0x50 + r, (o.ks & 3) << 6 | (o.ar & 31));
      write(0x60 + r, o.dr & 31);
      write(0x70 + r, o.sr & 31);
      write(0x80 + r, (o.sl & 15) << 4 | (o.rr & 15));
    }
    write(0xb0 + ch, (t.feedback & 7) << 3 | (t.algorithm & 7));
    write(0xb4 + ch, 0xc0);
  }
  void apply_volume(int ch) {
    if (ch >= 3) {
      int c = ch - 3;
      if (ssg_hardware_envelope[ch]) {
        write(8 + c, 0x10);
      } else if (envelope_active[ch]) {
        auto it = song.envelopes.find(tone[ch]);
        if (it != song.envelopes.end() &&
            envelope_index[ch] < it->second.levels.size())
          write_ssg_envelope_level(ch,
                                   it->second.levels[envelope_index[ch]]);
      } else {
        write(8 + c, effective_volume(ch));
      }
      return;
    }
    auto it = song.tones.find(tone[ch]);
    if (it == song.tones.end())
      return;
    auto &t = it->second;
    static const int slot[] = {0, 8, 4, 12};
    for (int op = 0; op < 4; op++)
      if (is_carrier(t.algorithm, op)) {
        int tl = t.op[op].tl + fm_volume_attenuation(ch);
        write(0x40 + ch + slot[op], std::clamp(tl, 0, 127));
      }
  }
  static double midi_hz(double midi) {
    return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
  }
  static double ramp_value_hz(int ch, double value) {
    // FM portamento is linear in F-number (and therefore frequency), while
    // SSG portamento is linear in the inverse-frequency tone period.
    return ch >= 3 ? 7987200.0 / (4 * 16 * value) : value;
  }
  void set_pitch_hz(int ch, double hz) {
    if (ch < 3) {
      int block = 1;
      double fnum = hz * 144.0 * (1 << 20) / 7987200.0;
      while (fnum > 2047 && block < 7) {
        fnum /= 2;
        ++block;
      }
      while (fnum < 1024 && block > 0) {
        fnum *= 2;
        --block;
      }
      int fn = std::clamp((int)std::lround(fnum), 0, 2047);
      write(0xa4 + ch, (block << 3) | (fn >> 8));
      write(0xa0 + ch, fn & 255);
    } else {
      int c = ch - 3;
      int period =
          std::clamp((int)std::lround(7987200.0 / (4 * 16 * hz)), 1, 4095);
      write(c * 2, period & 255);
      write(c * 2 + 1, period >> 8);
    }
  }
  void write_current_pitch(int ch) {
    if (base_hz[ch] <= 0)
      return;
    // N/I use 1/256-semitone units in MUSIC.COM.  The original performs the
    // equivalent adjustment in the chip's frequency-number domain.
    double ratio = std::pow(2.0, vibrato[ch].offset / (256.0 * 12.0));
    set_pitch_hz(ch, base_hz[ch] * ratio);
  }
  void restart_modulation(SquareModulation &m) {
    if (m.amount == 0) {
      m.offset = 0;
      return;
    }
    m.waiting_for_delay = m.delay_frames != 0;
    m.offset = m.waiting_for_delay ? 0 : m.amount;
    m.next_frame = frame +
                   (m.waiting_for_delay ? m.delay_frames : m.period_frames);
  }
  void start_pitch(int ch, int midi) {
    double target = midi_hz(midi + detune[ch] / 256.0);
    if (portamento_ticks[ch] > 0 && previous_midi[ch] >= 0) {
      double from = midi_hz(previous_midi[ch] + detune[ch] / 256.0);
      double from_value = ch >= 3 ? 7987200.0 / (4 * 16 * from) : from;
      double target_value =
          ch >= 3 ? 7987200.0 / (4 * 16 * target) : target;
      unsigned steps = portamento_ticks[ch] + 1;
      uint64_t step_frames = std::max<uint64_t>(
          1, (uint64_t)std::llround(portamento_tick_seconds[ch] * rate));
      base_hz[ch] = from;
      ramp[ch] = {from_value, target_value,
                  (target_value - from_value) / steps, step_frames,
                  frame + step_frames, steps};
    } else {
      base_hz[ch] = target;
      ramp[ch] = {};
    }
    previous_midi[ch] = midi;
    write_current_pitch(ch);
  }
  void write_ssg_envelope_level(int ch, int level) {
    int c = ch - 3;
    // MUSIC.COM combines the channel volume and software-envelope level as
    // attenuation values: CL = V + ENV - 15, saturated at zero (1018h).
    int output = std::clamp(std::clamp(effective_volume(ch), 0, 15) +
                                std::clamp(level, 0, 15) - 15,
                            0, 15);
    write(8 + c, output);
  }
  void start_ssg_envelope(int ch) {
    if (ssg_hardware_envelope[ch]) {
      envelope_active[ch] = false;
      write(8 + ch - 3, 0x10);
      // Writing the shape register restarts the hardware envelope.
      write(13, ssg_envelope_shape[ch] & 15);
      return;
    }
    auto it = song.envelopes.find(tone[ch]);
    if (tone[ch] == 0 || it == song.envelopes.end() ||
        it->second.levels.empty()) {
      envelope_active[ch] = false;
      write(8 + ch - 3, volume[ch] & 15);
      return;
    }
    envelope_active[ch] = true;
    envelope_index[ch] = 0;
    write_ssg_envelope_level(ch, it->second.levels[0]);
    uint64_t step = std::max<uint64_t>(1, envelope_step_frames[ch]);
    envelope_next_frame[ch] = frame + step;
  }
  void note(int ch, int midi, bool on, bool legato = false) {
    if (ch < 3) {
      if (!on) {
        write(0x28, ch);
        return;
      }
      if (!legato) {
        write(0x28, ch);
        ymfm::ym2608::output_data keyoff_clock[3]{};
        chip.generate(keyoff_clock, 3);
      }
      restart_modulation(vibrato[ch]);
      restart_modulation(tremolo[ch]);
      start_pitch(ch, midi);
      if (!legato)
        write(0x28, 0xf0 | ch);
    } else {
      int c = ch - 3;
      if (!on) {
        envelope_active[ch] = false;
        ssg_mixer |= (uint8_t)(1u << c);
        write(7, ssg_mixer);
        return;
      }
      restart_modulation(vibrato[ch]);
      restart_modulation(tremolo[ch]);
      start_pitch(ch, midi);
      if (!legato) {
        start_ssg_envelope(ch);
        ssg_mixer &= (uint8_t)~(1u << c);
        write(7, ssg_mixer);
      }
    }
  }
  void dispatch(const Event &e) {
    switch (e.type) {
    case EventType::NoteOn:
      note(e.channel, e.a, true, e.b != 0);
      break;
    case EventType::NoteOff:
      note(e.channel, 0, false);
      break;
    case EventType::Rest:
      previous_midi[e.channel] = -1;
      ramp[e.channel] = {};
      if (e.a == 0)
        note(e.channel, 0, false);
      break;
    case EventType::Tone:
      tone[e.channel] = e.a;
      if (e.channel < 3)
        apply_tone(e.channel, e.a);
      else {
        // @ selects a software envelope (or @0/fixed volume), overriding
        // hardware S/M mode just as MUSIC.COM does at 120dh.
        ssg_hardware_envelope[e.channel] = false;
        envelope_step_frames[e.channel] = std::max<uint64_t>(
            1, (uint64_t)std::llround((double)e.b * rate / 1000000.0));
      }
      break;
    case EventType::Volume:
      volume[e.channel] = e.a;
      apply_volume(e.channel);
      break;
    case EventType::Detune:
      detune[e.channel] = e.a;
      break;
    case EventType::Register:
      write(e.a, e.b);
      break;
    case EventType::Portamento:
      portamento_ticks[e.channel] = (unsigned)e.a;
      portamento_tick_seconds[e.channel] = e.value;
      // P and I are mutually exclusive; the later command wins.
      if (e.a != 0) {
        vibrato[e.channel] = {};
        write_current_pitch(e.channel);
      } else if (ramp[e.channel].remaining) {
        base_hz[e.channel] =
            ramp_value_hz(e.channel, ramp[e.channel].target);
        ramp[e.channel] = {};
        write_current_pitch(e.channel);
      }
      break;
    case EventType::Vibrato: {
      auto &m = vibrato[e.channel];
      m = {};
      m.amount = e.a;
      m.period_frames = std::max<uint64_t>(
          1, (uint64_t)std::llround(e.b * e.value * rate));
      m.delay_frames =
          (uint64_t)std::llround(e.c * e.value * rate);
      if (e.a != 0) {
        if (ramp[e.channel].remaining) {
          base_hz[e.channel] =
              ramp_value_hz(e.channel, ramp[e.channel].target);
          ramp[e.channel] = {};
        }
        portamento_ticks[e.channel] = 0;
        tremolo[e.channel] = {};
      }
      write_current_pitch(e.channel);
      break;
    }
    case EventType::Tremolo: {
      auto &m = tremolo[e.channel];
      m = {};
      m.amount = e.a;
      m.period_frames = std::max<uint64_t>(
          1, (uint64_t)std::llround(e.b * e.value * rate));
      m.delay_frames =
          (uint64_t)std::llround(e.c * e.value * rate);
      if (e.a != 0) {
        vibrato[e.channel] = {};
        write_current_pitch(e.channel);
      }
      apply_volume(e.channel);
      break;
    }
    case EventType::SsgEnvelopeShape:
      if (e.channel >= 3) {
        ssg_hardware_envelope[e.channel] = true;
        envelope_active[e.channel] = false;
        ssg_envelope_shape[e.channel] = e.a & 15;
      }
      break;
    case EventType::SsgEnvelopePeriod:
      if (e.channel >= 3) {
        ssg_hardware_envelope[e.channel] = true;
        envelope_active[e.channel] = false;
        write(11, e.a & 255);
        write(12, (e.a >> 8) & 255);
      }
      break;
    default:
      break;
    }
  }
  size_t render(float *out, size_t n) {
    double total = song.duration;
    size_t done = 0;
    uint32_t native = chip.sample_rate(7987200);
    for (; done < n && frame < (uint64_t)std::ceil(total * rate);
         done++, frame++) {
      double now = (double)frame / rate;
      while (event < song.events.size() && song.events[event].time <= now)
        dispatch(song.events[event++]);
      for (int ch = 0; ch < 6; ++ch) {
        auto &r = ramp[ch];
        bool pitch_changed = false;
        while (r.remaining && frame >= r.next_frame) {
          if (--r.remaining == 0) {
            base_hz[ch] = ramp_value_hz(ch, r.target);
            r = {};
          } else {
            r.current += r.step;
            base_hz[ch] = ramp_value_hz(ch, r.current);
            r.next_frame += r.step_frames;
          }
          pitch_changed = true;
        }
        auto &vib = vibrato[ch];
        if (vib.amount != 0 && frame >= vib.next_frame) {
          if (vib.waiting_for_delay) {
            vib.waiting_for_delay = false;
            vib.offset = vib.amount;
          } else {
            vib.offset = vib.offset > 0 ? -vib.amount : vib.amount;
          }
          vib.next_frame = frame + vib.period_frames;
          pitch_changed = true;
        }
        if (pitch_changed)
          write_current_pitch(ch);

        auto &tre = tremolo[ch];
        if (tre.amount != 0 && frame >= tre.next_frame) {
          if (tre.waiting_for_delay) {
            tre.waiting_for_delay = false;
            tre.offset = tre.amount;
          } else {
            tre.offset = tre.offset > 0 ? -tre.amount : tre.amount;
          }
          tre.next_frame = frame + tre.period_frames;
          apply_volume(ch);
        }
      }
      for (int ch = 3; ch < 6; ++ch) {
        if (!envelope_active[ch] || frame < envelope_next_frame[ch])
          continue;
        auto it = song.envelopes.find(tone[ch]);
        if (it == song.envelopes.end() ||
            ++envelope_index[ch] >= it->second.levels.size()) {
          envelope_active[ch] = false;
          continue;
        }
        write_ssg_envelope_level(ch, it->second.levels[envelope_index[ch]]);
        envelope_next_frame[ch] +=
            std::max<uint64_t>(1, envelope_step_frames[ch]);
      }
      chip_phase += native;
      ymfm::ym2608::output_data y{};
      while (chip_phase >= rate) {
        chip.generate(&y);
        chip_phase -= rate;
      }
      double gain = 1.0;
      if (fade > 0 && now > total - fade)
        gain = std::clamp((total - now) / fade, 0.0, 1.0);
      double l = (y.data[0] + y.data[1] + y.data[2]) * gain / 32768.0;
      double r = (y.data[0] + y.data[1] + y.data[2]) * gain / 32768.0;
      out[done * 2] = (float)std::clamp(l, -1.0, 1.0);
      out[done * 2 + 1] = (float)std::clamp(r, -1.0, 1.0);
    }
    return done;
  }
};
Player::Player(Song s, uint32_t r, double f)
    : p_(new Impl(std::move(s), r, f)) {}
Player::~Player() = default;
Player::Player(Player &&) noexcept = default;
Player &Player::operator=(Player &&) noexcept = default;
void Player::seek(double sec) {
  p_->reset();
  std::vector<float> sink(4096 * 2);
  uint64_t remain = (uint64_t)(std::max(0.0, sec) * p_->rate);
  while (remain) {
    size_t n = (size_t)std::min<uint64_t>(4096, remain),
           got = p_->render(sink.data(), n);
    if (!got)
      break;
    remain -= got;
  }
}
size_t Player::render(float *out, size_t n) { return p_->render(out, n); }
double Player::duration() const { return p_->song.duration; }
} // namespace musiccom
