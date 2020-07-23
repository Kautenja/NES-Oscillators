// Private oscillators used by NintendoGBS
// Copyright 2020 Christian Kauten
// Copyright 2006 Shay Green
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// derived from: Game_Music_Emu 0.5.2
//

#ifndef DSP_NINTENDO_GAMEBOY_OSCILLATORS_HPP_
#define DSP_NINTENDO_GAMEBOY_OSCILLATORS_HPP_

#include "blip_buffer.hpp"

struct NintendoGBS_Oscillator {
    /// TODO:
    enum { trigger = 0x80 };
    /// TODO:
    enum { len_enabled_mask = 0x40 };
    /// TODO:
    BLIPBuffer* outputs[4];  // NULL, right, left, center
    /// TODO:
    BLIPBuffer* output;
    /// TODO:
    int output_select;
    /// the 5 registers for the oscillator
    uint8_t* regs;  // osc's 5 registers

    /// TODO:
    int delay;
    /// TODO:
    int last_amp;
    /// TODO:
    int volume;
    /// TODO:
    int length;
    /// TODO:
    int enabled;

    /// Reset the oscillator to its default state.
    inline void reset() {
        delay = 0;
        last_amp = 0;
        length = 0;
        output_select = 3;
        output = outputs[output_select];
    }

    /// TODO:
    inline void clock_length() {
        if ((regs[4] & len_enabled_mask) && length)
            length--;
    }

    /// Return the 11-bit frequency of the oscillator.
    inline int frequency() const {
        return (regs[4] & 7) * 0x100 + regs[3];
    }
};

struct NintendoGBS_Envelope : NintendoGBS_Oscillator {
    int env_delay;

    inline void reset() {
        env_delay = 0;
        NintendoGBS_Oscillator::reset();
    }

    void clock_envelope() {
        if (env_delay && !--env_delay) {
            env_delay = regs[2] & 7;
            int v = volume - 1 + (regs[2] >> 2 & 2);
            if ((unsigned) v < 15)
                volume = v;
        }
    }

    bool write_register(int reg, int data) {
        switch (reg) {
        case 1:
            length = 64 - (regs[1] & 0x3F);
            break;
        case 2:
            if (!(data >> 4))
                enabled = false;
            break;
        case 4:
            if (data & trigger) {
                env_delay = regs[2] & 7;
                volume = regs[2] >> 4;
                enabled = true;
                if (length == 0)
                    length = 64;
                return true;
            }
        }
        return false;
    }
};

struct NintendoGBS_Pulse : NintendoGBS_Envelope {
    enum { period_mask = 0x70 };
    enum { shift_mask  = 0x07 };

    typedef BLIPSynth<blip_good_quality, 1> Synth;
    Synth const* synth;
    int sweep_delay;
    int sweep_freq;
    int phase;

    inline void reset() {
        phase = 0;
        sweep_freq = 0;
        sweep_delay = 0;
        NintendoGBS_Envelope::reset();
    }

    void clock_sweep() {
        int sweep_period = (regs[0] & period_mask) >> 4;
        if (sweep_period && sweep_delay && !--sweep_delay) {
            sweep_delay = sweep_period;
            regs[3] = sweep_freq & 0xFF;
            regs[4] = (regs[4] & ~0x07) | (sweep_freq >> 8 & 0x07);

            int offset = sweep_freq >> (regs[0] & shift_mask);
            if (regs[0] & 0x08)
                offset = -offset;
            sweep_freq += offset;

            if (sweep_freq < 0) {
                sweep_freq = 0;
            } else if (sweep_freq >= 2048) {
                sweep_delay = 0;  // don't modify channel frequency any further
                sweep_freq = 2048;  // silence sound immediately
            }
        }
    }

    void run(blip_time_t time, blip_time_t end_time, int playing) {
        if (sweep_freq == 2048)
            playing = false;

        static unsigned char const table[4] = { 1, 2, 4, 6 };
        int const duty = table[regs[1] >> 6];
        int amp = volume & playing;
        if (phase >= duty)
            amp = -amp;

        int frequency = this->frequency();
        if (unsigned (frequency - 1) > 2040) {  // frequency < 1 || frequency > 2041
            // really high frequency results in DC at half volume
            amp = volume >> 1;
            playing = false;
        }

        {
            int delta = amp - last_amp;
            if (delta) {
                last_amp = amp;
                synth->offset(time, delta, output);
            }
        }

        time += delay;
        if (!playing)
            time = end_time;

        if (time < end_time) {
            int const period = (2048 - frequency) * 4;
            BLIPBuffer* const output = this->output;
            int phase = this->phase;
            int delta = amp * 2;
            do {
                phase = (phase + 1) & 7;
                if (phase == 0 || phase == duty) {
                    delta = -delta;
                    synth->offset(time, delta, output);
                }
                time += period;
            } while (time < end_time);
            this->phase = phase;
            last_amp = delta >> 1;
        }
        delay = time - end_time;
    }
};

struct NintendoGBS_Noise : NintendoGBS_Envelope {
    typedef BLIPSynth<blip_med_quality, 1> Synth;
    Synth const* synth;
    unsigned bits;

    void run(blip_time_t time, blip_time_t end_time, int playing) {
        int amp = volume & playing;
        int tap = 13 - (regs[3] & 8);
        if (bits >> tap & 2)
            amp = -amp;

        {
            int delta = amp - last_amp;
            if (delta) {
                last_amp = amp;
                synth->offset(time, delta, output);
            }
        }

        time += delay;
        if (!playing)
            time = end_time;

        if (time < end_time) {
            static unsigned char const table[8] = { 8, 16, 32, 48, 64, 80, 96, 112 };
            int period = table[regs[3] & 7] << (regs[3] >> 4);

            // keep parallel resampled time to eliminate time conversion in the loop
            BLIPBuffer* const output = this->output;
            const auto resampled_period = output->resampled_duration(period);
            auto resampled_time = output->resampled_time(time);
            unsigned bits = this->bits;
            int delta = amp * 2;

            do {
                unsigned changed = (bits >> tap) + 1;
                time += period;
                bits <<= 1;
                if (changed & 2) {
                    delta = -delta;
                    bits |= 1;
                    synth->offset_resampled(resampled_time, delta, output);
                }
                resampled_time += resampled_period;
            } while (time < end_time);

            this->bits = bits;
            last_amp = delta >> 1;
        }
        delay = time - end_time;
    }
};

struct NintendoGBS_Wave : NintendoGBS_Oscillator {
    typedef BLIPSynth<blip_med_quality, 1> Synth;
    Synth const* synth;
    int wave_pos;
    enum { wave_size = 32 };
    uint8_t wave[wave_size];

    inline void write_register(int reg, int data) {
        switch (reg) {
        case 0:
            if (!(data & 0x80))
                enabled = false;
            break;
        case 1:
            length = 256 - regs[1];
            break;
        case 2:
            volume = data >> 5 & 3;
            break;
        case 4:
            if (data & trigger & regs[0]) {
                wave_pos = 0;
                enabled = true;
                if (length == 0)
                    length = 256;
            }
        }
    }

    void run(blip_time_t time, blip_time_t end_time, int playing) {
        int volume_shift = (volume - 1) & 7;  // volume = 0 causes shift = 7
        int frequency;
        {
            int amp = (wave[wave_pos] >> volume_shift & playing) * 2;
            frequency = this->frequency();
            if (unsigned (frequency - 1) > 2044) {  // frequency < 1 || frequency > 2045
                amp = 30 >> volume_shift & playing;
                playing = false;
            }

            int delta = amp - last_amp;
            if (delta) {
                last_amp = amp;
                synth->offset(time, delta, output);
            }
        }

        time += delay;
        if (!playing)
            time = end_time;

        if (time < end_time) {
            BLIPBuffer* const output = this->output;
            int const period = (2048 - frequency) * 2;
            int wave_pos = (this->wave_pos + 1) & (wave_size - 1);

            do {
                int amp = (wave[wave_pos] >> volume_shift) * 2;
                wave_pos = (wave_pos + 1) & (wave_size - 1);
                int delta = amp - last_amp;
                if (delta) {
                    last_amp = amp;
                    synth->offset(time, delta, output);
                }
                time += period;
            } while (time < end_time);

            this->wave_pos = (wave_pos - 1) & (wave_size - 1);
        }
        delay = time - end_time;
    }
};

#endif  // DSP_NINTENDO_GAMEBOY_OSCILLATORS_HPP_
