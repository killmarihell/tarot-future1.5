// Renderiza as cartas em WAV no PC para ouvir/analisar sem o 3DS.
//   gcc -O2 -Isource tools/synth_test.c source/synth.c -lm -o synth_test
//   ./synth_test <carta 0..77> <segundos> saida.wav      (ou "all" no lugar da carta: testa tudo, sem gravar)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "synth.h"

static void wav_header(FILE *f, int frames) {
    int data = frames * 4, rate = SYNTH_RATE;
    fwrite("RIFF", 1, 4, f); int v = 36 + data; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    short s = 1; fwrite(&s, 2, 1, f); s = 2; fwrite(&s, 2, 1, f);
    fwrite(&rate, 4, 1, f); v = rate * 4; fwrite(&v, 4, 1, f);
    s = 4; fwrite(&s, 2, 1, f); s = 16; fwrite(&s, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
}

static int run(int card, float secs, int16_t *buf, int frames_total, double *peak, double *rms, double *cpu) {
    synth_init();
    synth_request(card);
    clock_t t0 = clock();
    int done = 0;
    while (done < frames_total) {
        int n = frames_total - done < 1024 ? frames_total - done : 1024;
        synth_render(buf + done * 2, n);
        done += n;
    }
    *cpu = (double)(clock() - t0) / CLOCKS_PER_SEC / secs;   // fração de tempo real (no PC)
    double pk = 0, sq = 0; int bad = 0;
    for (int i = 0; i < frames_total * 2; i++) {
        double x = buf[i] / 32768.0;
        if (fabs(x) > pk) pk = fabs(x);
        sq += x * x;
    }
    *peak = pk; *rms = sqrt(sq / (frames_total * 2));
    for (int i = 0; i < SYNTH_SCOPE_N; i++) if (!isfinite(synth_scope(i))) bad = 1;
    return bad;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "uso: %s <carta|all> <segundos> [out.wav]\n", argv[0]); return 1; }
    float secs = (float)atof(argv[2]);
    int frames = (int)(secs * SYNTH_RATE);
    int16_t *buf = malloc((size_t)frames * 4);
    if (!strcmp(argv[1], "all")) {
        for (int c = 0; c < 78; c++) {
            double pk, rms, cpu;
            int bad = run(c, secs, buf, frames, &pk, &rms, &cpu);
            printf("carta %2d  pico %.2f  rms %.3f  cpu(PC) %.2f%%%s\n", c, pk, rms, cpu * 100, bad ? "  NaN!" : "");
        }
    } else {
        double pk, rms, cpu;
        run(atoi(argv[1]), secs, buf, frames, &pk, &rms, &cpu);
        printf("pico %.2f rms %.3f cpu(PC) %.2f%%\n", pk, rms, cpu * 100);
        if (argc > 3) { FILE *f = fopen(argv[3], "wb"); wav_header(f, frames); fwrite(buf, 4, frames, f); fclose(f); }
    }
    free(buf);
    return 0;
}
