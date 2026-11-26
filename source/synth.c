// Motor de síntese de "I trust you with my future!" (ex-Cartomancia Pratesi).
// Tudo é gerado em tempo real; cada carta define os parâmetros.
//
//  Maiores (0..21)  : pads + shimmer + drone/sub + sinos + plucks + vento + reverb longo (ambient)
//  Menores (22..77) : harsh noise em CAMADAS. O naipe escolhe a arquitetura, o valor (Ás..Rei) a intensidade.
//       Paus    = screech FM + highpass
//       Copas   = ruído com bandpass ressonante varrendo
//       Espadas = rajadas gateadas de ruído branco
//       Ouros   = ruído marrom + sub quadrado, grave e esmagado
//     Todos os naipes ganham camadas graves (sub, rumble, growl FM) e crackle.
//
// v1.5: EVOLUÇÃO. Dezenas de "wanderers" (passeios aleatórios suaves) mexem em corte, drive,
// camadas, ritmo, reverb etc. em escalas de 6 a 40 s, e a cada ~20-90 s acontece uma "cena"
// (modulação de tom no ambient; dropout + salto de sub no harsh). Dá pra deixar tocando
// por horas sem repetir.

#include "synth.h"
#include <math.h>
#include <string.h>

#define RATE ((float)SYNTH_RATE)
#define PI_F 3.14159265f

// ---------------------------------------------------------------- utilidades
static uint32_t rng_state = 0x1234ABCDu;
static inline uint32_t rng_u32(void) { rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5; return rng_state; }
static inline float rng_f(void) { return (float)(rng_u32() >> 8) * (1.0f / 16777216.0f); }
static inline float rng_s(void) { return rng_f() * 2.0f - 1.0f; }
static inline float fabs_(float x) { return x < 0 ? -x : x; }
static inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
static inline float wrap1(float p) { return p >= 1.0f ? p - 1.0f : p; }
// floor rápido (sem chamar libm; vale para |x| < 60000) - importante no ARM11 do 3DS
static inline float ffloor(float x) { return (float)((int)(x + 65536.0f)) - 65536.0f; }

// seno aproximado por parábola, fase 0..1  -> ~sin(2*pi*p)
static inline float psin(float p) {
    float t = p * 2.0f - 1.0f;
    float y = 4.0f * t * (1.0f - fabs_(t));
    y = 0.225f * (y * fabs_(y) - y) + y;
    return -y;
}
static inline float midi_hz(float m) { return 440.0f * exp2f((m - 69.0f) / 12.0f); }

// ---------------------------------------------------------------- wanderer (passeio aleatório suave)
// v persegue t; a cada 'per' segundos (sorteado entre pmin e pmax) sorteia um novo t em [lo,hi].
typedef struct { float v, t, lo, hi, pmin, pmax, coef; int cd, n; } Wnd;

static void wnd_init(Wnd *w, int n, float lo, float hi, float pmin, float pmax, float tau, float start) {
    w->n = n; w->lo = lo; w->hi = hi; w->pmin = pmin; w->pmax = pmax;
    w->coef = 1.0f - expf(-(float)n / (RATE * tau));
    w->v = w->t = start;
    w->cd = (int)(RATE * (pmin * 0.5f + (pmax - pmin) * rng_f()));
}
static inline float wnd_step(Wnd *w) {   // chamar uma vez a cada w->n amostras
    w->cd -= w->n;
    if (w->cd <= 0) {
        w->t = w->lo + (w->hi - w->lo) * rng_f();
        w->cd = (int)(RATE * (w->pmin + (w->pmax - w->pmin) * rng_f()));
    }
    w->v += (w->t - w->v) * w->coef;
    return w->v;
}

static inline void svf(float in, float f, float q, float *low, float *band, float *high) {
    *low += f * *band;
    *high = in - *low - q * *band;
    *band += f * *high;
    *low = clampf(*low, -8.0f, 8.0f); *band = clampf(*band, -8.0f, 8.0f);
}

// ---------------------------------------------------------------- parâmetros por carta
typedef struct { float root; int mode; float bright, density, detune, chord, rvb, wob; } AmbP;

static const int8_t SCALES[12][8] = {
    {0,2,4,5,7,9,11,0},   // 0 jônio
    {0,2,3,5,7,9,10,0},   // 1 dórico
    {0,1,3,5,7,8,10,0},   // 2 frígio
    {0,2,4,6,7,9,11,0},   // 3 lídio
    {0,2,4,5,7,9,10,0},   // 4 mixolídio
    {0,2,3,5,7,8,10,0},   // 5 eólio
    {0,1,4,5,7,8,10,0},   // 6 frígio dominante
    {0,3,5,7,10,0,0,0},   // 7 pentatônica menor
    {0,2,4,6,8,10,0,0},   // 8 tons inteiros
    {0,2,3,7,8,0,0,0},    // 9 hirajoshi
    {0,2,4,7,9,0,0,0},    // 10 pentatônica maior
    {0,1,3,4,6,7,9,10},   // 11 octatônica
};
static const uint8_t SCALE_N[12] = {7,7,7,7,7,7,7,5,6,5,5,8};

//                        root mode bright dens detune chord rvb  wob
static const AmbP AMB[22] = {
    {62, 10, 0.70f, 0.90f, 8,  9,  0.60f, 0.30f},  //  0 O Louco
    {50,  3, 0.80f, 1.20f, 6,  8,  0.50f, 0.20f},  //  1 O Mago
    {45,  1, 0.35f, 0.40f, 12, 14, 0.85f, 0.50f},  //  2 A Sacerdotisa
    {53,  0, 0.60f, 0.70f, 10, 10, 0.70f, 0.30f},  //  3 A Imperatriz
    {38,  5, 0.45f, 0.25f, 4,  12, 0.50f, 0.10f},  //  4 O Imperador
    {43,  4, 0.40f, 0.35f, 5,  13, 0.80f, 0.15f},  //  5 O Hierofante
    {55,  3, 0.75f, 0.80f, 14, 9,  0.70f, 0.40f},  //  6 Os Enamorados
    {48,  4, 0.70f, 1.60f, 7,  6,  0.40f, 0.35f},  //  7 O Carro
    {52,  0, 0.55f, 0.60f, 9,  11, 0.60f, 0.25f},  //  8 A Força
    {40,  7, 0.25f, 0.25f, 6,  16, 0.85f, 0.20f},  //  9 O Eremita
    {46,  8, 0.65f, 1.00f, 15, 7,  0.65f, 0.50f},  // 10 A Roda da Fortuna
    {50,  0, 0.50f, 0.50f, 3,  10, 0.45f, 0.05f},  // 11 A Justiça
    {44,  9, 0.35f, 0.30f, 20, 15, 0.90f, 0.60f},  // 12 O Enforcado
    {33,  2, 0.15f, 0.15f, 8,  18, 0.92f, 0.30f},  // 13 A Morte
    {51,  3, 0.55f, 0.70f, 10, 12, 0.80f, 0.30f},  // 14 A Temperança
    {34,  6, 0.30f, 0.45f, 25, 9,  0.55f, 0.70f},  // 15 O Diabo
    {36, 11, 0.50f, 1.40f, 30, 5,  0.70f, 0.90f},  // 16 A Torre
    {57, 10, 0.85f, 0.90f, 8,  12, 0.85f, 0.20f},  // 17 A Estrela
    {49,  1, 0.30f, 0.55f, 28, 13, 0.90f, 0.80f},  // 18 A Lua
    {60,  3, 0.95f, 1.10f, 6,  8,  0.50f, 0.15f},  // 19 O Sol
    {41,  4, 0.60f, 0.50f, 7,  14, 0.90f, 0.30f},  // 20 O Julgamento
    {48, 10, 0.80f, 0.90f, 10, 11, 0.80f, 0.30f},  // 21 O Mundo
};

typedef struct {
    int   filt;            // 0 bandpass, 1 highpass, 2 lowpass
    float color;           // 0 branco .. 1 marrom
    float pre, drive;      // ganho antes/dentro do clip
    float cutoff, q;       // q = 1/Q do filtro
    float sweepRate, sweepDepth;   // LFO do cutoff (Hz, oitavas)
    float gateRate, duty;  // gate rítmico
    float fmFreq, fmRatio, fmDepth, screech;
    float bits; int srdiv;
    float subFreq, subLevel;
    float rumble, growl, growlF, click;   // camadas graves + crackle (v1.5)
    float jitter;          // saltos aleatórios por segundo
    float ui_drive, ui_rate, ui_crush;
} HarshP;

static HarshP harsh_params(int card) {
    int c = card - 22; if (c < 0) c = 0; if (c > 55) c = 55;
    int suit = c / 14, k = c % 14;
    float r = (float)k / 13.0f;
    HarshP p; memset(&p, 0, sizeof p);
    p.duty = 0.5f; p.fmRatio = 1.7f; p.srdiv = 1; p.bits = 16; p.pre = 1; p.q = 0.5f; p.jitter = 1; p.sweepRate = 0.1f;
    switch (suit) {
    case 0: // Paus: screech
        p.filt = 1; p.color = 0; p.cutoff = 700 + 4300 * r; p.q = 0.6f; p.pre = 0.6f; p.drive = 6 + 24 * r;
        p.screech = 0.8f; p.fmFreq = 420 + 2600 * r; p.fmRatio = 1.5f + 2.5f * r; p.fmDepth = 0.4f + 5 * r;
        p.gateRate = r > 0.45f ? 2 + 10 * r : 0; p.duty = 0.65f;
        p.bits = 12 - 6 * r; p.srdiv = 1 + (r > 0.7f); p.jitter = 1 + 9 * r; p.sweepRate = 0.3f; p.sweepDepth = 0.5f + r;
        p.subFreq = 40 + 25 * r; p.subLevel = 0.30f + 0.20f * r; p.rumble = 0.30f;
        p.growl = 0.15f + 0.30f * r; p.growlF = 70 + 60 * r; p.click = 4 + 14 * r;
        p.ui_rate = 0.2f + 0.8f * r; break;
    case 1: // Copas: bandpass ressonante varrendo
        p.filt = 0; p.color = 0.35f; p.cutoff = 140 + 480 * r; p.sweepDepth = 2.2f + 1.8f * r; p.sweepRate = 0.06f + 1.3f * r;
        p.q = 0.06f - 0.045f * r; p.pre = 7; p.drive = 8 + 30 * r; p.bits = 16 - 9 * r; p.jitter = 0.4f + 3 * r;
        if (k >= 10) { p.gateRate = 1.5f + 2 * r; p.duty = 0.8f; }
        p.subFreq = 36 + 20 * r; p.subLevel = 0.45f; p.rumble = 0.45f;
        p.growl = 0.30f; p.growlF = 55 + 40 * r; p.click = 3 + 8 * r;
        p.ui_rate = p.sweepRate / 1.4f; break;
    case 2: // Espadas: rajadas
        p.filt = 1; p.color = 0; p.cutoff = 1000 + 5800 * r; p.q = 0.8f; p.pre = 1; p.drive = 10 + 30 * r;
        p.gateRate = 1.5f + 22 * r; p.duty = 0.5f - 0.3f * r; p.bits = 14 - 9 * r; p.srdiv = 1 + (int)(3 * r);
        p.jitter = 2 + 10 * r; p.sweepDepth = 0.7f; p.sweepRate = 0.2f;
        p.subFreq = 34 + 25 * r; p.subLevel = 0.35f + 0.20f * r; p.rumble = 0.30f;
        p.growl = 0.25f + 0.35f * r; p.growlF = 60 + 80 * r; p.click = 8 + 30 * r;
        p.ui_rate = p.gateRate / 24.0f; break;
    default: // Ouros: grave
        p.filt = 2; p.color = 0.92f; p.cutoff = 90 + 520 * r; p.q = 0.35f - 0.2f * r; p.pre = 2.5f; p.drive = 14 + 30 * r;
        p.gateRate = 0.35f + 3 * r; p.duty = 0.75f; p.subFreq = 30 + 34 * r; p.subLevel = 0.60f + 0.40f * r;
        p.bits = 12 - 6 * r; p.srdiv = 1 + (int)(2 * r); p.jitter = 0.3f + 2 * r; p.sweepDepth = 1; p.sweepRate = 0.05f;
        p.rumble = 0.80f; p.growl = 0.40f + 0.30f * r; p.growlF = 40 + 40 * r; p.click = 1 + 4 * r;
        p.ui_rate = p.gateRate / 3.5f; break;
    }
    if (k >= 10) { p.jitter *= 1.6f; p.drive *= 1.2f; }
    p.ui_drive = clampf(p.drive / 60.0f, 0, 1);
    p.ui_rate  = clampf(p.ui_rate, 0, 1);
    p.ui_crush = clampf((16.0f - p.bits) / 12.0f, 0, 1);
    return p;
}

const char *synth_param_name(int card, int i) {
    static const char *a[3] = {"BRILHO", "DENSIDADE", "REVERB"};
    static const char *h[3] = {"DRIVE", "RITMO", "CRUSH"};
    if (i < 0 || i > 2) return "";
    return card < 22 ? a[i] : h[i];
}
float synth_param_value(int card, int i) {
    if (card < 22) {
        const AmbP *p = &AMB[card < 0 ? 0 : card];
        return i == 0 ? p->bright : i == 1 ? clampf(p->density / 1.6f, 0, 1) : p->rvb;
    }
    HarshP p = harsh_params(card);
    return i == 0 ? p.ui_drive : i == 1 ? p.ui_rate : p.ui_crush;
}

// ---------------------------------------------------------------- reverb
#define CL 2800
static const int COMB_LEN[2][4] = {{1687, 1993, 2371, 2693}, {1709, 2017, 2399, 2719}};
static const int AP_LEN[2][2]   = {{347, 113}, {353, 127}};
static float comb_buf[2][4][CL]; static int comb_pos[2][4]; static float comb_lp[2][4];
static float ap_buf[2][2][360];  static int ap_pos[2][2];

static void reverb_clear(void) {
    memset(comb_buf, 0, sizeof comb_buf); memset(comb_pos, 0, sizeof comb_pos); memset(comb_lp, 0, sizeof comb_lp);
    memset(ap_buf, 0, sizeof ap_buf); memset(ap_pos, 0, sizeof ap_pos);
}
static float reverb_ch(int ch, float in, float fb, float damp) {
    float sum = 0;
    for (int k = 0; k < 4; k++) {
        int pos = comb_pos[ch][k];
        float y = comb_buf[ch][k][pos];
        comb_lp[ch][k] = y * (1.0f - damp) + comb_lp[ch][k] * damp;
        comb_buf[ch][k][pos] = in + comb_lp[ch][k] * fb + 1e-18f;   // 1e-18: evita denormais no VFP
        if (++pos >= COMB_LEN[ch][k]) pos = 0;
        comb_pos[ch][k] = pos;
        sum += y;
    }
    for (int j = 0; j < 2; j++) {
        int pos = ap_pos[ch][j];
        float b = ap_buf[ch][j][pos];
        float y = -sum + b;
        ap_buf[ch][j][pos] = sum + b * 0.5f;
        if (++pos >= AP_LEN[ch][j]) pos = 0;
        ap_pos[ch][j] = pos;
        sum = y;
    }
    return sum * 0.25f;
}

// ---------------------------------------------------------------- ambient
typedef struct { float f, ft, ph1, ph2, lfoph, lfoinc, lp, pan; } Pad;
typedef struct { float ph, ph2, inc, inc2, env, dec, amp, gl, gr, att; } Bell;
#define NBELL 10
#define A_CTL 64

static struct {
    AmbP p, home;
    Pad v[6]; Bell b[NBELL]; int bell_i;
    float dph, dinc, dinc_t, dlfo, dlfoinc, sph, sinc, sinc_t;
    float glfo, glfoinc, lpa, cut0, detA, detB, glide;
    int chord_cd, ctl, bell_cd, pl_cd, scene_cd, away;
    float fb, damp, wet, wet0;
    // evolução
    Wnd mCut, mDens, mShim, mSub, mWind, mRvb, mWob, mPluck;
    float dens, shim, subL, windL, wob, pluck, plStep;
    float wph, wf, wlL, wbL, wlR, wbR;
} A;

static const uint8_t ALT_MODE[12] = {3, 5, 5, 0, 1, 1, 5, 5, 0, 7, 7, 8};   // modo "vizinho" para as cenas
static const int8_t  KEY_SHIFT[6] = {5, 7, -5, 3, -2, 2};

static float deg_semi(const AmbP *p, int idx) {
    int n = SCALE_N[p->mode];
    return (float)((idx / n) * 12 + SCALES[p->mode][idx % n]);
}

static void amb_new_chord(int first) {
    int n = SCALE_N[A.p.mode];
    int base = (int)(rng_u32() % (uint32_t)n);
    for (int v = 0; v < 6; v++) {
        int idx = base + v * 2;
        if (rng_f() < 0.25f) idx += (int)(rng_u32() % 3) - 1;
        if (idx < 0) idx = 0;
        float m = A.p.root + 12.0f + deg_semi(&A.p, idx);
        if (v == 0) m -= 12.0f;
        if (v >= 4) m += 12.0f;          // 4 e 5 = camada shimmer, uma oitava acima
        A.v[v].ft = midi_hz(m);
        if (first) A.v[v].f = A.v[v].ft;
    }
    A.chord_cd = (int)(A.p.chord * RATE * (0.75f + 0.5f * rng_f()));
}

static float sub_hz(float root_midi) {          // oitava abaixo da tônica, mas sem descer demais
    float m = root_midi - 12.0f;
    while (m < 27.0f) m += 12.0f;
    return midi_hz(m);
}

static void amb_init(int card) {
    memset(&A, 0, sizeof A);
    A.p = AMB[card]; A.home = A.p;
    A.cut0 = 250.0f + 3600.0f * A.p.bright;
    A.detA = exp2f(A.p.detune / 1200.0f); A.detB = 1.0f / A.detA;
    A.glide = 1.0f - expf(-1.0f / (RATE * 2.5f));
    A.glfoinc = 1.0f / (RATE * (9.0f + 8.0f * (1.0f - A.p.wob)));
    A.dinc = A.dinc_t = midi_hz(A.p.root) / RATE; A.dlfoinc = 1.0f / (RATE * 13.0f);
    A.sinc = A.sinc_t = sub_hz(A.p.root) / RATE;
    A.fb = 0.70f + 0.26f * A.p.rvb;
    A.damp = 0.15f + 0.55f * (1.0f - A.p.bright);
    A.wet0 = 0.25f + 0.72f * A.p.rvb; A.wet = A.wet0;
    static const float pans[6] = {-0.6f, -0.2f, 0.2f, 0.6f, -0.9f, 0.9f};
    for (int v = 0; v < 6; v++) {
        A.v[v].pan = pans[v];
        A.v[v].lfoph = rng_f();
        A.v[v].lfoinc = 1.0f / (RATE * (7.0f + 16.0f * rng_f()));
    }
    amb_new_chord(1);
    A.bell_cd = (int)(RATE * 0.8f);
    A.ctl = 1;
    A.pl_cd = 1; A.plStep = RATE * (0.38f - 0.22f * clampf(A.p.density / 1.6f, 0, 1));
    // wanderers: começam no neutro (o som inicial é o "retrato" da carta) e passeiam
    wnd_init(&A.mCut,   A_CTL, -0.9f, 1.0f, 10, 26, 5.0f, 0.0f);   // oitavas de corte
    wnd_init(&A.mDens,  A_CTL, 0.30f, 2.2f, 12, 32, 6.0f, 1.0f);   // multiplicador de densidade dos sinos
    wnd_init(&A.mShim,  A_CTL, 0.0f, 1.0f, 14, 40, 8.0f, 0.0f);    // camada de brilho agudo
    wnd_init(&A.mSub,   A_CTL, 0.25f, 1.5f, 12, 32, 6.0f, 0.7f);   // sub drone
    wnd_init(&A.mWind,  A_CTL, 0.0f, 1.0f, 12, 36, 7.0f, 0.0f);    // respiração/vento
    wnd_init(&A.mRvb,   A_CTL, 0.70f, 1.25f, 12, 30, 7.0f, 1.0f);
    wnd_init(&A.mWob,   A_CTL, 0.4f, 1.8f, 10, 28, 6.0f, 1.0f);
    wnd_init(&A.mPluck, A_CTL, 0.0f, 1.0f, 18, 45, 4.0f, 0.0f);    // >0.5 liga as frases de pluck
    A.dens = A.p.density; A.wob = A.p.wob; A.subL = 0.7f;
    A.scene_cd = (int)(RATE * (35.0f + 35.0f * rng_f()));
    A.ctl = 1;
}

static void amb_scene(void) {
    A.away ^= 1;
    if (A.away) {                                  // viaja: outra tônica + modo vizinho
        float r = A.home.root + KEY_SHIFT[rng_u32() % 6];
        A.p.root = r; A.p.mode = ALT_MODE[A.home.mode];
    } else {                                       // volta pra casa
        A.p.root = A.home.root; A.p.mode = A.home.mode;
    }
    A.dinc_t = midi_hz(A.p.root) / RATE;
    A.sinc_t = sub_hz(A.p.root) / RATE;
    amb_new_chord(0);
    A.scene_cd = (int)(RATE * (45.0f + 50.0f * rng_f()));
}

static void bell_trigger_ex(float f, float tau, float amp, float pan, float partial, float att_rate) {
    Bell *b = &A.b[A.bell_i]; A.bell_i = (A.bell_i + 1) % NBELL;
    b->ph = 0; b->ph2 = 0;
    b->inc = f / RATE; b->inc2 = f * partial / RATE;
    if (b->inc2 > 0.45f) b->inc2 = 0.0f;
    if (b->inc > 0.45f) { b->env = 0; return; }
    b->env = 1.0f; b->dec = expf(-1.0f / (RATE * tau));
    b->amp = amp;
    b->gl = 0.5f - 0.5f * pan; b->gr = 0.5f + 0.5f * pan; b->att = 1.0f - att_rate * 50.0f;
    if (b->att < 0.0f) b->att = 0.0f;
}

static void bell_trigger(void) {
    int n = SCALE_N[A.p.mode];
    int idx = (int)(rng_u32() % (uint32_t)(n + 3));
    float m = A.p.root + 24.0f + deg_semi(&A.p, idx);
    bell_trigger_ex(midi_hz(m), 1.2f + 3.5f * rng_f(), 0.10f + 0.10f * rng_f(), rng_s() * 0.8f, 2.756f, 0.02f);
}

static void pluck_trigger(void) {
    int v = (int)(rng_u32() % 4);
    float f = A.v[v].ft * ((rng_u32() & 1) ? 2.0f : 4.0f);
    float amt = clampf((A.pluck - 0.5f) * 2.0f, 0, 1);
    bell_trigger_ex(f, 0.35f + 0.6f * rng_f(), 0.05f + 0.08f * amt, rng_s() * 0.9f, 2.0f, 0.2f);
}

static void amb_ctl(void) {
    float cm = wnd_step(&A.mCut);
    A.dens  = A.p.density * wnd_step(&A.mDens);
    A.shim  = wnd_step(&A.mShim);
    A.subL  = wnd_step(&A.mSub);
    A.windL = wnd_step(&A.mWind);
    A.wet   = A.wet0 * wnd_step(&A.mRvb);
    A.wob   = A.p.wob * wnd_step(&A.mWob);
    A.pluck = wnd_step(&A.mPluck);
    A.glfo = wrap1(A.glfo + (float)A_CTL * A.glfoinc);
    float fc = A.cut0 * exp2f(cm) * (1.0f + A.wob * 0.6f * psin(A.glfo));
    fc = clampf(fc, 80.0f, 9000.0f);
    A.lpa = 1.0f - expf(-6.2831853f * fc / RATE);
    // tônica do drone/sub desliza na troca de cena
    A.dinc += (A.dinc_t - A.dinc) * 0.0015f;
    A.sinc += (A.sinc_t - A.sinc) * 0.0015f;
    // vento: bandpass varrendo devagar
    A.wph = wrap1(A.wph + (float)A_CTL / (RATE * 17.0f));
    A.wf = 2.0f * sinf(PI_F * clampf(500.0f * exp2f(1.6f * psin(A.wph)), 120.0f, 6000.0f) / RATE);
    A.scene_cd -= A_CTL;
    if (A.scene_cd <= 0) amb_scene();
}

static inline void amb_sample(float *oL, float *oR) {
    if (--A.chord_cd <= 0) amb_new_chord(0);
    if (--A.ctl <= 0) { A.ctl = A_CTL; amb_ctl(); }
    float padL = 0, padR = 0;
    for (int i = 0; i < 6; i++) {
        Pad *v = &A.v[i];
        float lv = 1.0f;
        if (i >= 4) { lv = A.shim * 0.55f; if (lv < 0.01f) continue; }
        v->f += (v->ft - v->f) * A.glide;
        float inc = v->f / RATE;
        v->ph1 = wrap1(v->ph1 + inc * A.detA);
        v->ph2 = wrap1(v->ph2 + inc * A.detB);
        float q = v->ph1 * 2.0f; q -= (float)(int)q;
        float s = (psin(v->ph1) + psin(v->ph2)) * 0.5f + A.p.bright * 0.35f * psin(q);
        v->lfoph = wrap1(v->lfoph + v->lfoinc);
        float amp = (0.55f + 0.45f * psin(v->lfoph)) * 0.24f * lv;
        v->lp += (s - v->lp) * A.lpa;
        float o = v->lp * amp;
        padL += o * (0.7f - 0.5f * v->pan); padR += o * (0.7f + 0.5f * v->pan);
    }
    A.dph = wrap1(A.dph + A.dinc);
    A.sph = wrap1(A.sph + A.sinc);
    A.dlfo = wrap1(A.dlfo + A.dlfoinc);
    float d = psin(A.dph) * 0.20f * (0.75f + 0.25f * psin(A.dlfo));
    d += psin(A.sph) * 0.17f * A.subL * (0.8f + 0.2f * psin(A.dlfo));      // sub drone

    if (--A.bell_cd <= 0) {
        bell_trigger();
        A.bell_cd = (int)(-logf(rng_f() + 1e-6f) / A.dens * RATE) + 1;
    }
    if (A.pluck > 0.5f && --A.pl_cd <= 0) {
        pluck_trigger();
        A.pl_cd = (int)(A.plStep * (0.85f + 0.3f * rng_f())) + 1;
    }
    float bL = 0, bR = 0;
    for (int i = 0; i < NBELL; i++) {
        Bell *b = &A.b[i];
        if (b->env <= 0.0f) continue;
        b->ph = wrap1(b->ph + b->inc); b->ph2 = wrap1(b->ph2 + b->inc2);
        float s = (psin(b->ph) + 0.3f * psin(b->ph2)) * b->env * b->amp;
        if (b->att < 1.0f) { b->att += 0.02f; s *= b->att; }
        b->env *= b->dec; if (b->env < 0.0005f) b->env = 0.0f;
        bL += s * b->gl; bR += s * b->gr;
    }
    // vento / respiração
    float wL = 0, wR = 0;
    if (A.windL > 0.02f) {
        float hi;
        svf(rng_s(), A.wf, 0.35f, &A.wlL, &A.wbL, &hi); wL = A.wbL;
        svf(rng_s(), A.wf, 0.35f, &A.wlR, &A.wbR, &hi); wR = A.wbR;
        float wg = A.windL * 0.06f;
        wL *= wg; wR *= wg;
    }
    float send = (padL + padR) * 0.2f + (bL + bR) * 0.45f + d * 0.2f + (wL + wR) * 0.3f;
    float rL = reverb_ch(0, send, A.fb, A.damp);
    float rR = reverb_ch(1, send, A.fb, A.damp);
    float oL_ = ((padL + bL + d * 0.7f + wL) * 0.8f + rL * A.wet * 2.0f) * 0.50f;
    float oR_ = ((padR + bR + d * 0.7f + wR) * 0.8f + rR * A.wet * 2.0f) * 0.50f;
    *oL = oL_ / (1.0f + 0.5f * fabs_(oL_));      // limitador suave: camadas somando não estouram
    *oR = oR_ / (1.0f + 0.5f * fabs_(oR_));
}

// ---------------------------------------------------------------- harsh noise
#define H_CTL 16

static struct {
    HarshP p;
    float bL, bR, lowL, bandL, lowR, bandR;
    float gph, g, sweepph, lfo, cph, mph, subph, sub2ph, grph, grph2;
    float f, q, cutMul, gateMul, gateEff, fmMul, lev; int bitsOn;
    int ctl, jit_cd, hold_cnt;
    float holdL, holdR;
    // camadas
    float rb, rlow, rband, fr;
    float subMul, subMulT, subInc, sub2Inc, grInc, grInc2;
    float drive, subL, rumL, grL, fmL, clickRate, oth;
    float wl, wb, wh, bw_l, bw_b, bw_h;
    int click_cd;
    // cenas / dropout
    int scene_cd, drop_hold; float dg, dgT, burst;
    Wnd mCut, mDrive, mGate, mSweep, mSub, mRum, mGrowl, mFM, mBits, mClick, mLow, mBand, mHigh, mMorph;
} H;

static void harsh_init(int card) {
    memset(&H, 0, sizeof H);
    H.p = harsh_params(card);
    H.cutMul = H.gateMul = H.gateEff = H.fmMul = 1.0f;
    H.q = H.p.q;
    H.lev = exp2f(H.p.bits - 1.0f); H.bitsOn = H.p.bits < 15.5f;
    H.jit_cd = (int)(RATE / H.p.jitter);
    H.f = 0.2f; H.ctl = 1;
    H.subMul = H.subMulT = 1.0f; H.dg = H.dgT = 1.0f;
    H.drive = H.p.drive; H.subL = H.p.subLevel; H.rumL = H.p.rumble; H.grL = H.p.growl; H.fmL = H.p.screech;
    H.clickRate = H.p.click; H.click_cd = (int)RATE;
    H.bw_l = H.p.filt == 2; H.bw_b = H.p.filt == 0; H.bw_h = H.p.filt == 1;
    H.wl = H.bw_l; H.wb = H.bw_b; H.wh = H.bw_h;
    H.oth = H.p.filt == 0 ? 0.2f : 1.0f;      // no bandpass, low/high entram bem mais baixos
    // wanderers (começam no neutro)
    wnd_init(&H.mCut,   H_CTL, -1.3f, 0.9f, 8, 22, 3.5f, 0.0f);
    wnd_init(&H.mDrive, H_CTL, 0.60f, 1.25f, 10, 26, 4.0f, 1.0f);
    wnd_init(&H.mGate,  H_CTL, -1.0f, 1.2f, 9, 24, 2.5f, 0.0f);
    wnd_init(&H.mSweep, H_CTL, -1.0f, 1.3f, 9, 22, 4.0f, 0.0f);
    wnd_init(&H.mSub,   H_CTL, 0.35f, 1.5f, 8, 20, 3.0f, 1.0f);
    wnd_init(&H.mRum,   H_CTL, 0.10f, 1.3f, 10, 25, 4.0f, 1.0f);
    wnd_init(&H.mGrowl, H_CTL, 0.0f, 1.4f, 6, 18, 2.5f, 1.0f);
    wnd_init(&H.mFM,    H_CTL, 0.30f, 1.5f, 7, 20, 3.0f, 1.0f);
    wnd_init(&H.mBits,  H_CTL, -3.0f, 2.0f, 12, 30, 5.0f, 0.0f);
    wnd_init(&H.mClick, H_CTL, 0.20f, 2.2f, 10, 25, 3.0f, 1.0f);
    wnd_init(&H.mLow,   H_CTL, 0.0f, 1.0f, 10, 28, 5.0f, H.bw_l);
    wnd_init(&H.mBand,  H_CTL, 0.0f, 1.0f, 10, 28, 5.0f, H.bw_b);
    wnd_init(&H.mHigh,  H_CTL, 0.0f, 1.0f, 10, 28, 5.0f, H.bw_h);
    wnd_init(&H.mMorph, H_CTL, 0.0f, 0.7f, 15, 40, 6.0f, 0.0f);
    H.scene_cd = (int)(RATE * (18.0f + 25.0f * rng_f()));
}

static inline float crush(float x, float lev) { return ffloor(x * lev + 0.5f) / lev; }

static void harsh_scene(void) {
    static const float sm[5] = {1.0f, 1.0f, 1.25f, 1.5f, 0.75f};
    float m = sm[rng_u32() % 5];
    if (rng_f() < 0.25f && H.p.subFreq * 0.5f >= 24.0f) m = 0.5f;     // oitava abaixo (só se ainda audível)
    H.subMulT = m;
    H.gateMul = (rng_f() < 0.5f) ? 1.0f : exp2f(rng_s() * 1.0f);
    H.fmMul = exp2f(rng_s() * 0.9f);
    if (rng_f() < 0.40f) { H.drop_hold = (int)(RATE * (0.10f + 0.55f * rng_f())); H.dgT = 0.0f; }   // dropout -> slam
    H.scene_cd = (int)(RATE * (20.0f + 30.0f * rng_f()));
}

static void harsh_ctl(void) {
    HarshP *p = &H.p;
    float cm = wnd_step(&H.mCut), dm = wnd_step(&H.mDrive), gm = wnd_step(&H.mGate), sw = wnd_step(&H.mSweep);
    H.sweepph = wrap1(H.sweepph + (float)H_CTL * p->sweepRate * exp2f(sw) / RATE);
    H.lfo = psin(H.sweepph);
    float fc = p->cutoff * H.cutMul * exp2f(p->sweepDepth * H.lfo + cm);
    fc = clampf(fc, 40.0f, 9500.0f);
    float ft = 2.0f * sinf(PI_F * fc / RATE);
    H.f += (ft - H.f) * 0.5f;
    H.gateEff = H.gateMul * exp2f(gm);
    H.drive = p->drive * dm * (1.0f + 0.6f * H.burst);
    H.burst *= 0.998f;
    float bt = clampf(p->bits + wnd_step(&H.mBits), 4.0f, 16.0f);
    H.bitsOn = bt < 15.5f; H.lev = exp2f(bt - 1.0f);
    H.subL = p->subLevel * wnd_step(&H.mSub);
    H.rumL = p->rumble * wnd_step(&H.mRum);
    H.grL = p->growl * wnd_step(&H.mGrowl);
    H.fmL = p->screech * wnd_step(&H.mFM);
    H.clickRate = p->click * wnd_step(&H.mClick);
    float mm = wnd_step(&H.mMorph);                       // morph entre as saídas do filtro
    H.wl = H.bw_l * (1.0f - mm) + wnd_step(&H.mLow) * mm;
    H.wb = H.bw_b * (1.0f - mm) + wnd_step(&H.mBand) * mm;
    H.wh = H.bw_h * (1.0f - mm) + wnd_step(&H.mHigh) * mm;
    H.subMul += (H.subMulT - H.subMul) * 0.002f;
    float sf = p->subFreq * H.subMul;
    H.subInc = sf / RATE; H.sub2Inc = sf * 1.006f / RATE;
    H.grInc = p->growlF * H.subMul * (1.0f + 0.08f * H.lfo) / RATE; H.grInc2 = H.grInc * 1.5f;
    float fr = clampf(sf * 1.7f * (1.0f + 0.3f * H.lfo), 28.0f, 420.0f);
    H.fr = 2.0f * sinf(PI_F * fr / RATE);
    H.jit_cd -= H_CTL;
    if (H.jit_cd <= 0) {
        H.jit_cd = (int)(RATE / p->jitter * (0.4f + rng_f()));
        H.cutMul = exp2f(rng_s() * 1.2f);
        static const float gm5[5] = {1.0f, 1.0f, 0.5f, 2.0f, 1.5f};
        H.gateMul *= gm5[rng_u32() % 5]; H.gateMul = clampf(H.gateMul, 0.4f, 2.5f);
        H.fmMul = exp2f(rng_s() * 0.8f);
    }
    if (H.drop_hold > 0) { H.drop_hold -= H_CTL; if (H.drop_hold <= 0) { H.dgT = 1.0f; H.burst = 1.0f; } }
    H.scene_cd -= H_CTL;
    if (H.scene_cd <= 0) harsh_scene();
}

static inline void harsh_sample(float *oL, float *oR) {
    HarshP *p = &H.p;
    if (--H.ctl <= 0) { H.ctl = H_CTL; harsh_ctl(); }

    float wl = rng_s(), wr = rng_s();
    H.bL = H.bL * 0.995f + wl * 0.1f;
    H.bR = H.bR * 0.995f + wr * 0.1f;
    float nl = wl * (1.0f - p->color) + H.bL * p->color;
    float nr = wr * (1.0f - p->color) + H.bR * p->color;
    if (--H.click_cd <= 0) {                                    // crackle: impulsos que excitam o filtro
        float imp = rng_s() * 2.5f;
        nl += imp; nr -= imp * 0.7f;
        H.click_cd = (int)(-logf(rng_f() + 1e-6f) * RATE / (H.clickRate + 0.5f)) + 1;
    }

    float sc = 0;
    if (H.fmL > 0.0f) {
        float fm = p->fmFreq * H.fmMul / RATE;
        H.cph = wrap1(H.cph + fm);
        H.mph = wrap1(H.mph + fm * p->fmRatio);
        float pm = H.cph + p->fmDepth * psin(H.mph);
        pm -= ffloor(pm);
        sc = psin(pm) * H.fmL;
    }
    float gate = 1.0f;
    if (p->gateRate > 0) {
        H.gph = wrap1(H.gph + p->gateRate * H.gateEff / RATE);
        H.g += ((H.gph < p->duty ? 1.0f : 0.0f) - H.g) * 0.15f;
        gate = H.g;
    }
    float inL = (nl + sc) * gate, inR = (nr + sc * 0.9f) * gate;
    float hi, lowo, bandL, bandR, hL, hR;
    svf(inL, H.f, H.q, &H.lowL, &H.bandL, &hi); hL = hi;
    svf(inR, H.f, H.q, &H.lowR, &H.bandR, &hi); hR = hi;
    lowo = H.oth; bandL = H.bandL * H.q; bandR = H.bandR * H.q;
    float yl = H.wl * lowo * H.lowL + H.wb * bandL + H.wh * lowo * hL;
    float yr = H.wl * lowo * H.lowR + H.wb * bandR + H.wh * lowo * hR;
    float xl = clampf(yl * p->pre * H.drive, -1.0f, 1.0f);
    float xr = clampf(yr * p->pre * H.drive, -1.0f, 1.0f);
    if (H.bitsOn) { xl = crush(xl, H.lev); xr = crush(xr, H.lev); }
    if (p->srdiv > 1) {
        if (++H.hold_cnt >= p->srdiv) { H.hold_cnt = 0; H.holdL = xl; H.holdR = xr; }
        xl = H.holdL; xr = H.holdR;
    }

    // ---- bus grave (camadas), clipado à parte pra não ser engolido pelo ruído
    H.subph  = wrap1(H.subph  + H.subInc);
    H.sub2ph = wrap1(H.sub2ph + H.sub2Inc);
    float sq = H.subph < 0.5f ? 1.0f : -1.0f;
    float sub = (sq * 0.5f + psin(H.sub2ph) * 0.6f) * H.subL * (0.55f + 0.45f * gate);
    float b = H.rb * 0.997f + rng_s() * 0.06f; H.rb = b;
    float rhi; svf(b, H.fr, 0.9f, &H.rlow, &H.rband, &rhi);
    float rum = H.rlow * 2.0f * H.rumL;
    H.grph  = wrap1(H.grph  + H.grInc);
    H.grph2 = wrap1(H.grph2 + H.grInc2);
    float gp = H.grph + (1.5f + 2.5f * H.grL) * psin(H.grph2); gp -= ffloor(gp);
    float gr = psin(gp) * H.grL * (0.5f + 0.5f * gate);
    float lb = clampf((sub + rum + gr) * 2.2f, -1.0f, 1.0f);

    H.dg += (H.dgT - H.dg) * 0.02f;
    float mixL = clampf((xl * 0.64f + lb * 0.58f) * H.dg, -1.0f, 1.0f);
    float mixR = clampf((xr * 0.64f + lb * 0.58f) * H.dg, -1.0f, 1.0f);
    *oL = mixL * 0.44f; *oR = mixR * 0.44f;     // harsh noise é ALTO: master baixo de propósito
}

// ---------------------------------------------------------------- mestre
static volatile int g_req = -2;
static int   cur = -1, pend = -1, have_pend = 0, playing = 0;
static float gain = 0, up_step = 0.001f;
static volatile float g_scope[SYNTH_SCOPE_N];
static volatile int   g_scope_w = 0;
static volatile float g_level = 0;
static int scope_cd = 0;

void synth_init(void) {
    reverb_clear();
    playing = 0; cur = -1; gain = 0; have_pend = 0;
    for (int i = 0; i < SYNTH_SCOPE_N; i++) g_scope[i] = 0;
}

void synth_request(int card) { __atomic_store_n(&g_req, card, __ATOMIC_RELEASE); }

static void apply_card(int card) {
    reverb_clear();
    cur = card;
    if (card < 0) { playing = 0; return; }
    if (card < 22) { amb_init(card); up_step = 1.0f / (RATE * 2.5f); }
    else           { harsh_init(card); up_step = 1.0f / (RATE * 0.02f); }
    playing = 1;
}

void synth_render(int16_t *out, int frames) {
    int r = __atomic_exchange_n(&g_req, -2, __ATOMIC_ACQUIRE);
    if (r != -2) { pend = r; have_pend = 1; }
    const float dn = 1.0f / (RATE * 0.03f);
    for (int i = 0; i < frames; i++) {
        if (have_pend) {
            gain -= dn;
            if (gain <= 0.0f) { gain = 0.0f; apply_card(pend); have_pend = 0; }
        } else if (playing && gain < 1.0f) {
            gain += up_step; if (gain > 1.0f) gain = 1.0f;
        }
        float L = 0, R = 0;
        if (playing) {
            if (cur < 22) amb_sample(&L, &R); else harsh_sample(&L, &R);
            L *= gain; R *= gain;
        }
        L = clampf(L, -1.0f, 1.0f); R = clampf(R, -1.0f, 1.0f);
        out[2 * i]     = (int16_t)(L * 30000.0f);
        out[2 * i + 1] = (int16_t)(R * 30000.0f);
        float m = (L + R) * 0.5f;
        g_level += (fabs_(m) - g_level) * 0.0006f;
        if (--scope_cd <= 0) {
            scope_cd = 16;
            g_scope[g_scope_w] = m;
            g_scope_w = (g_scope_w + 1) % SYNTH_SCOPE_N;
        }
    }
}

float synth_scope(int i) { return g_scope[(g_scope_w + i) % SYNTH_SCOPE_N]; }
float synth_level(void) { float l = g_level * 3.0f; return l > 1.0f ? 1.0f : l; }
