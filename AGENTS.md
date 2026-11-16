# Instruções para agentes de IA (AGENTS.md)

Este arquivo é para **assistentes de IA** (Claude Code, Codex, Cursor, Copilot,
OpenCode e outros) que forem ajudar alguém a mexer no SaguiOS. Leia tudo antes
de escrever código. As mesmas regras valem para pessoas: veja `CONTRIBUTING.md`.

## O que é o SaguiOS

Um sistema operacional x86 de 32 bits **escrito do zero**, começado pelo Claude
(Anthropic) para o Eli Rigobeli, que roda no QEMU, no VirtualBox e num Lenovo
G470 de 2011. O kernel, os drivers, a pilha de rede, o TLS, o SSH, o navegador,
o compilador C e o visual foram escritos aqui, a partir de **especificações
públicas**. O projeto é GPL 3 (`LICENSE`). O plano está em `prd.md` (versão 1)
e `prd2.md` (versão 2), e cada decisão de projeto em `docs/decisoes.md`.

## Regra principal: nada de código pronto de outros sistemas

**Não copie, não adapte e não reproduza de memória** código de outros sistemas
operacionais ou projetos, mesmo que você "lembre" dele:

- nada do **Linux**, dos BSDs, do Windows, do ReactOS, do Haiku, do MINIX, do
  SerenityOS, do ToaruOS ou de tutoriais de sistemas operacionais (OSDev wiki,
  "James Molloy", "Bran's kernel" e parecidos);
- nada de **drivers** de outros sistemas (por exemplo, o `r8152`, o `atl1c`, o
  `e1000` do Linux), nem de pilhas de rede, TLS (OpenSSL, mbed TLS, BearSSL...),
  SSH (OpenSSH, Dropbear), navegadores, motores de JavaScript, compiladores
  (GCC, Clang, TCC, chibicc) ou decodificadores de imagem e vídeo.

**Pode** (e é o jeito certo):

- escrever o código **do zero**, lendo **especificações e manuais públicos**: RFCs
  da IETF, especificações do USB-IF, manuais da Intel, datasheets dos
  fabricantes, o padrão C, a especificação ELF, a especificação do HTML (WHATWG),
  a do ECMAScript etc. Cite o documento no comentário do arquivo;
- usar **dados** de terceiros (como os certificados raiz em
  `rootfs/sistema/certificados`) quando forem indispensáveis, com licença
  compatível com a GPL 3 e uma linha em `CREDITOS.md`.

### O núcleo é sempre do zero

O **núcleo** do SaguiOS é 100% escrito aqui, sem nenhum código de terceiros:
o kernel (`kernel/`), os drivers, o sistema de arquivos, a rede, o TLS e a
criptografia (`userland/tls`, `userland/cripto`), o SSH, o gerenciador de
janelas, a `libsagui`, o shell, o compilador C (`userland/compilador`) e os
programas básicos do sistema. Essa é a identidade do projeto (D041).

### Aplicativos: do zero é o padrão; código livre de terceiros é exceção

Nos **aplicativos** (navegador, tocadores, visualizadores, jogos...), escrever
do zero continua sendo o padrão. Código de terceiros só entra como exceção,
quando reescrever não faz sentido (um formato de vídeo, um motor de fontes...),
e **só se tudo isto valer**:

1. a licença é **compatível com a GPL 3**: MIT, BSD, Apache 2.0, zlib, ISC,
   LGPL 2.1+/3, GPL 2 "ou posterior", GPL 3 ou domínio público. **Nunca** GPL 2
   "somente" (como o Linux), código sem licença ou código fechado;
2. o código fica **numa pasta própria em `terceiros/<nome>/`**, com o arquivo de
   licença original e um `ORIGEM.md` (de onde veio, qual versão, o que mudou);
3. ele é listado em `CREDITOS.md` e na seção "O que não foi escrito aqui" do
   README;
4. a descrição do pull request diz isso **com todas as letras**.

Código de terceiros **nunca** vai para o núcleo, e nunca é misturado sem
aviso no meio de um arquivo do SaguiOS. Fora do sistema (as ferramentas de
build e de teste em `tools/` e `tests/`), usar Python, GCC, QEMU, OpenSSL ou
Pillow para conferir resultados é normal.

**Atenção, IA:** se um trecho que você está escrevendo veio da sua memória de
um projeto existente (o mesmo algoritmo, a mesma estrutura, as mesmas
constantes "mágicas", as mesmas tabelas), isso **é código de terceiros**. Ou
você o reescreve a partir da especificação e explica como pensou, ou o trata
como código de terceiros (pasta `terceiros/`, licença, crédito). Nunca o
deixe misturado como se fosse original.

## Como o código é escrito aqui

- **Português do Brasil**: nomes de funções e variáveis, comentários, mensagens
  e documentação (`imprimir`, `abrir`, `janela`, não `print`, `open`, `window`).
- **Comentários explicam o porquê**, para um leigo curioso entender. Arquivos
  novos começam com um comentário dizendo o que fazem e de onde veio o
  conhecimento (qual especificação).
- **Sem libc** dentro do sistema: os programas usam a `libsagui`
  (`userland/inc/sagui.h`, `gui.h`); o kernel usa as próprias rotinas.
- **Sem ponto flutuante no kernel** (`-mgeneral-regs-only`). Os programas podem
  usar `float` e `double` desde a versão 2.1 (SSE2, D042); a pilha deles fica
  alinhada em 16 bytes, e quem trocar de pilha precisa manter isso. Nada de
  divisão de 64 bits que precise da libgcc.
- Siga o estilo do arquivo que você está mudando.
- Decisões de projeto importantes vão em `docs/decisoes.md` (uma seção nova,
  numerada); problemas encontrados e como foram resolvidos, em `docs/erros.md`.

## Testes

- `make` compila a imagem (`build/saguios.img`); `python3 tests/rodar_todos.py`
  roda todos os testes no QEMU. **Todo pull request precisa passar neles.**
- Funcionalidade nova vem com **teste novo** em `tests/` (veja os existentes:
  usam `tests/qemu.py` para ligar a máquina, digitar, clicar e tirar capturas).
- Não apague nem enfraqueça testes para fazer o seu código passar.

## Pull requests

- **Um assunto por pull request, pequeno.** Como referência: até umas
  **500 linhas** de código mudadas (sem contar testes e dados gerados), uma
  funcionalidade ou uma correção por vez. Um pull request com milhares de linhas
  não dá para revisar e provavelmente vai ser fechado, com as ideias
  aproveitadas aos poucos. Coisa grande? Abra uma **issue** primeiro, explique o
  plano e mande em partes (por exemplo, "motor de CSS", depois "Canvas").
- **Baseie no `main` atual** e não mexa no que não é do assunto: nada de mudar
  o estilo de arquivos inteiros, renomear coisas sem motivo, trocar a pasta
  `entrega/` (as imagens são geradas pelo mantenedor) ou mudar a ABI das
  chamadas de sistema sem combinar antes numa issue.
- Cada commit com a linha **`Signed-off-by: Nome <email>`** (`git commit -s`):
  é a declaração de origem do código (DCO, a mesma do kernel do Linux), em que a
  pessoa afirma que tem direito de enviar aquele código sob a GPL 3.
- Se a IA ajudou, diga na descrição (e, se quiser, uma linha
  `Co-Authored-By:` da ferramenta).
- **Quem responde pelo pull request é a pessoa que o envia e assina os
  commits.** Não escreva na descrição que a responsabilidade é do mantenedor
  (Eli) ou da IA: o `Signed-off-by` é justamente a declaração de quem envia.
- Preencha o modelo de pull request (`.github/pull_request_template.md`).

## Coisas que não devem ser feitas

- Não escrever no disco interno do G470 (lá estão os dados do Eli): a
  instalação em HD só acontece num disco que o usuário escolher e confirmar.
- Não colocar chaves de API, senhas ou dados pessoais em código, capturas ou
  commits.
- Não usar nomes ou marcas de jogos e produtos existentes nos jogos do sistema.
