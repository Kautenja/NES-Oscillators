// Sony S-DSP emulator.
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

#ifndef DSP_SONY_S_DSP_ADSR_HPP_
#define DSP_SONY_S_DSP_ADSR_HPP_

#include <cstdint>


#include <iostream>

/// @brief Sony S-DSP chip emulator.
class Sony_S_DSP_ADSR {
 public:
    /// the sample rate of the S-DSP in Hz
    static constexpr unsigned SAMPLE_RATE = 32000;
    /// the number of sampler voices on the chip TODO: remove
    static constexpr unsigned VOICE_COUNT = 8;
    /// the number of registers on the chip TODO: remove
    static constexpr unsigned NUM_REGISTERS = 128;

 private:
    // -----------------------------------------------------------------------
    // Byte 1
    // -----------------------------------------------------------------------
    /// the attack rate (4-bits)
    uint8_t attack : 4;
    /// the decay rate (3-bits)
    uint8_t decay : 3;
    /// a dummy bit for byte alignment
    const uint8_t unused_spacer_for_byte_alignment : 1;
    // -----------------------------------------------------------------------
    // Byte 2
    // -----------------------------------------------------------------------
    /// the sustain rate (5-bits)
    uint8_t sustain_rate : 5;
    /// the sustain level (3-bits)
    uint8_t sustain_level : 3;
    // -----------------------------------------------------------------------
    // Byte 3
    // -----------------------------------------------------------------------
    /// the total amplitude level of the envelope generator (8-bit)
    int8_t amplitude = 0;
    // -----------------------------------------------------------------------
    // Byte 4
    // -----------------------------------------------------------------------
    /// the output value from the envelope generator
    int8_t envelope_output = 0;

    /// The stages of the ADSR envelope generator.
    enum class EnvelopeStage : uint8_t { Attack, Decay, Sustain, Release };
    /// the current stage of the envelope generator
    EnvelopeStage envelope_stage = EnvelopeStage::Release;
    /// the current value of the envelope generator
    uint16_t envelope_value = 0;
    /// the sample (time) counter for the envelope
    short envelope_counter = 0;
    /// true if the envelope generator is running (clocking an envelope)
    bool keys = 0;

    /// @brief Process the envelope for the voice with given index.
    ///
    /// @param voice_index the index of the voice to clock the envelope of
    ///
    int clock_envelope();

 public:
    /// @brief Initialize a new Sony_S_DSP_ADSR.
    Sony_S_DSP_ADSR() : unused_spacer_for_byte_alignment(1) {
        attack = 0;
        decay = 0;
        sustain_rate = 0;
        sustain_level = 0;
        amplitude = 0;
    }

    /// @brief Set the attack parameter to a new value.
    ///
    /// @param value the attack rate to use.
    ///
    inline void setAttack(uint8_t value) { attack = value; }

    /// @brief Set the decay parameter to a new value.
    ///
    /// @param value the decay rate to use.
    ///
    inline void setDecay(uint8_t value) { decay = value; }

    /// @brief Set the sustain rate parameter to a new value.
    ///
    /// @param value the sustain rate to use.
    ///
    inline void setSustainRate(uint8_t value) { sustain_rate = value; }

    /// @brief Set the sustain level parameter to a new value.
    ///
    /// @param value the sustain level to use.
    ///
    inline void setSustainLevel(uint8_t value) { sustain_level = value; }

    /// @brief Set the amplitude parameter to a new value.
    ///
    /// @param value the amplitude level to use.
    ///
    inline void setAmplitude(int8_t value) { amplitude = value; }

    /// @brief Run DSP for some samples and write them to the given buffer.
    int16_t run(bool trigger, bool gate_on);
};

#endif  // DSP_SONY_S_DSP_ADSR_HPP_
