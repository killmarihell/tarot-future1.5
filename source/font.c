#include "font.h"
#include <citro2d.h>
#include <string.h>

// 5 linhas de 3 bits (bit 2 = coluna esquerda)
#define G(a, b, c, d, e) {a, b, c, d, e}
static const unsigned char GL_A[26][5] = {
    G(2,5,7,5,5), G(6,5,6,5,6), G(3,4,4,4,3), G(6,5,5,5,6), G(7,4,6,4,7), G(7,4,6,4,4),
    G(3,4,5,5,3), G(5,5,7,5,5), G(7,2,2,2,7), G(1,1,1,5,2), G(5,5,6,5,5), G(4,4,4,4,7),
    G(5,7,7,5,5), G(6,5,5,5,5), G(2,5,5,5,2), G(6,5,6,4,4), G(2,5,5,7,3), G(6,5,6,5,5),
    G(3,4,2,1,6), G(7,2,2,2,2), G(5,5,5,5,7), G(5,5,5,5,2), G(5,5,7,7,5), G(5,5,2,5,5),
    G(5,5,2,2,2), G(7,1,2,4,7)};
static const unsigned char GL_D[10][5] = {
    G(7,5,5,5,7), G(2,6,2,2,7), G(6,1,2,4,7), G(6,1,2,1,6), G(5,5,7,1,1),
    G(7,4,6,1,6), G(3,4,7,5,7), G(7,1,2,2,2), G(7,5,7,5,7), G(7,5,7,1,6)};

static const unsigned char *glyph(char ch, unsigned char *tmp) {
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch >= 'A' && ch <= 'Z') return GL_A[ch - 'A'];
    if (ch >= '0' && ch <= '9') return GL_D[ch - '0'];
    memset(tmp, 0, 5);
    switch (ch) {
    case '-': tmp[2] = 7; return tmp;
    case '.': tmp[4] = 2; return tmp;
    case ':': tmp[1] = 2; tmp[3] = 2; return tmp;
    case '!': tmp[0] = 2; tmp[1] = 2; tmp[2] = 2; tmp[4] = 2; return tmp;
    case '?': tmp[0] = 6; tmp[1] = 1; tmp[2] = 2; tmp[4] = 2; return tmp;
    case '/': tmp[0] = 1; tmp[1] = 1; tmp[2] = 2; tmp[3] = 4; tmp[4] = 4; return tmp;
    default:  return NULL;   // espaço e desconhecidos
    }
}

int font_width(const char *s, int scale) {
    int n = (int)strlen(s);
    return n > 0 ? (n * 4 - 1) * scale : 0;
}

void font_draw(const char *s, float x, float y, int scale, u32 color) {
    unsigned char tmp[5];
    for (; *s; s++, x += 4 * scale) {
        const unsigned char *g = glyph(*s, tmp);
        if (!g) continue;
        for (int r = 0; r < 5; r++)
            for (int c = 0; c < 3; c++)
                if (g[r] & (4 >> c))
                    C2D_DrawRectSolid(x + c * scale, y + r * scale, 0.0f, (float)scale, (float)scale, color);
    }
}

void font_draw_shadow(const char *s, float x, float y, int scale, u32 color, u32 shadow) {
    font_draw(s, x + scale, y + scale, scale, shadow);
    font_draw(s, x, y, scale, color);
}

void font_draw_centered(const char *s, float cx, float y, int scale, u32 color, u32 shadow) {
    font_draw_shadow(s, cx - font_width(s, scale) * 0.5f, y, scale, color, shadow);
}
