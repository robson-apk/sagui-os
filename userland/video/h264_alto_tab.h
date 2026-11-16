/* h264_alto_tab.h - números da norma que o perfil High do H.264 acrescenta.
 *
 * O perfil High (profile_idc 100) junta ao Main a transformada 8x8, a predição
 * intra 8x8 e as matrizes de escala. O código está em h264.c, escrito a partir
 * do texto da Recomendação ITU-T H.264 (ISO/IEC 14496-10), que a ITU publica
 * de graça; aqui ficam só os DADOS que a norma dá em tabelas, cada um com o
 * número da tabela ou da equação de onde saiu (D048). São valores que qualquer
 * decodificador precisa ter iguais; a forma de guardar é a daqui.
 *
 * O que NÃO está aqui porque se calcula: a varredura em ziguezague 8x8 (tabela
 * 8-13, é o ziguezague comum, montado andando pelas diagonais em h264.c) e os
 * valores iniciais dos contextos 399 a 435 do CABAC (tabelas 9-16 e 9-24 a
 * 9-26), que já vinham junto com os outros em terceiros/ffmpeg-h264-tabelas. */
#ifndef SAGUI_H264_ALTO_TAB_H
#define SAGUI_H264_ALTO_TAB_H

/* Tabela 9-43, colunas "frame coded": para cada posição da varredura de um
 * bloco 8x8 (0 a 62; a 63ª nunca é perguntada), qual contexto do CABAC decide
 * o significant_coeff_flag e qual decide o last_significant_coeff_flag. Num
 * bloco 4x4 cada posição tem o seu contexto; no 8x8 são 63 posições para 15
 * e 9 contextos, e a tabela diz quais posições dividem o mesmo. */
static const uint8_t cabac_sig8[63] = {
     0,  1,  2,  3,  4,  5,  5,  4,  4,  3,  3,  4,  4,  4,  5,  5,
     4,  4,  4,  4,  3,  3,  6,  7,  7,  7,  8,  9, 10,  9,  8,  7,
     7,  6, 11, 12, 13, 11,  6,  7,  8,  9, 14, 10,  9,  8,  6, 11,
    12, 13, 11,  6,  9, 14, 10,  9, 11, 12, 13, 11, 14, 10, 12 };
static const uint8_t cabac_ult8[63] = {
     0,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
     3,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  4,  4,  4,
     5,  5,  5,  5,  6,  6,  6,  6,  7,  7,  7,  7,  8,  8,  8 };

/* Equação 8-316 (item 8.5.9): normAdjust8x8. A linha é QP % 6; a coluna é a
 * "classe" da posição (i, j) dentro do bloco 8x8, que a equação 8-317 define
 * (veja classe8 em h264.c). É o fator que desfaz a quantização e, de quebra,
 * compensa as linhas da transformada 8x8 terem tamanhos diferentes. */
static const uint8_t norma8[6][6] = {
    { 20, 18, 32, 19, 25, 24 },
    { 22, 19, 35, 21, 28, 26 },
    { 26, 23, 42, 24, 33, 31 },
    { 28, 25, 45, 26, 35, 33 },
    { 32, 28, 51, 30, 40, 38 },
    { 36, 32, 58, 34, 46, 43 } };

/* Tabela 7-3: listas de escala padrão 4x4 (Default_4x4_Intra e
 * Default_4x4_Inter), na ordem da varredura em ziguezague. */
static const uint8_t escala4_intra[16] = { 6, 13, 13, 20, 20, 20, 28, 28, 28, 28, 32, 32, 32, 37, 37, 42 };
static const uint8_t escala4_inter[16] = { 10, 14, 14, 20, 20, 20, 24, 24, 24, 24, 27, 27, 27, 30, 30, 34 };

/* Tabela 7-4: listas de escala padrão 8x8 (Default_8x8_Intra e
 * Default_8x8_Inter), também na ordem do ziguezague. */
static const uint8_t escala8_intra[64] = {
     6, 10, 10, 13, 11, 13, 16, 16, 16, 16, 18, 18, 18, 18, 18, 23,
    23, 23, 23, 23, 23, 25, 25, 25, 25, 25, 25, 25, 27, 27, 27, 27,
    27, 27, 27, 27, 29, 29, 29, 29, 29, 29, 29, 31, 31, 31, 31, 31,
    31, 33, 33, 33, 33, 33, 36, 36, 36, 36, 38, 38, 38, 40, 40, 42 };
static const uint8_t escala8_inter[64] = {
     9, 13, 13, 15, 13, 15, 17, 17, 17, 17, 19, 19, 19, 19, 19, 21,
    21, 21, 21, 21, 21, 22, 22, 22, 22, 22, 22, 22, 24, 24, 24, 24,
    24, 24, 24, 24, 25, 25, 25, 25, 25, 25, 25, 27, 27, 27, 27, 27,
    27, 28, 28, 28, 28, 28, 30, 30, 30, 30, 32, 32, 32, 33, 33, 35 };

#endif
