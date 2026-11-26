// Motor de síntese em tempo real (C puro, sem dependência do 3DS).
// Cartas 0..21  = Arcanos Maiores  -> ambient
// Cartas 22..77 = Arcanos Menores  -> harsh noise (Paus, Copas, Espadas, Ouros)
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SYNTH_RATE     32728   // taxa nativa do NDSP
#define SYNTH_SCOPE_N  120

void  synth_init(void);
// Thread-safe. card 0..77 toca; -1 para.
void  synth_request(int card);
// Só a thread de áudio. out = estéreo intercalado.
void  synth_render(int16_t *out, int frames);

// Para a UI (leitura relaxada, sem lock)
float synth_scope(int i);      // 0..SYNTH_SCOPE_N-1, ordem cronológica, [-1,1]
float synth_level(void);       // envelope 0..1

// Parâmetros que a carta define (funções puras de 'card'), 3 barras na UI
const char *synth_param_name(int card, int i);
float       synth_param_value(int card, int i);

#ifdef __cplusplus
}
#endif
