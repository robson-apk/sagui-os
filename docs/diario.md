# Diário do SaguiOS

Log cronológico do que foi feito. Uma entrada por bloco de trabalho.

## 2026-11-03

- **H01.** Leitura do PRD. Instalados NASM 2.16, ImageMagick 6.9 e `gcc-multilib`;
  QEMU 8.2.2, Python 3 e mtools já estavam na VPS. Validado o GCC do sistema com
  `-m32 -ffreestanding` (gera ELF32 i386). Criada a estrutura de pastas da seção 5,
  o repositório git, o `status.md`, o painel e a ferramenta `tools/status.py`.
- **H02.** MBR de 512 bytes: salto + BPB falso reservado, leitura por LBA com
  3 tentativas, cabeçalho de boot no LBA 1 como única posição fixa, tabela com
  uma partição ativa. Testado isolado (estágio 2 de mentira que só para) em IDE
  e em USB/EHCI: a mensagem `SaguiOS: carregando...` aparece nos dois.
- **H03.** Estágio 2 com as 6 etapas na tela, unreal mode para copiar acima de
  1 MB, soma de verificação do kernel e do ramdisk, procura do modo VESA. Kernel
  em C: BSS, pilha, cópia da boot_info, saudação e memória. A memória mostrada
  acompanha a RAM da VM (511 MB com 512, 127 MB com 128), prova de que vem do E820.
- **H04.** `.vdi` pelo `qemu-img`; o teste dá boot no próprio `.vdi` e confere
  que ele é idêntico ao `.img`. Imagem mínima de 1 MB (D004). `como-gravar.md`
  e `teste-fase1.md` escritos. Nenhum erro de execução nesta fase: todos os
  testes passaram na primeira rodada completa (ver `docs/erros.md`).
- **Fim da fase 1.** Todos os testes (H01 a H04) passando. Imagens em
  `entrega/fase1/`. Parada obrigatória: aguardando o teste do Eli.

## 2026-11-03 15:22

- **Confirmação do Eli no hardware:** "fase 1 ok". Fase 1 validada no VirtualBox e no G470.
- Foto do Eli no G470: `testes_eli/fase_1_no_notebook.jpg` (cópia em
  `docs/capturas/G470-fase1-tela-final.png`). Boot pelo pendrive (disco 0x80),
  22 setores lidos, A20 já ligada pela BIOS, VESA 1024x768x32 (modo 0x118),
  ramdisk íntegro. Mapa E820 com ~20 regiões e ~8 GB de RAM.
- **Problema visto na foto:** o mapa E820 do G470 é grande e rolou a tela, e a
  saudação e a linha de memória saíram do topo. Correção combinada com o Eli:
  fazer no começo da fase 2 (H04-fix).
- **H04-fix.** Mapa E820 completo só na serial; a tela mostra o resumo e,
  separada, a memória que um kernel de 32 bits consegue usar. Testado com
  6 GB no QEMU (6143 MB no total, 3071 MB abaixo de 4 GB).
- **Fase 2 iniciada.** Mecanismo de autotestes pelo cabeçalho de boot (D006).
- **H05.** GDT e TSS próprias, 256 vetores gerados por `%rep`, despachante que
  devolve o quadro a retomar (D007), tela de erro em português, PIC
  remapeado, PIT a 100 Hz conferido contra o relógio CMOS. Kernel passou a ser
  compilado com strings em Latin-1, traduzidas para a fonte da placa VGA.
- **H06.** Bitmap de páginas até 896 MB e `kmalloc` por classes de tamanho
  (D008). Bug achado na revisão antes do primeiro teste: o elo da lista livre
  sobrescrevia a assinatura do bloco; corrigido usando o campo do cabeçalho.
  Autoteste com 450 blocos passou de primeira com 128 MB e 1 GB.
- **H07.** Teclado ABNT2 completo (D009). O teste digita 25 grupos de teclas e
  compara byte a byte. Descoberta: no QEMU o AltGr é `alt_r`, não `altgr`.
  Captura revelou a faixa da saudação passando de 80 colunas (erro 2).
- **H08.** Leitor do SGFS e resolução de caminhos. Captura revelou que o
  `formatar()` não aceitava largura `*` (erro 3).
- **H09.** Terminal provisório com paginação. O harness digitava `/` com a
  tecla americana (erro 4, do teste, não do kernel).
- **Fim da fase 2.** 9/9 testes passando (H01 a H09). Imagens em
  `entrega/fase2/`. Parada obrigatória: aguardando o teste do Eli.

## 2026-11-03 16:05

- **Confirmação do Eli no hardware:** "fase 2 ok". Fase 2 validada no VirtualBox e no G470.
- Foto do Eli no G470: `testes_eli/fase_2_no_notebook.jpg` (cópia em
  `docs/capturas/G470-fase2-tela-inicial.png`). Saudação no topo (correção
  confirmada), 8135 MB no total, 3017 MB abaixo de 4 GB, 892 MB gerenciados,
  21 regiões no E820. Todos os comandos funcionaram, sem tela vermelha; `/`,
  `?` e `ç` conferidos. Acentos com teclas mortas ainda não testados
  isoladamente no notebook (o Eli avisa se der erro).
- **Fase 3 iniciada** (tag `pre-fase3`). Plano de arquitetura em D010: mapa de
  endereços, chamadas reiniciáveis, herança só de 0-2.
- **H10.** Paginação de 2 níveis, tabelas do kernel compartilhadas, janelas
  para dispositivos, tela de falta de página explicada.
- **H11.** Escalonador round-robin: `AABBAABB...` com duas tarefas que nunca
  cedem a vez. Truque usado: o quadro inicial de uma tarefa do kernel guarda
  o endereço de retorno e o argumento nos campos que o `iret` do anel 0 não lê.
- **H12.** Anel 3, TSS e `int 0x80` com validação de ponteiros; `cli` e
  leitura do kernel no anel 3 encerram só o processo.
- **H13.** Carregador ELF, libsagui, build da userland; `/bin/ola um dois três`.
- **H14.** Árvore de arquivos na RAM com cópia na escrita, descritores,
  terminal como arquivo (modos linha e bruto). Regressão encontrada na H12 e
  corrigida (erro 5); o commit tinha passado porque o script encadeava o
  commit depois de um `grep` (passei a conferir PASSOU explicitamente).
- **H15.** Pipes: 20.000 bytes num buffer de 512, nos dois sentidos.
- **H16.** sagsh com redirecionamento, pipes, `&`, histórico e Tab; 18
  programas com os apelidos; terminal provisório removido; teste da H09
  adaptado ao shell. Achado: o GCC não divide em `1 / x` (erro 6).
- **Fim da fase 3.** 16/16 testes passando. Imagem com 223 KB úteis. Imagens
  em `entrega/fase3/`. Parada obrigatória: aguardando o teste do Eli.

## 2026-11-03 17:21

- **Confirmação do Eli no hardware:** "fase 3 ok". Fase 3 validada no VirtualBox e no G470.
- Foto do Eli no G470: `testes_eli/fase_3_no_notebook.jpg` (cópia em
  `docs/capturas/G470-fase3-contador-ps.png`): `contador 10 &` contando
  enquanto `ps` roda, quatro processos, shell no anel 3. Setas para cima e
  para baixo (histórico) e acentos com teclas mortas conferidos. Pedidos do
  Eli: as setas para os lados não editam a linha (não estava implementado:
  vai na H18, com o terminal gráfico) e o texto de um programa em segundo
  plano se mistura com a linha que está sendo digitada (também na H18).
- **Fase 4 iniciada** (tag `pre-fase4`). Pedidos do Eli registrados.
- **H17.** Console separado em camada (`console_*`) com dois motores. Vídeo
  VESA com buffer duplo de 32 bits, cópia só do que mudou, primitivas.
- **H18.** Fonte própria desenhada em texto (`tools/fonte/sagui8x16.txt`),
  acentuadas compostas pelo gerador; terminal gráfico 128x48; boot
  reordenado para a tela ficar gráfica logo após ligar a memória. O harness
  passou a ler a tela gráfica reconhecendo as células pela própria fonte
  (sem OCR), e todos os testes antigos seguiram valendo. Pedidos do Eli:
  edição com setas no shell e redesenho da linha quando outro processo
  escreve.
- **H19.** Mouse PS/2 (IRQ 12) com cursor próprio. Corrigido um problema
  latente: o tratador do teclado descartava bytes do mouse.
- **Fim da fase 4.** 19/19 testes passando. Imagens em `entrega/fase4/`.
  Parada obrigatória: aguardando o teste do Eli.
- **Teste do Eli no G470 (fase 4):** o touchpad não respondeu e o teclado
  parou. **H19-fix:** módulo `i8042.c` único, fila esvaziada também pelo
  relógio, configuração conferida, porta do mouse desligada se ele não
  responder, diagnóstico na tela (erro 9, D020). Um teste novo com a IRQ 12
  bloqueada reproduz o defeito no QEMU com o código antigo e passa com o
  novo. Na regressão apareceram duas disputas de tempo em testes (erro 10),
  corrigidas. 19/19 passando. Nova imagem em `entrega/fase4/`.

## 2026-11-03 18:37

- **Confirmação do Eli no hardware:** "fase 4 ok". Fase 4 validada no VirtualBox e no G470.
- Foto do Eli no G470 depois da correção: `testes_eli/fase_4_no_notebook.jpg`
  (cópia em `docs/capturas/G470-fase4-grafico-touchpad.png`). Modo gráfico
  1024x768x32 ligado, fonte própria com acentos (`Ação, vovô, pão, ÇÃO`),
  touchpad respondendo (linha `[ ok ] Mouse/touchpad PS/2`) e seta na tela,
  teclado funcionando. A correção H19-fix resolveu.
- **Fase 5 iniciada** (tag `pre-fase5`). Plano em D021.
- **H20.** Área de trabalho: degradê verde-amarelo-azul, mascote e ícones em
  pixel art no código, barra com relógio. Opção de build `--console` para os
  testes das fases anteriores. Achado: tarefa do kernel sempre pronta deixava
  o processo 0 sem CPU; criada a interrupção 0x81 para ceder a vez (erro 11).
- **H21.** Gerenciador de janelas no kernel com pixels compartilhados com o
  programa (páginas "emprestadas"), eventos por janela, arrastar, foco,
  fechar. Captura revelou rastros ao arrastar (erro 12), corrigido.
- **H22.** Kit gráfico na libsagui (botão, caixa, área de texto, lista,
  diálogos) e programa de demonstração.
- **H24.** Terminal em janela com o sagsh por pipes; chamada `disponivel`;
  sequência ESC [ J; comandos que abrem programas gráficos.
- **H25.** Editor com Ctrl+S, título com `*` e aviso ao fechar; chamada
  `registrar` para os programas gráficos não sujarem o terminal.
- **H23.** Arquivos com ícones; abre no Editor; cria e apaga pastas. O teste
  também reabre pelo Arquivos a nota criada no Editor (critério da H25).
- **H26.** Calculadora em ponto fixo com divisão longa própria (D023).
- **H27.** Sagui Pega Banana; o teste joga de verdade, guiado pelo log.
- **H28.** Ícones abrindo os programas e tela Sobre; imagem com 428.544 bytes
  úteis (29% da meta).
- **Fim da fase 5.** 28/28 testes passando. Imagens em `entrega/fase5/`.
  Parada obrigatória: aguardando o teste do Eli.

## 2026-11-03 20:00

- **Confirmação do Eli no hardware:** "fase 5 ok". Fase 5 validada no VirtualBox e no G470.
- Foto do Eli no G470: `testes_eli/fase_5_no_notebook.jpg` (cópia em
  `docs/capturas/G470-fase5-janelas.png`). Área de trabalho com Calculadora,
  Terminal, Editor, Sagui Pega Banana e Sobre abertos ao mesmo tempo;
  Sobre mostra 419,0 KB de imagem, 8 processos e 879 MB livres de 892 MB.
  Detalhe visto na foto: na tela de início do jogo os corações das vidas
  não aparecem (corrigido no começo da fase 6).
- **Fase 6 iniciada** (tag `pre-fase6`). Investigação: o hub do QEMU 8.2 é
  USB 1.1 e não liga no EHCI; o caminho do hub fica para o G470 (D024).
  Corrigidos os corações das vidas na tela de início do jogo (erro 13).
- **H29-H31.** PCI, EHCI tomado da BIOS, transferências síncronas medidas pelo
  FRINDEX, enumeração, hubs, conexão a quente por consulta, Bulk-Only/SCSI.
  O primeiro boot com o driver já enumerou o pendrive e leu o rótulo do FAT32.
  Setores conferidos byte a byte na VPS.
- **H32.** FAT32 com nomes longos e acentos montado em `/usb` pela camada de
  arquivos virtual. Achado: textos de fora vêm em UTF-8; conversão para
  Latin-1 no `cat` e no Editor (D027, erro 14).
- **H33.** Gravação conferida com `mtools` e `fsck.vfat` (íntegro).
- **H34.** Notificação, ícone do pendrive, Arquivos com lateral e "Remover
  com segurança", aviso de remoção sem desmontar (erro 15: notificação
  cortada). A regressão mostrou que o próprio pendrive de boot gerava
  notificação e ícone: agora é ignorado nesses dois lugares.
- A pedido do Eli, o Sobre diz "para o projeto do Eli Rigobeli". Versão 1.0.
- **Fim da fase 6 (final).** Resumo em `docs/resumo-final.md`. Imagens em
  `entrega/fase6/` e `entrega/final/`. Aguardando o teste do Eli.

### Correção da fase 6 (G470)

- **Relato do Eli:** pendrive espetado não aparecia; `usb` mostrava a porta
  ocupada mesmo sem o pendrive; `montar` não achava FAT32. O pendrive tinha a
  partição do SaguiOS e uma FAT32 de ~3 GB criada depois.
- **Causa 1 (erro 17):** o EHCI do G470 é de 64 bits e exige qTD/QH maiores;
  o QEMU é de 32 bits e não mostra o defeito. Corrigido (D028), sem como
  validar no QEMU: o teste real é no G470. O `usb` ganhou diagnóstico por porta.
- **Causa 2 (erro 18):** só a primeira partição era examinada. Corrigido e
  testado no QEMU com o mesmo arranjo do pendrive do Eli.
- **Segundo teste (erro 19):** o diagnóstico novo do `usb` mostrou que a
  correção de 64 bits funcionou (o segundo controlador enumerou o hub e o
  leitor de cartão), mas o primeiro falhava no hub. Causa: os dois
  controladores compartilhavam o mesmo anel de transferências. Reproduzido no
  QEMU com dois EHCI (`h29_dois_controladores`) e corrigido.

## 2026-11-03 22:57

- **Confirmação do Eli no hardware:** "fase 6 ok". Fase 6 validada no VirtualBox e no G470.

### Fase bônus (replanejada pelo Eli)

- Fase 6 aprovada no G470 depois da terceira imagem (erro 19).
- O Eli cancelou o B1 (HD interno) e o B2 (UEFI), manteve o B3 (bip) e criou
  o B4 (Sagui 3D).
- **B3.** Fila de notas tocada pelo tique do relógio (D029). A primeira
  conferência do canal 2 viu 0 viradas no QEMU porque media o tempo por
  número de voltas; passou a medir pelo canal 0.
- **B4.** Primeiro motor do jogo funcionou de cara para as paredes e o céu;
  dois ajustes vistos nas capturas: uma faixa da direita sem desenho (conta
  que estourava 32 bits com uma parede encostada no olho) e o macaco que
  escorregava deslizava para trás do sagui. Na jogabilidade, o macaco que
  pegava uma banana voltava na hora e tirava as três vidas em segundos: agora
  ele sai correndo com a banana.

## 2026-11-04 00:19

- **Confirmação do Eli:** "Funcionou tudo perfeitamente". Fase bônus (B3 bip e B4 Sagui 3D) aprovada; a foto enviada é da VM (`testes_eli/fase_bonus_vm.png`).

## 2026-11-14 23:00

### H87: programas úteis

- Comandos novos no terminal: `head`, `tail`, `sort`, `uniq`, `find`, `du`,
  `less`, `hexdump`, `tar`, `zip`, `unzip` e `captura`. Programas novos em
  janela: Visualizador de imagens e Música (no menu do sagui). O Arquivos
  ganhou a lixeira e abre imagens, músicas e .zip nos programas certos; a aba
  Rede das Configurações ganhou "Testar conexão". Decisão D049.
- Teste novo `h87_utilitarios` (e `h87b_testar_rede`). No caminho apareceram:
  a FAT lista na ordem de gravação (o visualizador e a música agora ordenam
  pelo nome); o zoom pulava de 100% para 126% (passos de 25% agora); a janela
  do visualizador passava da tela em 1024x768 (encolheu); e erros do próprio
  teste (linhas do kernel no meio da saída, o eco do comando fora do lugar,
  os .txt do rootfs em Latin-1 dentro do sistema).

## 2026-11-15 05:00

### H88: revisão do visual

- Vistoria de todos os programas com capturas (`tests/vistoria.py`, fora da
  suíte). Problemas achados: cantos em escada, moldura preta quadrada em volta
  do canto redondo nos efeitos leves, cantos de baixo retos, botões da barra de
  título serrilhados, menu e Configurações sem ícones, texto saindo dos botões.
  Decisão D050.
- Bordas suavizadas (`kernel/inc/suave.h`, para o kernel e a libsagui),
  janelas com os 4 cantos redondos e sombra também nos efeitos leves, brilho no
  vidro só nos completos. 21 ícones novos (tools/mkarte.py, 20x20 para os
  programas). Menu do sagui, Configurações, Arquivos, Música, Visualizador e
  Desligar refeitos.
- Suíte: 82/83 na primeira rodada; o h74_h77 pegou o brilho do vidro nos
  efeitos leves (a barra tem de ficar na cor pura). Corrigido, e o h74_h77
  passou de novo, com h38, h36, h23, h87 e h81. Teste novo `h88_visual`, que
  mede nos pixels: canto de cima suavizado, canto de baixo redondo, ícones nas
  7 abas e nos 13 itens do menu, botões de efeitos.

## 2026-11-15 13:20

### H88b e H89: doca redonda e menu Iniciar

- Doca: a sombra era quadrada. Fora da curva o fundo ficava claro, cercado de
  sombra, e a quina aparecia. Agora `vidro_sombra_r` e `vidro_pintar_r`
  recebem o raio, a doca usa raio 18, e a sombra das janelas também ficou
  arredondada.
- Rede: no QEMU do Mac, o DNS falhava ("sem rede ou sem resposta"), mas o
  ping no roteador 10.0.2.2 respondia. A causa está fora do SaguiOS: o DNS
  virtual do QEMU (10.0.2.3) repassa as consultas para o `/etc/resolv.conf`
  do Mac (1.1.1.1), e o 1.1.1.1 não responde nessa rede (o `dig` do próprio
  Mac dá tempo esgotado). Com os mesmos parâmetros no X79, o Google abre
  (200, TLS conferido).
- Laço de seleção translúcido na área de trabalho: ele existiu no commit
  0b3c462, nos ramos `area-lixeira`, `cipo-web` e `js-tamanho`, que são do
  código antigo. Nunca entrou no main. O H88 não removeu nada.
- Menu Iniciar com todos os programas, busca e categorias (D051). Teste novo
  `h89_menu_iniciar`: abre o menu, busca "musica", Enter abre a Música,
  categoria Jogos, clique na Banana e Esc fecha. O `h88_visual` agora confere
  ícones nas 17 células da grade, com 3 células vazias como controle.
- Suíte completa: 84/85. Falhou o `b4_sagui3d` ("a terceira banana abre a
  porteira"). Rodadas avulsas: a imagem nova passou 9 de 14, e a do H88
  (bd59cd2) passou 8 de 8, alternando as duas. Os quadros por segundo do jogo
  são parecidos (51 contra 50 a 57), e o tempo do quadro inteiro também (27 ms
  contra 25 ms). Na falha, o sagui passa da porteira sem comer a terceira
  banana. A causa ainda não foi achada; fica em aberto. A raiz quadrada da
  sombra nova era um laço de até cerca de 200 passos por pixel; virou bit a bit
  e só roda nos cantos, mas isso não resolveu.
