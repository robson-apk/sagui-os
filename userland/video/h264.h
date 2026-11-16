/* h264.h - decodificador H.264 Baseline, Main e High (h264.c). */
#ifndef SAGUI_H264_H
#define SAGUI_H264_H

#define H264_ERRO        (-1)
#define H264_SEM_SUPORTE (-2)
#define H264_SEM_MEMORIA (-3)

typedef struct h264 h264_t;
typedef struct {
    const uint8_t *y, *u, *v;   /* planos 4:2:0 já cortados */
    int larg, alt, passo_y, passo_c;
    int erros;
} h264_imagem_t;

h264_t *h264_novo(void);
void    h264_fim(h264_t *h);
/* avcC do MP4: lê SPS/PPS e devolve o tamanho do prefixo de cada NAL. */
int     h264_config(h264_t *h, const uint8_t *avcc, int n, int *tam_len);
/* Depois de um H264_SEM_SUPORTE: o que o vídeo tem que o decodificador não faz (ou NULL). */
const char *h264_motivo(h264_t *h);
/* Uma amostra do MP4 (um quadro): NALs com prefixo de tamanho. */
int     h264_amostra(h264_t *h, const uint8_t *d, int n, int tam_len);
int     h264_nal(h264_t *h, const uint8_t *nal, int n);
bool    h264_imagem(h264_t *h, h264_imagem_t *im);   /* próximo quadro na ordem de exibição (false: ainda não) */
void    h264_esvaziar(h264_t *h);                     /* fim do vídeo: libera os quadros retidos */
#endif
