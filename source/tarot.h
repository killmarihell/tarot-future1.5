#pragma once
// Ordem global das cartas (igual no script de assets e no atlas):
//   0..21  Arcanos Maiores
//   22..35 Paus (Ás..Rei)   36..49 Copas   50..63 Espadas   64..77 Ouros
#define N_CARDS 78
#define N_MAJOR 22

void        tarot_init(void);            // monta os nomes
const char *tarot_name(int id);          // nome em PT-BR, MAIÚSCULAS sem acento
const char *tarot_number(int id);        // "XIII" / "AS" / "REI"...
static inline int tarot_is_major(int id) { return id < N_MAJOR; }
