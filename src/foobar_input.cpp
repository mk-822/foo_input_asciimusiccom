#include "musiccom/mml.hpp"
#include "musiccom/player.hpp"
#include <foobar2000/SDK/foobar2000.h>
#include <memory>
#include <string>
#include <vector>

DECLARE_COMPONENT_VERSION(
    "ASCII MUSIC.COM MML decoder", "0.1.16",
    "Plays PC-98 MUSIC.COM-style MML. Two loops, then an 8 second fade.");
VALIDATE_COMPONENT_FILENAME("foo_musiccom.dll");
class input_musiccom : public input_stubs {
  service_ptr_t<file> file_;
  std::string text_;
  musiccom::Song song_;
  std::unique_ptr<musiccom::Player> player_;

public:
  void open(service_ptr_t<file> hint, const char *path,
            t_input_open_reason reason, abort_callback &a) {
    if (reason == input_open_info_write)
      throw exception_tagging_unsupported();
    file_ = hint;
    input_open_file_helper(file_, path, reason, a);
    auto n = file_->get_size_ex(a);
    text_.resize((size_t)n);
    file_->read_object(text_.data(), (size_t)n, a);
    song_ = musiccom::parse_mml(text_);
  }
  void get_info(file_info &i, abort_callback &) {
    i.set_length(song_.duration);
    i.info_set_int("samplerate", 48000);
    i.info_set_int("channels", 2);
    i.info_set_int("bitspersample", 32);
    i.info_set("encoding", "synthesized OPNA (YM2608)");
  }
  t_filestats2 get_stats2(unsigned f, abort_callback &a) {
    return file_->get_stats2_(f, a);
  }
  t_filestats get_file_stats(abort_callback &a) { return file_->get_stats(a); }
  void decode_initialize(unsigned, abort_callback &) {
    player_ = std::make_unique<musiccom::Player>(song_);
  }
  bool decode_run(audio_chunk &c, abort_callback &) {
    std::vector<float> b(1024 * 2);
    size_t n = player_->render(b.data(), 1024);
    if (!n)
      return false;
    std::vector<audio_sample> converted(n * 2);
    std::copy_n(b.data(), n * 2, converted.data());
    c.set_data(converted.data(), n, 2, 48000,
               audio_chunk::channel_config_stereo);
    return true;
  }
  void decode_seek(double s, abort_callback &) { player_->seek(s); }
  bool decode_can_seek() { return true; }
  bool decode_get_dynamic_info(file_info &, double &) { return false; }
  bool decode_get_dynamic_info_track(file_info &, double &) { return false; }
  void decode_on_idle(abort_callback &a) { file_->on_idle(a); }
  void retag(const file_info &, abort_callback &) {
    throw exception_tagging_unsupported();
  }
  void remove_tags(abort_callback &) { throw exception_tagging_unsupported(); }
  static bool g_is_our_content_type(const char *) { return false; }
  static bool g_is_our_path(const char *, const char *ext) {
    return stricmp_utf8(ext, "mml") == 0;
  }
  static const char *g_get_name() { return "ASCII MUSIC.COM MML"; }
  static const GUID g_get_guid() {
    return {0xdf649e8a,
            0xb94a,
            0x4e61,
            {0x91, 0x47, 0xdb, 0xd9, 0xd3, 0xec, 0x1d, 0x72}};
  }
};
static input_singletrack_factory_t<input_musiccom> g_factory;
DECLARE_FILE_TYPE("ASCII MUSIC.COM MML files", "*.MML");
