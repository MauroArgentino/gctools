#include <inttypes.h>
#include <stdio.h>

#include <samplerate.h>

#include <map>
#include <phosg/Encoding.hh>
#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <string>
#include <unordered_map>

#include "../wav.hh"
#include "aaf.hh"
#include "audio.hh"

using namespace std;



enum DebugFlag {
  ShowResampleEvents      = 0x0000000000000001,
  ShowNotesOn             = 0x0000000000000002,
  ShowUnknownPerfOptions  = 0x0000000000000004,
  ShowUnknownParamOptions = 0x0000000000000008,

  Default                 = 0x0000000000000002,
};

uint64_t debug_flags = DebugFlag::Default;

int resample_method = SRC_SINC_BEST_QUALITY;

vector<float> resample(const vector<float>& input_samples, size_t num_channels,
    double src_ratio) {
  size_t input_frame_count = input_samples.size() / num_channels;
  size_t output_frame_count = (input_frame_count * src_ratio) + 1;
  size_t output_sample_count = output_frame_count * num_channels;

  vector<float> output_samples(output_sample_count, 0.0f);

  SRC_DATA data;
  data.data_in = const_cast<float*>(input_samples.data());
  data.data_out = const_cast<float*>(output_samples.data());
  data.input_frames = input_frame_count;
  data.output_frames = output_frame_count;
  data.input_frames_used = 0;
  data.output_frames_gen = 0;
  data.end_of_input = 0;
  data.src_ratio = src_ratio;

  int error = src_simple(&data, resample_method, num_channels);
  if (error) {
    throw runtime_error(string_printf("src_simple failed: %s",
        src_strerror(error)));
  }

  output_samples.resize(data.output_frames_gen * num_channels);
  return output_samples;
}



class StringReader {
public:
  explicit StringReader(shared_ptr<string> data, size_t offset = 0) :
      data(data), offset(offset) { }
  ~StringReader() = default;

  size_t where() const {
    return this->offset;
  }

  void go(size_t offset) {
    this->offset = offset;
  }

  bool eof() const {
    return (this->offset >= this->data->size());
  }

  uint8_t get_u8() {
    return static_cast<uint8_t>((*this->data)[this->offset++]);
  }

  int8_t get_s8() {
    return static_cast<int8_t>((*this->data)[this->offset++]);
  };

  uint16_t get_u16() {
    uint16_t ret = bswap16(*reinterpret_cast<const uint16_t*>(
        this->data->data() + this->offset));
    this->offset += 2;
    return ret;
  };

  int16_t get_s16() {
    int16_t ret = bswap16(*reinterpret_cast<const int16_t*>(
        this->data->data() + this->offset));
    this->offset += 2;
    return ret;
  };

  uint32_t get_u24() {
    uint32_t high = this->get_u8();
    uint32_t low = this->get_u16();
    return low | (high << 16);
  };

private:
  shared_ptr<string> data;
  size_t offset;
};



string name_for_note(uint8_t note) {
  if (note >= 0x80) {
    return "invalid-note";
  }
  const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A",
      "A#", "B"};
  return string_printf("%s%hhu", names[note % 12], note / 12);
}

uint8_t lower_c_note_for_note(uint8_t note) {
  return note - (note % 12);
}

double frequency_for_note(uint8_t note) {
  static const double freq_table[0x80] = {
    8.1757989156,     8.6619572180,     9.1770239974,     9.7227182413,
    10.3008611535,    10.9133822323,    11.5623257097,    12.2498573744,
    12.9782717994,    13.7500000000,    14.5676175474,    15.4338531643,
    16.3515978313,    17.3239144361,    18.3540479948,    19.4454364826,
    20.6017223071,    21.8267644646,    23.1246514195,    24.4997147489,
    25.9565435987,    27.5000000000,    29.1352350949,    30.8677063285,
    32.7031956626,    34.6478288721,    36.7080959897,    38.8908729653,
    41.2034446141,    43.6535289291,    46.2493028390,    48.9994294977,
    51.9130871975,    55.0000000000,    58.2704701898,    61.7354126570,
    65.4063913251,    69.2956577442,    73.4161919794,    77.7817459305,
    82.4068892282,    87.3070578583,    92.4986056779,    97.9988589954,
    103.8261743950,   110.0000000000,   116.5409403795,   123.4708253140,
    130.8127826503,   138.5913154884,   146.8323839587,   155.5634918610,
    164.8137784564,   174.6141157165,   184.9972113558,   195.9977179909,
    207.6523487900,   220.0000000000,   233.0818807590,   246.9416506281,
    261.6255653006,   277.1826309769,   293.6647679174,   311.1269837221,
    329.6275569129,   349.2282314330,   369.9944227116,   391.9954359817,
    415.3046975799,   440.0000000000,   466.1637615181,   493.8833012561,
    523.2511306012,   554.3652619537,   587.3295358348,   622.2539674442,
    659.2551138257,   698.4564628660,   739.9888454233,   783.9908719635,
    830.6093951599,   880.0000000000,   932.3275230362,   987.7666025122,
    1046.5022612024,  1108.7305239075,  1174.6590716696,  1244.5079348883,
    1318.5102276515,  1396.9129257320,  1479.9776908465,  1567.9817439270,
    1661.2187903198,  1760.0000000000,  1864.6550460724,  1975.5332050245,
    2093.0045224048,  2217.4610478150,  2349.3181433393,  2489.0158697766,
    2637.0204553030,  2793.8258514640,  2959.9553816931,  3135.9634878540,
    3322.4375806396,  3520.0000000000,  3729.3100921447,  3951.0664100490,
    4186.009044809,   4434.922095630,   4698.636286678,   4978.031739553,
    5274.040910605,   5587.651702928,   5919.910763386,   6271.926975708,
    6644.875161279,   7040.000000000,   7458.620234756,   7902.132834658,
    8372.0180896192,  8869.8441912599,  9397.2725733570,  9956.0634791066,
    10548.0818212118, 11175.3034058561, 11839.8215267723, 12543.8539514160,
  };
  if (note >= 0x80) {
    throw invalid_argument("note does not exist");
  }
  return freq_table[note];
}

void disassemble_set_perf(size_t opcode_offset, uint8_t opcode, uint8_t type,
    uint8_t data_type, int16_t value, uint8_t duration_flags,
    uint16_t duration) {

  string param_name;
  if (type == 0x00) {
    param_name = "volume";
  } else if (type == 0x01) {
    param_name = "pitch_bend";
  } else if (type == 0x02) {
    param_name = "reverb";
  } else if (type == 0x03) {
    param_name = "panning";
  } else {
    param_name = string_printf("[%02hhX]", type);
  }

  printf("%08zX: set_perf        %s=", opcode_offset, param_name.c_str());
  if (data_type == 4) {
    printf("0x%02hhX (u8)", static_cast<uint8_t>(value));
  } else if (data_type == 8) {
    printf("0x%02hhX (s8)", static_cast<int8_t>(value));
  } else if (data_type == 12) {
    printf("0x%04hX (s16)", static_cast<int16_t>(value));
  }
  if (duration_flags == 2) {
    printf(", duration=0x%02hhX", static_cast<uint8_t>(duration));
  } else if (duration == 3) {
    printf(", duration=0x%04hX", static_cast<uint16_t>(duration));
  }
  printf("\n");
}

void disassemble_set_param(size_t opcode_offset, bool is16, uint8_t param,
    uint16_t value) {
  string value_str = is16 ? string_printf("0x%04hX", value) :
      string_printf("0x%02hhX", static_cast<uint8_t>(value));

  if (param == 0x20) {
    printf("%08zX: set_param       bank, value=%s\n", opcode_offset,
        value_str.c_str());
  } else if (param == 0x21) {
    printf("%08zX: set_param       insprog, value=%s\n", opcode_offset,
        value_str.c_str());
  } else {
    printf("%08zX: set_param       param=0x%02hhX, value=%s\n", opcode_offset,
        param, value_str.c_str());
  }
}

void disassemble_stream(StringReader& r) {
  unordered_map<size_t, string> track_start_labels;

  while (!r.eof()) {
    size_t opcode_offset = r.where();

    {
      auto label_it = track_start_labels.find(opcode_offset);
      if (label_it != track_start_labels.end()) {
        printf("%s:\n", label_it->second.c_str());
        track_start_labels.erase(label_it);
      }
    }

    uint8_t opcode = r.get_u8();
    if (opcode < 0x80) {
      uint8_t voice = r.get_u8(); // between 1 and 8 inclusive
      uint8_t vel = r.get_u8();
      string note_name = name_for_note(opcode);
      printf("%08zX: note            note=%s, voice=%hhu, vel=0x%02hhX\n",
          opcode_offset, note_name.c_str(), voice, vel);
      continue;
    }

    switch (opcode) {
      case 0x80: {
        uint8_t wait_time = r.get_u8();
        printf("%08zX: wait            %hhu\n", opcode_offset, wait_time);
        break;
      }
      case 0x88: {
        uint16_t wait_time = r.get_u16();
        printf("%08zX: wait            %hu\n", opcode_offset, wait_time);
        break;
      }

      case 0x81:
      case 0x82:
      case 0x83:
      case 0x84:
      case 0x85:
      case 0x86:
      case 0x87: {
        uint8_t voice = opcode & 7;
        printf("%08zX: voice_off       %hhu\n", opcode_offset, voice);
        break;
      }

      case 0x94:
      case 0x96:
      case 0x97:
      case 0x98:
      case 0x9A:
      case 0x9B:
      case 0x9C:
      case 0x9E:
      case 0x9F: {
        uint8_t type = r.get_u8();
        uint8_t duration_flags = opcode & 0x03;
        uint8_t data_type = opcode & 0x0C;
        int16_t value = 0;
        uint16_t duration = 0;
        if (data_type == 4) {
          value = r.get_u8();
        } else if (data_type == 8) {
          value = r.get_s8();
        } else if (data_type == 12) {
          value = r.get_s16();
        }
        if (duration_flags == 2) {
          duration = r.get_u8();
        } else if (duration == 3) {
          duration = r.get_u16();
        }

        disassemble_set_perf(opcode_offset, opcode, type, data_type, value,
            duration_flags, duration);
        break;
      }

      case 0xA4:
      case 0xAC: {
        uint8_t param = r.get_u8();
        uint16_t value = (opcode & 0x08) ? r.get_u16() : r.get_u8();
        disassemble_set_param(opcode_offset, (opcode & 0x08), param, value);
        break;
      }

      case 0xC1: {
        uint8_t track_id = r.get_u8();
        uint32_t offset = r.get_u24();
        printf("%08zX: start_track     %hhu, offset=0x%" PRIX32 "\n",
            opcode_offset, track_id, offset);
        track_start_labels.emplace(offset, string_printf("track_%02hhX_start",
            track_id));
        break;
      }

      case 0xC3:
      case 0xC4:
      case 0xC7:
      case 0xC8: {
        const char* opcode_name = (opcode > 0xC4) ? "jmp " : "call";
        string conditional_str = (opcode & 1) ? "" :
            string_printf("cond=0x%02hhX, ", r.get_u8());

        uint32_t offset = r.get_u24();
        printf("%08zX: %s            %soffset=0x%" PRIX32 "\n",
            opcode_offset, opcode_name, conditional_str.c_str(), offset);
        break;
      }

      case 0xC5:
        printf("%08zX: ret\n", opcode_offset);
        break;

      case 0xC6: {
        string conditional_str = string_printf("cond=0x%02hhX", r.get_u8());
        printf("%08zX: ret             %s\n", opcode_offset, conditional_str.c_str());
        break;
      }

      // case 0xE6: // vibrato
      case 0xE7: { // sync_gpu
        uint16_t arg = r.get_u16();
        printf("%08zX: sync_gpu        0x%04hX\n", opcode_offset, arg);
        break;
      }

      case 0xFD: {
        uint16_t pulse_rate = r.get_u16();
        printf("%08zX: set_pulse_rate  %hu\n", opcode_offset,
            pulse_rate);
        break;
      }

      case 0xFE: {
        uint16_t tempo = r.get_u16();
        uint64_t usec_pqn = 60000000 / tempo;
        printf("%08zX: set_tempo       %hu /* usecs per quarter note = %"
            PRIu64 " */\n", opcode_offset, tempo, usec_pqn);
        break;
      }

      case 0xFF: {
        printf("%08zX: end_track\n", opcode_offset);
        break;
      }

      default:
        printf("%08zX: .unknown        0x%02hhX\n", opcode_offset, opcode);
    }
  }
}



class Voice {
public:
  Voice(size_t sample_rate, int8_t note, int8_t vel) : sample_rate(sample_rate),
      note(note), vel(vel), note_off_decay_total(this->sample_rate / 5),
      note_off_decay_remaining(-1) { }

  virtual vector<float> render(size_t count, float volume, float panning) = 0;

  void off() {
    // TODO: for now we use a constant release time of 1/4 second; we probably
    // should get this from the AAF somewhere but I don't know where
    this->note_off_decay_remaining = this->note_off_decay_total;
  }

  float advance_note_off_factor() {
    if (this->note_off_decay_remaining == 0) {
      return 0.0f;
    }
    if (this->note_off_decay_remaining > 0) {
      return static_cast<float>(this->note_off_decay_remaining--) /
          this->note_off_decay_total;
    }
    return 1.0f;
  }

  size_t sample_rate;
  int8_t note;
  int8_t vel;
  ssize_t note_off_decay_total;
  ssize_t note_off_decay_remaining;
};

class SineVoice : public Voice {
public:
  SineVoice(size_t sample_rate, int8_t note, int8_t vel) :
      Voice(sample_rate, note, vel), offset(0) { }

  virtual vector<float> render(size_t count, float volume, float panning) {
    vector<float> data(count * 2, 0.0f);

    double frequency = frequency_for_note(this->note);
    float vel_factor = static_cast<float>(this->vel) / 0x7F;
    for (size_t x = 0; x < count; x++) {
      // panning is 0.0f (left) - 1.0f (right)
      float off_factor = this->advance_note_off_factor();
      data[2 * x + 0] = vel_factor * off_factor * (1.0f - panning) * volume * sin((2.0f * M_PI * frequency) / this->sample_rate * (x + this->offset));
      data[2 * x + 1] = vel_factor * off_factor * panning * volume * sin((2.0f * M_PI * frequency) / this->sample_rate * (x + this->offset));
    }
    this->offset += count;

    return data;
  }

  size_t offset;
};

class SampleVoice : public Voice {
public:
  SampleVoice(size_t sample_rate, const SoundEnvironment* env, uint16_t bank_id,
      uint16_t instrument_id, int8_t note, int8_t vel) :
      Voice(sample_rate, note, vel),
      instrument_bank(&env->instrument_banks.at(bank_id)),
      instrument(&this->instrument_bank->id_to_instrument.at(instrument_id)),
      key_region(&this->instrument->region_for_key(note)),
      vel_region(&this->key_region->region_for_velocity(vel)), offset(0) {

    if (this->vel_region->sound->num_channels != 1) {
      // TODO: this probably wouldn't be that hard to support
      throw invalid_argument("sampled sound is multi-channel");
    }

    // stretch it out by the sample rate difference
    float sample_rate_factor = static_cast<float>(sample_rate) /
        static_cast<float>(this->vel_region->sound->sample_rate);

    // compress it so it's the right note
    int8_t base_note = this->vel_region->base_note;
    if (base_note < 0) {
      base_note = this->vel_region->sound->base_note;
    }
    float note_factor = frequency_for_note(base_note) /
        frequency_for_note(this->note);

    float src_ratio = note_factor * sample_rate_factor / this->vel_region->freq_mult;
    this->loop_start_offset = this->vel_region->sound->loop_start * src_ratio;
    this->loop_end_offset = this->vel_region->sound->loop_end * src_ratio;

    if (debug_flags & DebugFlag::ShowResampleEvents) {
      string key_low_str = name_for_note(this->key_region->key_low);
      string key_high_str = name_for_note(this->key_region->key_high);
      fprintf(stderr, "[%s:%" PRIX64 "] resampling note %02hhX in range "
          "[%02hhX,%02hhX] [%s,%s] (base %02hhX from %s) (%g), with freq_mult %g, from "
          "%zuHz to %zuHz (%g) with loop at [%zu,%zu]->[%zu,%zu] for an overall "
          "ratio of %g\n", this->vel_region->sound->source_filename.c_str(),
          this->vel_region->sound->sound_id, this->note,
          this->key_region->key_low, this->key_region->key_high,
          key_low_str.c_str(), key_high_str.c_str(), base_note,
          (this->vel_region->base_note == -1) ? "sample" : "vel region", note_factor,
          this->vel_region->freq_mult, this->vel_region->sound->sample_rate,
          this->sample_rate, sample_rate_factor,
          this->vel_region->sound->loop_start, this->vel_region->sound->loop_end,
          this->loop_start_offset, this->loop_end_offset, src_ratio);
    }

    this->samples = resample(this->vel_region->sound->samples,
        this->vel_region->sound->num_channels, src_ratio);
  }

  virtual vector<float> render(size_t count, float volume, float panning) {
    // TODO: implement pitch bend here somehow... probably have to resample
    // again, sigh

    vector<float> data(count * 2, 0.0f);

    float vel_factor = static_cast<float>(this->vel) / 0x7F;
    for (size_t x = 0; (x < count) && (this->offset < this->samples.size()); x++) {
      float off_factor = this->advance_note_off_factor();
      data[2 * x + 0] = vel_factor * off_factor * (1.0f - panning) * volume * this->samples[this->offset];
      data[2 * x + 1] = vel_factor * off_factor * panning * volume * this->samples[this->offset];

      this->offset++;
      if ((this->loop_end_offset > 0) && (this->offset > this->loop_end_offset)) {
        this->offset = this->loop_start_offset;
      }
    }

    return data;
  }

  const InstrumentBank* instrument_bank;
  const Instrument* instrument;
  const KeyRegion* key_region;
  const VelocityRegion* vel_region;

  vector<float> samples;
  size_t loop_start_offset;
  size_t loop_end_offset;
  size_t offset;
};

class Renderer {
private:
  struct Track {
    int16_t id;
    StringReader r;

    float volume;
    float pitch_bend;
    float reverb;
    float panning;
    int32_t bank; // technically uint16, but uninitialized as -1
    int32_t instrument; // technically uint16, but uninitialized as -1

    shared_ptr<Voice> voices[8];
    vector<uint32_t> call_stack;

    Track(int16_t id, shared_ptr<string> data, size_t start_offset) :
        id(id), r(data, start_offset), volume(0), pitch_bend(0), panning(0.5f),
        bank(-1), instrument(-1) {
      for (size_t x = 0; x < 8; x++) {
        this->voices[x].reset();
      }
    }
  };

  shared_ptr<string> data;
  string output_data;
  unordered_map<int16_t, shared_ptr<Track>> id_to_track;
  multimap<uint64_t, shared_ptr<Track>> next_event_to_track;

  size_t sample_rate;
  uint64_t current_time;
  size_t samples_rendered;
  uint16_t tempo;
  uint16_t pulse_rate;

  shared_ptr<const SoundEnvironment> env;
  unordered_set<int16_t> disable_tracks;

  vector<float> samples;

public:
  explicit Renderer(shared_ptr<string> data, size_t sample_rate,
      shared_ptr<const SoundEnvironment> env,
      const unordered_set<int16_t>& disable_tracks) : data(data),
      sample_rate(sample_rate), current_time(0), samples_rendered(0), tempo(0),
      pulse_rate(0), env(env), disable_tracks(disable_tracks) {
    // the default track has a track id of -1; all others are uint8_t
    if (!this->disable_tracks.count(-1)) {
      shared_ptr<Track> default_track(new Track(-1, data, 0));
      id_to_track.emplace(default_track->id, default_track);
      next_event_to_track.emplace(0, default_track);
    }
  }

  ~Renderer() = default;

  vector<float> render_time_step() {
    if (this->next_event_to_track.empty()) {
      return vector<float>();
    }

    // run all opcodes that should execute on the current time step
    while (current_time == this->next_event_to_track.begin()->first) {
      this->execute_opcode(this->next_event_to_track.begin());
    }

    // figure out how many samples to produce
    if (this->sample_rate == 0) {
      throw invalid_argument("sample rate not set before producing audio");
    }
    if (this->tempo == 0) {
      throw invalid_argument("tempo not set before producing audio");
    }
    if (this->pulse_rate == 0) {
      throw invalid_argument("pulse rate not set before producing audio");
    }
    uint64_t usecs_per_qnote = 60000000 / this->tempo;
    double usecs_per_pulse = static_cast<double>(usecs_per_qnote) / this->pulse_rate;
    double samples_per_pulse = usecs_per_pulse / (1000000 / this->sample_rate);
    uint64_t end_total_samples = (this->current_time + 1) * samples_per_pulse;
    uint64_t samples_to_produce = end_total_samples - this->samples_rendered / 2;

    // render this timestep
    vector<float> step_samples(2 * samples_to_produce, 0);
    char notes_table[0x81];
    memset(notes_table, ' ', 0x80);
    notes_table[0x80] = 0;
    for (const auto& track_it : this->id_to_track) {
      for (size_t x = 0; x < 8; x++) {
        if (!track_it.second->voices[x].get()) {
          continue;
        }

        auto v = track_it.second->voices[x];
        vector<float> voice_samples = v->render(samples_to_produce,
            track_it.second->volume, track_it.second->panning);
        if (voice_samples.size() != step_samples.size()) {
          throw logic_error("voice produced incorrect sample count");
        }
        for (size_t y = 0; y < voice_samples.size(); y++) {
          step_samples[y] += voice_samples[y];
        }

        // only render the note if it's on
        if (v->note_off_decay_remaining < 0) {
          int8_t note = track_it.second->voices[x]->note;
          char track_char = '0' + track_it.second->id;
          if (notes_table[note] == track_char) {
            continue;
          }
          if (notes_table[note] == ' ') {
            notes_table[note] = track_char;
          } else {
            notes_table[note] = '+';
          }
        }
      }
    }

    // render the text view. if this is a regular step, render the headers also
    if (debug_flags & DebugFlag::ShowNotesOn) {
      if (this->current_time % 20 == 0) {
        fprintf(stderr, "        : C D EF G A BC D EF G A BC D EF G A BC "
            "D EF G A BC D EF G A BC D EF G A BC D EF G A BC D EF G A BC D EF G A"
            " BC D EF G A BC D EF\n");
      }
      double when = static_cast<double>(this->samples_rendered / 2) / this->sample_rate;
      fprintf(stderr, "%08" PRIX64 ": %s @ %g (#%zu)\n",
          current_time, notes_table, when, this->samples_rendered);
    }

    // advance to the next time step
    this->current_time++;
    this->samples_rendered += step_samples.size();

    return step_samples;
  }

  vector<float> render_until(uint64_t time) {
    vector<float> samples;
    while (!this->next_event_to_track.empty() && (this->current_time < time)) {
      auto step_samples = this->render_time_step();
      samples.insert(samples.end(), step_samples.begin(), step_samples.end());
    }
    return samples;
  }

  vector<float> render_until_seconds(float seconds) {
    vector<float> samples;
    size_t target_size = seconds * this->sample_rate * 2;
    while (!this->next_event_to_track.empty() && (this->samples_rendered < target_size)) {
      auto step_samples = this->render_time_step();
      samples.insert(samples.end(), step_samples.begin(), step_samples.end());
    }
    return samples;
  }

  vector<float> render_all() {
    vector<float> samples;
    while (!this->next_event_to_track.empty()) {
      auto step_samples = this->render_time_step();
      samples.insert(samples.end(), step_samples.begin(), step_samples.end());
    }
    return samples;
  }

private:

  void execute_set_perf(shared_ptr<Track> t, uint8_t type, float value,
      uint16_t duration) {
    if (type == 0x00) {
      t->volume = value;
    } else if (type == 0x01) {
      t->pitch_bend = value;
      if (debug_flags & DebugFlag::ShowUnknownPerfOptions) {
        fprintf(stderr, "pitch bend modified on track %hd: %g\n", t->id, value);
      }
    } else if (type == 0x02) {
      t->reverb = value;
    } else if (type == 0x03) {
      t->panning = value;
    } else {
      if (debug_flags & DebugFlag::ShowUnknownPerfOptions) {
        fprintf(stderr, "unknown perf type option: %02hhX\n", type);
      }
    }
  }

  void execute_set_param(shared_ptr<Track> t, uint8_t param, uint16_t value) {
    if (param == 0x20) {
      t->bank = value;
    } else if (param == 0x21) {
      t->instrument = value;
    } else {
      if (debug_flags & DebugFlag::ShowUnknownParamOptions) {
        fprintf(stderr, "unknown param type option: %02hhX\n", param);
      }
    }
  }

  void execute_opcode(multimap<uint64_t, shared_ptr<Track>>::iterator track_it) {
    shared_ptr<Track> t = track_it->second;

    uint8_t opcode = t->r.get_u8();
    if (opcode < 0x80) {
      // note: opcode is also the note
      uint8_t voice = t->r.get_u8() - 1; // between 1 and 8 inclusive
      uint8_t vel = t->r.get_u8();
      if (voice >= 0x08) {
        throw invalid_argument("voice out of range");
      }

      // figure out which sample to use
      if (this->env) {
        try {
          SampleVoice* v = new SampleVoice(this->sample_rate, this->env.get(),
              t->bank, t->instrument, opcode, vel);
          t->voices[voice].reset(v);
        } catch (const out_of_range& e) {
          fprintf(stderr, "warning: can\'t find sample (%s): bank=%" PRIX32
              " instrument=%" PRIX32 " key=%02hhX vel=%02hhX\n", e.what(),
              t->bank, t->instrument, opcode, vel);
          t->voices[voice].reset(new SineVoice(this->sample_rate, opcode, vel));
        }
      } else {
        t->voices[voice].reset(new SineVoice(this->sample_rate, opcode, vel));
      }

      return;
    }

    switch (opcode) {
      case 0x80:
      case 0x88: {
        uint16_t wait_time = (opcode & 0x08) ? t->r.get_u16() : t->r.get_u8();
        uint64_t reactivation_time = this->current_time + wait_time;
        this->next_event_to_track.erase(track_it);
        this->next_event_to_track.emplace(reactivation_time, t);
        break;
      }

      case 0x81:
      case 0x82:
      case 0x83:
      case 0x84:
      case 0x85:
      case 0x86:
      case 0x87: {
        uint8_t voice = (opcode & 7) - 1;
        if (!t->voices[voice].get()) {
          throw invalid_argument("nonexistent voice was disabled");
        }
        t->voices[voice]->off();
        break;
      }

      case 0x94:
      case 0x96:
      case 0x97:
      case 0x98:
      case 0x9A:
      case 0x9B:
      case 0x9C:
      case 0x9E:
      case 0x9F: {
        uint8_t type = t->r.get_u8();
        uint8_t duration_flags = opcode & 0x03;
        uint8_t data_type = opcode & 0x0C;
        float value = 0.0f;
        uint16_t duration = 0;
        if (data_type == 4) {
          value = static_cast<float>(t->r.get_u8()) / 0xFF;
        } else if (data_type == 8) {
          value = static_cast<float>(t->r.get_s8()) / 0x7F;
        } else if (data_type == 12) {
          value = static_cast<float>(t->r.get_s16()) / 0x7FFF;
        }
        if (duration_flags == 2) {
          duration = t->r.get_u8();
        } else if (duration == 3) {
          duration = t->r.get_u16();
        }

        this->execute_set_perf(t, type, value, duration);
        break;
      }

      case 0xA4:
      case 0xAC: {
        uint8_t param = t->r.get_u8();
        uint16_t value = (opcode & 0x08) ? t->r.get_u16() : t->r.get_u8();
        this->execute_set_param(t, param, value);
        break;
      }

      case 0xC1: {
        uint8_t track_id = t->r.get_u8();
        uint32_t offset = t->r.get_u24();
        if (!this->disable_tracks.count(track_id)) {
          shared_ptr<Track> new_track(new Track(track_id, this->data, offset));
          auto emplace_ret = this->id_to_track.emplace(track_id, new_track);
          if (!emplace_ret.second) {
            throw invalid_argument("attempted to start track that already existed");
          }
          this->next_event_to_track.emplace(this->current_time, new_track);
        }
        break;
      }

      case 0xC3:
      case 0xC4:
      case 0xC7:
      case 0xC8: {
        bool is_call = (opcode <= 0xC4);
        bool is_conditional = !(opcode & 1);

        int16_t cond = is_conditional ? t->r.get_u8() : -1;
        uint32_t offset = t->r.get_u24();

        if (cond > 0) {
          throw invalid_argument(string_printf(
              "unimplemented condition: 0x%02hhX", cond));
        }

        if (is_call) {
          t->call_stack.emplace_back(t->r.where());
        }
        t->r.go(offset);
        break;
      }

      case 0xC5:
      case 0xC6: {
        bool is_conditional = !(opcode & 1);
        int16_t cond = is_conditional ? t->r.get_u8() : -1;

        if (cond > 0) {
          throw invalid_argument(string_printf(
              "unimplemented condition: 0x%02hhX", cond));
        }

        if (t->call_stack.empty()) {
          throw invalid_argument("return executed with empty call stack");
        }
        t->r.go(t->call_stack.back());
        t->call_stack.pop_back();
        break;
      }

      // case 0xE6: // vibrato
      case 0xE7: { // sync_gpu
        t->r.get_u16();
        // TODO: what should we do here? anything?
        break;
      }

      case 0xFD: {
        uint16_t new_pulse_rate = t->r.get_u16();
        if (this->current_time && (new_pulse_rate != this->pulse_rate)) {
          throw invalid_argument("pulse rate changed during execution");
        }
        this->pulse_rate = new_pulse_rate;
        break;
      }

      case 0xFE: {
        uint16_t new_tempo = t->r.get_u16();
        if (this->current_time && (new_tempo != this->tempo)) {
          throw invalid_argument("tempo changed during execution");
        }
        this->tempo = new_tempo;
        break;
      }

      case 0xFF: {
        this->id_to_track.erase(t->id);
        this->next_event_to_track.erase(track_it);
        break;
      }

      default:
        throw invalid_argument("unknown opcode: " + to_string(opcode));
    }
  }
};



int main(int argc, char** argv) {

  const char* filename = NULL;
  const char* output_filename = NULL;
  const char* aaf_directory = NULL;
  unordered_set<int16_t> disable_tracks;
  float time_limit = 60.0f;
  size_t sample_rate = 192000;
  bool play = false;
  for (int x = 1; x < argc; x++) {
    if (!strncmp(argv[x], "--disable-track=", 16)) {
      disable_tracks.emplace(atoi(&argv[x][16]));
    } else if (!strncmp(argv[x], "--time-limit=", 13)) {
      time_limit = atof(&argv[x][13]);
    } else if (!strncmp(argv[x], "--sample-rate=", 14)) {
      sample_rate = atoi(&argv[x][14]);
    } else if (!strncmp(argv[x], "--audiores-directory=", 21)) {
      aaf_directory = &argv[x][21];
    } else if (!strncmp(argv[x], "--output-filename=", 18)) {
      output_filename = &argv[x][18];
    } else if (!strcmp(argv[x], "--verbose")) {
      debug_flags = 0xFFFFFFFFFFFFFFFF;
    } else if (!strncmp(argv[x], "--linear", 8)) {
      resample_method = SRC_LINEAR;
    } else if (!strcmp(argv[x], "--play")) {
      play = true;
    } else if (!filename) {
      filename = argv[x];
    } else {
      throw invalid_argument("too many positional command-line args");
    }
  }
  if (!filename) {
    throw invalid_argument("no filename given");
  }

  shared_ptr<string> data(new string(load_file(filename)));

  if (output_filename) {
    shared_ptr<const SoundEnvironment> env(aaf_directory ?
        new SoundEnvironment(aaf_decode_directory(aaf_directory)) : NULL);
    Renderer r(data, sample_rate, env, disable_tracks);

    auto samples = r.render_until_seconds(time_limit);
    save_wav(output_filename, samples, sample_rate, 2);

  } else if (play) {
    shared_ptr<const SoundEnvironment> env(aaf_directory ?
        new SoundEnvironment(aaf_decode_directory(aaf_directory)) : NULL);
    Renderer r(data, sample_rate, env, disable_tracks);

    init_al();
    al_stream stream(sample_rate, AL_FORMAT_STEREO16);
    vector<float> pending_samples;
    for (;;) {
      auto step_samples = r.render_time_step();
      if (step_samples.empty()) {
        break;
      }
      pending_samples.insert(pending_samples.end(), step_samples.begin(), step_samples.end());

      // enqueue 1/8 of a second at a time
      if (pending_samples.size() > sample_rate / 4) {
        vector<int16_t> al_samples = convert_samples_to_int(pending_samples);
        stream.add_samples(al_samples.data(), al_samples.size() / 2);
        pending_samples.clear();
      }
    }

    // enqueue any remaining samples
    if (pending_samples.empty()) {
      vector<int16_t> al_samples = convert_samples_to_int(pending_samples);
      stream.add_samples(al_samples.data(), al_samples.size() / 2);
    }
    stream.wait();
    exit_al();

  } else {
    StringReader r(data);
    disassemble_stream(r);
  }

  return 0;
}