# FFmpeg: tabelas do H.264 (CABAC e CAVLC)

- **De onde veio:** os números de `libavcodec/h264_cabac.c`, `cabac.c` e
  `h264_cavlc.c` do [FFmpeg](https://ffmpeg.org). Os valores são os da norma
  (ITU-T H.264 / ISO/IEC 14496-10: tabelas 9-12 a 9-33 e 9-44/9-45 do CABAC,
  9-4, 9-5, 9-7, 9-8 e 9-9 do CAVLC), mas a disposição (quais vetores, em que
  ordem, com que forma) é a do FFmpeg.
- **Licença:** esses arquivos do FFmpeg estão sob a LGPL 2.1 **ou posterior**
  (`LICENSE` é o texto da LGPL 2.1; `LICENSE-FFmpeg.md` é o LICENSE.md do
  FFmpeg, que diz que a LGPL 2.1+ vale para o libavcodec por padrão; nenhum
  destes arquivos está na lista das partes GPL). Compatível com a GPL 3.
- **Como chegou aqui:** no pull request #6 (o tocador de vídeo do Cipó, de
  Robson de Lima Barbosa) `cabac_tab.h` dizia "números extraídos do
  libavcodec/h264_cabac.c e cabac.c do FFmpeg", e as tabelas CAVLC de
  `h264.c` (linhas 79 a 131 do #6) seguiam a disposição do FFmpeg. Na 2.1
  (H82, D043) as duas ficaram aqui; `userland/video/h264.c` as inclui.
- **O que mudou:** nada nos números. `cavlc_tab.h` ganhou o comentário de
  origem; `cabac_tab.h` está como veio do #6 (a `rangeTabLPS` foi conferida
  pelo autor com a tabela 9-44 da norma).
- **Teste:** `tests/h82_video.py` (só roda a decodificação quando a máquina
  dos testes tem ffmpeg para gerar o MP4 de teste).
- **Perfil High (D048):** o código do High em `userland/video/h264.c` usa
  daqui só os valores iniciais dos contextos 399 a 435 do CABAC, que já
  estavam em `cabac_tab.h`; as tabelas novas (da norma) ficam em
  `userland/video/h264_alto_tab.h`. Teste: `tests/h88_video_alto.py`.
