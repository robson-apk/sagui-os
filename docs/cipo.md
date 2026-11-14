# Cipó: o navegador do SaguiOS

O Cipó é o navegador do SaguiOS. Ele começou como um leitor de HTML em texto, sem HTTPS,
imagens ou JavaScript ([D033](decisoes.md)). Este documento conta o que ele virou, o que
ainda não faz e os problemas difíceis que apareceram no caminho.

O código é em C, sem bibliotecas externas ligadas ao programa: rede, TLS, HTTP, HTML, CSS,
layout, fontes, imagens, JavaScript, Canvas, WebGL, vídeo e áudio.

**Correção (14/11/2026):** uma versão anterior deste texto dizia que tudo foi "escrito do
zero". Isso estava errado. A revisão do mantenedor (decisão D043 no repositório principal)
achou trechos que seguem de perto projetos conhecidos: o rasterizador de fontes por área
acumulada (font-rs, Apache 2.0), o decodificador VP8 do WebP e suas tabelas (libwebp, BSD),
o `toString` de números em outras bases (V8, BSD), as tabelas CABAC/CAVLC do H.264 (FFmpeg,
LGPL 2.1+) e os livros de Huffman do AAC (FAAD2, GPL 2+). Também foram copiados dados de
especificações: o dicionário do Brotli (RFC 7932) e as tabelas do Unicode. Na versão
integrada ao projeto, cada trecho foi separado em `terceiros/` com a licença e a origem.

**Resumo honesto:** o Cipó abre muitos sites de verdade (Wikipédia, Hacker News, DuckDuckGo,
68k.news, páginas com fontes da web, SVG, canvas e WebGL). Ele ainda **não** é um navegador
para o dia a dia: o Google pede captcha e o captcha falha, o YouTube abre só a versão
mínima, o GitHub sai com o layout quebrado e tudo é lento dentro do QEMU sem aceleração.
Ele não se compara ao Chrome nem ao Firefox.

## Capturas

Todas as capturas são do SaguiOS rodando no QEMU (sem KVM), na resolução 1024x768, com a
imagem de boot deste commit. Elas foram tiradas pela mesma automação dos
testes (`tests/qemu.py`) e não foram editadas.

### Sites reais

| | |
| --- | --- |
| ![Wikipédia](capturas/C10-wikipedia.png) | ![DuckDuckGo](capturas/C17-duckduckgo.png) |
| **Wikipédia** (pt.wikipedia.org/wiki/Sagui): CSS externo com mais de 1.500 regras, imagens, infobox, fontes do sistema. | **DuckDuckGo** (versão HTML): busca funciona, com formulário e resultados. |
| ![Hacker News](capturas/C15-hacker-news.png) | ![68k.news](capturas/C11-68k-news.png) |
| **Hacker News**: tabelas, cores e links. | **68k.news**: notícias do Google News em HTML simples. |
| ![example.com](capturas/C16-example.png) | ![GitHub](capturas/C12-github.png) |
| **example.com**: o script da página insere o mesmo aviso em várias línguas. Francês, russo e espanhol aparecem; árabe e outras escritas sem glifo nas fontes do sistema ficam em branco. | **GitHub**: abre, mas o layout sai quebrado (ver Limitações). |
| ![YouTube](capturas/C13-youtube.png) | ![Google](capturas/C14-google-busca.png) |
| **YouTube**: o site manda o Cipó para m.youtube.com, e a página inicial fica quase vazia (ver Limitações). | **Google**: a busca cai na página de captcha do Google (ver Limitações). |

### Páginas de teste (servidas pelos próprios testes automáticos)

| | |
| --- | --- |
| ![Fontes da web](capturas/C06-fontes-css.png) | ![CSS avançado](capturas/C07-css-avancado.png) |
| **h48**: fontes WOFF2 baixadas pela página, texto grego e cirílico, `transform`, `z-index`, `position: fixed/sticky`. | **h49**: fonte OpenType CFF, contextos de empilhamento, `mask-image`, SVG externo, `data:` e gradientes. |
| ![WebGL](capturas/C02-webgl.png) | ![iframes](capturas/C03-iframes.png) |
| **h54**: WebGL 1 em software. A página compila shaders GLSL ES e confere 8 resultados com `readPixels`; o quadrado verde é o último desenho. | **h55**: um `<iframe>` é uma janela própria, com `postMessage` nos dois sentidos e clique dentro do quadro. |
| ![Componentes](capturas/C05-componentes.png) | ![Captcha](capturas/C04-captcha-layout.png) |
| **h47**: custom elements e shadow DOM com `<slot>`. | **h56**: o layout do widget de checkbox do reCAPTCHA, conferido caixa a caixa contra o Chrome. |
| ![Unicode](capturas/C08-unicode.png) | |
| **h50**: texto do JavaScript em UTF-16 sobre UTF-8. | |

## Como experimentar

Gere a imagem com `make` (ver README) ou use a do último build. No macOS ou no Linux:

```
qemu-system-i386 -m 512M -drive format=raw,file=build/saguios.img \
  -netdev user,id=n0 -device e1000,netdev=n0
```

Dentro do SaguiOS, abra o ícone do Cipó ou digite `cipo https://pt.wikipedia.org` no
Terminal. Texto na barra de endereço que não é endereço vira uma busca no Google.

O QEMU sem KVM é lento. Tempos medidos nas capturas (QEMU sem KVM num Xeon X79): Wikipédia
9,5 s, GitHub 31 s, YouTube 10 s, DuckDuckGo 8 s, Hacker News 3,5 s, example.com 2 s. Com KVM
ou numa máquina real deve ser mais rápido; isso não foi medido.

## O que existe

### Rede e segurança

- Driver da placa Intel PRO/1000 (e1000), IPv4, ICMP, UDP, DHCP, DNS (com servidores de
  reserva) e TCP.
- **TLS 1.3** (X25519, AES-128-GCM) e **TLS 1.2** (ECDHE X25519/P-256, AES-128-GCM),
  escritos a partir das RFCs.
- Verificação de certificados: cadeia X.509, nome, validade e assinatura (RSA, ECDSA P-256 e
  P-384) contra uma lista de autoridades em `/etc/raizes.bin`; SHA-256/384/512.
  Certificado recusado não cai para HTTP.
- O Cipó avisa no rodapé: "a segurança ainda é experimental: evite senhas". Ninguém auditou
  esse código. **Não use para nada sensível.**

### HTTP

- HTTP/1.1 com conexões persistentes (keep-alive), `chunked`, `Content-Length`,
  redirecionamentos e cookies (RFC 6265).
- O Cipó não pede compressão ao servidor; o descompressor **Brotli** próprio (RFC 7932)
  serve às fontes WOFF2.
- `fetch` e `XMLHttpRequest` com métodos, cabeçalhos, corpo, status e URL final reais.
- Não tem HTTP/2 nem HTTP/3.

### HTML, CSS e layout (`libsagui/motor.c`)

- Árvore HTML, cascata CSS com seletores compostos, especificidade, `!important`, `var()`,
  `@media`, pseudo-elementos `::before`/`::after`.
- Layout de bloco e inline, floats, inline-block, **flex**, **grid** (com áreas), tabelas,
  posicionamento `relative/absolute/fixed/sticky`, margens que colapsam.
- Pintura pela árvore de contextos de empilhamento (CSS 2.1, Apêndice E), `z-index`,
  `opacity` de grupo, `transform` (rotate, scale, skew, matrix), bordas com cantos
  arredondados, gradientes, `mask-image`, imagens de fundo com todos os modos.
- Quebra de linha pelo UAX #14 (Unicode 16: passa nos 16.672 casos do LineBreakTest).
- Formulários: campos com foco e digitação dentro da caixa, botões, `<select>`, envio.
- Rolagem pela roda do mouse e barra de rolagem arrastável.

### Fontes e imagens

- Fontes do sistema DejaVu Sans, Serif e Mono.
- Fontes da web: TrueType, OpenType CFF, WOFF e WOFF2, fontes variáveis (fvar/gvar/avar),
  casamento de faces pelo CSS Fonts 4, `letter-spacing`.
- Imagens PNG, JPEG (sequencial e progressivo), GIF (primeiro quadro) e WebP com perdas.
- SVG inline e externo: caminhos, formas, `use/symbol`, gradientes, `clipPath`, `viewBox`.

### JavaScript (`libsagui/js.c` e companhia)

Um interpretador de árvore sintática, com:

- Objetos, closures, classes (com campos e métodos `#privados` e blocos `static`),
  geradores, `async/await` de verdade, Promise, Proxy e Reflect, Symbol como tipo próprio,
  **modo estrito**, `const` imutável, atributos de propriedade (writable, enumerable,
  configurable), `Object.freeze/seal`.
- Números em dupla precisão IEEE 754 com a conversão para texto mais curta
  (Burger & Dybvig) e **BigInt** de precisão arbitrária.
- Strings em UTF-16 sobre UTF-8, expressões regulares com `u`, lookbehind e grupos nomeados.
- Map/Set com hash, WeakMap/WeakRef, helpers de iterador, `structuredClone`, `Intl`
  (número, data, plural, ordenação, segmentação, tempo relativo; idiomas principais),
  typed arrays (incluindo BigInt64Array), DataView.
- **Coletor de lixo** marca-e-varre (antes nada era liberado e páginas pesadas morriam com
  160 MB).
- Uma bateria com 129 recursos de JavaScript e DOM (teste `h57`) passa em 125.

### DOM e APIs da web

- DOM completo para os sites comuns: eventos, timers em tempo real,
  `requestAnimationFrame`, `getBoundingClientRect` e medidas reais (o script que mede força o
  layout, como num navegador), `getComputedStyle`, `IntersectionObserver`,
  `ResizeObserver` (estavam sendo trocados por versões vazias até este commit).
- Custom elements e shadow DOM com slots; `DOMParser`, `TreeWalker`, `Range`, `Selection`.
- Cookies, `localStorage`, `indexedDB` (só em memória), `FileReader`, `Blob`, `TextEncoder`.
- **iframes** como janelas próprias (cada uma com seu `window`, `document` e protótipos),
  `postMessage` entre janelas e `MessageChannel`.
- **Canvas 2D** completo (caminhos, traço, gradientes, recorte, composição, `drawImage`,
  `getImageData`, texto).
- **WebGL 1** em software, com um compilador de GLSL ES próprio.

### Vídeo e som

- Decodificador **H.264** (perfis Baseline e Main, CABAC, quadros B), bit a bit igual ao
  ffmpeg nos fluxos de teste; decodificador **AAC-LC**; leitor de MP4.
- Saída de som pela placa AC'97.
- Vídeos do YouTube já tocaram por esse caminho: o próprio script do player do YouTube monta o
  endereço do vídeo e o Cipó entrega ao tocador `/bin/video`. Isso não foi conferido de novo
  depois das últimas mudanças (ver Limitações).

## Superações: os problemas difíceis

Alguns problemas que custaram caro e como foram resolvidos de verdade:

- **`new Function` não criava função.** Devolvia o próprio texto. Toda biblioteca que monta
  código em tempo de execução quebrava em silêncio. A bateria `h57` mostrou 129 de 129
  falhas na primeira rodada por causa disso.
- **Segfault no reCAPTCHA.** A mensagem de erro lia 40 bytes antes do começo do código-fonte.
  Agora há um anel de fontes lidas, e o trecho só é lido dentro dos limites.
- **Recursão no limite da pilha** (Bing): montar o `RangeError` rodava JavaScript, que
  estourava a pilha de novo, até corromper a memória. O erro passou a ser montado em C.
- **Arrow functions** guardavam um ponteiro para o `this` na pilha C de quem as criou;
  chamadas depois (timer, evento, `await`) liam lixo. Agora o valor é copiado.
- **`"x".toString()` devolvia `""`**, e isso quebrava o reCAPTCHA. Corrigido junto com
  `Function.prototype.toString`.
- **`* html` casava com o nó do documento**, e o checkbox do captcha virava inline. O nó do
  documento não casa mais com seletores.
- **Laço infinito no layout da Wikipédia** (achado ao tirar estas capturas): a medição de
  texto com `white-space: pre` não avançava sobre espaços. Reproduzido no Linux com o motor
  compilado no host, achado com o gdb e corrigido. A página que nunca terminava agora monta
  em 0,4 s no host.
- **Ciclo no DOM no example.com** (também achado agora): `insertBefore(nó, nó)` ligava o nó
  a si mesmo, e a busca de scripts girava para sempre. Também `insertAdjacentHTML`
  percorria uma lista viva e pulava metade dos nós.
- **A imagem de boot tem meta de 1,44 MB.** O navegador inteiro cabe nela: o ramdisk vai em
  LZMA, e as partes que rodam pouco são compiladas otimizando tamanho (`-Os`). O núcleo que
  executa o JavaScript ficou otimizado por velocidade; o tempo do widget do reCAPTCHA
  continuou em ~700 ms.
- **IntersectionObserver e ResizeObserver vazios** (achado ao escrever este documento):
  as implementações reais existiam, mas uma linha antiga da base JS as trocava por versões
  que não faziam nada. Sites que carregam imagens ou listas ao rolar dependem delas.
- **Sem atalhos:** o Cipó mostrava uma página "esta página só existe com JavaScript
  completo" e forçava a aparecer texto escondido pelo CSS. As duas coisas saíram. A página
  carrega como o site manda, mesmo que fique em branco.

## Limitações

Estas são as limitações conhecidas e medidas. Há outras que ninguém mediu.

- **Google:** o Google manda o Cipó para a página de verificação (`/sorry`). O widget do
  reCAPTCHA carrega e é desenhado, mas troca por "Sorry, something went wrong". A causa não foi
  encontrada. O Cipó não tenta contornar captchas nem proteções contra robôs, e não vai
  tentar. A busca por texto na barra de endereço, portanto, não funciona hoje. A versão
  HTML do DuckDuckGo funciona.
- **YouTube:** o site manda o Cipó para m.youtube.com. A página inicial aparece quase vazia
  ("YouTube" e a caixa de busca), um dos 21 scripts para com erro e a lista de vídeos vem
  vazia. A interface nova do YouTube depende de muita coisa que o motor ainda não tem
  completa.
- **GitHub:** abre, mas com cores, menus e imagens fora do lugar.
- **Velocidade:** no QEMU sem KVM, páginas grandes levam de 10 a 30 segundos. O motor
  JavaScript é um interpretador de árvore, sem compilação.
- **JavaScript:** faltam a flag `v` das expressões regulares, subclasses de `Array`
  (`class X extends Array`), buracos em arrays (`[,1]` cria o índice 0) e `with`.
  `eval` direto não enxerga as variáveis locais. `Intl` cobre só os idiomas principais.
  `indexedDB` some ao sair da página. `Element.animate` só aplica o estado final.
  `MutationObserver` existe mas nunca chama de volta.
- **CSS:** sem animações e transições desenhadas no tempo, sem `filter`; regras dentro de
  `@container`, `@supports` e `@layer` são aplicadas sem conferir a condição.
- **Rede:** sem HTTP/2 e HTTP/3, sem compressão HTTP, sem WebSocket (o construtor existe,
  mas não conecta), sem cache em disco.
- **Segurança:** TLS e verificação de certificados nunca foram auditados. Não digite senhas.
- **Hardware:** a placa de rede do notebook de teste (G470) não tem driver; o Cipó só foi
  usado no QEMU e no VirtualBox.

## Testes

Cada recurso tem um teste automático em `tests/`, que liga o SaguiOS no QEMU, abre a página
de teste no Cipó e confere o log e os pixels da tela. A suíte inteira roda com
`python3 tests/rodar_todos.py`. Resultado neste commit: **60 de 60 testes passaram** (QEMU sem KVM, no X79).

Testes do Cipó: h36 a h57 (rede, HTTPS, CSS, JavaScript, certificados, vídeo, componentes,
fontes, Unicode, números, interação, coletor, WebGL, iframes, Proxy e a bateria de recursos).

## Números

| | |
| --- | --- |
| Imagem de boot | 1.469.440 bytes úteis (meta 1,44 MB) |
| Código novo desde a entrega da fase bônus | 52 commits, ~65 mil linhas em 200 arquivos |
| Motor de layout (`motor.c`) | 5.964 linhas |
| JavaScript (`js.c`, `js_nativo.h`, `js_num.h`, `js_big.h`, `js_quadros.h`, `js_base.js`) | 7.291 linhas |
| Interface do Cipó (`cipo.c`) | ~2.700 linhas |
| WebGL + GLSL | 2.665 linhas |
| H.264 + AAC + MP4 | 3.248 linhas |
