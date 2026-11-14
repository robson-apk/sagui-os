<div align="center">

# 🐒 SaguiOS

[![CI](https://github.com/erigobeli/sagui-os/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/erigobeli/sagui-os/actions/workflows/ci.yml)

**Um sistema operacional escrito do zero por IA**
para PCs x86 de 32 bits, rodando de verdade num notebook de 2011.

Feito por Claude (Anthropic) para o projeto do **Eli Rigobeli**.

`562 KB` · `sem GRUB` · `sem libc` · `sem código de outros sistemas` · `suíte de testes automáticos`

<img src="docs/capturas/B4-3-fase1-corredor.png" width="49%" alt="Sagui 3D, o jogo em primeira pessoa do SaguiOS">
<img src="docs/capturas/H28-tres-programas.png" width="49%" alt="Área de trabalho do SaguiOS com três programas abertos">

</div>

---

## SaguiOS 2.1

A versão 2.0 ([`prd2.md`](prd2.md)) trouxe som, rede, HTTPS próprio, terminal com SSH,
navegador, um compilador de C rodando dentro do sistema e o **Sagui Code**, um agente de
programação com IA. A **2.1** é a primeira com contribuições da comunidade, trazidas para o
código atual com os autores nos commits (veja [`CONTRIBUIDORES.md`](CONTRIBUIDORES.md)):

- **Cipó**, o navegador, com CSS moderno, JavaScript, fontes TrueType, Canvas, SVG, WebGL e
  vídeo (motor de Robson de Lima Barbosa, nome de M4R4G0N);
- o **visual novo**: ícones suavizados, temas "Mata" e "Azul", fundos novos, menus do botão
  direito e barra de tarefas (Robson e Gabriel);
- `poweroff` e a tela "pode desligar" (Clayton); `top`, `grep`, `fetch`, `uname`, `df` e duas
  demonstrações (Gabriel);
- ponto flutuante nos programas, Ctrl+C no Terminal e, para o G470, um driver de vídeo na
  resolução nativa do painel (opcional) e um driver de adaptador USB de rede.

Acompanhe pelo painel [`dashboard/v2/`](dashboard/v2/index.html). Quer contribuir? Leia
[`CONTRIBUTING.md`](CONTRIBUTING.md).

## O que é

O SaguiOS é um sistema operacional completo, do carregador de boot ao jogo em 3D, escrito
inteiro neste projeto, seguindo a especificação em [`prd.md`](prd.md). Ele dá boot por um
pendrive num **Lenovo G470** (2011), no VirtualBox e no QEMU, e cabe em **575.488 bytes**
(39% de um disquete de 1,44 MB).

Tudo o que vai dentro da imagem foi escrito aqui: carregador de boot em dois estágios, kernel,
drivers (teclado ABNT2, touchpad, vídeo VESA, USB 2.0, pendrive), sistema de arquivos FAT32,
gerenciador de janelas, a biblioteca dos programas (`libsagui`), o shell, os programas, a fonte
de letras e todos os desenhos. Cada decisão de projeto está explicada em
[`docs/decisoes.md`](docs/decisoes.md) e cada erro encontrado, com causa e correção, em
[`docs/erros.md`](docs/erros.md).

## Rodando no hardware real

Fotos do Eli a cada fase, no Lenovo G470:

| Fase 1: boot | Fase 2: kernel e terminal | Fase 3: processos e shell |
| :---: | :---: | :---: |
| <img src="testes_eli/fase_1_no_notebook.jpg" width="260"> | <img src="testes_eli/fase_2_no_notebook.jpg" width="260"> | <img src="testes_eli/fase_3_no_notebook.jpg" width="260"> |
| **Fase 4: gráfico e mouse** | **Fase 5: janelas e programas** | **Bônus: Sagui 3D (VM)** |
| <img src="testes_eli/fase_4_no_notebook.jpg" width="260"> | <img src="testes_eli/fase_5_no_notebook.jpg" width="260"> | <img src="testes_eli/fase_bonus_vm.png" width="260"> |

## As fases

| Fase | O que foi construído | Na tela |
| --- | --- | :---: |
| **1. Boot** | Carregador próprio em dois estágios (MBR + estágio 2), linha A20, mapa de memória E820, cópia para acima de 1 MB em *unreal mode*, soma de verificação, modo protegido, kernel em C | <img src="docs/capturas/H04-boot-vdi.png" width="200"> |
| **2. Kernel e terminal** | GDT, IDT, TSS, tela de erro, PIC, relógio a 100 Hz, memória física, `kmalloc`, teclado ABNT2 com acentos, sistema de arquivos SGFS | <img src="docs/capturas/H09-ls-cd.png" width="200"> |
| **3. Processos e programas** | Paginação com um espaço por processo, escalonador preemptivo, anel 3, chamadas de sistema, carregador ELF, pipes, shell `sagsh` | <img src="docs/capturas/H18-edicao-e-segundo-plano.png" width="200"> |
| **4. Gráfico e mouse** | VESA 1024x768x32 com buffer duplo, fonte 8x16 própria com acentos, terminal gráfico, touchpad PS/2 | <img src="docs/capturas/H18-fonte-acentos.png" width="200"> |
| **5. Janelas e programas** | Área de trabalho, gerenciador de janelas, Arquivos, Terminal, Editor, Calculadora, Sagui Pega Banana | <img src="docs/capturas/H28-programas-abertos.png" width="200"> |
| **6. Pendrive de dados** | PCI, driver USB 2.0 (EHCI) com hubs e conexão a quente, armazenamento USB, FAT32 com leitura e gravação | <img src="docs/capturas/H34-2-arquivos-no-pendrive.png" width="200"> |
| **Bônus** | B3: bip pelo alto-falante do PC. B4: **Sagui 3D**. (B1 e B2 cancelados pelo Eli.) | <img src="docs/capturas/B4-6-macaco-tonto.png" width="200"> |

## Sagui 3D

O sagui foi preso num labirinto de pedra no meio da Mata Atlântica. Ele precisa juntar bananas
para abrir a porteira de cada fase, enquanto macacos-prego guardas tentam pegar as bananas do
bolso dele. O estilingue atira sementes de jabuticaba (três deixam o macaco tonto, e ele foge),
cada banana comida deixa uma casca que faz o macaco escorregar, e cada fase esconde um cacho
dourado atrás de uma parede de cipó.

<div align="center">
<img src="docs/capturas/B4-1-tela-de-inicio.png" width="32%">
<img src="docs/capturas/B4-7-macaco-escorregou.png" width="32%">
<img src="docs/capturas/B4-5-vitoria.png" width="32%">
</div>

O motor 2,5D foi pensado a partir da geometria, sem consultar nenhum código, tutorial ou
repositório de jogo: salas convexas com altura própria de chão e de teto, desenho coluna por
coluna com uma "fresta" que estreita a cada passagem, perspectiva sem olho de peixe, ponto fixo
(os programas não usam a FPU), chão e teto texturizados e luz por sala. São três fases em
arquivos de texto, com texturas e personagens desenhados no próprio código. Explicação completa
na seção "Sagui 3D" de [`docs/decisoes.md`](docs/decisoes.md).

## Números

| | |
| --- | --- |
| Imagem de boot | **575.488 bytes** úteis (39% de 1,44 MB) |
| Linhas de código | boot 903, kernel 9.614, programas 6.317 (mais 3.591 de ferramentas e testes) |
| Programas | 31 em `/bin`: shell, comandos e 8 programas gráficos |
| Testes automáticos | uma suíte com testes no QEMU e no host; o CI roda todos |
| Decisões registradas | 30, com o raciocínio de cada uma |
| Erros registrados | 19, com sintoma, causa e correção |
| Commits | ~55, um por história |

## Experimentar

As imagens prontas estão em [`entrega/final/`](entrega/final/):

- **VirtualBox:** `saguios.vdi` (máquina "Other/Unknown", 32 bits, EFI desligado).
- **PC de verdade:** grave `saguios.img` num pendrive em modo DD (Rufus, balenaEtcher ou `dd`)
  e dê boot pela BIOS tradicional.

O passo a passo está em [`docs/como-gravar.md`](docs/como-gravar.md).

## Integração contínua

Todo push na `main`, nas branches `feat/**`/`fix/**`/`ci/**` e todo pull
request para a `main` passam pelo **GitHub Actions** num runner **Ubuntu
24.04**. O workflow compila com o mesmo `make` do projeto e roda a suíte
inteira no QEMU, sem pular testes: boot, memória, janelas, USB/FAT32, rede,
DHCP, DNS, HTTPS, SSH com OpenSSH real, Cipó, compilador C, Sagui Code e vídeo
H.264/AAC. Os testes de internet (H45, H53 e H63) usam a rede de verdade.

O desenvolvimento continua no **Windows 11**: não é preciso instalar Ubuntu,
WSL, Docker ou QEMU na máquina. A validação Linux acontece no runner do
GitHub. Quando um commit na `main` compila e passa em tudo, o CI publica as
imagens como artefato por 14 dias:

1. abra a aba [**Actions**](https://github.com/erigobeli/sagui-os/actions/workflows/ci.yml);
2. entre na execução da `main`;
3. baixe o artefato `saguios-<commit>` em **Artifacts**;
4. confira o SHA-256 antes de gravar: no Linux, `sha256sum -c saguios.img.sha256`
   (e o do `.vdi`); no PowerShell, `Get-FileHash saguios.img -Algorithm SHA256`
   e compare com o valor do arquivo `.sha256`.

Pull requests e branches de trabalho não publicam artefatos de entrega. O
funcionamento completo, as dependências e o diagnóstico de falhas estão em
[`docs/ci-cd.md`](docs/ci-cd.md).

## Compilar e testar

Requisitos (Ubuntu 24.04): `nasm gcc gcc-multilib binutils make python3
python3-pil qemu-system-x86 qemu-utils imagemagick mtools dosfstools fdisk
procps openssl openssh-client openssh-server vim htop ffmpeg`.

```bash
make            # build/saguios.img
make vdi        # build/saguios.vdi (VirtualBox)
make test       # todos os testes no QEMU (sem janela)
make rodar      # abre no QEMU com janela
make run        # idem, com 768 MB, rede (e1000), som (intel-hda) e USB 2.0; SOM=alsa troca o áudio
```

O CI chama `make` e `python3 tests/rodar_todos.py`; os mesmos comandos
funcionam numa máquina Linux ou no WSL2.

## Estrutura

```
boot/        estágio 1 (MBR) e estágio 2 do carregador, em NASM
kernel/      kernel em C freestanding + NASM: núcleo, memória, drivers, USB, arquivos, janelas
userland/    libsagui e os programas; userland/sagui3d/ é o jogo do bônus B4;
             userland/navegador/ é o Cipó, o navegador; userland/video/ o tocador de vídeo
terceiros/   o pouco código de terceiros dos aplicativos, cada um com licença e ORIGEM.md
rootfs/      arquivos que vão para o disco na RAM (textos, fases do Sagui 3D)
tools/       build (mksgfs.py, mkimagem.py), painel (status.py), verificador de fases
tests/       testes automáticos no QEMU, um por história
docs/        histórias, decisões, diário, erros, roteiros de teste e capturas de tela
dashboard/   painel do projeto (abra dashboard/index.html no navegador)
entrega/     imagens .img e .vdi de cada fase, com SHA-256
testes_eli/  fotos dos testes do Eli no G470
```

## Documentação

| Documento | O que tem |
| --- | --- |
| [`docs/resumo-final.md`](docs/resumo-final.md) | Resumo do projeto, tempo de cada fase e a tabela de conceitos do livro do Tanenbaum preenchida |
| [`docs/decisoes.md`](docs/decisoes.md) | As 30 decisões de projeto e a seção do Sagui 3D |
| [`docs/erros.md`](docs/erros.md) | Os 19 erros encontrados, inclusive os achados no G470 |
| [`docs/diario.md`](docs/diario.md) | O diário do projeto, fase a fase |
| [`docs/historias/`](docs/historias/) | Uma página por história (H01 a H34, B1 a B4) |
| [`status.md`](status.md) e [`dashboard/`](dashboard/index.html) | Situação de cada fase e história |

## As regras do projeto

Do [`prd.md`](prd.md): nada de código de outros sistemas operacionais, nada de GRUB ou outro
carregador pronto, nada de libc ou bibliotecas prontas dentro do sistema, fontes e imagens
criadas no projeto, nomes e comentários em português explicando o porquê, um teste automático
por história e cada fase validada pelo Eli no hardware real antes da próxima.

## O que não foi escrito aqui

O núcleo do SaguiOS (kernel, drivers, rede, TLS, janelas, libsagui, shell,
compilador) é todo escrito no projeto. Nos aplicativos, alguns trechos vieram
de projetos livres (D041, D043), cada um numa pasta de [`terceiros/`](terceiros/)
com a licença original e um `ORIGEM.md`. A imagem de fundo "Mata Atlântica" foi gerada por
IA (trazida no pull request #1). A lista completa, com os dados e as fontes de letra, está em
[`CREDITOS.md`](CREDITOS.md):

| Pasta | De onde | Licença | Quem usa |
| --- | --- | --- | --- |
| `terceiros/font-rs/` | rasterizador do font-rs (Raph Levien, Google) | Apache 2.0 | fontes e Canvas do Cipó |
| `terceiros/libwebp-vp8/` | decodificador VP8 e tabelas do libwebp (Google) | BSD 3 cláusulas + patentes WebM | imagens WebP do Cipó |
| `terceiros/v8-numeros/` | `DoubleToRadixCString` do V8 | BSD 3 cláusulas | JavaScript do Cipó |
| `terceiros/ffmpeg-h264-tabelas/` | tabelas CABAC/CAVLC do FFmpeg | LGPL 2.1 ou posterior | tocador de vídeo |
| `terceiros/faad2-aac-huffman/` | livros de Huffman do FAAD2 (Code from FAAD2 is copyright (c) Nero AG, www.nero.com) | GPL 2 ou posterior | tocador de vídeo |

## Licença

O SaguiOS é software livre, distribuído sob a **GNU General Public License, versão 3**
([`LICENSE`](LICENSE)), a mesma família de licença do Linux.

Copyright (C) 2026 Eli Rigobeli. Código escrito por Claude (Anthropic) para o projeto do Eli Rigobeli.

Na prática: qualquer pessoa pode usar, estudar, modificar, distribuir e até vender o SaguiOS, desde
que entregue o código-fonte completo, com as modificações, sob a mesma licença, e mantenha os créditos.
Ninguém pode fechar o código.
