# I trust you with my future! v1.5 (3DS homebrew)

*(antes: Cartomancia Pratesi)*

Tarot de música experimental para Nintendo 3DS. Duas mesas de feltro vermelho, estética pixelada (estilo Balatro).

## Novidades da v1.5

- **Y = reset.** Para o som, a carta de cima cai da tela e as 3 cartas voltam pro deck. Tudo volta ao zero (antes o Y só redistribuía o deck selecionado).
- **Menu de cartas (X) com divisão:** seção *ARCANOS MAIORES* (azul-gelo) e *ARCANOS MENORES* (laranja), cada uma com cabeçalho, contorno e fundo próprio. **L / R** pulam entre as seções.
- **Harsh noise mais grave e em camadas:** todos os naipes ganharam sub (quadrado + seno desafinado), rumble (ruído marrom filtrado), growl FM grave e crackle, num bus grave clipado separado. Cortes dos filtros mais baixos.
- **Evolução ao vivo (ambient e harsh):** dezenas de "wanderers" (passeios aleatórios suaves) mexem em corte, drive, camadas, ritmo, bitcrush, reverb etc. em escalas de 6 a 40 s. A cada ~20-90 s rola uma *cena*: no ambient o tom viaja para outra tônica/modo vizinho e depois volta pra casa; no harsh pode haver dropout seguido de "slam" e salto de sub. Dá pra deixar tocando o tempo que quiser.
- **Ambient com mais camadas:** shimmer agudo, sub drone, vento/respiração, frases de pluck que entram e saem e 10 vozes de sino.
- **Efeitos:** anel + faíscas ao escolher a carta (gelo no ambient, brasa no harsh), aura pulsando, osciloscópio real do som atrás da carta, bolhas subindo no ambient, carta tremendo + glitch + estilhaços no harsh, faíscas quando a carta pousa, flash no reset e brilhos na abertura.
- Novo nome e tela de abertura.

- **Embaixo:** dois decks nos cantos. Esquerda = Arcanos Maiores, direita = Menores. Tocar num deck abre ele, distribui 3 cartas (animação estilo Spider) e ele volta pro cantinho. Tocar de novo no deck recolhe as 3 cartas e distribui 3 novas.
- **Tocar numa carta:** ela vai pra tela de cima em foco e a música é gerada em tempo real. Tocar de novo na mesma carta para o som.
- **Maiores (22 cartas) = ambient.** Cada carta tem modo/escala, tônica, brilho, densidade de sinos, desafinação, velocidade dos acordes, reverb e modulação próprios.
- **Menores (56 cartas) = harsh noise.** O naipe escolhe a arquitetura, o valor (Ás..Rei) define a intensidade:
  - Paus: screech FM + highpass
  - Copas: ruído com bandpass ressonante varrendo
  - Espadas: rajadas gateadas de ruído branco
  - Ouros: ruído marrom + sub quadrado, grave e esmagado
  - (todos: camadas graves + crackle, evoluindo com o tempo)

> **Cuidado com o volume.** As Menores são harsh noise de verdade. Comece com o volume do 3DS baixo, principalmente com fone.

## Controles

| | |
|---|---|
| Toque | decks e cartas |
| D-Pad ←/→ + A | escolhe/toca carta |
| L / R | deck Maiores / Menores (no menu: pula de seção) |
| X | menu com todas as cartas (D-Pad navega, A toca, B para, Y fecha) |
| **Y** | **reset: recolhe as cartas, para o som, volta ao zero** |
| B | para o som |
| START | sai |

O áudio usa o DSP. No console real precisa do `dspfirm.cdc` (use "DSP1" no hbmenu/Luma). No Citra/Azahar funciona direto.

## Build pelo GitHub (o que você pediu)

1. Suba esta pasta num repositório.
2. A Action `Build 3DSX` roda sozinha em push/PR (ou manualmente em *Actions → Run workflow*).
3. O `.3dsx` fica em *Artifacts → cartomancia-pratesi-3dsx*. Se você der push de uma tag `v*` (ex.: `git tag v1.5 && git push --tags`), ele também vai para a *Release*.

A Action faz: (1) Python baixa as 78 cartas RWS do Wikimedia Commons, pixeliza e monta os atlas; (2) container `devkitpro/devkitarm` converte com `tex3ds` e compila.

### Cartas

Fonte padrão: Rider-Waite-Smith de 1909 (Pamela Colman Smith), domínio público, do Wikimedia Commons. Se alguma carta não for encontrada, a Action emite um `::warning::` e usa um placeholder (`#NN`); veja o log do job *assets*.

Para usar suas próprias imagens, ponha em `cards_src/` arquivos `NN.png` ou `NN.jpg`, onde NN é o id (00..77), que têm prioridade:

| ids | cartas |
|---|---|
| 00–21 | Maiores (Louco .. Mundo) |
| 22–35 | Paus (Ás..Rei) |
| 36–49 | Copas |
| 50–63 | Espadas |
| 64–77 | Ouros |

(Ás, 2..10, Pajem, Cavaleiro, Rainha, Rei.) Qualquer proporção serve; o script corta as bordas, reduz para 54x98, quantiza em 20 cores e adiciona a moldura.

## Build local

```bash
# precisa de devkitARM + 3ds-dev (dkp-pacman -S 3ds-dev), python3 e Pillow
make assets   # gera romfs/gfx/*.t3x
make          # gera cartomancia-pratesi.3dsx
```

## Ouvir no PC sem o 3DS

```bash
gcc -O2 -Isource tools/synth_test.c source/synth.c -lm -o synth_test
./synth_test 13 20 morte.wav     # carta 13 (A Morte), 20 s (use 120+ s pra ouvir a evolução/cenas)
./synth_test 50 10 as_espadas.wav
./synth_test all 8               # testa as 78 cartas (pico, rms, NaN)
```

## Estrutura

```
source/main.c     UI, animações, entrada, efeitos (partículas), menu
source/synth.c    síntese (ambient + harsh), parâmetros por carta
source/audio.c    NDSP + thread de áudio
source/font.c     fonte 3x5 desenhada com retângulos
source/tarot.c    nomes (PT-BR)
tools/make_assets.py   download + pixelização + atlas
.github/workflows/build.yml
```

## Ideias para evoluir

Arrastar o stylus sobre a carta em foco para modular (cutoff/ritmo), mais de uma carta tocando junto, gravar a saída em WAV no SD.

Código: faça o que quiser. Imagens: arte original de 1909, domínio público (confira a lei do seu país se for distribuir).
