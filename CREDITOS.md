# Créditos: o que vem de fora dentro do SaguiOS

O **núcleo** do SaguiOS (kernel, drivers, sistema de arquivos, rede, TLS, SSH, janelas,
`libsagui`, shell, compilador e programas básicos) foi todo escrito neste projeto (D041).
Dentro da imagem, o que veio de fora está listado aqui: **dados públicos** (os certificados
raiz, usados pelo HTTPS para conferir que um site é quem diz ser; as fontes DejaVu; tabelas
do Unicode e da RFC 7932), a **imagem de fundo** da
Mata Atlântica e, nos aplicativos, o código livre de terceiros da pasta `terceiros/`.

## O que não foi escrito aqui

### Código de terceiros nos aplicativos (`terceiros/`)

Todos chegaram pelo pull request #6 (o navegador Cipó, de Robson de Lima Barbosa): a IA que
ajudou o Robson reproduziu esses trechos de memória, e a revisão os reconheceu. Em vez de
ficarem misturados no meio do Cipó, foram separados, com a licença original e um `ORIGEM.md`
(de onde vieram e o que mudou). Licenças conferidas nos arquivos de licença dos projetos.

| Pasta | O quê | De onde | Licença |
| --- | --- | --- | --- |
| `terceiros/font-rs/` | rasterizador por área acumulada (glifos, Canvas, SVG) | font-rs, Raph Levien, Copyright 2015 Google Inc. | Apache License 2.0 |
| `terceiros/libwebp-vp8/` | WebP com perdas (VP8) e as tabelas de probabilidades | libwebp 1.4.0, Copyright (c) 2010 Google Inc. | BSD 3 cláusulas + concessão de patentes do WebM (`PATENTS`) |
| `terceiros/v8-numeros/` | `Number.prototype.toString(base)` | V8, Copyright 2006-2011 the V8 project authors | BSD 3 cláusulas |
| `terceiros/ffmpeg-h264-tabelas/` | tabelas do CABAC e do CAVLC do H.264 | FFmpeg (libavcodec) | LGPL 2.1 ou posterior |
| `terceiros/faad2-aac-huffman/` | livros de Huffman do AAC | FAAD2 — **Code from FAAD2 is copyright (c) Nero AG, www.nero.com** | GPL 2 ou posterior (com o preâmbulo do FAAD2: uso só sob a GPL) |

### Dados e fontes do Cipó

- **Fontes DejaVu** (`/fontes/`, em `rootfs/fontes/`): DejaVu Sans, Serif e Sans Mono, recortadas
  e sem hinting por `tools/mkfontes.py` (do #6). Licença Bitstream Vera / Arev (livre; as mudanças
  da DejaVu são de domínio público; fontes modificadas não podem usar os nomes "Bitstream" ou
  "Vera"). Texto completo em `rootfs/fontes/LICENCA-DejaVu.txt`.
- **Classes de quebra de linha do Unicode** (`userland/navegador/motor/quebra_tab.h`): geradas de
  LineBreak.txt, EastAsianWidth.txt, DerivedGeneralCategory.txt e emoji-data.txt do Unicode 16.0.0.
  Unicode License v3 (`userland/navegador/motor/LICENCA-Unicode.txt`).
- **Dicionário do Brotli** (`userland/navegador/fontes/brotli_tab.h`): o dicionário estático e as
  transformações da RFC 7932 (apêndices A e B). Componentes de código das RFCs: licença BSD
  simplificada do IETF Trust ("Copyright (c) 2016 IETF Trust and the persons identified as the
  document authors").

### Dados do decodificador de MP3

- **Tabelas da norma do MP3** (`userland/tocador/mp3_tabelas.h`): os códigos de Huffman, as faixas
  de fator de escala, a pré-ênfase e a janela de síntese dos anexos da ISO/IEC 11172-3, e as
  contagens de fatores de escala da ISO/IEC 13818-3. São dados que todo decodificador de MP3 tem
  de ter, iguais em todos; o código que os usa (`userland/tocador/mp3.c`) foi escrito aqui (D047).

### Dados do decodificador de H.264 (perfil High)

- **Tabelas da norma do H.264** (`userland/video/h264_alto_tab.h`): os contextos do CABAC para
  os blocos 8x8 (tabela 9-43), o normAdjust8x8 (equação 8-316) e as listas de escala padrão
  (tabelas 7-3 e 7-4) da Recomendação ITU-T H.264 (ISO/IEC 14496-10). São dados que todo
  decodificador de H.264 tem de ter, iguais em todos; o código do perfil High que os usa (em
  `userland/video/h264.c`) foi escrito aqui, a partir do texto da norma (D048). As tabelas do
  CABAC e do CAVLC que já vinham do pull request #6 continuam em `terceiros/ffmpeg-h264-tabelas/`.

### Imagens

#### Fundo "Mata Atlântica" (`/arte/mata-atlantica.sgi`)

- **Origem:** imagem gerada por inteligência artificial (modelo Z-Image Turbo) e trazida por
  Robson de Lima Barbosa no pull request #1; convertida para o formato `.sgi` por
  `tools/mkfundo.py`. Aprovada pelo Eli (H81).
- **Licença:** distribuída com o SaguiOS sob a GPL 3, como o resto da arte do projeto.

## Certificados raiz (`/sistema/certificados/`)

- **Origem:** a lista de autoridades certificadoras da Mozilla (NSS), na forma em que o Ubuntu a
  distribui no pacote `ca-certificates` (versão 20260816ubuntu0.24.04.2), copiada por `tools/mkraizes.py` em
  2026-11-13.
- **Licença:** Mozilla Public License 2.0 (permite redistribuir; compatível com a GPL 3 do SaguiOS).
- **Quantidade:** 24 raízes, 23777 bytes. Só as que cobrem os sites mais usados e os
  serviços de que o SaguiOS precisa; a lista completa tem mais de 100.

| Arquivo | Para quê | Certificado |
| --- | --- | --- |
| `isrg-x1.der` | Let's Encrypt (RSA): a maior parte dos sites, inclusive a Wikipédia | subject=C = US, O = Internet Security Research Group, CN = ISRG Root X1; notAfter=Jun  4 11:04:38 2035 GMT |
| `isrg-x2.der` | Let's Encrypt (curva elíptica) | subject=C = US, O = Internet Security Research Group, CN = ISRG Root X2; notAfter=Sep 17 16:00:00 2040 GMT |
| `gts-r1.der` | Google Trust Services (Google, Cloudflare) | subject=C = US, O = Google Trust Services LLC, CN = GTS Root R1; notAfter=Jun 22 00:00:00 2036 GMT |
| `gts-r3.der` | Google Trust Services | subject=C = US, O = Google Trust Services LLC, CN = GTS Root R3; notAfter=Jun 22 00:00:00 2036 GMT |
| `gts-r4.der` | Google Trust Services (curva elíptica: Cloudflare, OpenRouter) | subject=C = US, O = Google Trust Services LLC, CN = GTS Root R4; notAfter=Jun 22 00:00:00 2036 GMT |
| `digicert-global-g2.der` | DigiCert (GitHub, Microsoft e muitos outros) | subject=C = US, O = DigiCert Inc, OU = www.digicert.com, CN = DigiCert Global Root G2; notAfter=Jan 15 12:00:00 2038 GMT |
| `digicert-global-g3.der` | DigiCert (curva elíptica) | subject=C = US, O = DigiCert Inc, OU = www.digicert.com, CN = DigiCert Global Root G3; notAfter=Jan 15 12:00:00 2038 GMT |
| `digicert-assured-id-g2.der` | DigiCert | subject=C = US, O = DigiCert Inc, OU = www.digicert.com, CN = DigiCert Assured ID Root G2; notAfter=Jan 15 12:00:00 2038 GMT |
| `globalsign-raiz-r3.der` | GlobalSign | subject=OU = GlobalSign Root CA - R3, O = GlobalSign, CN = GlobalSign; notAfter=Mar 18 10:00:00 2029 GMT |
| `globalsign-r46.der` | GlobalSign | subject=C = BE, O = GlobalSign nv-sa, CN = GlobalSign Root R46; notAfter=Mar 20 00:00:00 2046 GMT |
| `globalsign-e46.der` | GlobalSign (curva elíptica) | subject=C = BE, O = GlobalSign nv-sa, CN = GlobalSign Root E46; notAfter=Mar 20 00:00:00 2046 GMT |
| `usertrust-rsa-ca.der` | Sectigo/USERTrust (muitos sites) | subject=C = US, ST = New Jersey, L = Jersey City, O = The USERTRUST Network, CN = USERTrust RSA Certification Authority; notAfter=Jan 18 23:59:59 2038 GMT |
| `usertrust-ecc-ca.der` | Sectigo/USERTrust (curva elíptica) | subject=C = US, ST = New Jersey, L = Jersey City, O = The USERTRUST Network, CN = USERTrust ECC Certification Authority; notAfter=Jan 18 23:59:59 2038 GMT |
| `comodo-rsa-ca.der` | Sectigo (Comodo) | subject=C = GB, ST = Greater Manchester, L = Salford, O = COMODO CA Limited, CN = COMODO RSA Certification Authority; notAfter=Jan 18 23:59:59 2038 GMT |
| `sectigo-servidor-r46.der` | Sectigo | subject=C = GB, O = Sectigo Limited, CN = Sectigo Public Server Authentication Root R46; notAfter=Mar 21 23:59:59 2046 GMT |
| `amazon-raiz-1.der` | Amazon (sites na AWS) | subject=C = US, O = Amazon, CN = Amazon Root CA 1; notAfter=Jan 17 00:00:00 2038 GMT |
| `amazon-raiz-3.der` | Amazon (curva elíptica) | subject=C = US, O = Amazon, CN = Amazon Root CA 3; notAfter=May 26 00:00:00 2040 GMT |
| `microsoft-rsa-2017.der` | Microsoft | subject=C = US, O = Microsoft Corporation, CN = Microsoft RSA Root Certificate Authority 2017; notAfter=Jul 18 23:00:23 2042 GMT |
| `sslcom-raiz-rsa.der` | SSL.com | subject=C = US, ST = Texas, L = Houston, O = SSL Corporation, CN = SSL.com Root Certification Authority RSA; notAfter=Feb 12 17:39:39 2041 GMT |
| `sslcom-tls-ecc-raiz-20.der` | SSL.com 2022 (curva elíptica: Cloudflare, inclusive example.com) | subject=C = US, O = SSL Corporation, CN = SSL.com TLS ECC Root CA 2022; notAfter=Aug 19 16:33:47 2046 GMT |
| `sslcom-tls-rsa-raiz-20.der` | SSL.com 2022 (Cloudflare) | subject=C = US, O = SSL Corporation, CN = SSL.com TLS RSA Root CA 2022; notAfter=Aug 19 16:34:21 2046 GMT |
| `certum-trusted-network.der` | Certum | subject=C = PL, O = Unizeto Technologies S.A., OU = Certum Certification Authority, CN = Certum Trusted Network CA; notAfter=Dec 31 12:07:37 2029 GMT |
| `go-daddy-raiz-g2.der` | GoDaddy | subject=C = US, ST = Arizona, L = Scottsdale, O = "GoDaddy.com, Inc.", CN = Go Daddy Root Certificate Authority - G2; notAfter=Dec 31 23:59:59 2037 GMT |
| `starfield-raiz-g2.der` | Starfield | subject=C = US, ST = Arizona, L = Scottsdale, O = "Starfield Technologies, Inc.", CN = Starfield Root Certificate Authority - G2; notAfter=Dec 31 23:59:59 2037 GMT |
