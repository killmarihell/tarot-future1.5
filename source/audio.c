#include "audio.h"
#include <3ds.h>
#include <string.h>
#include "synth.h"

#define NBUF       4
#define BUFFRAMES  1024          // ~31 ms por buffer; ~125 ms de latência total
#define STACKSZ    (32 * 1024)

static ndspWaveBuf wbuf[NBUF];
static int16_t    *abuf = NULL;
static LightEvent  ev;
static volatile bool quit = false;
static Thread      thr = NULL;
static bool        ready = false;

static void ndsp_cb(void *d) {
    (void)d;
    if (quit) return;
    LightEvent_Signal(&ev);
}

static void audio_thread(void *arg) {
    (void)arg;
    int cur = 0;
    while (!quit) {
        while (wbuf[cur].status == NDSP_WBUF_DONE) {
            synth_render(wbuf[cur].data_pcm16, BUFFRAMES);
            DSP_FlushDataCache(wbuf[cur].data_pcm16, BUFFRAMES * 2 * sizeof(int16_t));
            ndspChnWaveBufAdd(0, &wbuf[cur]);
            cur = (cur + 1) % NBUF;
        }
        LightEvent_Wait(&ev);
    }
}

bool audio_init(void) {
    synth_init();
    if (R_FAILED(ndspInit())) return false;

    abuf = (int16_t *)linearAlloc(NBUF * BUFFRAMES * 2 * sizeof(int16_t));
    if (!abuf) { ndspExit(); return false; }
    memset(abuf, 0, NBUF * BUFFRAMES * 2 * sizeof(int16_t));

    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetRate(0, (float)SYNTH_RATE);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    float mix[12]; memset(mix, 0, sizeof mix); mix[0] = 1.0f; mix[1] = 1.0f;
    ndspChnSetMix(0, mix);

    memset(wbuf, 0, sizeof wbuf);
    for (int i = 0; i < NBUF; i++) {
        wbuf[i].data_vaddr = &abuf[i * BUFFRAMES * 2];
        wbuf[i].nsamples = BUFFRAMES;
        wbuf[i].status = NDSP_WBUF_DONE;
    }

    LightEvent_Init(&ev, RESET_ONESHOT);
    ndspSetCallback(ndsp_cb, NULL);
    quit = false;

    s32 prio = 0;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    thr = threadCreate(audio_thread, NULL, STACKSZ, prio - 1, -1, false);
    if (!thr) { ndspExit(); linearFree(abuf); abuf = NULL; return false; }
    ready = true;
    return true;
}

void audio_exit(void) {
    if (!ready) return;
    quit = true;
    LightEvent_Signal(&ev);
    threadJoin(thr, UINT64_MAX);
    threadFree(thr);
    ndspChnReset(0);
    ndspExit();
    linearFree(abuf);
    abuf = NULL; ready = false;
}
