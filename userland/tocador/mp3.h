/* mp3.h - decodificador de MP3 (MPEG-1 e MPEG-2 Audio Layer III) do SaguiOS.
 *
 * Escrito do zero a partir da norma ISO/IEC 11172-3 (e da ISO/IEC 13818-3
 * para as taxas baixas, de 16 a 24 kHz). A explicação de cada etapa está em
 * mp3.c; as tabelas que a norma traz nos anexos estão em mp3_tabelas.h.
 *
 * Uso: o arquivo inteiro fica na memória de quem chama (e precisa continuar
 * lá até o mp3_fechar); o decodificador entrega o som em pedaços, sempre em
 * 16 bits estéreo intercalado (esquerdo, direito), na taxa do arquivo.
 */
#ifndef MP3_H
#define MP3_H

typedef struct mp3_s mp3_t;                 /* opaco */

/* Abre o MP3 que está inteiro na memória (dados, tam). NULL se não for MP3. */
mp3_t *mp3_abrir(const unsigned char *dados, unsigned tam);
unsigned mp3_taxa(mp3_t *m);                /* amostras por segundo (44100, 48000, 32000...) */
unsigned mp3_canais(mp3_t *m);              /* 1 ou 2 */
unsigned mp3_duracao_ms(mp3_t *m);          /* estimada (VBR com cabeçalho Xing/Info, se houver; senão pelos quadros) */
/* Decodifica até max quadros de saída em 16 bits estéreo intercalado (esq, dir; mono é duplicado).
 * Devolve quantos quadros escreveu; 0 no fim do arquivo. */
int mp3_decodificar(mp3_t *m, short *saida, int max);
int mp3_ir_para_ms(mp3_t *m, unsigned ms);   /* pula para perto desse tempo (procura o quadro); 0 ok */
void mp3_fechar(mp3_t *m);

#endif
