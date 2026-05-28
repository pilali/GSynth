/*
 * GSynth — LV2 plug-in guitar synth (subharmonic generator + filter sweep)
 *
 * Architecture (réplique fonctionnelle d'une chaîne classique de synth
 * guitare analogique : étage d'entrée à JFET, voix générées par flip-flops
 * diviseurs ou suivi de hauteur, sweep d'enveloppe vers un VCF résonant) :
 *
 *   - DC-blocker
 *   - Saturation FET d'entrée (asymétrique, JFET-style)
 *   - Envelope follower (peak detector A/R)
 *   - Trigger detector à hystérésis
 *   - Squarer = Schmitt trigger -> voie SQUARE WAVE (à la hauteur)
 *   - Flip-flops T diviseurs /2 et /4 -> OCTAVE (-1) et SUB OCTAVE (-2)
 *     (utilisés si pitch_track = 0)
 *   - YIN-style pitch tracker + oscillateurs carrés synchronisés
 *     (si pitch_track = 1)
 *   - Sweep generator : Attack Delay puis rampe vers Stop
 *   - VCF passe-bas, deux topologies :
 *       * SVF TPT (Zavalishin) — propre, neutre
 *       * Moog ladder (Stilson/Smith) — 24 dB/oct, saturation tanh
 *   - Mixeur final + soft-clipper de sortie
 */

#include <lv2/core/lv2.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define GSYNTH_URI "https://github.com/pilali/gsynth/plugin"

#define YIN_THRESHOLD 0.12f
#define YIN_F_MIN_HZ   60.0f
#define YIN_F_MAX_HZ 2000.0f
#define YIN_HOP_MS      5.0f

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
    /* ports */
    const float* in;
    float*       out;
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

    double sr;

    /* DC blocker */
    float dc_x1, dc_y1;

    /* envelope follower */
    float env;
    float env_atk_coef, env_rel_coef;
    int   triggered;

    /* Schmitt squarer + flip-flops T */
    float sq_state, div2_state, div4_state, prev_sq, prev_div2;

    /* sweep envelope */
    float sweep;
    int   delay_samples;
    int   sweeping;

    /* SVF (TPT) */
    float svf_ic1eq, svf_ic2eq;

    /* Moog ladder */
    float ladder_y[4];

    /* YIN pitch tracker */
    float*   yin_buf;
    uint32_t yin_buf_size;
    uint32_t yin_buf_mask;
    uint32_t yin_pos;
    uint32_t yin_hop_counter;
    uint32_t yin_hop;
    uint32_t yin_tau_min;
    uint32_t yin_tau_max;
    float*   yin_d;
    float*   yin_work;
    float    yin_f0;
    float    yin_f0_smoothed;
    int      yin_voiced;

    /* phase accumulators (synchros sur F0 quand pitch_track activé) */
    float phase_pitch;
    float phase_sub;
    float phase_sub2;
} GSynth;

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */

static inline float clampf(float x, float lo, float hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

/* tanh rationnel rapide, précis ~1% sur [-3,3], saturé hors zone */
static inline float fast_tanh(float x)
{
    if (x >  4.0f) return  1.0f;
    if (x < -4.0f) return -1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/* Saturation type JFET common-source self-biased.
 *
 *   - gain de pré-saturation 1..4x suivant `drive`
 *   - bias additif avant tanh → asymétrie : l'arche positive compresse
 *     plus tôt (côté Idss), l'arche négative est légèrement raidie
 *     (côté cutoff)
 *   - normalisation pour conserver un gain petit-signal ≈ 1, de sorte que
 *     `drive` ne fait qu'augmenter la quantité de coloration, pas le niveau
 *
 *   drive = 0  → bypass strict
 *   drive = 1  → overdrive net, harmoniques paires marquées
 */
static inline float fet_saturate(float x, float drive)
{
    if (drive <= 1e-4f) return x;

    const float pre  = 1.0f + drive * 3.0f;
    const float bias = 0.20f * drive;

    float pre_x = pre * x;
    if (pre_x < 0.0f) pre_x *= 1.10f;        /* asymétrie côté cutoff */

    float y = fast_tanh(pre_x + bias) - fast_tanh(bias);

    float th_b = fast_tanh(bias);
    float k    = 1.0f / (pre * (1.0f - th_b * th_b));
    return y * k;
}

static uint32_t next_pow2(uint32_t v)
{
    uint32_t p = 1;
    while (p < v) p <<= 1;
    return p;
}

/* -------------------------------------------------------------------------- */
/* YIN pitch tracker                                                          */
/* -------------------------------------------------------------------------- */

static void yin_process(GSynth* self, float x)
{
    self->yin_buf[self->yin_pos] = x;
    self->yin_pos = (self->yin_pos + 1) & self->yin_buf_mask;

    if (++self->yin_hop_counter < self->yin_hop) return;
    self->yin_hop_counter = 0;

    const uint32_t W       = self->yin_tau_max;
    const uint32_t tau_min = self->yin_tau_min;
    const uint32_t tau_max = self->yin_tau_max;
    const uint32_t span    = W + tau_max;

    uint32_t start = (self->yin_pos + self->yin_buf_size - span) & self->yin_buf_mask;
    for (uint32_t k = 0; k < span; ++k) {
        self->yin_work[k] = self->yin_buf[(start + k) & self->yin_buf_mask];
    }

    for (uint32_t tau = tau_min; tau <= tau_max; ++tau) {
        float sum = 0.0f;
        const float* a = self->yin_work;
        const float* b = self->yin_work + tau;
        for (uint32_t j = 0; j < W; ++j) {
            float diff = a[j] - b[j];
            sum += diff * diff;
        }
        self->yin_d[tau] = sum;
    }

    self->yin_d[0] = 1.0f;
    float running = 0.0f;
    for (uint32_t tau = 1; tau <= tau_max; ++tau) {
        running += self->yin_d[tau];
        if (running > 1e-12f)
            self->yin_d[tau] *= (float)tau / running;
        else
            self->yin_d[tau] = 1.0f;
    }

    uint32_t best = 0;
    for (uint32_t tau = tau_min; tau < tau_max; ++tau) {
        if (self->yin_d[tau] < YIN_THRESHOLD) {
            while (tau + 1 < tau_max && self->yin_d[tau + 1] < self->yin_d[tau])
                ++tau;
            best = tau;
            break;
        }
    }

    if (best == 0 || best <= tau_min || best >= tau_max) {
        self->yin_voiced = 0;
        return;
    }

    float y0 = self->yin_d[best - 1];
    float y1 = self->yin_d[best];
    float y2 = self->yin_d[best + 1];
    float denom = 2.0f * (y0 - 2.0f * y1 + y2);
    float refined = (float)best;
    if (fabsf(denom) > 1e-9f)
        refined += (y0 - y2) / denom;
    if (refined < (float)tau_min) refined = (float)tau_min;

    float new_f0 = (float)self->sr / refined;

    if (self->yin_voiced) {
        float ratio = new_f0 / self->yin_f0;
        if (ratio > 1.9f && ratio < 2.1f) new_f0 *= 0.5f;
        else if (ratio > 0.45f && ratio < 0.55f) new_f0 *= 2.0f;
    }

    self->yin_f0    = new_f0;
    self->yin_voiced = 1;
}

/* -------------------------------------------------------------------------- */
/* Moog ladder (Stilson/Smith)                                                */
/* -------------------------------------------------------------------------- */

static inline float moog_ladder_process(GSynth* self, float x,
                                        float cutoff_hz, float resonance)
{
    float fc_norm = 2.0f * cutoff_hz / (float)self->sr;
    if (fc_norm > 0.96f) fc_norm = 0.96f;

    float g  = 1.0f - expf(-2.0f * (float)M_PI * cutoff_hz / (float)self->sr);

    float fb = resonance * 4.0f * (1.0f - 0.15f * fc_norm * fc_norm);

    float input = x - fast_tanh(fb * self->ladder_y[3]);

    self->ladder_y[0] += g * (fast_tanh(input)              - fast_tanh(self->ladder_y[0]));
    self->ladder_y[1] += g * (fast_tanh(self->ladder_y[0]) - fast_tanh(self->ladder_y[1]));
    self->ladder_y[2] += g * (fast_tanh(self->ladder_y[1]) - fast_tanh(self->ladder_y[2]));
    self->ladder_y[3] += g * (fast_tanh(self->ladder_y[2]) - fast_tanh(self->ladder_y[3]));

    return self->ladder_y[3];
}

/* -------------------------------------------------------------------------- */
/* SVF (TPT Zavalishin), passe-bas                                            */
/* -------------------------------------------------------------------------- */

static inline float svf_lp_process(GSynth* self, float x,
                                   float cutoff_hz, float Q)
{
    float g  = tanf((float)M_PI * cutoff_hz / (float)self->sr);
    float k  = 1.0f / Q;
    float a1 = 1.0f / (1.0f + g * (g + k));
    float a2 = g * a1;
    float a3 = g * a2;

    float v3 = x - self->svf_ic2eq;
    float v1 = a1 * self->svf_ic1eq + a2 * v3;
    float v2 = self->svf_ic2eq + a2 * self->svf_ic1eq + a3 * v3;
    self->svf_ic1eq = 2.0f * v1 - self->svf_ic1eq;
    self->svf_ic2eq = 2.0f * v2 - self->svf_ic2eq;
    return v2;
}

/* -------------------------------------------------------------------------- */
/* LV2 lifecycle                                                              */
/* -------------------------------------------------------------------------- */

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor,
            double               rate,
            const char*          bundle_path,
            const LV2_Feature* const* features)
{
    (void)descriptor; (void)bundle_path; (void)features;

    GSynth* self = (GSynth*)calloc(1, sizeof(GSynth));
    if (!self) return NULL;

    self->sr = rate;
    self->env_atk_coef = expf(-1.0f / (0.003f * (float)rate));
    self->env_rel_coef = expf(-1.0f / (0.120f * (float)rate));

    self->sq_state   = 1.0f;
    self->div2_state = 1.0f;
    self->div4_state = 1.0f;
    self->prev_sq    = 1.0f;
    self->prev_div2  = 1.0f;
    self->sweep      = 1.0f;

    self->yin_tau_min = (uint32_t)(rate / YIN_F_MAX_HZ);
    self->yin_tau_max = (uint32_t)(rate / YIN_F_MIN_HZ);
    if (self->yin_tau_min < 2) self->yin_tau_min = 2;
    self->yin_hop     = (uint32_t)(YIN_HOP_MS * 0.001f * rate);
    if (self->yin_hop < 64) self->yin_hop = 64;

    self->yin_buf_size = next_pow2(2u * self->yin_tau_max + 16u);
    self->yin_buf_mask = self->yin_buf_size - 1u;

    self->yin_buf  = (float*)calloc(self->yin_buf_size, sizeof(float));
    self->yin_d    = (float*)calloc(self->yin_tau_max + 2u, sizeof(float));
    self->yin_work = (float*)calloc(2u * self->yin_tau_max, sizeof(float));
    if (!self->yin_buf || !self->yin_d || !self->yin_work) {
        free(self->yin_buf); free(self->yin_d); free(self->yin_work);
        free(self);
        return NULL;
    }

    self->yin_f0          = 110.0f;
    self->yin_f0_smoothed = 110.0f;

    return (LV2_Handle)self;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void* data)
{
    GSynth* self = (GSynth*)instance;
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
    GSynth* self = (GSynth*)instance;
    self->dc_x1 = self->dc_y1 = 0.0f;
    self->env   = 0.0f;
    self->triggered = 0;
    self->sweep = 1.0f;
    self->delay_samples = 0;
    self->sweeping = 0;
    self->svf_ic1eq = self->svf_ic2eq = 0.0f;
    self->ladder_y[0] = self->ladder_y[1] = self->ladder_y[2] = self->ladder_y[3] = 0.0f;
    self->phase_pitch = self->phase_sub = self->phase_sub2 = 0.0f;
    self->yin_pos = 0;
    self->yin_hop_counter = 0;
    self->yin_voiced = 0;
    if (self->yin_buf) memset(self->yin_buf, 0, self->yin_buf_size * sizeof(float));
}

/* -------------------------------------------------------------------------- */
/* run                                                                        */
/* -------------------------------------------------------------------------- */

static void
run(LV2_Handle instance, uint32_t n_samples)
{
    GSynth* self = (GSynth*)instance;

    const float* const in  = self->in;
    float*       const out = self->out;

    const float v_guitar = clampf(*self->p_guitar,     0.0f, 1.0f);
    const float v_oct    = clampf(*self->p_octave,     0.0f, 1.0f);
    const float v_sub    = clampf(*self->p_sub_octave, 0.0f, 1.0f);
    const float v_sq     = clampf(*self->p_square,     0.0f, 1.0f);

    const float attack_ms  = clampf(*self->p_attack_delay, 0.0f, 2000.0f);
    const float start_hz   = clampf(*self->p_start_freq, 20.0f, 12000.0f);
    const float stop_hz    = clampf(*self->p_stop_freq,  20.0f, 12000.0f);
    const float res        = clampf(*self->p_resonance,  0.0f, 1.0f);
    const float rate_param = clampf(*self->p_filter_rate, 0.0f, 1.0f);
    const float trig_sens  = clampf(*self->p_trigger,    0.0f, 1.0f);

    const int   filter_type = (*self->p_filter_type > 0.5f) ? 1 : 0;
    const int   pitch_track = (*self->p_pitch_track > 0.5f) ? 1 : 0;
    const float input_drive = clampf(*self->p_input_drive, 0.0f, 1.0f);

    const float sr = (float)self->sr;
    const float dt = 1.0f / sr;

    const float sweep_time = 0.030f + (1.0f - rate_param) * 2.97f;
    const float sweep_inc  = dt / sweep_time;

    const int delay_init = (int)(attack_ms * 0.001f * sr);

    /* Quadratic taper: wide, usable range across the full slider travel.
       At sens=1.0: ~0.1 mFS; at sens=0.5: ~25 mFS; at sens=0.0: ~100 mFS. */
    float _k = (1.0f - trig_sens);
    const float trig_on  = 1e-4f + _k * _k * 0.10f;
    const float trig_off = trig_on * 0.40f;

    const float log_start = logf(start_hz);
    const float log_stop  = logf(stop_hz);

    const float Q = 0.6f + res * 11.4f;

    const float nyquist_clip = sr * 0.45f;

    for (uint32_t i = 0; i < n_samples; ++i) {
        float x = in[i];

        /* DC blocker */
        float y_dc = x - self->dc_x1 + 0.995f * self->dc_y1;
        self->dc_x1 = x;
        self->dc_y1 = y_dc;
        x = y_dc;

        /* saturation FET d'entrée (tout l'aval voit le signal coloré) */
        x = fet_saturate(x, input_drive);

        /* envelope follower */
        float ax = fabsf(x);
        if (ax > self->env)
            self->env = self->env_atk_coef * self->env + (1.0f - self->env_atk_coef) * ax;
        else
            self->env = self->env_rel_coef * self->env + (1.0f - self->env_rel_coef) * ax;

        /* trigger à hystérésis */
        if (!self->triggered && self->env > trig_on) {
            self->triggered     = 1;
            self->delay_samples = delay_init;
            self->sweep         = 0.0f;
            self->sweeping      = 0;
        } else if (self->triggered && self->env < trig_off) {
            self->triggered = 0;
        }

        /* voies synthétiques */
        float sq, d2, d4;

        if (pitch_track) {
            yin_process(self, x);

            if (self->yin_voiced) {
                self->yin_f0_smoothed += 0.05f * (self->yin_f0 - self->yin_f0_smoothed);
            }

            float f0 = self->yin_f0_smoothed;
            if (f0 < 30.0f) f0 = 30.0f;

            self->phase_pitch += f0 * dt;
            if (self->phase_pitch >= 1.0f) self->phase_pitch -= 1.0f;

            self->phase_sub += 0.5f * f0 * dt;
            if (self->phase_sub >= 1.0f) self->phase_sub -= 1.0f;

            self->phase_sub2 += 0.25f * f0 * dt;
            if (self->phase_sub2 >= 1.0f) self->phase_sub2 -= 1.0f;

            sq = (self->phase_pitch < 0.5f) ?  1.0f : -1.0f;
            d2 = (self->phase_sub   < 0.5f) ?  1.0f : -1.0f;
            d4 = (self->phase_sub2  < 0.5f) ?  1.0f : -1.0f;
        } else {
            float schmitt_th = 0.10f * self->env + 1e-4f;
            if (self->sq_state > 0.0f) {
                if (x < -schmitt_th) self->sq_state = -1.0f;
            } else {
                if (x >  schmitt_th) self->sq_state =  1.0f;
            }
            sq = self->sq_state;

            if (self->prev_sq < 0.0f && sq > 0.0f)
                self->div2_state = -self->div2_state;
            self->prev_sq = sq;
            d2 = self->div2_state;

            if (self->prev_div2 < 0.0f && d2 > 0.0f)
                self->div4_state = -self->div4_state;
            self->prev_div2 = d2;
            d4 = self->div4_state;
        }

        float gate = clampf(self->env * 12.0f, 0.0f, 1.0f);
        sq *= gate;
        d2 *= gate;
        d4 *= gate;

        if (self->triggered) {
            if (self->delay_samples > 0) self->delay_samples--;
            else self->sweeping = 1;
        }
        if (self->sweeping) {
            self->sweep += sweep_inc;
            if (self->sweep >= 1.0f) {
                self->sweep = 1.0f;
                self->sweeping = 0;
            }
        }

        float cutoff = expf(log_start + self->sweep * (log_stop - log_start));
        if (cutoff > nyquist_clip) cutoff = nyquist_clip;
        if (cutoff < 20.0f) cutoff = 20.0f;

        /* Synth voices go through the filter; guitar is added dry after,
           so it stays audible regardless of filter sweep position. */
        float synth_mix = (v_sq * sq + v_oct * d2 + v_sub * d4) * 0.5f;

        float filtered;
        if (filter_type == 1) {
            filtered = moog_ladder_process(self, synth_mix, cutoff, res);
        } else {
            filtered = svf_lp_process(self, synth_mix, cutoff, Q);
        }

        float o = filtered + v_guitar * x * 0.5f;
        if (o >  1.5f) o =  1.5f;
        if (o < -1.5f) o = -1.5f;
        o = o - (o * o * o) * (1.0f / 3.0f);

        out[i] = o;
    }
}

static void deactivate(LV2_Handle instance) { (void)instance; }

static void cleanup(LV2_Handle instance)
{
    GSynth* self = (GSynth*)instance;
    if (self) {
        free(self->yin_buf);
        free(self->yin_d);
        free(self->yin_work);
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
