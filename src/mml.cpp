#include "musiccom/mml.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace musiccom {
namespace {
std::string trim(std::string s) {
  auto ws = [](unsigned char c) { return std::isspace(c) || c == 0x1a; };
  while (!s.empty() && ws(s.back()))
    s.pop_back();
  size_t n = 0;
  while (n < s.size() && ws(s[n]))
    ++n;
  return s.substr(n);
}
int integer(const std::string &s, size_t &p, int fallback, bool sign = true) {
  while (p < s.size() && std::isspace((unsigned char)s[p]))
    ++p;
  int sg = 1;
  if (sign && p < s.size() && (s[p] == '-' || s[p] == '+')) {
    sg = s[p++] == '-' ? -1 : 1;
  }
  size_t q = p;
  int v = 0;
  while (p < s.size() && std::isdigit((unsigned char)s[p]))
    v = v * 10 + s[p++] - '0';
  return p == q ? fallback : v * sg;
}
std::vector<int> numbers(const std::string &s) {
  std::vector<int> r;
  for (size_t p = 0; p < s.size();) {
    if (std::isdigit((unsigned char)s[p]) || s[p] == '-') {
      r.push_back(integer(s, p, 0));
    } else
      ++p;
  }
  return r;
}
std::string expand_macros(std::string s,
                          const std::map<std::string, std::string> &m,
                          int depth = 0) {
  if (depth > 32)
    throw std::runtime_error("MML macro recursion exceeds 32");
  for (size_t p = 0; (p = s.find('$', p)) != std::string::npos;) {
    size_t e = s.find('$', p + 1);
    if (e == std::string::npos)
      break;
    auto k = s.substr(p + 1, e - p - 1);
    auto i = m.find(k);
    if (i == m.end()) {
      p = e + 1;
      continue;
    }
    auto v = expand_macros(i->second, m, depth + 1);
    s.replace(p, e - p + 1, v);
    p += v.size();
  }
  return s;
}
std::string expand_loops(const std::string &s, unsigned infinite,
                         int depth = 0) {
  if (depth > 16)
    throw std::runtime_error("MML loop nesting exceeds 16");
  std::string out;
  for (size_t p = 0; p < s.size();) {
    if (s[p] != '{') {
      out += s[p++];
      continue;
    }
    size_t q = p + 1;
    int count = integer(s, q, 0, false), level = 1, e = q;
    for (; e < s.size() && level; e++) {
      if (s[e] == '{')
        ++level;
      else if (s[e] == '}')
        --level;
    }
    if (level)
      throw std::runtime_error("Unclosed MML loop");
    auto body = expand_loops(s.substr(q, e - q - 1), infinite, depth + 1);
    // Older MUSIC.COM scores commonly use 99 as a practical infinite loop.
    // Treat it like {0} so the decoder's configured loop count is respected.
    unsigned n = (count == 0 || count == 99) ? infinite : (unsigned)count;
    for (unsigned x = 0; x < n; x++)
      out += body;
    p = e;
  }
  return out;
}
// MUSIC.COM's PC-98 timer runs about 10% faster than the nominal BPM formula
// when compared against the reference driver. Keep the quirk explicit.
constexpr double musiccom_tempo_scale = 1.1;
double note_length(int denom, bool dot, double tempo) {
  double d = 240.0 / (tempo * musiccom_tempo_scale * std::max(1, denom));
  return dot ? d * 1.5 : d;
}
bool has_tie_after_boundary_commands(const std::string &s, size_t p) {
  for (;;) {
    while (p < s.size() && std::isspace((unsigned char)s[p]))
      ++p;
    if (p >= s.size())
      return false;
    if (s[p] == '&')
      return true;
    if (s[p] == '<' || s[p] == '>') {
      ++p;
      continue;
    }
    char command = (char)std::toupper((unsigned char)s[p]);
    if (s[p] != '@' && !std::strchr("PIVQNTLOUSMY", command))
      return false;
    ++p;
    while (p < s.size() &&
           (std::isspace((unsigned char)s[p]) ||
            std::isdigit((unsigned char)s[p]) || s[p] == '-' ||
            s[p] == '+' || s[p] == ','))
      ++p;
  }
}
void parse_track(int ch, std::string s, Song &song, double initial_tempo) {
  int octave = 4, deflen = 4, volume = 12, tone = 0, gate = 8, noise = 0;
  bool defdot = false;
  double tempo = initial_tempo, time = 0;
  bool continuing = false;
  for (size_t p = 0; p < s.size();) {
    unsigned char raw = s[p];
    char c = (char)std::toupper(raw);
    if (std::isspace(raw) || c == ',') {
      ++p;
      continue;
    }
    if (c == 'O') {
      ++p;
      octave = integer(s, p, octave);
      continue;
    }
    if (c == 'L') {
      ++p;
      deflen = integer(s, p, deflen);
      defdot = p < s.size() && s[p] == '.';
      if (defdot)
        ++p;
      continue;
    }
    if (c == 'T') {
      ++p;
      tempo = integer(s, p, (int)tempo);
      continue;
    }
    if (c == 'V') {
      ++p;
      volume = std::clamp(integer(s, p, volume), 0, 15);
      song.events.push_back({time, EventType::Volume, ch, volume, 0});
      continue;
    }
    if (c == '@') {
      ++p;
      tone = integer(s, p, tone);
      int envelope_step_us = 0;
      if (ch >= 3) {
        auto envelope = song.envelopes.find(tone);
        if (envelope != song.envelopes.end())
          envelope_step_us = (int)std::llround(
              note_length(64, false, tempo) *
              std::max(1, envelope->second.period) * 1000000.0);
      }
      song.events.push_back(
          {time, EventType::Tone, ch, tone, envelope_step_us});
      continue;
    }
    if (c == 'Q') {
      ++p;
      gate = std::clamp(integer(s, p, gate), 1, 8);
      continue;
    }
    if (c == 'N') {
      ++p;
      noise = integer(s, p, noise);
      song.events.push_back({time, EventType::Noise, ch, noise, 0});
      continue;
    }
    if (c == 'Y') {
      ++p;
      int a = integer(s, p, 0);
      if (p < s.size() && s[p] == ',')
        ++p;
      int b = integer(s, p, 0);
      song.events.push_back({time, EventType::Register, ch, a, b});
      continue;
    }
    if (c == 'P') {
      ++p;
      int amount = std::max(0, integer(s, p, 0));
      double seconds = note_length(64, false, tempo) * amount;
      song.events.push_back(
          {time, EventType::Portamento, ch, amount, 0, seconds});
      continue;
    }
    if (c == '>') {
      ++octave;
      ++p;
      continue;
    }
    if (c == '<') {
      --octave;
      ++p;
      continue;
    }
    if (c == '&') {
      ++p;
      continue;
    }
    if (c == 'R' || c == 'W' || (c >= 'A' && c <= 'G')) {
      ++p;
      int acc = 0;
      if (c != 'R' && c != 'W' && p < s.size() && (s[p] == '+' || s[p] == '-'))
        acc = s[p++] == '+' ? 1 : -1;
      size_t lengthpos = p;
      while (lengthpos < s.size() && std::isspace((unsigned char)s[lengthpos]))
        ++lengthpos;
      bool explicit_length =
          lengthpos < s.size() && std::isdigit((unsigned char)s[lengthpos]);
      int len = integer(s, p, deflen, false);
      bool explicit_dot = p < s.size() && s[p] == '.';
      bool dot = explicit_length ? explicit_dot : defdot;
      if (explicit_dot)
        ++p;
      // MUSIC.COM allows boundary commands between a note and its slur.  In
      // particular, SDM13 uses "P4 G P0 & G2": P0 takes effect at the note
      // boundary, but must not cause a key-off before the connected G2.
      bool tie = has_tie_after_boundary_commands(s, p);
      double dur = note_length(len, dot, tempo);
      if (c != 'R' && c != 'W') {
        static const int semis[] = {9, 11, 0, 2, 4, 5, 7};
        int idx = c - 'A';
        int midi = (octave + 1) * 12 + semis[idx] + acc;
        song.events.push_back(
            {time, EventType::NoteOn, ch, midi, continuing ? 1 : 0});
        if (!tie)
          song.events.push_back(
              {time + dur * gate / 8.0, EventType::NoteOff, ch, 0, 0});
        continuing = tie;
      } else
        continuing = false;
      time += dur;
      continue;
    } // unsupported modulation/portamento: consume command and numeric
      // arguments
    if (std::strchr("IUSM", c)) {
      ++p;
      while (p < s.size() && (std::isdigit((unsigned char)s[p]) ||
                              s[p] == '-' || s[p] == '+' || s[p] == ','))
        ++p;
      continue;
    }
    ++p;
  }
  song.duration = std::max(song.duration, time);
}
} // namespace

Song parse_mml(const std::string &bytes, const ParseOptions &opt) {
  Song song;
  std::map<std::string, std::string> macros;
  std::array<std::string, 6> tracks;
  std::istringstream in(bytes);
  std::string line;
  int active_tone = -1;
  auto rx = [](const char *p) {
    return std::regex(p, std::regex_constants::icase);
  };
  while (std::getline(in, line)) {
    if (auto x = line.find(';'); x != std::string::npos)
      line.resize(x);
    line = trim(line);
    if (line.empty())
      continue;
    if (line.rfind("->", 0) == 0) {
      auto v = numbers(line);
      if (!song.envelopes.empty())
        song.envelopes.rbegin()->second.levels.insert(
            song.envelopes.rbegin()->second.levels.end(), v.begin(), v.end());
      continue;
    }
    std::smatch m;
    if (std::regex_match(line, m,
                         rx("STR:\\s*\\$?([^$= ]+)\\$?\\s*=\\s*(.*)"))) {
      macros[m[1].str()] = m[2].str();
      continue;
    }
    if (std::regex_match(line, m, std::regex("([1-6])\\s*:\\s*(.*)"))) {
      tracks[m[1].str()[0] - '1'] += ' ' + m[2].str();
      continue;
    }
    if (std::regex_search(line, m, rx("SOUND:\\s*@?([0-9]+)"))) {
      active_tone = std::stoi(m[1]);
      song.tones[active_tone] = Tone{};
      continue;
    }
    if (active_tone >= 0 && std::regex_search(line, m, rx("LFO:\\s*(.*)"))) {
      auto v = numbers(m[1]);
      if (v.size() >= 5) {
        auto &t = song.tones[active_tone];
        t.waveform = v[0];
        t.speed = v[1];
        t.depth = v[2];
        t.algorithm = v[3];
        t.feedback = v[4];
      }
      continue;
    }
    if (active_tone >= 0 &&
        std::regex_search(line, m, rx("OP([1-4]):\\s*(.*)"))) {
      auto v = numbers(m[2]);
      if (v.size() >= 9) {
        Operator o;
        int *x[] = {&o.ar, &o.dr, &o.sr, &o.rr, &o.sl,
                    &o.tl, &o.ks, &o.ml, &o.dt, &o.dt2};
        for (size_t i = 0; i < v.size() && i < 10; i++)
          *x[i] = v[i];
        song.tones[active_tone].op[std::stoi(m[1]) - 1] = o;
      }
      continue;
    }
    if (std::regex_search(line, m, rx("SSGENV:\\s*@?([0-9]+)\\s*,(.*)"))) {
      auto v = numbers(m[2]);
      if (!v.empty()) {
        auto &e = song.envelopes[std::stoi(m[1])];
        e.period = v[0];
        e.levels.assign(v.begin() + 1, v.end());
      }
      continue;
    }
  }
  std::array<std::string, 6> source;
  for (int ch = 0; ch < 6; ch++)
    source[ch] = expand_macros(tracks[ch], macros);
  double initial_tempo = 120;
  std::smatch tempo_match;
  for (auto &s : source)
    if (std::regex_search(s, tempo_match, std::regex("[Tt]\\s*([0-9]+)"))) {
      initial_tempo = std::max(1, std::stoi(tempo_match[1]));
      break;
    }
  auto duration_of = [&](int ch, const std::string &text) {
    Song probe;
    parse_track(ch, text, probe, initial_tempo);
    return probe.duration;
  };
  std::array<double, 6> loop_duration{};
  std::array<double, 6> loop_intro{};
  double master_loop_duration = 0;
  double master_intro = 0;
  bool has_infinite_loop = false;
  // A missing repeat count is MUSIC.COM's normal infinite-loop notation.
  // Keep it in the same duration/synchronization path as the legacy {0}/{99}
  // spellings; otherwise short accompaniment tracks run out before longer,
  // nested tracks (as in COP26) finish their configured repetitions.
  const std::regex infinite_loop(
      "\\{\\s*(?:(?:0|99)(?![0-9])|(?=[^0-9]))");
  for (int ch = 0; ch < 6; ++ch) {
    if (!std::regex_search(source[ch], infinite_loop))
      continue;
    has_infinite_loop = true;
    double once = duration_of(ch, expand_loops(source[ch], 1));
    double twice = duration_of(ch, expand_loops(source[ch], 2));
    loop_duration[ch] = std::max(0.0, twice - once);
    loop_intro[ch] = std::max(0.0, once - loop_duration[ch]);
    master_loop_duration = std::max(master_loop_duration, loop_duration[ch]);
    master_intro = std::max(master_intro, loop_intro[ch]);
  }
  std::array<std::string, 6> expanded;
  for (int ch = 0; ch < 6; ++ch) {
    unsigned repetitions = opt.infinite_loop_count;
    if (loop_duration[ch] > 0 && master_loop_duration > 0) {
      double coverage =
          opt.infinite_loop_count * master_loop_duration + opt.fade_seconds;
      repetitions = std::max(
          1u, (unsigned)std::ceil(coverage / loop_duration[ch] - 1e-9));
    }
    expanded[ch] = expand_loops(source[ch], repetitions);
  }
  for (int ch = 0; ch < 6; ch++)
    parse_track(ch, expanded[ch], song, initial_tempo);
  if (has_infinite_loop && master_loop_duration > 0) {
    song.duration = master_intro +
                    opt.infinite_loop_count * master_loop_duration +
                    opt.fade_seconds;
  } else if (opt.infinite_loop_count > 1 && song.duration > 0) {
    double cycle = song.duration;
    auto original = song.events;
    unsigned cycles_needed = std::max(
        opt.infinite_loop_count,
        (unsigned)std::ceil(
            (opt.infinite_loop_count * cycle + opt.fade_seconds) / cycle));
    for (unsigned n = 1; n < cycles_needed; ++n)
      for (auto e : original) {
        e.time += n * cycle;
        song.events.push_back(e);
      }
    song.duration = opt.infinite_loop_count * cycle + opt.fade_seconds;
  }
  std::stable_sort(song.events.begin(), song.events.end(),
                   [](auto &a, auto &b) { return a.time < b.time; });
  return song;
}
} // namespace musiccom
