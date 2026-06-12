/*
 * level_test — measures GSynth voice output levels against a test signal.
 *
 * Feeds a steady sine (guitar-ish fundamental) into the DSP core, solos each
 * voice (guitar / octave / sub / square) in both pitch-tracking modes, and
 * reports steady-state peak and RMS in dBFS, plus the dry-input reference.
 *
 * Build: cc -O2 -Wall level_test.c <repo>/src/gsynth_dsp.c -lm -o level_test
 */
#include "../src/gsynth_dsp.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define SR        48000.0
#define BLOCK     256
#define WARM_SEC  1.5      /* let env/trigger/YIN settle */
#define MEAS_SEC  2.0

static double measure(GSynthDsp* dsp, const GSynthParams* p,
                      double freq, float amp,
                      double* out_peak, double* out_rms)
{
    static float in[BLOCK], out[BLOCK];
    const uint32_t warm = (uint32_t)(WARM_SEC * SR);
    const uint32_t meas = (uint32_t)(MEAS_SEC * SR);
    double phase = 0.0, inc = freq / SR;
    double peak = 0.0, sum2 = 0.0;
    uint32_t done = 0;

    gsynth_dsp_reset(dsp);

    while (done < warm + meas) {
        for (int i = 0; i < BLOCK; ++i) {
            in[i] = amp * (float)sin(2.0 * M_PI * phase);
            phase += inc; if (phase >= 1.0) phase -= 1.0;
        }
        gsynth_dsp_process(dsp, p, in, out, BLOCK);
        for (int i = 0; i < BLOCK; ++i, ++done) {
            if (done >= warm) {
                double v = fabs(out[i]);
                if (v > peak) peak = v;
                sum2 += (double)out[i] * out[i];
            }
        }
    }
    *out_peak = peak;
    *out_rms  = sqrt(sum2 / meas);
    return peak;
}

static double dB(double x) { return x > 1e-9 ? 20.0 * log10(x) : -999.0; }

int main(void)
{
    GSynthDsp* dsp = gsynth_dsp_new(SR);
    if (!dsp) { fprintf(stderr, "alloc failed\n"); return 1; }

    /* neutral settings: filter wide open, no resonance, no sweep surprise,
       no drive — we want the raw voice levels */
    GSynthParams base;
    memset(&base, 0, sizeof base);
    base.attack_delay = 0.0f;
    base.start_freq   = 12000.0f;
    base.stop_freq    = 12000.0f;
    base.resonance    = 0.0f;
    base.filter_rate  = 0.5f;
    base.trigger      = 0.5f;
    base.filter_type  = 0.0f;     /* SVF (clean) */
    base.input_drive  = 0.0f;

    const struct { const char* name; int voice; } voices[] = {
        { "GTR (dry)", 0 }, { "OCT", 1 }, { "SUB", 2 }, { "SQR", 3 },
    };
    const double freqs[] = { 82.41, 110.0, 196.0 };   /* E2, A2, G3 */
    const float  amps[]  = { 0.05f, 0.20f };          /* quiet / hot pickup */

    for (int ai = 0; ai < 2; ++ai) {
        float amp = amps[ai];
        printf("\n=== input sine peak %.2f (%.1f dBFS, RMS %.1f dBFS) ===\n",
               amp, dB(amp), dB(amp / sqrt(2.0)));
        printf("%-10s %-6s", "voice", "mode");
        for (int fi = 0; fi < 3; ++fi) printf("   %6.1fHz pk/rms  ", freqs[fi]);
        printf("\n");
        for (int vi = 0; vi < 4; ++vi) {
            for (int pt = 0; pt < 2; ++pt) {
                if (vi == 0 && pt == 1) continue;   /* dry: mode irrelevant */
                GSynthParams p = base;
                p.pitch_track = (float)pt;
                p.guitar     = (voices[vi].voice == 0) ? 1.0f : 0.0f;
                p.octave     = (voices[vi].voice == 1) ? 1.0f : 0.0f;
                p.sub_octave = (voices[vi].voice == 2) ? 1.0f : 0.0f;
                p.square     = (voices[vi].voice == 3) ? 1.0f : 0.0f;
                printf("%-10s %-6s", voices[vi].name, vi == 0 ? "-" : (pt ? "YIN" : "FF"));
                for (int fi = 0; fi < 3; ++fi) {
                    double pk, rms;
                    measure(dsp, &p, freqs[fi], amp, &pk, &rms);
                    printf("   %6.1f / %6.1f dB", dB(pk), dB(rms));
                }
                printf("\n");
            }
        }

        /* all synth voices together, plus dry, both modes */
        for (int pt = 0; pt < 2; ++pt) {
            GSynthParams p = base;
            p.pitch_track = (float)pt;
            p.guitar = 0.7f; p.octave = 1.0f; p.sub_octave = 1.0f; p.square = 1.0f;
            printf("%-10s %-6s", "ALL(g.7)", pt ? "YIN" : "FF");
            for (int fi = 0; fi < 3; ++fi) {
                double pk, rms;
                measure(dsp, &p, freqs[fi], amp, &pk, &rms);
                printf("   %6.1f / %6.1f dB", dB(pk), dB(rms));
            }
            printf("\n");
        }
    }

    gsynth_dsp_free(dsp);
    return 0;
}
