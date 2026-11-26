// I TRUST YOU WITH MY FUTURE! v1.5 (ex-Cartomancia Pratesi) - homebrew de música experimental para 3DS
// Duas "mesas" de feltro vermelho. Embaixo: decks (Maiores à esquerda, Menores à direita)
// que distribuem 3 cartas. Tocar numa carta a coloca em foco na tela de cima e toca
// a música gerada em tempo real (Maiores = ambient, Menores = harsh noise).

#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "font.h"
#include "synth.h"
#include "tarot.h"

#define TOP_W   400
#define BOT_W   320
#define SCR_H   240
#define CARD_W  64
#define CARD_H  108
#define ATLAS_COLS 16
#define PIf 3.14159265f

// layout do atlas de UI (tools/make_assets.py usa os mesmos números)
enum { UI_BACK, UI_FELT, UI_DECK_MAJ, UI_DECK_MIN, UI_COUNT };
static const int UI_RECT[UI_COUNT][4] = {
    {0, 0, 64, 108}, {64, 0, 32, 32}, {96, 0, 48, 64}, {144, 0, 48, 64}};

// ---------------------------------------------------------------- cores
#define COL(r, g, b, a) C2D_Color32(r, g, b, a)
static u32 C_FELT_CLEAR, C_GOLD, C_CREAM, C_DARK, C_SHADOW, C_DIM, C_ACCENT_A, C_ACCENT_H;

// ---------------------------------------------------------------- recursos
static C2D_SpriteSheet sheet_cards, sheet_ui;
static Tex3DS_SubTexture sub_cards[N_CARDS], sub_ui[UI_COUNT];
static C2D_Image img_card[N_CARDS], img_ui[UI_COUNT];

// ---------------------------------------------------------------- estado
static const float SLOT_X[3] = {62, 160, 258};
static const float SLOT_Y = 72;
static const float SLOT_ANG[3] = {-3, 0, 3};
static const float DECK_POS[2][2] = {{34, 196}, {286, 196}};
static const float DECK_CX = 160, DECK_CY = 190;

static int   hand[3] = {-1, -1, -1};
static int   old_hand[3], new_hand[3];
static int   dealing = 0, deal_from = 0, deal_has_old = 0;
static float deal_t = 0;

static int   focus = -1, playing = 0;
static float focus_t = 0;
static float time_s = 0;
static int   cursor = 1, use_buttons = 0;
static bool  audio_ok = false;
static int   last_deck = 0;
static int   intro = 1;
static float intro_t = 0, intro_out = -1;
// menu de todas as cartas
static int   menu = 0, mcur = 0, rise = 0;
static float mscroll = 0, fly_t = -1, ret_t = -1;
static int   fly_id = -1;
#define M_COLS 4
#define M_CW 76
#define M_CH 118
#define FLY_DUR 0.30f
// layout do menu: [cabeçalho MAIORES][6 linhas][vão][cabeçalho MENORES][14 linhas]
#define M_HDR      16
#define M_GAP      8
#define MAJ_ROWS   ((N_MAJOR + M_COLS - 1) / M_COLS)
#define MIN_ROWS   ((N_CARDS - N_MAJOR + M_COLS - 1) / M_COLS)
#define SEC0_TOP   4
#define SEC0_CARDS (SEC0_TOP + M_HDR)
#define SEC0_END   (SEC0_CARDS + MAJ_ROWS * M_CH)
#define SEC1_TOP   (SEC0_END + M_GAP)
#define SEC1_CARDS (SEC1_TOP + M_HDR)
#define SEC1_END   (SEC1_CARDS + MIN_ROWS * M_CH)
#define M_TOTAL    (SEC1_END + 4)

// efeitos (v1.5): partículas, flash, anel de pulso
#define NPT 110
typedef struct { float x, y, vx, vy, life, max, g; u32 rgb; u8 size, kind; } Particle;
static Particle pts[2][NPT];            // [0] = tela de cima, [1] = tela de baixo
static float flash_t = -1, ring_t = -1, prev_deal_t = 0;
static int   gather = 0;                // 1 = animação de recolher (reset)

// ---------------------------------------------------------------- matemática / easing
static float clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float ease_out_cubic(float t) { t = 1 - t; return 1 - t * t * t; }
static float ease_in_cubic(float t) { return t * t * t; }
static float ease_inout(float t) { return t < 0.5f ? 4 * t * t * t : 1 - powf(-2 * t + 2, 3) / 2; }
static float ease_out_back(float t) { const float c1 = 1.70158f, c3 = c1 + 1; return 1 + c3 * powf(t - 1, 3) + c1 * powf(t - 1, 2); }

// ---------------------------------------------------------------- desenho básico
static void draw_img(C2D_Image img, float cx, float cy, float w, float h, float ang_deg, u32 tint, float blend) {
    C2D_DrawParams p;
    p.pos.x = cx; p.pos.y = cy; p.pos.w = w; p.pos.h = h;
    p.center.x = w * 0.5f; p.center.y = h * 0.5f;
    p.depth = 0.0f; p.angle = ang_deg * (PIf / 180.0f);
    if (blend > 0) {
        C2D_ImageTint t; C2D_PlainImageTint(&t, tint, blend);
        C2D_DrawImage(img, &p, &t);
    } else {
        C2D_DrawImage(img, &p, NULL);
    }
}

static void draw_card(int id, float cx, float cy, float sc, float wf, float ang, int shadow) {
    C2D_Image img = id >= 0 ? img_card[id] : img_ui[UI_BACK];
    float w = CARD_W * sc * wf; if (w < 1) w = 1;
    float h = CARD_H * sc;
    if (shadow) draw_img(img, cx + 3 * sc, cy + 5 * sc, w, h, ang, C_SHADOW, 1.0f);
    draw_img(img, cx, cy, w, h, ang, 0, 0);
}

static void draw_felt(float w) {
    float ox = floorf(fmodf(time_s * 5.0f, 32.0f)), oy = floorf(fmodf(time_s * 3.0f, 32.0f));
    for (int gy = -1; gy * 32 < SCR_H + 32; gy++)
        for (int gx = -1; gx * 32 < w + 32; gx++)
            C2D_DrawImageAt(img_ui[UI_FELT], gx * 32 - ox, gy * 32 - oy, 0.0f, NULL, 1.0f, 1.0f);
}

static void draw_vignette(float w) {
    for (int k = 0; k < 6; k++) {
        u32 c = COL(20, 0, 4, 80 - k * 13);
        float t = 4;
        C2D_DrawRectSolid(0, k * t, 0, w, t, c);
        C2D_DrawRectSolid(0, SCR_H - (k + 1) * t, 0, w, t, c);
        C2D_DrawRectSolid(k * t, 0, 0, t, SCR_H, c);
        C2D_DrawRectSolid(w - (k + 1) * t, 0, 0, t, SCR_H, c);
    }
}

static void draw_marker(float cx, float y, u32 col) {   // setinha pixelada apontando pra baixo
    C2D_DrawRectSolid(cx - 6, y, 0, 12, 2, col);
    C2D_DrawRectSolid(cx - 4, y + 2, 0, 8, 2, col);
    C2D_DrawRectSolid(cx - 2, y + 4, 0, 4, 2, col);
}

// ---------------------------------------------------------------- efeitos
static float frand(void) { return (float)(rand() % 10000) / 10000.0f; }

static void fx_spawn(int scr, float x, float y, float vx, float vy, float life, float g, u32 rgb, int size, int kind) {
    for (int i = 0; i < NPT; i++) {
        Particle *p = &pts[scr][i];
        if (p->life > 0) continue;
        p->x = x; p->y = y; p->vx = vx; p->vy = vy; p->life = p->max = life; p->g = g;
        p->rgb = rgb; p->size = (u8)size; p->kind = (u8)kind;
        return;
    }
}

// explosão radial de faíscas
static void fx_burst(int scr, float x, float y, int n, float speed, u32 rgb, u32 rgb2) {
    for (int i = 0; i < n; i++) {
        float a = frand() * 2.0f * PIf, s = speed * (0.35f + frand() * 0.85f);
        fx_spawn(scr, x, y, cosf(a) * s, sinf(a) * s - speed * 0.25f, 0.5f + frand() * 0.6f, 160.0f,
                 (i & 1) ? rgb : rgb2, 2 + (rand() % 2), (rand() % 3) == 0);
    }
}

static void fx_update(float dt) {
    for (int s = 0; s < 2; s++)
        for (int i = 0; i < NPT; i++) {
            Particle *p = &pts[s][i];
            if (p->life <= 0) continue;
            p->life -= dt;
            p->vy += p->g * dt;
            p->x += p->vx * dt; p->y += p->vy * dt;
        }
    if (flash_t >= 0) { flash_t += dt; if (flash_t > 0.45f) flash_t = -1; }
    if (ring_t >= 0)  { ring_t += dt;  if (ring_t > 0.6f)  ring_t = -1; }
}

static void fx_draw(int scr) {
    for (int i = 0; i < NPT; i++) {
        Particle *p = &pts[scr][i];
        if (p->life <= 0) continue;
        float k = p->life / p->max;
        u8 a = (u8)(255 * clamp01(k * 1.6f));
        u32 c = COL((p->rgb >> 16) & 255, (p->rgb >> 8) & 255, p->rgb & 255, a);
        float x = floorf(p->x), y = floorf(p->y), sz = p->size;
        if (p->kind) {   // faísca em cruz
            C2D_DrawRectSolid(x - sz, y, 0, sz * 3, sz, c);
            C2D_DrawRectSolid(x, y - sz, 0, sz, sz * 3, c);
        } else {
            C2D_DrawRectSolid(x, y, 0, sz, sz, c);
        }
    }
}

static void fx_clear(void) { memset(pts, 0, sizeof pts); flash_t = ring_t = -1; }

static void fx_flash(float x, float y, float w, float h, u32 rgb, float k) {   // flash de tela cheia com decaimento
    if (k <= 0) return;
    C2D_DrawRectSolid(x, y, 0, w, h, COL((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, (u8)(200 * clamp01(k))));
}

// ---------------------------------------------------------------- lógica de mão
static void pick_hand(int kind, int out[3]) {
    int lo = kind == 0 ? 0 : N_MAJOR, n = kind == 0 ? N_MAJOR : N_CARDS - N_MAJOR;
    for (int i = 0; i < 3; i++) {
        int v, dup;
        do {
            v = lo + rand() % n; dup = 0;
            for (int j = 0; j < i; j++) if (out[j] == v) dup = 1;
        } while (dup);
        out[i] = v;
    }
}

static void start_deal(int deck) {
    if (dealing) return;
    memcpy(old_hand, hand, sizeof hand);
    pick_hand(deck, new_hand);
    deal_has_old = hand[0] >= 0 || hand[1] >= 0 || hand[2] >= 0;
    deal_from = deck; last_deck = deck; deal_t = 0; dealing = 1; gather = 0; prev_deal_t = 0;
}

// Y = RESET: som para, carta de cima cai, as 3 cartas voltam pro deck e tudo volta ao zero
static void reset_all(void) {
    if (audio_ok) synth_request(-1);
    playing = 0; rise = 0; fly_t = -1; cursor = 1;
    if (focus >= 0 && ret_t < 0) ret_t = 0;
    flash_t = 0;
    fx_burst(1, DECK_POS[0][0], DECK_POS[0][1], 16, 120, 0x78DCFF, 0xF4E8C8);
    fx_burst(1, DECK_POS[1][0], DECK_POS[1][1], 16, 120, 0xFF5A3C, 0xFFCD50);
    int had = hand[0] >= 0 || hand[1] >= 0 || hand[2] >= 0;
    if (dealing) {                       // aborta qualquer distribuição em andamento
        dealing = 0; gather = 0;
        for (int i = 0; i < 3; i++) hand[i] = -1;
    } else if (had) {                    // recolhe as cartas voltando pro deck (animação existente)
        memcpy(old_hand, hand, sizeof hand);
        for (int i = 0; i < 3; i++) new_hand[i] = -1;
        deal_has_old = 1; deal_from = last_deck; deal_t = 0; dealing = 1; gather = 1; prev_deal_t = 0;
    }
    last_deck = 0;
}

static void fx_on_focus(int id) {         // tchan: anel + faíscas na tela de cima
    int major = tarot_is_major(id);
    u32 a = major ? 0x78DCFF : 0xFF5A3C, b = major ? 0xF4E8C8 : 0xFFCD50;
    ring_t = 0;
    fx_burst(0, 200, 120, major ? 22 : 34, major ? 120 : 190, a, b);
}

static void pick_card(int slot) {
    int id = hand[slot];
    if (id < 0) return;
    if (id == focus && playing) {          // tocar de novo na mesma carta = parar
        playing = 0;
        if (audio_ok) synth_request(-1);
    } else {
        focus = id; focus_t = 0; playing = 1; rise = 0;
        if (audio_ok) synth_request(id);
        fx_on_focus(id);
    }
}

static int mrow(int i) { return i < N_MAJOR ? i / M_COLS : MAJ_ROWS + (i - N_MAJOR) / M_COLS; }
static int mcol(int i) { return (i < N_MAJOR ? i : i - N_MAJOR) % M_COLS; }
static int midx(int row, int col) {      // índice da carta na linha/coluna (ajusta se a linha for curta)
    if (row < MAJ_ROWS) { int i = row * M_COLS + col; return i >= N_MAJOR ? N_MAJOR - 1 : i; }
    int i = N_MAJOR + (row - MAJ_ROWS) * M_COLS + col;
    return i >= N_CARDS ? N_CARDS - 1 : i;
}
static float mcy(int i) {                // y do centro da carta, sem scroll
    return (i < N_MAJOR ? SEC0_CARDS + (i / M_COLS) * M_CH : SEC1_CARDS + ((i - N_MAJOR) / M_COLS) * M_CH) + M_CH / 2;
}

// tempos da animação de distribuição
#define T_DECK_MOVE   0.35f
#define T_OLD_START   0.30f
#define T_OLD_STEP    0.08f
#define T_OLD_DUR     0.28f
#define T_NEW_STEP    0.20f
#define T_NEW_DUR     0.32f
#define T_FLIP        0.16f
static float deal_td(void) { return deal_has_old ? 0.80f : 0.40f; }
static float deal_te(void) { return gather ? deal_td() : deal_td() + 2 * T_NEW_STEP + T_NEW_DUR + T_FLIP; }
static float deal_end(void) { return deal_te() + T_DECK_MOVE; }

static void update(float dt) {
    time_s += dt;
    intro_t += dt;
    if (intro_out >= 0) { intro_out += dt; if (intro_out >= 0.4f) { intro = 0; intro_out = -1; } }
    focus_t += dt;
    if (fly_t >= 0) {
        fly_t += dt;
        if (fly_t >= FLY_DUR) { fly_t = -1; focus = fly_id; focus_t = 0; playing = 1; rise = 1; fx_on_focus(fly_id); }
    }
    if (ret_t >= 0) {
        ret_t += dt;
        if (ret_t >= 0.3f) { ret_t = -1; focus = -1; rise = 0; }
    }
    {
        float target = mcy(mcur) - 113;
        if (mrow(mcur) == 0) target = 0;                          // 1ª linha dos Maiores: mostra o cabeçalho
        if (mrow(mcur) == MAJ_ROWS) target = SEC1_TOP - 4;        // 1ª linha dos Menores: idem
        float mx = M_TOTAL - (SCR_H - 14);
        if (target > mx) target = mx;
        if (target < 0) target = 0;
        mscroll += (target - mscroll) * clamp01(dt * 12);
    }
    if (dealing) {
        deal_t += dt;
        if (!gather) {                   // faíscas quando cada carta nova pousa
            for (int i = 0; i < 3; i++) {
                float land = deal_td() + i * T_NEW_STEP + T_NEW_DUR + T_FLIP;
                if (prev_deal_t < land && deal_t >= land)
                    fx_burst(1, SLOT_X[i], SLOT_Y, 12, 95, 0xFFCD50, 0xF4E8C8);
            }
        }
        prev_deal_t = deal_t;
        if (deal_t >= deal_end()) { memcpy(hand, new_hand, sizeof hand); dealing = 0; gather = 0; }
    }
    fx_update(dt);
    if (intro && frand() < 0.35f)        // brilhos na abertura
        fx_spawn(0, 30 + frand() * 340, 14 + frand() * 90, (frand() - 0.5f) * 14, -6 - frand() * 10, 0.7f + frand() * 0.7f, 0, 0xFFCD50, 1 + rand() % 2, 1);
    if (playing && focus >= 0 && ret_t < 0 && !intro) {
        float lv = audio_ok ? synth_level() : 0.0f;
        if (tarot_is_major(focus)) {     // ambient: bolhas/pó subindo
            if (frand() < 0.10f + lv * 0.5f)
                fx_spawn(0, frand() * TOP_W, SCR_H + 4, (frand() - 0.5f) * 10, -(14 + frand() * 26), 4.0f + frand() * 3.0f, 0,
                         (rand() & 1) ? 0xBEEBFF : 0x78DCFF, 2, (rand() % 6) == 0);
        } else if (lv > 0.20f && frand() < lv) {   // harsh: estilhaços
            fx_spawn(0, 150 + frand() * 100, 20 + frand() * 200, (frand() - 0.5f) * 300, (frand() - 0.5f) * 300, 0.18f + frand() * 0.25f, 0,
                     (rand() & 1) ? 0xFF5A3C : 0xFFCD50, 2, rand() % 2);
        }
    }
}

// ---------------------------------------------------------------- tela de baixo
static void idle_pose(int i, float *x, float *y, float *ang) {
    *x = SLOT_X[i];
    *y = SLOT_Y + sinf(time_s * 1.7f + i * 2.1f) * 1.5f;
    *ang = SLOT_ANG[i] + sinf(time_s * 1.1f + i) * 1.2f;
    if (!dealing && hand[i] >= 0 && hand[i] == focus) *y -= 10;
}

static void draw_deck_icon(int d, float x, float y, float s) {
    draw_img(img_ui[UI_DECK_MAJ + d], x + 2 * s, y + 3 * s, 48 * s, 64 * s, 0, C_SHADOW, 1.0f);
    draw_img(img_ui[UI_DECK_MAJ + d], x, y, 48 * s, 64 * s, 0, 0, 0);
}

// flip: v 0..1 -> fator de largura e se já virou
static float flipw(float v, int *second) { *second = v >= 0.5f; return fabsf(cosf(v * PIf)); }

static void draw_bottom(void) {
    draw_felt(BOT_W);
    draw_vignette(BOT_W);

    if (!dealing) {
        for (int i = 0; i < 3; i++) {
            if (hand[i] < 0) continue;
            float x, y, a; idle_pose(i, &x, &y, &a);
            draw_card(hand[i], x, y, 1.0f, 1.0f, a, 1);
            if (hand[i] == focus) draw_marker(x, y - CARD_H / 2 - 9 + sinf(time_s * 6) * 1.5f, C_GOLD);
            if (use_buttons && i == cursor) C2D_DrawRectSolid(x - 22, y + CARD_H / 2 + 5, 0, 44, 2, C_CREAM);
        }
    } else {
        float t = deal_t, Td = deal_td(), Te = deal_te();
        float amt = t < Te ? ease_out_cubic(clamp01(t / T_DECK_MOVE)) : 1.0f - ease_inout(clamp01((t - Te) / T_DECK_MOVE));
        float dx = lerpf(DECK_POS[deal_from][0], DECK_CX, amt);
        float dy = lerpf(DECK_POS[deal_from][1], DECK_CY, amt);
        float ds = lerpf(1.0f, 1.35f, amt);

        // cartas antigas voltando pro deck
        for (int i = 0; i < 3; i++) {
            if (old_hand[i] < 0) continue;
            float u = (t - (T_OLD_START + i * T_OLD_STEP)) / T_OLD_DUR;
            float x, y, a; idle_pose(i, &x, &y, &a);
            if (u >= 1) continue;
            if (u <= 0) { draw_card(old_hand[i], x, y, 1.0f, 1.0f, a, 1); continue; }
            float e = ease_in_cubic(u);
            float wf = 1; int second = 0;
            if (u > 0.2f && u < 0.6f) wf = flipw((u - 0.2f) / 0.4f, &second); else second = u >= 0.6f;
            draw_card(second ? -1 : old_hand[i], lerpf(x, dx, e), lerpf(y, dy, e), lerpf(1.0f, 0.65f, e), wf, lerpf(a, 18, e), 1);
        }

        // outro deck parado no cantinho
        draw_deck_icon(1 - deal_from, DECK_POS[1 - deal_from][0], DECK_POS[1 - deal_from][1], 1.0f);
        // o deck em movimento
        draw_deck_icon(deal_from, dx, dy, ds);

        // cartas novas saindo do deck
        for (int i = 0; i < 3; i++) {
            float fs = Td + i * T_NEW_STEP, fl = fs + T_NEW_DUR;
            if (new_hand[i] < 0 || t < fs) continue;
            float tx = SLOT_X[i], ty = SLOT_Y, ta = SLOT_ANG[i];
            if (t < fl) {
                float u = ease_out_cubic((t - fs) / T_NEW_DUR);
                draw_card(-1, lerpf(DECK_CX, tx, u), lerpf(DECK_CY, ty, u) - sinf(u * PIf) * 14, lerpf(0.65f, 1.0f, u), 1.0f, lerpf(-14 + i * 14, ta, u), 1);
            } else if (t < fl + T_FLIP) {
                float v = (t - fl) / T_FLIP; int second;
                float wf = flipw(v, &second);
                draw_card(second ? new_hand[i] : -1, tx, ty - sinf(v * PIf) * 6, 1.0f, wf, ta, 1);
            } else {
                draw_card(new_hand[i], tx, ty + sinf(time_s * 1.7f + i * 2.1f) * 1.5f, 1.0f, 1.0f, ta, 1);
            }
        }
    }

    // decks nos cantos
    if (!dealing) {
        draw_deck_icon(0, DECK_POS[0][0], DECK_POS[0][1], 1.0f);
        draw_deck_icon(1, DECK_POS[1][0], DECK_POS[1][1], 1.0f);
    }
    font_draw_centered("MAIORES", DECK_POS[0][0], 230, 2, C_CREAM, C_DARK);
    font_draw_centered("MENORES", DECK_POS[1][0], 230, 2, C_CREAM, C_DARK);
    if (!audio_ok) font_draw_shadow("SEM AUDIO: DSP FIRMWARE", 6, 6, 1, C_DIM, C_DARK);
}


static void menu_pos(int i, float *x, float *y) {
    *x = 8 + M_CW / 2 + mcol(i) * M_CW;
    *y = mcy(i) - mscroll;
}

static void draw_section(float top, float end, const char *name, const char *count, int r, int g, int b) {
    float y0 = top - mscroll, y1 = end - mscroll;
    if (y1 < -4 || y0 > SCR_H + 4) return;
    u32 acc = COL(r, g, b, 255), fr = COL(r, g, b, 190);
    C2D_DrawRectSolid(3, y0, 0, 314, M_HDR, COL(20, 0, 6, 225));
    C2D_DrawRectSolid(3, y0 + M_HDR - 2, 0, 314, 2, fr);
    font_draw_shadow(name, 9, y0 + 3, 2, acc, C_DARK);
    font_draw_shadow(count, 311 - font_width(count, 2), y0 + 3, 2, C_CREAM, C_DARK);
    C2D_DrawRectSolid(3, y0, 0, 314, 2, fr);                       // contorno da seção
    C2D_DrawRectSolid(3, y1 - 2, 0, 314, 2, fr);
    C2D_DrawRectSolid(3, y0, 0, 2, y1 - y0, fr);
    C2D_DrawRectSolid(315, y0, 0, 2, y1 - y0, fr);
}

static void draw_menu(void) {
    draw_felt(BOT_W);
    // fundo escuro suave por seção (separa visualmente os dois decks)
    C2D_DrawRectSolid(3, SEC0_TOP - mscroll, 0, 314, SEC0_END - SEC0_TOP, COL(30, 40, 70, 60));
    C2D_DrawRectSolid(3, SEC1_TOP - mscroll, 0, 314, SEC1_END - SEC1_TOP, COL(70, 20, 10, 60));
    for (int i = 0; i < N_CARDS; i++) {
        float x, y; menu_pos(i, &x, &y);
        if (y < -M_CH || y > SCR_H + M_CH) continue;
        if (fly_t >= 0 && i == fly_id) continue;
        int cur = i == mcur;
        if (cur) {
            float p = 1.0f + 0.03f * sinf(time_s * 8);
            C2D_DrawRectSolid(x - 36 * p, y - 59 * p, 0, 72 * p, 118 * p, C_GOLD);
        }
        draw_card(i, x, y, cur ? 1.05f : 0.95f, 1.0f, 0, !cur);
    }
    if (fly_t >= 0) {
        float x, y; menu_pos(fly_id, &x, &y);
        float e = ease_in_cubic(clamp01(fly_t / FLY_DUR));
        draw_card(fly_id, lerpf(x, 160, e), lerpf(y, -90, e), lerpf(1.05f, 1.6f, e), 1.0f, lerpf(0, -8, e), 1);
    }
    draw_section(SEC0_TOP, SEC0_END, "ARCANOS MAIORES", "22", 120, 220, 255);
    draw_section(SEC1_TOP, SEC1_END, "ARCANOS MENORES", "56", 255, 90, 60);
    draw_vignette(BOT_W);
    C2D_DrawRectSolid(0, SCR_H - 14, 0, BOT_W, 14, COL(20, 0, 6, 220));
    char bar[48];
    if (mcur < N_MAJOR) snprintf(bar, sizeof bar, "MAIORES %d/22", mcur + 1);
    else                snprintf(bar, sizeof bar, "MENORES %d/56", mcur - N_MAJOR + 1);
    font_draw_shadow(bar, 6, SCR_H - 11, 1, mcur < N_MAJOR ? C_ACCENT_A : C_ACCENT_H, C_DARK);
    font_draw_shadow("A TOCA  B PARA  L/R SECAO  Y FECHA", 82, SCR_H - 11, 1, C_CREAM, C_DARK);
}

static void menu_select(int i) {
    if (fly_t >= 0 || ret_t >= 0) return;
    mcur = i; fly_id = i; fly_t = 0;
    if (audio_ok) synth_request(i);
}

static void menu_back(void) {
    if (fly_t >= 0 || ret_t >= 0 || focus < 0) return;
    playing = 0; if (audio_ok) synth_request(-1);
    ret_t = 0;
}

static void menu_close(void) {
    menu = 0; use_buttons = 1;
}

static void draw_intro(int top) {
    float W = top ? TOP_W : BOT_W;
    draw_felt(W);
    draw_vignette(W);
    float t = intro_t, cx = W / 2;
    if (top) {
        for (int i = 0; i < 3; i++) {
            float u = ease_out_back(clamp01((t - 0.2f - i * 0.15f) / 0.5f));
            float a = (i - 1) * 16 * u;
            draw_card(-1, cx + (i - 1) * 46 * u, 196 - fabsf((float)(i - 1)) * 6, 0.9f * u, 1.0f, a, 1);
        }
        float f = clamp01((t - 0.3f) / 0.8f);
        u8 al = (u8)(255 * f);
        float bob = sinf(t * 1.5f) * 2;
        font_draw_centered("I TRUST YOU", cx, 14 + bob, 8, COL(255, 205, 80, al), COL(30, 8, 14, al));
        font_draw_centered("WITH MY FUTURE!", cx, 62 + bob * 0.7f, 6, COL(244, 232, 200, al), COL(30, 8, 14, al));
        float g = clamp01((t - 1.0f) / 0.6f);
        font_draw_centered("V1.5", cx, 108, 2, COL(210, 150, 150, (u8)(255 * g)), COL(30, 8, 14, (u8)(255 * g)));
        font_draw_centered("MADE BY MJCDN", cx, 128, 1, COL(210, 150, 150, (u8)(200 * g)), COL(30, 8, 14, (u8)(200 * g)));
    } else if (t > 1.2f && fmodf(t, 1.2f) < 0.8f) {
        font_draw_centered("TOQUE OU PRESSIONE A", cx, 112, 2, C_CREAM, C_DARK);
    }
    if (intro_out >= 0) C2D_DrawRectSolid(0, 0, 0, W, SCR_H, COL(20, 0, 4, (u8)(255 * clamp01(intro_out / 0.4f))));
}

// ---------------------------------------------------------------- tela de cima
static int wrap_lines(const char *s, int maxc, char out[][20], int maxl) {
    int n = 0; char cur[20] = ""; char word[20];
    const char *p = s;
    while (*p && n < maxl) {
        int k = 0;
        while (*p && *p != ' ' && k < 19) word[k++] = *p++;
        word[k] = 0;
        while (*p == ' ') p++;
        if (cur[0] && (int)(strlen(cur) + 1 + strlen(word)) > maxc) { strcpy(out[n++], cur); cur[0] = 0; }
        if (n >= maxl) break;
        if (cur[0]) strcat(cur, " ");
        strcat(cur, word);
    }
    if (cur[0] && n < maxl) strcpy(out[n++], cur);
    return n;
}

static void draw_top(void) {
    draw_felt(TOP_W);
    draw_vignette(TOP_W);
    if (focus < 0) return;

    int major = tarot_is_major(focus);
    int r = major ? 120 : 255, g = major ? 220 : 90, b = major ? 255 : 60;
    float t = focus_t;
    float pop = ease_out_back(clamp01(t / 0.4f));
    float lv = playing ? synth_level() : 0.0f;
    float sc = 2.0f * pop * (1.0f + lv * 0.04f);
    float ang = lerpf(-10, 0, ease_out_cubic(clamp01(t / 0.5f))) + sinf(time_s * 0.9f) * 1.6f;
    float cy = 120 + sinf(time_s * 1.5f) * 3;
    if (rise) cy += (1.0f - ease_out_cubic(clamp01(t / 0.45f))) * 220;
    if (ret_t >= 0) cy += ease_in_cubic(clamp01(ret_t / 0.3f)) * 220;
    float cw = CARD_W * sc, chh = CARD_H * sc;

    // aura pulsando atrás da carta (cor do tipo: gelo = ambient, brasa = harsh)
    if (playing && pop > 0.9f) {
        float pulse = 0.5f + 0.5f * sinf(time_s * (major ? 1.6f : 9.0f));
        for (int k = 3; k >= 1; k--) {
            float e = k * 7 + pulse * 5 + lv * 10;
            u8 al = (u8)(16 + 10 * (4 - k) + lv * 30);
            C2D_DrawRectSolid(200 - cw / 2 - e, cy - chh / 2 - e, 0, cw + 2 * e, chh + 2 * e, COL(r, g, b, al));
        }
    }

    // osciloscópio de verdade (o que o sintetizador está tocando), atrás da carta
    if (playing && audio_ok) {
        u32 sc_col = COL(r, g, b, 210);
        float gain = major ? 2.4f : 1.5f;
        for (int i = 0; i < SYNTH_SCOPE_N; i++) {
            float v = synth_scope(i) * gain; if (v > 1) v = 1; if (v < -1) v = -1;
            float h = 2 + fabsf(v) * 22;
            C2D_DrawRectSolid(20 + i * 3.17f, 214 - h * 0.5f + v * 4, 0, 2, h, sc_col);
        }
    }

    // harsh: a carta treme com o nível do som
    float sx = 0, sy = 0;
    if (playing && !major) { sx = (frand() - 0.5f) * lv * 8.0f; sy = (frand() - 0.5f) * lv * 6.0f; }
    draw_card(focus, 200 + sx, cy + sy, sc, 1.0f, ang, 1);

    // anel de pulso ao escolher a carta
    if (ring_t >= 0) {
        float k = ring_t / 0.6f, e = 1.0f + k * 0.9f;
        float w = cw * e, h = chh * e;
        u32 c = COL(255, 205, 80, (u8)(230 * (1.0f - k)));
        C2D_DrawRectSolid(200 - w / 2, cy - h / 2, 0, w, 3, c);
        C2D_DrawRectSolid(200 - w / 2, cy + h / 2 - 3, 0, w, 3, c);
        C2D_DrawRectSolid(200 - w / 2, cy - h / 2, 0, 3, h, c);
        C2D_DrawRectSolid(200 + w / 2 - 3, cy - h / 2, 0, 3, h, c);
    }

    // harsh: barras de glitch cortando a tela
    if (playing && !major) {
        int nb = (int)(lv * 4.0f + frand());
        for (int i = 0; i < nb; i++) {
            float gy = frand() * SCR_H, gh = 2 + frand() * 5, gw = 40 + frand() * 240, gx = frand() * (TOP_W - 40);
            C2D_DrawRectSolid(gx, gy, 0, gw, gh, (rand() & 1) ? COL(255, 90, 60, 150) : COL(244, 232, 200, 120));
        }
    }

    // coluna esquerda: número + nome + tipo
    float x0 = 14;
    font_draw_shadow(tarot_number(focus), x0, 20, 4, C_GOLD, C_DARK);
    char lines[4][20];
    int n = wrap_lines(tarot_name(focus), 14, lines, 4);
    for (int i = 0; i < n; i++) font_draw_shadow(lines[i], x0, 56 + i * 13, 2, C_CREAM, C_DARK);
    font_draw_shadow(major ? "AMBIENTE" : "HARSH NOISE", x0, 56 + n * 13 + 8, 2, major ? C_ACCENT_A : C_ACCENT_H, C_DARK);
    if (!playing) font_draw_shadow("PARADO", x0, 56 + n * 13 + 24, 2, C_DIM, C_DARK);
}

// ---------------------------------------------------------------- entrada
static void handle_tap(int px, int py) {
    if (dealing) return;
    for (int d = 0; d < 2; d++)
        if (fabsf(px - DECK_POS[d][0]) <= 30 && fabsf(py - DECK_POS[d][1]) <= 38) { start_deal(d); return; }
    for (int i = 0; i < 3; i++)
        if (hand[i] >= 0 && fabsf(px - SLOT_X[i]) <= 34 && fabsf(py - SLOT_Y) <= 58) { cursor = i; pick_card(i); return; }
}

static void fatal(const char *msg) {
    consoleInit(GFX_TOP, NULL);
    printf("\x1b[2;2HI TRUST YOU WITH MY FUTURE!\n\n  %s\n\n  START para sair\n", msg);
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        gfxFlushBuffers(); gfxSwapBuffers(); gspWaitForVBlank();
    }
    gfxExit();
}

static bool load_sheet(C2D_SpriteSheet *out, const char *path, C2D_Image *base) {
    *out = C2D_SpriteSheetLoad(path);
    if (!*out) return false;
    *base = C2D_SpriteSheetGetImage(*out, 0);
    C3D_TexSetFilter(base->tex, GPU_NEAREST, GPU_NEAREST);
    return true;
}

static Tex3DS_SubTexture make_sub(int x, int y, int w, int h, int tw, int th) {
    Tex3DS_SubTexture s;
    s.width = (u16)w; s.height = (u16)h;
    s.left = (float)x / tw;       s.right = (float)(x + w) / tw;
    s.top = 1.0f - (float)y / th; s.bottom = 1.0f - (float)(y + h) / th;
    return s;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    romfsInit();
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    C3D_RenderTarget *top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget *bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    C2D_Image base_cards, base_ui;
    if (!load_sheet(&sheet_cards, "romfs:/gfx/cards.t3x", &base_cards) ||
        !load_sheet(&sheet_ui, "romfs:/gfx/ui.t3x", &base_ui)) {
        C2D_Fini(); C3D_Fini(); gfxExit();
        gfxInitDefault();
        fatal("romfs:/gfx/*.t3x nao encontrado.\n  Rode tools/make_assets.py e tex3ds (veja README).");
        romfsExit();
        return 1;
    }
    int cw = base_cards.tex->width, ch = base_cards.tex->height;
    for (int i = 0; i < N_CARDS; i++) {
        sub_cards[i] = make_sub((i % ATLAS_COLS) * CARD_W, (i / ATLAS_COLS) * CARD_H, CARD_W, CARD_H, cw, ch);
        img_card[i].tex = base_cards.tex; img_card[i].subtex = &sub_cards[i];
    }
    int uw = base_ui.tex->width, uh = base_ui.tex->height;
    for (int i = 0; i < UI_COUNT; i++) {
        sub_ui[i] = make_sub(UI_RECT[i][0], UI_RECT[i][1], UI_RECT[i][2], UI_RECT[i][3], uw, uh);
        img_ui[i].tex = base_ui.tex; img_ui[i].subtex = &sub_ui[i];
    }

    C_FELT_CLEAR = COL(120, 18, 30, 255);
    C_GOLD = COL(255, 205, 80, 255);  C_CREAM = COL(244, 232, 200, 255);
    C_DARK = COL(30, 8, 14, 255);     C_SHADOW = COL(0, 0, 0, 110);
    C_DIM = COL(210, 150, 150, 255);
    C_ACCENT_A = COL(120, 220, 255, 255);   // ambient: azul-gelo
    C_ACCENT_H = COL(255, 90, 60, 255);     // harsh: laranja-vermelho

    tarot_init();
    srand((unsigned)osGetTime());
    audio_ok = audio_init();

    u64 last = osGetTime();
    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown();
        if (down & KEY_START) break;

        touchPosition tp; hidTouchRead(&tp);
        static int rep_t = 0;
        if (intro) {
            if (intro_out < 0 && intro_t > 0.8f && (down & (KEY_A | KEY_B | KEY_X | KEY_Y | KEY_L | KEY_R | KEY_TOUCH))) intro_out = 0;
            goto render;
        }
        if (menu) {
            if (down & KEY_Y) menu_close();
            else if (down & KEY_L) mcur = 0;                       // pula pra seção dos Maiores
            else if (down & KEY_R) mcur = N_MAJOR;                 // pula pra seção dos Menores
            else if (down & KEY_B) menu_back();
            else if (down & KEY_A) menu_select(mcur);
            else if (down & KEY_TOUCH) {
                for (int i = 0; i < N_CARDS; i++) {
                    float x, y; menu_pos(i, &x, &y);
                    if (fabsf(tp.px - x) <= 36 && fabsf(tp.py - y) <= 59 && tp.py < SCR_H - 14) { menu_select(i); break; }
                }
            }
            u32 held = hidKeysHeld();
            u32 dirs = held & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT);
            if (!dirs) rep_t = 0;
            else if (rep_t == 0 || (rep_t > 18 && rep_t % 4 == 0)) {
                int c = mcur, rw = mrow(c), cl = mcol(c), nrows = MAJ_ROWS + MIN_ROWS;
                if (dirs & KEY_LEFT)  c = cl > 0 ? c - 1 : c;
                if (dirs & KEY_RIGHT) c = (cl < M_COLS - 1 && c + 1 < N_CARDS && c + 1 != N_MAJOR) ? c + 1 : c;
                if (dirs & KEY_UP)    c = rw > 0 ? midx(rw - 1, cl) : c;
                if (dirs & KEY_DOWN)  c = rw < nrows - 1 ? midx(rw + 1, cl) : c;
                mcur = c;
            }
            if (dirs) rep_t++;
            goto render;
        }
        rep_t = 0;
        if ((down & KEY_X) && !dealing) { menu = 1; use_buttons = 1; goto render; }
        if (down & KEY_Y) reset_all();
        if (down & KEY_TOUCH) { use_buttons = 0; handle_tap(tp.px, tp.py); }
        if (down & (KEY_DLEFT | KEY_DRIGHT | KEY_A | KEY_L | KEY_R | KEY_B)) use_buttons = 1;
        if (down & KEY_DLEFT)  cursor = (cursor + 2) % 3;
        if (down & KEY_DRIGHT) cursor = (cursor + 1) % 3;
        if (down & KEY_A) pick_card(cursor);
        if (down & KEY_L) start_deal(0);
        if (down & KEY_R) start_deal(1);
        if (down & KEY_B) { playing = 0; if (audio_ok) synth_request(-1); }

        render:;
        u64 now = osGetTime();
        float dt = (float)(now - last) / 1000.0f; last = now;
        if (dt > 0.05f) dt = 0.05f;
        update(dt);

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(top, C_FELT_CLEAR);
        C2D_SceneBegin(top);
        if (intro) draw_intro(1); else draw_top();
        fx_draw(0);
        if (flash_t >= 0) fx_flash(0, 0, TOP_W, SCR_H, 0xFFE9A8, 1.0f - flash_t / 0.45f);
        C2D_TargetClear(bot, C_FELT_CLEAR);
        C2D_SceneBegin(bot);
        if (intro) draw_intro(0); else if (menu) draw_menu(); else draw_bottom();
        fx_draw(1);
        if (flash_t >= 0) fx_flash(0, 0, BOT_W, SCR_H, 0xFFE9A8, 1.0f - flash_t / 0.45f);
        C3D_FrameEnd(0);
    }

    audio_exit();
    C2D_SpriteSheetFree(sheet_cards);
    C2D_SpriteSheetFree(sheet_ui);
    C2D_Fini(); C3D_Fini(); gfxExit(); romfsExit();
    return 0;
}
