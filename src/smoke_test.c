/* Quick smoke test: dlopen plugin, feed 0.5s of 220Hz sine, check finiteness. */
#include <dlfcn.h>
#include <lv2/core/lv2.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv)
{
    const char* path = argc > 1 ? argv[1] : "eh_micro_synth.lv2/eh_micro_synth.so";
    void* h = dlopen(path, RTLD_NOW);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }

    typedef const LV2_Descriptor* (*desc_fn)(uint32_t);
    desc_fn ld = (desc_fn)dlsym(h, "lv2_descriptor");
    if (!ld) { fprintf(stderr, "no lv2_descriptor\n"); return 1; }

    const LV2_Descriptor* d = ld(0);
    const double sr = 48000.0;
    LV2_Handle inst = d->instantiate(d, sr, "./", NULL);

    const uint32_t N = (uint32_t)(sr * 0.5);
    float* in  = calloc(N, sizeof(float));
    float* out = calloc(N, sizeof(float));

    /* 220 Hz sine */
    for (uint32_t i = 0; i < N; ++i)
        in[i] = 0.3f * sinf(2.0f * (float)M_PI * 220.0f * (float)i / (float)sr);

    float guitar = 0.7f, oct = 0.6f, sub = 0.6f, sq = 0.4f;
    float atk_ms = 5.0f, start_hz = 200.0f, stop_hz = 3000.0f;
    float res = 0.4f, rate = 0.5f, trig = 0.6f;
    float filter_type, pitch_track;

    d->connect_port(inst,  0, in);
    d->connect_port(inst,  1, out);
    d->connect_port(inst,  2, &guitar);
    d->connect_port(inst,  3, &oct);
    d->connect_port(inst,  4, &sub);
    d->connect_port(inst,  5, &sq);
    d->connect_port(inst,  6, &atk_ms);
    d->connect_port(inst,  7, &start_hz);
    d->connect_port(inst,  8, &stop_hz);
    d->connect_port(inst,  9, &res);
    d->connect_port(inst, 10, &rate);
    d->connect_port(inst, 11, &trig);
    d->connect_port(inst, 12, &filter_type);
    d->connect_port(inst, 13, &pitch_track);

    /* Exerce les 4 combinaisons (SVF/Moog) x (Schmitt/YIN) */
    int all_finite = 1;
    for (int mode = 0; mode < 4; ++mode) {
        filter_type = (float)((mode >> 0) & 1);
        pitch_track = (float)((mode >> 1) & 1);

        d->activate(inst);
        d->run(inst, N);
        d->deactivate(inst);

        double peak = 0, rms = 0; int finite = 1;
        for (uint32_t i = 0; i < N; ++i) {
            if (!isfinite(out[i])) { finite = 0; break; }
            float a = fabsf(out[i]); if (a > peak) peak = a;
            rms += out[i] * out[i];
        }
        rms = sqrt(rms / N);
        printf("mode filter=%d pitch=%d  finite=%d  peak=%.4f  rms=%.4f\n",
               (int)filter_type, (int)pitch_track, finite, peak, rms);
        if (!finite) all_finite = 0;
    }

    d->cleanup(inst);
    free(in); free(out); dlclose(h);
    return all_finite ? 0 : 2;
}
