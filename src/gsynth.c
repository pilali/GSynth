/*
 * GSynth — LV2 wrapper.
 *
 * Thin plug-in-format glue around the host-agnostic engine in gsynth_dsp.c:
 * this file only maps LV2 ports onto GSynthParams and forwards the lifecycle
 * (instantiate/activate/run/cleanup) to the shared core. The DSP itself lives
 * in gsynth_dsp.{c,h} and is reused as-is by the JUCE/VST/AU wrappers.
 */

#include "gsynth_dsp.h"

#include <lv2/core/lv2.h>

#include <stdlib.h>

#define GSYNTH_URI "https://github.com/pilali/gsynth/plugin"

typedef enum {
    PORT_IN           = 0,
    PORT_OUT          = 1,
    PORT_GUITAR       = 2,
    PORT_OCTAVE       = 3,
    PORT_SUB_OCTAVE   = 4,
    PORT_SQUARE       = 5,
    PORT_ATTACK_DELAY = 6,
    PORT_START_FREQ   = 7,
    PORT_STOP_FREQ    = 8,
    PORT_RESONANCE    = 9,
    PORT_FILTER_RATE  = 10,
    PORT_TRIGGER      = 11,
    PORT_FILTER_TYPE  = 12,
    PORT_PITCH_TRACK  = 13,
    PORT_INPUT_DRIVE  = 14
} PortIndex;

typedef struct {
    GSynthDsp* dsp;

    /* audio ports */
    const float* in;
    float*       out;

    /* control ports */
    const float* p_guitar;
    const float* p_octave;
    const float* p_sub_octave;
    const float* p_square;
    const float* p_attack_delay;
    const float* p_start_freq;
    const float* p_stop_freq;
    const float* p_resonance;
    const float* p_filter_rate;
    const float* p_trigger;
    const float* p_filter_type;
    const float* p_pitch_track;
    const float* p_input_drive;
} GSynthLV2;

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor,
            double               rate,
            const char*          bundle_path,
            const LV2_Feature* const* features)
{
    (void)descriptor; (void)bundle_path; (void)features;

    GSynthLV2* self = (GSynthLV2*)calloc(1, sizeof(GSynthLV2));
    if (!self) return NULL;

    self->dsp = gsynth_dsp_new(rate);
    if (!self->dsp) {
        free(self);
        return NULL;
    }
    return (LV2_Handle)self;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void* data)
{
    GSynthLV2* self = (GSynthLV2*)instance;
    switch ((PortIndex)port) {
        case PORT_IN:           self->in             = (const float*)data; break;
        case PORT_OUT:          self->out            = (float*)data;       break;
        case PORT_GUITAR:       self->p_guitar       = (const float*)data; break;
        case PORT_OCTAVE:       self->p_octave       = (const float*)data; break;
        case PORT_SUB_OCTAVE:   self->p_sub_octave   = (const float*)data; break;
        case PORT_SQUARE:       self->p_square       = (const float*)data; break;
        case PORT_ATTACK_DELAY: self->p_attack_delay = (const float*)data; break;
        case PORT_START_FREQ:   self->p_start_freq   = (const float*)data; break;
        case PORT_STOP_FREQ:    self->p_stop_freq    = (const float*)data; break;
        case PORT_RESONANCE:    self->p_resonance    = (const float*)data; break;
        case PORT_FILTER_RATE:  self->p_filter_rate  = (const float*)data; break;
        case PORT_TRIGGER:      self->p_trigger      = (const float*)data; break;
        case PORT_FILTER_TYPE:  self->p_filter_type  = (const float*)data; break;
        case PORT_PITCH_TRACK:  self->p_pitch_track  = (const float*)data; break;
        case PORT_INPUT_DRIVE:  self->p_input_drive  = (const float*)data; break;
    }
}

static void
activate(LV2_Handle instance)
{
    GSynthLV2* self = (GSynthLV2*)instance;
    gsynth_dsp_reset(self->dsp);
}

static void
run(LV2_Handle instance, uint32_t n_samples)
{
    GSynthLV2* self = (GSynthLV2*)instance;

    const GSynthParams params = {
        .guitar       = *self->p_guitar,
        .octave       = *self->p_octave,
        .sub_octave   = *self->p_sub_octave,
        .square       = *self->p_square,
        .attack_delay = *self->p_attack_delay,
        .start_freq   = *self->p_start_freq,
        .stop_freq    = *self->p_stop_freq,
        .resonance    = *self->p_resonance,
        .filter_rate  = *self->p_filter_rate,
        .trigger      = *self->p_trigger,
        .filter_type  = *self->p_filter_type,
        .pitch_track  = *self->p_pitch_track,
        .input_drive  = *self->p_input_drive,
    };

    gsynth_dsp_process(self->dsp, &params, self->in, self->out, n_samples);
}

static void deactivate(LV2_Handle instance) { (void)instance; }

static void cleanup(LV2_Handle instance)
{
    GSynthLV2* self = (GSynthLV2*)instance;
    if (self) {
        gsynth_dsp_free(self->dsp);
        free(self);
    }
}

static const void* extension_data(const char* uri) { (void)uri; return NULL; }

static const LV2_Descriptor descriptor = {
    GSYNTH_URI,
    instantiate,
    connect_port,
    activate,
    run,
    deactivate,
    cleanup,
    extension_data
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor*
lv2_descriptor(uint32_t index)
{
    return index == 0 ? &descriptor : NULL;
}
