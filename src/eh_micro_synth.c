/*
 * GSynth EH Micro Synthesizer - LV2 plug-in
 *
 * Modélisation digitale de la pédale Electro-Harmonix Micro Synthesizer.
 *
 * Architecture (réplique fonctionnelle, pas component-level) :
 *   - DC-blocker en entrée
 *   - Envelope follower (peak detector A/R)
 *   - Trigger detector à hystérésis sur l'enveloppe
 *   - Squarer = Schmitt trigger -> voie SQUARE WAVE (à la hauteur)
 *   - Flip-flop diviseur /2 (toggle T sur front montant) -> OCTAVE (-1 octave)
 *   - Flip-flop diviseur /4 cascade                       -> SUB OCTAVE (-2 oct)
 *   - Voie GUITAR : signal direct
 *   - Sweep generator : déclenché par TRIGGER, Attack Delay puis rampe linéaire
 *   - VCF passe-bas SVF (Zavalishin TPT) à résonance variable balayé entre
 *     START et STOP (interpolation exponentielle en Hz)
 *   - Mélangeur final
 */

#include <lv2/core/lv2.h>

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define EHMS_URI "https://github.com/pilali/gsynth/eh-micro-synth"

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
    PORT_TRIGGER      = 11
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

    double sr;

    /* DC blocker */
    float dc_x1, dc_y1;

    /* enveloppe (peak follower) */
    float env;
    float env_atk_coef;
    float env_rel_coef;

    /* trigger */
    int   triggered;

    /* squarer (Schmitt) + dividers (T flip-flops) */
    float sq_state;        /* +1 / -1 */
    float div2_state;
    float div4_state;
    float prev_sq;
    float prev_div2;

    /* sweep envelope */
    float sweep;              /* 0..1 */
    int   delay_samples;      /* compte à rebours avant rampe */
    int   sweeping;           /* 1 pendant la rampe */

    /* SVF (TPT, Zavalishin) */
    float svf_ic1eq;
    float svf_ic2eq;
} EHMS;

/* -------------------------------------------------------------------------- */

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor,
            double               rate,
            const char*          bundle_path,
            const LV2_Feature* const* features)
{
    (void)descriptor; (void)bundle_path; (void)features;

    EHMS* self = (EHMS*)calloc(1, sizeof(EHMS));
    if (!self) return NULL;

    self->sr = rate;
    /* peak follower : attack 3 ms, release 120 ms */
    self->env_atk_coef = expf(-1.0f / (0.003f * (float)rate));
    self->env_rel_coef = expf(-1.0f / (0.120f * (float)rate));

    self->sq_state   = 1.0f;
    self->div2_state = 1.0f;
    self->div4_state = 1.0f;
    self->prev_sq    = 1.0f;
    self->prev_div2  = 1.0f;

    return (LV2_Handle)self;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void* data)
{
    EHMS* self = (EHMS*)instance;
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
    }
}

static void
activate(LV2_Handle instance)
{
    EHMS* self = (EHMS*)instance;
    self->dc_x1 = self->dc_y1 = 0.0f;
    self->env = 0.0f;
    self->triggered = 0;
    self->sweep = 1.0f;        /* repose sur STOP avant le premier trigger */
    self->delay_samples = 0;
    self->sweeping = 0;
    self->svf_ic1eq = self->svf_ic2eq = 0.0f;
}

static inline float clampf(float x, float lo, float hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

static void
run(LV2_Handle instance, uint32_t n_samples)
{
    EHMS* self = (EHMS*)instance;

    const float* const in  = self->in;
    float*       const out = self->out;

    const float v_guitar = clampf(*self->p_guitar,     0.0f, 1.0f);
    const float v_oct    = clampf(*self->p_octave,     0.0f, 1.0f);
    const float v_sub    = clampf(*self->p_sub_octave, 0.0f, 1.0f);
    const float v_sq     = clampf(*self->p_square,     0.0f, 1.0f);

    const float attack_ms   = clampf(*self->p_attack_delay, 0.0f, 2000.0f);
    const float start_hz    = clampf(*self->p_start_freq, 20.0f, 12000.0f);
    const float stop_hz     = clampf(*self->p_stop_freq,  20.0f, 12000.0f);
    const float res         = clampf(*self->p_resonance,  0.0f, 1.0f);
    const float rate_param  = clampf(*self->p_filter_rate, 0.0f, 1.0f);
    const float trig_sens   = clampf(*self->p_trigger,    0.0f, 1.0f);

    const float sr  = (float)self->sr;
    const float dt  = 1.0f / sr;

    /* durée de balayage : rate=1 -> 30 ms (rapide), rate=0 -> 3 s (lent) */
    const float sweep_time = 0.030f + (1.0f - rate_param) * 2.97f;
    const float sweep_inc  = dt / sweep_time;

    const int delay_init   = (int)(attack_ms * 0.001f * sr);

    /* seuil de déclenchement : trig=1 -> très sensible, trig=0 -> peu sensible */
    const float trig_on    = 0.005f + (1.0f - trig_sens) * 0.30f;
    const float trig_off   = trig_on * 0.35f;

    const float log_start  = logf(start_hz);
    const float log_stop   = logf(stop_hz);

    /* Q : 0 -> 0.6 (à peine résonant), 1 -> 12 (auto-oscillation proche) */
    const float Q = 0.6f + res * 11.4f;
    const float k = 1.0f / Q;

    const float nyquist_clip = sr * 0.45f;

    for (uint32_t i = 0; i < n_samples; ++i) {
        float x = in[i];

        /* DC blocker (pôle proche de DC) */
        float y_dc = x - self->dc_x1 + 0.995f * self->dc_y1;
        self->dc_x1 = x;
        self->dc_y1 = y_dc;
        x = y_dc;

        /* envelope follower (peak) */
        float ax = fabsf(x);
        if (ax > self->env)
            self->env = self->env_atk_coef * self->env + (1.0f - self->env_atk_coef) * ax;
        else
            self->env = self->env_rel_coef * self->env + (1.0f - self->env_rel_coef) * ax;

        /* trigger à hystérésis : détecte une nouvelle attaque */
        if (!self->triggered && self->env > trig_on) {
            self->triggered     = 1;
            self->delay_samples = delay_init;
            self->sweep         = 0.0f;
            self->sweeping      = 0;
        } else if (self->triggered && self->env < trig_off) {
            self->triggered = 0;
        }

        /* Schmitt trigger -> carré à la hauteur de la corde */
        float schmitt_th = 0.10f * self->env + 1e-4f;
        if (self->sq_state > 0.0f) {
            if (x < -schmitt_th) self->sq_state = -1.0f;
        } else {
            if (x >  schmitt_th) self->sq_state =  1.0f;
        }
        float sq = self->sq_state;

        /* /2 : T flip-flop sur front montant du carré */
        if (self->prev_sq < 0.0f && sq > 0.0f)
            self->div2_state = -self->div2_state;
        self->prev_sq = sq;
        float d2 = self->div2_state;

        /* /4 : T flip-flop sur front montant du /2 */
        if (self->prev_div2 < 0.0f && d2 > 0.0f)
            self->div4_state = -self->div4_state;
        self->prev_div2 = d2;
        float d4 = self->div4_state;

        /* gate par enveloppe pour faire taire les voies synthétiques au repos */
        float gate = clampf(self->env * 12.0f, 0.0f, 1.0f);
        sq *= gate;
        d2 *= gate;
        d4 *= gate;

        /* sweep generator */
        if (self->triggered) {
            if (self->delay_samples > 0) {
                self->delay_samples--;
            } else {
                self->sweeping = 1;
            }
        }
        if (self->sweeping) {
            self->sweep += sweep_inc;
            if (self->sweep >= 1.0f) {
                self->sweep = 1.0f;
                self->sweeping = 0;
            }
        }

        /* fréquence de coupure : interpolation log entre Start et Stop */
        float cutoff = expf(log_start + self->sweep * (log_stop - log_start));
        if (cutoff > nyquist_clip) cutoff = nyquist_clip;
        if (cutoff < 20.0f) cutoff = 20.0f;

        /* coefficients SVF TPT (mis à jour chaque sample : peu coûteux) */
        float g  = tanf((float)M_PI * cutoff / sr);
        float a1 = 1.0f / (1.0f + g * (g + k));
        float a2 = g * a1;
        float a3 = g * a2;

        /* mélange des voix avant le filtre */
        float mix = v_guitar * x
                  + v_sq     * sq
                  + v_oct    * d2
                  + v_sub    * d4;
        mix *= 0.5f; /* headroom */

        /* SVF passe-bas */
        float v3 = mix - self->svf_ic2eq;
        float v1 = a1 * self->svf_ic1eq + a2 * v3;
        float v2 = self->svf_ic2eq + a2 * self->svf_ic1eq + a3 * v3;
        self->svf_ic1eq = 2.0f * v1 - self->svf_ic1eq;
        self->svf_ic2eq = 2.0f * v2 - self->svf_ic2eq;
        float lp = v2;

        /* soft clip de sécurité (tanh approx) en sortie */
        float o = lp;
        if (o >  1.5f) o =  1.5f;
        if (o < -1.5f) o = -1.5f;
        o = o - (o * o * o) * (1.0f / 3.0f);

        out[i] = o;
    }
}

static void deactivate(LV2_Handle instance) { (void)instance; }
static void cleanup(LV2_Handle instance)    { free(instance); }
static const void* extension_data(const char* uri) { (void)uri; return NULL; }

static const LV2_Descriptor descriptor = {
    EHMS_URI,
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
