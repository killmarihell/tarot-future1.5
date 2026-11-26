#include "tarot.h"
#include <stdio.h>

static const char *MAJOR[22] = {
    "O LOUCO", "O MAGO", "A SACERDOTISA", "A IMPERATRIZ", "O IMPERADOR", "O HIEROFANTE",
    "OS ENAMORADOS", "O CARRO", "A FORCA", "O EREMITA", "A RODA DA FORTUNA", "A JUSTICA",
    "O ENFORCADO", "A MORTE", "A TEMPERANCA", "O DIABO", "A TORRE", "A ESTRELA",
    "A LUA", "O SOL", "O JULGAMENTO", "O MUNDO"};
static const char *ROMAN[22] = {"0", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X",
                                "XI", "XII", "XIII", "XIV", "XV", "XVI", "XVII", "XVIII", "XIX", "XX", "XXI"};
static const char *RANK[14] = {"AS", "DOIS", "TRES", "QUATRO", "CINCO", "SEIS", "SETE", "OITO",
                               "NOVE", "DEZ", "PAJEM", "CAVALEIRO", "RAINHA", "REI"};
static const char *RANK_SHORT[14] = {"AS", "2", "3", "4", "5", "6", "7", "8", "9", "10", "PAJEM", "CAV.", "RAINHA", "REI"};
static const char *SUIT[4] = {"PAUS", "COPAS", "ESPADAS", "OUROS"};

static char names[N_CARDS][32];

void tarot_init(void) {
    for (int i = 0; i < N_MAJOR; i++) snprintf(names[i], sizeof names[i], "%s", MAJOR[i]);
    for (int i = N_MAJOR; i < N_CARDS; i++) {
        int c = i - N_MAJOR;
        snprintf(names[i], sizeof names[i], "%s DE %s", RANK[c % 14], SUIT[c / 14]);
    }
}

const char *tarot_name(int id) { return (id >= 0 && id < N_CARDS) ? names[id] : ""; }

const char *tarot_number(int id) {
    if (id < 0 || id >= N_CARDS) return "";
    if (id < N_MAJOR) return ROMAN[id];
    return RANK_SHORT[(id - N_MAJOR) % 14];
}
