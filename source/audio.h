#pragma once
#include <stdbool.h>

// Inicia NDSP + thread de áudio que chama synth_render().
// Retorna false se o DSP não está disponível (sem dspfirm.cdc no console).
bool audio_init(void);
void audio_exit(void);
