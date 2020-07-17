// Band-limited sound synthesis buffer (BLIPBuffer 0.4.1).
// Copyright 2020 Christian Kauten
// Copyright 2003-2006 Shay Green
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

#ifndef BLIP_BUFFER_HPP_
#define BLIP_BUFFER_HPP_

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <cmath>
#include <limits>

/// A 32-bit signed value
typedef int32_t blip_long;

/// A 32-bit unsigned value
typedef uint32_t blip_ulong;

/// A time unit at source clock rate
typedef blip_long blip_time_t;

/// An output sample type for 16-bit signed samples[-32768, 32767]
typedef int16_t blip_sample_t;

/// TODO:
typedef blip_ulong blip_resampled_time_t;

/// The number of bits in re-sampled ratio fraction. Higher values give a more
/// accurate ratio but reduce maximum buffer size.
static constexpr uint8_t BLIP_BUFFER_ACCURACY = 16;

/// Number bits in phase offset. Fewer than 6 bits (64 phase offsets) results
/// in noticeable broadband noise when synthesizing high frequency square
/// waves. Affects size of BLIPSynth objects since they store the waveform
/// directly.
#if BLIP_BUFFER_FAST
static constexpr uint8_t BLIP_PHASE_BITS = 8;
#else
static constexpr uint8_t BLIP_PHASE_BITS = 6;
#endif  // BLIP_BUFFER_FAST

/// TODO:
static constexpr int blip_widest_impulse_ = 16;

/// TODO:
static constexpr int blip_buffer_extra_ = blip_widest_impulse_ + 2;

/// TODO:
static constexpr int blip_res = 1 << BLIP_PHASE_BITS;

/// TODO:
static constexpr uint32_t blip_max_length = 0;

/// TODO:
static constexpr uint32_t blip_default_length = 250;

/// TODO:
static constexpr uint8_t blip_sample_bits = 30;

/// Constant value to use instead of BLIP_READER_BASS(), for slightly more
/// optimal code at the cost of having no bass control
static constexpr uint32_t blip_reader_default_bass = 9;

/// A Band-limited sound synthesis buffer.
class BLIPBuffer {
 public:
    /// The result from setting the sample rate to a new value
    enum class SampleRateStatus {
        Success = 0,  // setting the sample rate succeeded
        OutOfMemory   // ran out of resources for buffer
    };

    /// @brief Set the output sample rate and buffer length in milliseconds.
    ///
    /// @param samples_per_sec the number of samples per second
    /// @param buffer_length length of the buffer in milliseconds (1/1000 sec).
    /// defaults to 250, i.e., 1/4 sec.
    /// @returns NULL on success, otherwise if there isn't enough memory,
    /// returns error without affecting current buffer setup.
    ///
    SampleRateStatus set_sample_rate(
        uint32_t samples_per_sec,
        uint32_t buffer_length = 1000 / 4
    );

    /// @brief Return the current output sample rate.
    ///
    /// @returns the audio sample rate
    ///
    inline uint32_t get_sample_rate() const { return sample_rate_; }

    /// @brief Return the number of source time units per second.
    ///
    /// @returns the number of source time units per second
    ///
    inline uint32_t get_clock_rate() const { return clock_rate_; }

    /// @brief End current time frame of specified duration and make its
    /// samples available (along with any still-unread samples). Begins a new
    /// time frame at the end of the current frame.
    ///
    inline void end_frame(blip_time_t) {
        offset_ = 1 << BLIP_BUFFER_ACCURACY;
        // time outside buffer length
        assert(samples_count() <= (long) buffer_size_);
    }

    /// @brief Return the number of samples available for reading.
    ///
    /// @returns the number of samples available for reading from the buffer
    ///
    inline long samples_count() const {
        return (long) (offset_ >> BLIP_BUFFER_ACCURACY);
    }

    /// @brief Read out of this buffer into `dest` and remove them from the buffer.
    ///
    /// @param dest the destination to push samples from the buffer into
    /// @param stereo if true increments `dest` one extra time after writing
    /// each sample to allow easy interleaving of two channels into a stereo
    /// output buffer.
    /// @returns the number of samples actually read and removed
    ///
    long read_samples(blip_sample_t* dest, bool stereo = false);

    /// @brief Remove samples from those waiting to be read.
    ///
    /// @param count the number of samples to remove from the buffer
    ///
    inline void remove_samples(long count) {
        if (count) {
            remove_silence(count);
            // copy remaining samples to beginning and clear old samples
            long remain = samples_count() + blip_buffer_extra_;
            memmove(buffer_, buffer_ + count, remain * sizeof *buffer_);
            memset(buffer_ + remain, 0, count * sizeof *buffer_);
        }
    }

    /// @brief Remove all available samples and clear buffer to silence.
    ///
    /// @param entire_buffer is false, clears out any samples waiting rather
    /// than the entire buffer.
    ///
    inline void clear(bool entire_buffer = true) {
        offset_      = 0;
        reader_accum_ = 0;
        if (buffer_) {
            long count = (entire_buffer ? buffer_size_ : samples_count());
            memset(buffer_, 0, (count + blip_buffer_extra_) * sizeof (buf_t_));
        }
    }

    /// @brief Set frequency high-pass filter frequency, where higher values
    /// reduce the bass more.
    ///
    /// @param frequency TODO:
    ///
    inline void bass_freq(int frequency) {
        bass_freq_ = frequency;
        int shift = 31;
        if (frequency > 0) {
            shift = 13;
            long f = (frequency << 16) / sample_rate_;
            while ((f >>= 1) && --shift) { }
        }
        bass_shift_ = shift;
    }

// ---------------------------------------------------------------------------
// TODO: not documented yet
// ---------------------------------------------------------------------------

    typedef blip_ulong blip_resampled_time_t;

    inline void remove_silence(long count) {
        // tried to remove more samples than available
        assert(count <= samples_count());
        offset_ -= (blip_resampled_time_t) count << BLIP_BUFFER_ACCURACY;
    }

    inline blip_resampled_time_t resampled_duration(int time) const {
        return time * factor_;
    }

    inline blip_resampled_time_t resampled_time(blip_time_t time) const {
        return time * factor_ + offset_;
    }

    inline blip_resampled_time_t clock_rate_factor(uint32_t clock_rate) const {
        double ratio = (double) sample_rate_ / clock_rate;
        blip_long factor = (blip_long) floor(ratio * (1L << BLIP_BUFFER_ACCURACY) + 0.5);
        // fails if clock/output ratio is too large
        assert(factor > 0 || !sample_rate_);
        return (blip_resampled_time_t) factor;
    }

 public:
    /// Initialize a new BLIP Buffer.
    BLIPBuffer() :
        factor_(1),
        offset_(0),
        buffer_(0),
        buffer_size_(0),
        reader_accum_(0),
        bass_shift_(0),
        sample_rate_(0),
        bass_freq_(16) { }

    /// Destroy an existing BLIP Buffer.
    ~BLIPBuffer() { free(buffer_); }

 private:
    /// Disable the copy constructor.
    BLIPBuffer(const BLIPBuffer&);

    /// Disable the assignment operator
    BLIPBuffer& operator=(const BLIPBuffer&);

 public:
    typedef blip_time_t buf_t_;
    blip_ulong factor_;
    blip_resampled_time_t offset_;
    buf_t_* buffer_;
    blip_long buffer_size_;
    blip_long reader_accum_;
    int bass_shift_;

 private:
    uint32_t sample_rate_;
    uint32_t clock_rate_;
    int bass_freq_;
};

class blip_eq_t;

/// A faster implementation of BLIP synthesizer logic.
class BLIPSynth_Fast_ {
 public:
    /// TODO:
    BLIPBuffer* buf = 0;
    /// TODO:
    int last_amp = 0;
    /// TODO:
    int delta_factor = 0;

    /// Initialize a new fast BLIP synthesizer
    BLIPSynth_Fast_() { }

    /// set the volume unit to a new value.
    inline void volume_unit(double new_unit) {
        delta_factor = int (new_unit * (1L << blip_sample_bits) + 0.5);
    }

    /// Set the treble EQ to a new value (ignore).
    inline void treble_eq(blip_eq_t const&) { }
};

/// A more accurate implementation of BLIP synthesizer logic.
class BLIPSynth_ {
 public:
    BLIPBuffer* buf;
    int last_amp;
    int delta_factor;

    void volume_unit(double);
    BLIPSynth_(blip_sample_t* impulses, int width);
    void treble_eq(blip_eq_t const&);

 private:
    double volume_unit_;
    blip_sample_t* const impulses;
    int const width;
    blip_long kernel_unit;
    inline int impulses_size() const { return blip_res / 2 * width + 1; }
    void adjust_impulse();
};

// Quality level. Start with blip_good_quality.
const int blip_med_quality  = 8;
const int blip_good_quality = 12;
const int blip_high_quality = 16;

/// Range specifies the greatest expected change in amplitude. Calculate it
/// by finding the difference between the maximum and minimum expected
/// amplitudes (max - min).
template<int quality, int range>
class BLIPSynth {
 public:
    /// Set overall volume of waveform.
    ///
    /// @param volume TODO:
    ///
    inline void volume(double volume) {
        impl.volume_unit(volume * (1.0 / (range < 0 ? -range : range)));
    }

    /// Configure low-pass filter (see blip_buffer.txt).
    ///
    /// @param eq TODO:
    ///
    inline void treble_eq(blip_eq_t const& eq) { impl.treble_eq(eq); }

    /// Get the BLIPBuffer used for output.
    ///
    /// @returns the BLIPBuffer that this synthesizer is outputting to
    ///
    inline BLIPBuffer* output() const { return impl.buf; }

    /// Set the BLIPBuffer used for output.
    ///
    /// @param buffer the BLIPBuffer that this synthesizer is outputting to
    ///
    inline void output(BLIPBuffer* buffer) {
        impl.buf = buffer;
        impl.last_amp = 0;
    }

    /// Update amplitude of waveform at given time. Using this requires a
    /// separate BLIPSynth for each waveform.
    ///
    /// @param time TODO:
    /// @param amplitude TODO:
    ///
    inline void update(blip_time_t time, int amplitude) {
        int delta = amplitude - impl.last_amp;
        impl.last_amp = amplitude;
        offset_resampled(time * impl.buf->factor_ + impl.buf->offset_, delta, impl.buf);
    }

// ---------------------------------------------------------------------------
// MARK: Low-level interface
// TODO: document
// ---------------------------------------------------------------------------

    /// Add an amplitude transition of specified delta, optionally into
    /// specified buffer rather than the one set with output(). Delta can be
    /// positive or negative. The actual change in amplitude is
    /// delta * (volume / range)
    inline void offset(blip_time_t time, int delta, BLIPBuffer* buf) const {
        offset_resampled(time * buf->factor_ + buf->offset_, delta, buf);
    }

    inline void offset(blip_time_t time, int delta) const {
        offset(time, delta, impl.buf);
    }

    /// Works directly in terms of fractional output samples. Contact Shay Green
    /// for more info.
    void offset_resampled(blip_resampled_time_t time, int delta, BLIPBuffer* buf) const;

 private:
#if BLIP_BUFFER_FAST
    BLIPSynth_Fast_ impl;
#else
    BLIPSynth_ impl;
    blip_sample_t impulses[blip_res * (quality / 2) + 1];
 public:
    BLIPSynth() : impl(impulses, quality) { }
#endif
};

/// Low-pass equalization parameters
class blip_eq_t {
 public:
    /// Logarithmic rolloff to treble dB at half sampling rate. Negative values
    /// reduce treble, small positive values (0 to 5.0) increase treble.
    blip_eq_t(double treble_db) :
        treble(treble_db),
        rolloff_freq(0),
        sample_rate(44100),
        cutoff_freq(0) { }

    // See blip_buffer.txt
    blip_eq_t(
        double treble,
        long rolloff_freq,
        long sample_rate,
        long cutoff_freq
    ) :
        treble(treble),
        rolloff_freq(rolloff_freq),
        sample_rate(sample_rate),
        cutoff_freq(cutoff_freq) { }

 private:
    double treble;
    long rolloff_freq;
    long sample_rate;
    long cutoff_freq;
    void generate(float* out, int count) const;
    friend class BLIPSynth_;
};

#if defined (__GNUC__) || _MSC_VER >= 1100
    #define BLIP_RESTRICT __restrict
#else
    #define BLIP_RESTRICT
#endif

// Begin reading from buffer. Name should be unique to the current block.
#define BLIP_READER_BEGIN(name, blip_buffer) \
    const BLIPBuffer::buf_t_* BLIP_RESTRICT name##_reader_buf = (blip_buffer).buffer_;\
    blip_long name##_reader_accum = (blip_buffer).reader_accum_

// Get value to pass to BLIP_READER_NEXT()
#define BLIP_READER_BASS(blip_buffer) ((blip_buffer).bass_shift_)

// Current sample
#define BLIP_READER_READ(name)        (name##_reader_accum >> (blip_sample_bits - 16))

// Current raw sample in full internal resolution
#define BLIP_READER_READ_RAW(name)    (name##_reader_accum)

// Advance to next sample
#define BLIP_READER_NEXT(name, bass) \
    (void) (name##_reader_accum += *name##_reader_buf++ - (name##_reader_accum >> (bass)))

// End reading samples from buffer. The number of samples read must now be
// removed using BLIPBuffer::remove_samples().
#define BLIP_READER_END(name, blip_buffer) \
    (void) ((blip_buffer).reader_accum_ = name##_reader_accum)

// ---------------------------------------------------------------------------
// MARK: End of public interface
// ---------------------------------------------------------------------------

template<int quality, int range>
inline void BLIPSynth<quality, range>::offset_resampled(
    blip_resampled_time_t time,
    int delta,
    BLIPBuffer* blip_buf
) const {
    // Fails if time is beyond end of BLIPBuffer, due to a bug in caller code
    // or the need for a longer buffer as set by set_sample_rate().
    assert((blip_long) (time >> BLIP_BUFFER_ACCURACY) < blip_buf->buffer_size_);
    delta *= impl.delta_factor;
    blip_long* BLIP_RESTRICT buf = blip_buf->buffer_ + (time >> BLIP_BUFFER_ACCURACY);
    int phase = (int) (time >> (BLIP_BUFFER_ACCURACY - BLIP_PHASE_BITS) & (blip_res - 1));

#if BLIP_BUFFER_FAST
    blip_long left = buf[0] + delta;

    // Kind of crappy, but doing shift after multiply results in overflow.
    // Alternate way of delaying multiply by delta_factor results in worse
    // sub-sample resolution.
    blip_long right = (delta >> BLIP_PHASE_BITS) * phase;
    left  -= right;
    right += buf[1];

    buf[0] = left;
    buf[1] = right;
#else  // BLIP_BUFFER_FAST (falsem)

    int const fwd = (blip_widest_impulse_ - quality) / 2;
    int const rev = fwd + quality - 2;
    int const mid = quality / 2 - 1;

    blip_sample_t const* BLIP_RESTRICT imp = impulses + blip_res - phase;

    #if defined (_M_IX86)    || \
        defined (_M_IA64)    || \
        defined (__i486__)   || \
        defined (__x86_64__) || \
        defined (__ia64__)   || \
        defined (__i386__)  // CISC

    // straight forward implementation resulted in better code on GCC for x86

    #define ADD_IMP(out, in) \
        buf[out] += (blip_long) imp[blip_res * (in)] * delta

    #define BLIP_FWD(i) {\
        ADD_IMP(fwd     + i, i    );\
        ADD_IMP(fwd + 1 + i, i + 1);\
    }
    #define BLIP_REV(r) {\
        ADD_IMP(rev     - r, r + 1);\
        ADD_IMP(rev + 1 - r, r    );\
    }

        BLIP_FWD(0)
        if (quality > 8 ) BLIP_FWD(2)
        if (quality > 12) BLIP_FWD(4) {
            ADD_IMP(fwd + mid - 1, mid - 1);
            ADD_IMP(fwd + mid    , mid    );
            imp = impulses + phase;
        }
        if (quality > 12) BLIP_REV(6)
        if (quality > 8 ) BLIP_REV(4)
        BLIP_REV(2)

        ADD_IMP(rev    , 1);
        ADD_IMP(rev + 1, 0);

    #else  // CISC (false)

    // for RISC processors, help compiler by reading ahead of writes

    #define BLIP_FWD(i) {\
        blip_long t0 =                       i0 * delta + buf[fwd     + i];\
        blip_long t1 = imp[blip_res * (i + 1)] * delta + buf[fwd + 1 + i];\
        i0 =           imp[blip_res * (i + 2)];\
        buf[fwd     + i] = t0;\
        buf[fwd + 1 + i] = t1;\
    }
    #define BLIP_REV(r) {\
        blip_long t0 =                 i0 * delta + buf[rev     - r];\
        blip_long t1 = imp[blip_res * r] * delta + buf[rev + 1 - r];\
        i0 =           imp[blip_res * (r - 1)];\
        buf[rev     - r] = t0;\
        buf[rev + 1 - r] = t1;\
    }

        blip_long i0 = *imp;
        BLIP_FWD(0)
        if (quality > 8 ) BLIP_FWD(2)
        if (quality > 12) BLIP_FWD(4) {
            blip_long t0 =                   i0 * delta + buf[fwd + mid - 1];
            blip_long t1 = imp[blip_res * mid] * delta + buf[fwd + mid    ];
            imp = impulses + phase;
            i0 = imp[blip_res * mid];
            buf[fwd + mid - 1] = t0;
            buf[fwd + mid    ] = t1;
        }
        if (quality > 12) BLIP_REV(6)
        if (quality > 8 ) BLIP_REV(4)
        BLIP_REV(2)

        blip_long t0 =   i0 * delta + buf[rev    ];
        blip_long t1 = *imp * delta + buf[rev + 1];
        buf[rev    ] = t0;
        buf[rev + 1] = t1;
    #endif  // CISC
#endif  // BLIP_BUFFER_FAST
}

#undef BLIP_FWD
#undef BLIP_REV

#endif  // BLIP_BUFFER_HPP_
