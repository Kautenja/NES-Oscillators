// A macro oscillator based on the Konami VRC6 synthesis chip.
// Copyright 2020 Christian Kauten
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

#ifndef NES_VRC6_HPP
#define NES_VRC6_HPP

#include "apu.hpp"

/// A macro oscillator based on the Konami VRC6 synthesis chip.
class VRC6 {
 public:
    VRC6() {
        output(NULL);
        volume(1.0);
        reset();
    }

    // See APU.h for reference
    void reset() {
        last_time = 0;
        for (int i = 0; i < OSC_COUNT; i++) {
            Vrc6_Osc& osc = oscs[i];
            for (int j = 0; j < reg_count; j++)
                osc.regs[j] = 0;
            osc.delay = 0;
            osc.last_amp = 0;
            osc.phase = 1;
            osc.amp = 0;
        }
    }

    void volume(double v) {
        v *= 0.0967 * 2;
        saw_synth.volume(v);
        square_synth.volume(v * 0.5);
    }

    void treble_eq(blip_eq_t const& eq) {
        saw_synth.treble_eq(eq);
        square_synth.treble_eq(eq);
    }

    void output(BLIPBuffer* buf) {
        for (int i = 0; i < OSC_COUNT; i++) osc_output(i, buf);
    }

    static constexpr int OSC_COUNT = 3;
    inline void osc_output(int i, BLIPBuffer* buf) {
        // assert((unsigned) i < OSC_COUNT);
        oscs[i].output = buf;
    }

    void end_frame(cpu_time_t time) {
        if (time > last_time) run_until(time);
        last_time -= time;
        assert(last_time >= 0);
    }

    // Oscillator 0 write-only registers are at $9000-$9002
    // Oscillator 1 write-only registers are at $A000-$A002
    // Oscillator 2 write-only registers are at $B000-$B002
    enum { reg_count = 3 };
    enum { base_addr = 0x9000 };
    enum { addr_step = 0x1000 };
    void write_osc(cpu_time_t time, int osc_index, int reg, int data) {
        assert((unsigned) osc_index < OSC_COUNT);
        assert((unsigned) reg < reg_count);

        run_until(time);
        oscs[osc_index].regs[reg] = data;
    }

 private:
    // noncopyable
    VRC6(const VRC6&);
    VRC6& operator = (const VRC6&);

    struct Vrc6_Osc {
        uint8_t regs[3];
        BLIPBuffer* output;
        int delay;
        int last_amp;
        int phase;
        int amp;  // only used by saw

        int period() const {
            return (regs[2] & 0x0f) * 0x100L + regs[1] + 1;
        }
    };

    Vrc6_Osc oscs[OSC_COUNT];
    cpu_time_t last_time;

    BLIPSynth<BLIPQuality::Medium, 31> saw_synth;
    BLIPSynth<BLIPQuality::Good, 15> square_synth;

    void run_until(cpu_time_t time) {
        assert(time >= last_time);
        run_square(oscs[0], time);
        run_square(oscs[1], time);
        run_saw(time);
        last_time = time;
    }

    void run_square(Vrc6_Osc& osc, cpu_time_t end_time) {
        BLIPBuffer* output = osc.output;
        if (!output) return;

        int volume = osc.regs[0] & 15;
        if (!(osc.regs[2] & 0x80))
            volume = 0;

        int gate = osc.regs[0] & 0x80;
        int duty = ((osc.regs[0] >> 4) & 7) + 1;
        int delta = ((gate || osc.phase < duty) ? volume : 0) - osc.last_amp;
        cpu_time_t time = last_time;
        if (delta) {
            osc.last_amp += delta;
            square_synth.offset(time, delta, output);
        }

        time += osc.delay;
        osc.delay = 0;
        int period = osc.period();
        if (volume && !gate && period > 4) {
            if (time < end_time) {
                int phase = osc.phase;

                do {
                    phase++;
                    if (phase == 16) {
                        phase = 0;
                        osc.last_amp = volume;
                        square_synth.offset(time, volume, output);
                    }
                    if (phase == duty) {
                        osc.last_amp = 0;
                        square_synth.offset(time, -volume, output);
                    }
                    time += period;
                } while (time < end_time);

                osc.phase = phase;
            }
            osc.delay = time - end_time;
        }
    }

    void run_saw(cpu_time_t end_time) {
        Vrc6_Osc& osc = oscs[2];
        BLIPBuffer* output = osc.output;
        if (!output)
            return;

        int amp = osc.amp;
        int amp_step = osc.regs[0] & 0x3F;
        cpu_time_t time = last_time;
        int last_amp = osc.last_amp;
        if (!(osc.regs[2] & 0x80) || !(amp_step | amp)) {
            osc.delay = 0;
            int delta = (amp >> 3) - last_amp;
            last_amp = amp >> 3;
            saw_synth.offset(time, delta, output);
        } else {
            time += osc.delay;
            if (time < end_time) {
                int period = osc.period() * 2;
                int phase = osc.phase;

                do {
                    if (--phase == 0) {
                        phase = 7;
                        amp = 0;
                    }

                    int delta = (amp >> 3) - last_amp;
                    if (delta) {
                        last_amp = amp >> 3;
                        saw_synth.offset(time, delta, output);
                    }

                    time += period;
                    amp = (amp + amp_step) & 0xFF;
                } while (time < end_time);

                osc.phase = phase;
                osc.amp = amp;
            }

            osc.delay = time - end_time;
        }

        osc.last_amp = last_amp;
    }
};

#endif  // NES_VRC6_HPP
