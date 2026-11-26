#pragma once
// Fonte bitmap 3x5 desenhada com retângulos (visual pixelado, zero assets).
// Aceita A-Z (minúsculas viram maiúsculas), 0-9, espaço, '-', '.', ':', '/', '!', '?'.
// Cada pixel da fonte = 'scale' pixels da tela. Avanço por letra = 4*scale.
#include <3ds.h>

int  font_width(const char *s, int scale);
void font_draw(const char *s, float x, float y, int scale, u32 color);
void font_draw_shadow(const char *s, float x, float y, int scale, u32 color, u32 shadow);
void font_draw_centered(const char *s, float cx, float y, int scale, u32 color, u32 shadow);
