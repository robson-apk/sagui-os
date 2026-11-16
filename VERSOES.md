# Histórico de versões do SaguiOS

Este arquivo conta a história do SaguiOS, versão por versão. Ele também vai
dentro do sistema: é o que aparece no botão "Histórico" da janela "Sobre o
SaguiOS" (`/sistema/ajuda/versoes.md`).

Cada mudança nova entra primeiro na seção "Em desenvolvimento"; quando uma
versão é lançada, o que estava lá vira a seção dessa versão.

## As três eras

1. **1.0: criado inteiramente por IA.** Um sistema operacional escrito do
   zero pelo Claude (Anthropic), sob a direção do Eli Rigobeli, sem código de
   outros sistemas e sem bibliotecas prontas.
2. **2.x: a comunidade chega.** O projeto passa a aceitar contribuições da
   comunidade e, nos aplicativos, bibliotecas de terceiros, desde que livres,
   com licença compatível com a GPL 3 e com crédito. O núcleo continua do
   zero.
3. **3.0: 64 bits e evolução contínua (em breve).** O kernel passa a rodar em
   64 bits (x86-64), com os programas de antes funcionando sem mudança, e o
   projeto passa a seguir o versionamento MAIOR.MENOR.CORREÇÃO, crescendo com
   a comunidade.

## Em desenvolvimento

### 2.2.0 (em preparação)

Já no código (no `main`), ainda não lançado como imagem:

- **Teclado espanhol** e troca do layout do teclado nas Configurações (H90,
  D051), do fork SaguiOS-Extended de Alejandro Anuncibay.
- **Comandos de texto ao estilo Unix** (H91), também do fork do Alejandro:
  `head`, `tail`, `sort`, `uniq`, `cut`, `tr`, `seq`, `rev`, `tac`, `nl`,
  `diff`, `find`, `sed`, `tree`, `cal`, `bc`, `expr`, `base64`, `md5sum`, `od`
  e `xxd`, cada um com o nome do Unix e um apelido em português.
- **Compactação** (H93, D052): `gzip`/`gunzip`, `tar` e `zip`/`unzip`, com um
  DEFLATE escrito aqui, na `libsagui`; a ideia veio do fork do Alejandro.
- **A janela "Sobre o SaguiOS" nova** (H94): versão, arquitetura, memória e
  créditos; os botões "Créditos e licenças" (todos os textos de licença dentro
  da imagem, em `/sistema/licencas`) e "Histórico" (este arquivo).

- **Jogos com tema de banana** (H92, D053), do fork do Alejandro:
  Caça-Bananas, Paciência Banana, um emulador de CHIP-8 e o visor de
  atividades JClic, num menu "Jogos".
- Do Robson de Lima Barbosa: **encaixe das janelas** ao arrastar para a borda
  e atalhos de teclado; redimensionar sem a janela embaralhar enquanto o
  programa redesenha; o Cipó sem decodificar de novo as imagens quando a
  página muda; a **doca com cantos redondos** e ícones novos dos programas,
  num estilo só.
- **Ícones desenhados em código** (D055), no estilo do Robson: todos os
  programas com o mesmo fundo de cantos redondos, centrados, e o build gera
  os tamanhos 64 e 40 sem depender de imagem pronta.

## Versões lançadas

### 2.1 (novembro de 2026): a primeira com a comunidade

A primeira versão com contribuições da comunidade, trazidas para o código
atual com os autores nos commits (`CONTRIBUIDORES.md`). Com ela as regras
mudaram (D041): **o núcleo continua sempre do zero** (kernel, drivers, rede,
TLS, janelas, `libsagui`, shell, compilador e os programas básicos), mas os
**aplicativos podem usar código livre de terceiros**, com licença compatível
com a GPL 3, numa pasta própria em `terceiros/` e com crédito.

- **Cipó**, o navegador: CSS moderno, JavaScript com DOM, fontes TrueType,
  WOFF e WOFF2, Canvas, SVG, WebGL, imagens GIF e WebP e vídeo H.264/AAC
  (motor de Robson de Lima Barbosa; o nome veio de M4R4G0N).
- **Visual novo**: ícones suavizados, letras suavizadas, fundos "Com sol" e
  "Mata Atlântica", menus do botão direito e barra de tarefas (Robson); o
  tema "Azul" (Gabriel).
- `poweroff` e a tela "pode desligar" (Clayton); `top`, `grep`, `fetch`,
  `uname`, `df` e duas demonstrações (Gabriel).
- **Tocador** de músicas, com um decodificador de **MP3** escrito do zero pela
  norma (D047); vídeo H.264 no perfil High (D048); copiar e colar entre os
  programas.
- **Integração contínua** no GitHub Actions, de DevLJSP (pull request #10):
  todo pull request é compilado e testado.
- Para o Lenovo G470: **rede por um adaptador USB** (CDC Ethernet), vídeo na
  resolução nativa do painel (opcional) e correções do som.
- Ponto flutuante nos programas, Ctrl+C no Terminal e o **Sagui Code** numa
  janela própria.

### 2.0 (novembro de 2026): o SaguiOS conectado

Ainda escrita inteiramente por IA, seguindo o `prd2.md`, em nove fases:

- Configurações, desligar e reiniciar pelo ACPI e a lista do hardware.
- **Som** pela placa Intel HD Audio, com os sons do sistema e dos jogos.
- **Rede**: placa de rede, IP, DHCP, DNS e `ping`; depois TCP, HTTP e o
  relógio acertado pela internet.
- **Criptografia e HTTPS** com TLS 1.3, tudo escrito aqui.
- Terminal completo e um cliente **SSH**.
- O primeiro **navegador**, com texto, links e imagens PNG e JPEG.
- Um **compilador de C** que roda dentro do próprio SaguiOS.
- O **Sagui Code**, um agente de programação com IA.
- **Visual moderno**: transparência, sombras, doca, temas, janelas que mudam
  de tamanho.

### 1.0 (novembro de 2026): criado inteiramente por IA

Escrita do zero pelo Claude (Anthropic) para o projeto do Eli Rigobeli,
seguindo o `prd.md`: sem código de outros sistemas, sem GRUB e sem
bibliotecas prontas. Cada fase foi testada pelo Eli num Lenovo G470 de 2011.

1. **Boot**: carregador próprio em dois estágios, modo protegido e o kernel em
   C.
2. **Kernel e terminal**: interrupções, relógio, memória, teclado ABNT2 com
   acentos e o sistema de arquivos SGFS.
3. **Processos e programas**: paginação, multitarefa, programas no anel 3,
   chamadas de sistema, pipes e o shell `sagsh`.
4. **Gráfico e mouse**: vídeo VESA, uma fonte de letras própria e o touchpad.
5. **Janelas e programas**: área de trabalho, Arquivos, Terminal, Editor,
   Calculadora e o jogo Sagui Pega Banana.
6. **Pendrives**: USB 2.0 (EHCI) com hubs e conexão a quente, e FAT32 com
   leitura e gravação.
7. **Bônus**: o bip pelo alto-falante e o **Sagui 3D**, um jogo em primeira
   pessoa feito do zero.

A imagem de boot da 1.0 tinha 575.488 bytes.

## Como os números das versões funcionam a partir da 3.0

A partir da 3.0, o SaguiOS segue o versionamento **MAIOR.MENOR.CORREÇÃO**
(o "versionamento semântico"):

- só **correções** de erros: muda o último número (3.0.0 vira 3.0.1);
- **coisas novas** (um programa, um comando, um driver): muda o número do
  meio (3.0.1 vira 3.1.0);
- mudanças na **base** do sistema (o que pode fazer programas antigos pararem
  de funcionar): muda o primeiro número (3.4.2 vira 4.0.0).

Os números não têm limite: depois da 3.9 vem a 3.10, e pode haver uma 3.0.12.
As versões são lançadas de tempos em tempos, cada uma juntando muitas
mudanças; o que já está no código mas ainda não foi lançado fica na seção
"Em desenvolvimento" deste arquivo.
