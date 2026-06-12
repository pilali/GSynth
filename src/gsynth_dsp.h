/*
 * GSynth — host-agnostic DSP core.
 *
 * This header exposes the synth engine independently of any plug-in format.
 * The LV2 wrapper (gsynth.c) and, later, JUCE/VST/AU wrappers all share this
 * exact same core: instantiate with the sample rate, push control values via
 * GSynthParams, and process audio block by block.
 *
 * No realtime allocation happens in gsynth_dsp_process(); all buffers are
 * allocated in gsynth_dsp_new() and released in gsynth_dsp_free().
 *
 * The DSP signal chain (FET input saturation, envelope follower, trigger,
 * Schmitt/flip-flop or YIN-tracked voices, swept resonant VCF) is documented
 * in gsynth_dsp.c.
 */
#ifndef GSYNTH_DSP_H
#define GSYNTH_DSP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Control values, one block's worth. Mirrors the plug-in's control ports;
   raw values are accepted and clamped internally, so a host may pass them
   through unprocessed. */
typedef struct {
    float guitar;        /* dry guitar mix              [0..1]            */
    float octave;        /* -1 octave (div/2) mix       [0..1]            */
    float sub_octave;    /* -2 octave (div/4) mix       [0..1]            */
    float square;        /* square (pitch) voice mix    [0..1]            */
    float attack_delay;  /* sweep attack delay          [ms, 0..2000]     */
    float start_freq;    /* VCF sweep start cutoff       [Hz, 20..12000]   */
    float stop_freq;     /* VCF sweep stop cutoff        [Hz, 20..12000]   */
    float resonance;     /* VCF resonance               [0..1]            */
    float filter_rate;   /* sweep speed                 [0..1]            */
    float trigger;       /* trigger sensitivity         [0..1]            */
    float filter_type;   /* 0 = SVF (TPT), >0.5 = Moog ladder             */
    float pitch_track;   /* 0 = flip-flop dividers, >0.5 = YIN tracker
                            (ignored when built with -DGSYNTH_NO_YIN)      */
    float input_drive;   /* input FET saturation amount [0..1]            */
} GSynthParams;

/* Opaque DSP instance. */
typedef struct GSynthDsp GSynthDsp;

/* Allocate and initialise an instance for the given sample rate.
   Returns NULL on allocation failure. */
GSynthDsp* gsynth_dsp_new(double sample_rate);

/* Release an instance (NULL-safe). */
void gsynth_dsp_free(GSynthDsp* dsp);

/* Clear all runtime state (filters, envelopes, phases, pitch history).
   Equivalent to the LV2 activate() step; call before (re)starting playback. */
void gsynth_dsp_reset(GSynthDsp* dsp);

/* Process n frames. `in` and `out` are mono buffers of length n; in-place
   processing (in == out) is allowed. `p` holds the control values for the
   block and must not be NULL. */
void gsynth_dsp_process(GSynthDsp* dsp, const GSynthParams* p,
                        const float* in, float* out, uint32_t n);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* GSYNTH_DSP_H */
