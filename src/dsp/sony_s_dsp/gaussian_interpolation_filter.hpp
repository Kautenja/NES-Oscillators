// An emulation of the Gaussian filter from the Sony S-DSP.
// Copyright 2020 Christian Kauten
// Copyright 2006 Shay Green
// Copyright 2002 Brad Martin
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

#ifndef DSP_SONY_S_DSP_GAUSSIAN_INTERPOLATION_FILTER_HPP_
#define DSP_SONY_S_DSP_GAUSSIAN_INTERPOLATION_FILTER_HPP_

#include <string>
#include "common.hpp"

/// @brief Emulations of components from the Sony S-DSP chip
namespace SonyS_DSP {

/// @brief An emulation of the Gaussian filter from the Sony S-DSP.
/// @details
/// The emulator consumes 16 bytes of RAM and is aligned to 16-byte addresses.
class __attribute__((packed, aligned(16))) GaussianInterpolationFilter {
 private:
    // -----------------------------------------------------------------------
    // Byte 1,2, 3,4, 5,6, 7,8
    // -----------------------------------------------------------------------
    /// a history of the four most recent samples
    int16_t samples[4] = {0, 0, 0, 0};
    // -----------------------------------------------------------------------
    // Byte 9,10
    // -----------------------------------------------------------------------
    /// 12-bit fractional position in the Gaussian table
    int16_t fraction = 0x3FFF;
    // -----------------------------------------------------------------------
    // Byte 11,12
    // -----------------------------------------------------------------------
    /// the 14-bit frequency value
    uint16_t rate = 0;
    // -----------------------------------------------------------------------
    // Byte 13
    // -----------------------------------------------------------------------
    /// the volume level after the Gaussian filter
    int8_t volume = 0;
    // -----------------------------------------------------------------------
    // Byte 14
    // -----------------------------------------------------------------------
    /// the discrete filter mode (i.e., the set of coefficients to use)
    uint8_t filter = 0;
    // -----------------------------------------------------------------------
    // Byte 15,16
    // -----------------------------------------------------------------------
    /// a dummy byte for byte alignment to 16-bytes
    const uint16_t unused_spacer_for_byte_alignment;

 public:
    /// the sample rate of the S-DSP in Hz
    static constexpr unsigned SAMPLE_RATE = 32000;
    /// the number of valid filter modes
    static constexpr uint8_t FILTER_MODES = 4;

    /// @brief Return the filter label for the given index.
    ///
    /// @param index the index of the filter mode to get the label of
    /// @returns a string label describing the given filter mode
    ///
    inline static std::string getFilterLabel(uint8_t index) {
        static const std::string LABELS[FILTER_MODES] = {
            "Barely Audible",
            "Quiet",
            "Weird",
            "Loud"
        };
        return LABELS[index];
    }

    /// @brief Initialize a new GaussianInterpolationFilter.
    GaussianInterpolationFilter() : unused_spacer_for_byte_alignment(0) { }

    /// @brief Set the filter coefficients to a discrete mode.
    ///
    /// @param value the new mode for the filter
    ///
    inline void setFilter(uint8_t value) { filter = value & 0x3; }

    /// @brief Set the volume level of the filter to a new value.
    ///
    /// @param value the volume level after the Gaussian low-pass filter
    ///
    inline void setVolume(int8_t value) { volume = value; }

    /// @brief Set the frequency of the filter to a new value.
    ///
    /// @param frequency the frequency to set the filter to in Hz
    ///
    inline void setFrequency(float frequency) { rate = get_pitch(frequency); }

    /// @brief Run the filter for the given input sample.
    ///
    /// @param input the 8-bit BRR sample to pass through the filter
    /// @returns the 14-bit output from the filter system for given input
    ///
    int16_t run(int8_t input) {
        // -------------------------------------------------------------------
        // MARK: Filter
        // -------------------------------------------------------------------
        // VoiceState& voice = voice_state;
        // cast the input to 32-bit to do maths
        int delta = input;
        // One, two and three point IIR filters
        int smp1 = samples[0];
        int smp2 = samples[1];
        switch (filter) {
        case 0:  // !filter1 !filter2
            break;
        case 1:  // !filter1 filter2
            delta += smp1 >> 1;
            delta += (-smp1) >> 5;
            break;
        case 2:  // filter1 !filter2
            delta += smp1;
            delta -= smp2 >> 1;
            delta += (-smp1 - (smp1 >> 1)) >> 5;
            delta += smp2 >> 5;
            break;
        case 3:  // filter1 filter2
            delta += smp1;
            delta -= smp2 >> 1;
            delta += (-smp1 * 13) >> 7;
            delta += (smp2 + (smp2 >> 1)) >> 4;
            break;
        }
        // update sample history
        samples[3] = samples[2];
        samples[2] = smp2;
        samples[1] = smp1;
        samples[0] = 2 * clamp_16(delta);
        // -------------------------------------------------------------------
        // MARK: Interpolation
        // -------------------------------------------------------------------
        // Gaussian interpolation using most recent 4 samples. update the
        // fractional increment based on the 14-bit frequency rate of the voice
        const int index = fraction >> 2 & 0x3FC;
        fraction = (fraction & 0x0FFF) + rate;
        // lookup the interpolation values in the table based on the index and
        // inverted index value
        const auto table1 = get_gaussian(index);
        const auto table2 = get_gaussian(255 * 4 - index);
        // apply the Gaussian interpolation to the incoming sample
        int sample = ((table1[0] * samples[3]) >> 12) +
                     ((table1[1] * samples[2]) >> 12) +
                     ((table2[1] * samples[1]) >> 12);
        sample = static_cast<int16_t>(2 * sample);
        sample +=    ((table2[0] * samples[0]) >> 11) & ~1;
        // apply the volume/amplitude level
        sample = (sample * volume) >> 7;
        // return the sample clipped to 16-bit PCM
        return clamp_16(sample);
    }
};

}  // namespace SonyS_DSP

#endif  // DSP_SONY_S_DSP_GAUSSIAN_INTERPOLATION_FILTER_HPP_
