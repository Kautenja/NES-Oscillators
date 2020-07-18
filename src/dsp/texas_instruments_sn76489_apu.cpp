// Sega Master System SN76489 programmable sound generator sound chip emulator.
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

#include "texas_instruments_sn76489_apu.hpp"

TexasInstrumentsSN76489_Osc::TexasInstrumentsSN76489_Osc() {
    output = 0;
    outputs[0] = 0; // always stays NULL
    outputs[1] = 0;
    outputs[2] = 0;
    outputs[3] = 0;
}

void TexasInstrumentsSN76489_Osc::reset() {
    delay = 0;
    last_amp = 0;
    volume = 0;
    output_select = 3;
    output = outputs[3];
}

// TexasInstrumentsSN76489_Square

inline void TexasInstrumentsSN76489_Square::reset() {
    period = 0;
    phase = 0;
    TexasInstrumentsSN76489_Osc::reset();
}

void TexasInstrumentsSN76489_Square::run(blip_time_t time, blip_time_t end_time) {
    if (!volume || period <= 128) {
        // ignore 16kHz and higher
        if (last_amp) {
            synth->offset(time, -last_amp, output);
            last_amp = 0;
        }
        time += delay;
        if (!period) {
            time = end_time;
        } else if (time < end_time) {
            // keep calculating phase
            int count = (end_time - time + period - 1) / period;
            phase = (phase + count) & 1;
            time += count * period;
        }
    } else {
        int amp = phase ? volume : -volume;
        {
            int delta = amp - last_amp;
            if (delta) {
                last_amp = amp;
                synth->offset(time, delta, output);
            }
        }

        time += delay;
        if (time < end_time) {
            BLIPBuffer* const output = this->output;
            int delta = amp * 2;
            do {
                delta = -delta;
                synth->offset(time, delta, output);
                time += period;
                phase ^= 1;
            } while (time < end_time);
            this->last_amp = phase ? volume : -volume;
        }
    }
    delay = time - end_time;
}

// TexasInstrumentsSN76489_Noise

static int const noise_periods[3] = { 0x100, 0x200, 0x400 };

inline void TexasInstrumentsSN76489_Noise::reset() {
    period = &noise_periods[0];
    shifter = 0x8000;
    feedback = 0x9000;
    TexasInstrumentsSN76489_Osc::reset();
}

void TexasInstrumentsSN76489_Noise::run(blip_time_t time, blip_time_t end_time) {
    int amp = volume;
    if (shifter & 1)
        amp = -amp;

    {
        int delta = amp - last_amp;
        if (delta) {
            last_amp = amp;
            synth.offset(time, delta, output);
        }
    }

    time += delay;
    if (!volume)
        time = end_time;

    if (time < end_time) {
        BLIPBuffer* const output = this->output;
        unsigned shifter = this->shifter;
        int delta = amp * 2;
        int period = *this->period * 2;
        if (!period)
            period = 16;

        do {
            int changed = shifter + 1;
            shifter = (feedback & -(shifter & 1)) ^ (shifter >> 1);
            if (changed & 2) // true if bits 0 and 1 differ
            {
                delta = -delta;
                synth.offset(time, delta, output);
            }
            time += period;
        } while (time < end_time);

        this->shifter = shifter;
        this->last_amp = delta >> 1;
    }
    delay = time - end_time;
}

// TexasInstrumentsSN76489

TexasInstrumentsSN76489::TexasInstrumentsSN76489() {
    for (int i = 0; i < 3; i++) {
        squares[i].synth = &square_synth;
        oscs[i] = &squares[i];
    }
    oscs[3] = &noise;

    volume(1.0);
    reset();
}

TexasInstrumentsSN76489::~TexasInstrumentsSN76489() {
}

void TexasInstrumentsSN76489::volume(double vol) {
    vol *= 0.85 / (OSC_COUNT * 64 * 2);
    square_synth.volume(vol);
    noise.synth.volume(vol);
}

void TexasInstrumentsSN76489::treble_eq(const blip_eq_t& eq) {
    square_synth.treble_eq(eq);
    noise.synth.treble_eq(eq);
}

void TexasInstrumentsSN76489::osc_output(int index, BLIPBuffer* center, BLIPBuffer* left, BLIPBuffer* right) {
    assert((unsigned) index < OSC_COUNT);
    assert((center && left && right) || (!center && !left && !right));
    TexasInstrumentsSN76489_Osc& osc = *oscs[index];
    osc.outputs[1] = right;
    osc.outputs[2] = left;
    osc.outputs[3] = center;
    osc.output = osc.outputs[osc.output_select];
}

void TexasInstrumentsSN76489::output(BLIPBuffer* center, BLIPBuffer* left, BLIPBuffer* right) {
    for (int i = 0; i < OSC_COUNT; i++)
        osc_output(i, center, left, right);
}

void TexasInstrumentsSN76489::reset(unsigned feedback, int noise_width) {
    last_time = 0;
    latch = 0;

    if (!feedback || !noise_width) {
        feedback = 0x0009;
        noise_width = 16;
    }
    // convert to "Galios configuration"
    looped_feedback = 1 << (noise_width - 1);
    noise_feedback  = 0;
    while (noise_width--) {
        noise_feedback = (noise_feedback << 1) | (feedback & 1);
        feedback >>= 1;
    }

    squares[0].reset();
    squares[1].reset();
    squares[2].reset();
    noise.reset();
}

void TexasInstrumentsSN76489::run_until(blip_time_t end_time) {
    assert(end_time >= last_time); // end_time must not be before previous time
    if (end_time > last_time) {
        // run oscillators
        for (int i = 0; i < OSC_COUNT; ++i) {
            TexasInstrumentsSN76489_Osc& osc = *oscs[i];
            if (osc.output) {
                if (i < 3)
                    squares[i].run(last_time, end_time);
                else
                    noise.run(last_time, end_time);
            }
        }
        last_time = end_time;
    }
}

void TexasInstrumentsSN76489::end_frame(blip_time_t end_time) {
    if (end_time > last_time)
        run_until(end_time);

    assert(last_time >= end_time);
    last_time -= end_time;
}

// volumes[i] = 64 * pow(1.26, 15 - i) / pow(1.26, 15)
static unsigned char const volumes[16] = {
    64, 50, 39, 31, 24, 19, 15, 12, 9, 7, 5, 4, 3, 2, 1, 0
};

void TexasInstrumentsSN76489::write_data(blip_time_t time, int data) {
    assert((unsigned) data <= 0xFF);

    run_until(time);

    if (data & 0x80)
        latch = data;

    int index = (latch >> 5) & 3;
    if (latch & 0x10) {
        oscs[index]->volume = volumes[data & 15];
    }
    else if (index < 3) {
        TexasInstrumentsSN76489_Square& sq = squares[index];
        if (data & 0x80)
            sq.period = (sq.period & 0xFF00) | (data << 4 & 0x00FF);
        else
            sq.period = (sq.period & 0x00FF) | (data << 8 & 0x3F00);
    }
    else
    {
        int select = data & 3;
        if (select < 3)
            noise.period = &noise_periods[select];
        else
            noise.period = &squares[2].period;

        noise.feedback = (data & 0x04) ? noise_feedback : looped_feedback;
        noise.shifter = 0x8000;
    }
}
