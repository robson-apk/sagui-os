# Decisões do SaguiOS

Toda decisão que o PRD não fixa, com o motivo. Também registra como cada parte
central foi pensada (prova de autoria, PRD seção 2.1).

## D001. Compilador: GCC do sistema com `-m32` freestanding (H01)

**Decisão:** usar o GCC 13 do Ubuntu com `-m32 -ffreestanding -nostdlib -fno-pic
-fno-stack-protector -mgeneral-regs-only`, e o `ld` com `-m elf_i386`, em vez de
compilar um `i686-elf-gcc`.

**Motivo:** o PRD aceita as duas opções. O GCC do sistema já está instalado,
gera código i386 correto com essas flags e evita uma hora de compilação de
binutils + GCC. O risco conhecido (o compilador assumir coisas do Linux) é
coberto pelas flags: sem libc, sem PIC, sem proteção de pilha, sem SSE/x87
(`-mgeneral-regs-only` garante que o kernel nunca toca a FPU, o que combina com a
decisão do PRD de não usar ponto flutuante).

## D002. Painel lido por `status.js`, não só por `status.json` (H01)

**Decisão:** `tools/status.py` grava `dashboard/status.json` (fonte única) e uma
cópia `dashboard/status.js` (`window.SAGUI_STATUS = {...}`).

**Motivo:** o Eli abre o painel direto do disco (baixado por FTP). Navegadores
bloqueiam `fetch()` de arquivos locais (`file://`), mas carregam `<script src>`.
O painel tenta o `.js` e, se não houver, cai para `fetch("status.json")`.

## D003. Carregador de boot: como foi pensado (H02, H03)

**Problema.** A BIOS só traz 512 bytes para a memória, em modo real, e só a
BIOS sabe ler o pendrive. O kernel precisa estar acima de 1 MB e rodar em modo
protegido, onde a BIOS não existe mais.

**Divisão em dois estágios.** O MBR faz o mínimo que cabe em 356 bytes de
código: guarda o disco (DL), lê um *cabeçalho de boot* no LBA 1 e, com ele, o
estágio 2. Todo o resto vai para o estágio 2, que pode crescer até 32 KB. O
cabeçalho é a única posição fixa; os demais LBAs, tamanhos e endereços são
calculados pelo `tools/mkimagem.py`. Assim mexer no kernel nunca exige mexer
no MBR.

**Formato do MBR.** Os primeiros 90 bytes imitam o início de um setor com BPB
(salto curto + NOP + área zerada). Motivo: algumas BIOS, ao dar boot por USB,
"corrigem" a geometria escrevendo nessa área; se houvesse código ali, ele
seria corrompido. A tabela de partições tem uma única partição ativa (0x80)
cobrindo a imagem a partir do LBA 1, porque várias BIOS só tratam um pendrive
como disco rígido (e não como disquete) quando encontram uma partição ativa
válida. O tipo escolhido é 0xDA ("dados sem sistema de arquivos"), para que
nenhum sistema tente montar o pendrive de boot.

**Leitura por LBA.** Só `INT 13h AH=42h`. CHS seria mais compatível com BIOS
muito antigas, mas o G470 (2011) e as VMs têm as extensões, e CHS dependeria
da geometria inventada pela BIOS para o pendrive, uma fonte clássica de
problemas. O MBR verifica as extensões (AH=41h) e avisa na tela se faltarem.
Cada leitura tem 3 tentativas com reset do disco entre elas.

**Acima de 1 MB: unreal mode.** O estágio 2 lê blocos de 32 KB num buffer em
0x10000 e copia cada um para o destino final. Para copiar acima de 1 MB sem
sair do modo real (precisamos da BIOS para o próximo bloco), entramos no modo
protegido só pelo tempo de carregar DS e ES com um descritor de 4 GB e
voltamos. O processador mantém o limite em cache. Isso é refeito antes de cada
cópia, porque uma rotina da BIOS pode ter recarregado os segmentos. A outra
opção (carregar tudo abaixo de 0x9F000) limitaria kernel + ramdisk a ~570 KB,
e a fase 5 vai passar disso.

**Onde fica o ramdisk.** Logo depois do fim da BSS do kernel, alinhado em
4 KB. O build lê o símbolo `_kernel_fim` do ELF. Se o ramdisk ficasse logo
depois do *arquivo* do kernel, o kernel apagaria o começo do ramdisk ao zerar
a própria BSS.

**Integridade.** O cabeçalho guarda uma soma simples dos bytes do kernel e do
ramdisk; o estágio 2 confere antes de saltar. No G470 não há serial: um
"dados lidos estão corrompidos" na tela é muito mais útil que uma tela preta.

**Progresso na tela.** `[1/6]` a `[6/6]`, como pede a seção 8 do PRD.

**A20.** Primeiro verifica se já está ligada (comparando 0x000500 com
0x100500); depois tenta, em ordem, o serviço da BIOS, a porta 0x92 e o
controlador de teclado, conferindo após cada um. O método usado vai na
boot_info e aparece na tela do kernel, para sabermos qual funcionou no G470.

**VESA.** O estágio 2 já percorre a lista de modos e escolhe o melhor
(1024x768 > 800x600 > 640x480; 32 bits > 24 bits; exige framebuffer linear e
cor direta), mas só troca de modo se o bit `OPC_VESA` do cabeçalho estiver
ligado, o que o build fará a partir da fase 4. A troca é a última coisa antes
do modo protegido, porque depois dela o texto da BIOS some.

## D004. Tamanho dos arquivos de imagem (H04)

**Decisão:** o `saguios.img` tem no mínimo 1 MB (e depois cresce em múltiplos
de 32 KB); o disco do `saguios.vdi` é arredondado para múltiplos de 1 MB.

**Motivo:** a primeira imagem tinha 32 KB. BIOS e VirtualBox inventam uma
geometria (cilindros, cabeças, setores) a partir do tamanho do disco, e um
disco menor que um cilindro é um caso pouco testado neles. 1 MB evita isso e
continua abaixo da meta de 1,44 MB. A meta do PRD é medida pelos **bytes
úteis** (MBR + cabeçalho + estágio 2 + kernel + ramdisk), que o build grava em
`build/tamanho.txt` e o painel mostra.

## D005. `.vdi` gerado pelo `qemu-img`, testado no QEMU (H04)

**Decisão:** o `.vdi` é convertido com `qemu-img convert -f raw -O vdi`, como
pede o PRD, e o teste da H04 dá boot no próprio `.vdi` pelo QEMU
(`-drive format=vdi`).

**Motivo:** a VPS não tem virtualização aninhada, então não há VirtualBox
para testar aqui. Dar boot no `.vdi` prova que a conversão preservou a
imagem byte a byte; o teste no VirtualBox de verdade fica no roteiro do Eli.

## D006. Autotestes ligados pelo cabeçalho de boot (fase 2)

**Decisão:** o cabeçalho de boot tem um campo de 16 bytes (`CAB_TESTE`) com o
nome de um autoteste. O estágio 2 copia para a boot_info e o kernel, se o
campo não estiver vazio, roda `autoteste_executar(nome)` depois de iniciar.
Os testes geram imagens próprias com `mkimagem.py --teste NOME`.

**Motivo:** critérios como "dividir por zero mostra a tela de erro" ou
"alocar e liberar sem corromper" precisam de código que nunca deve rodar na
imagem do Eli. Recompilar o kernel para cada teste seria lento e testaria um
binário diferente do entregue. Assim o kernel testado é exatamente o mesmo;
só muda um campo do cabeçalho.

## D007. Interrupções: como foi pensado (H05)

**Um trecho por vetor, gerado por repetição.** A CPU não informa qual vetor
disparou, e só algumas exceções empilham código de erro. Cada um dos 256
vetores tem um trecho de 2-3 instruções, gerado por `%rep` no NASM, que iguala
o formato (empilha 0 quando não há código de erro) e empilha o número do
vetor. Um único trecho comum salva o resto. A tabela com o endereço de cada
trecho é exportada para o C, que monta a IDT num laço.

**O despachante devolve o quadro a retomar.** `tratar_interrupcao(quadro)`
retorna um ponteiro de quadro, e o assembly faz `mov esp, eax` antes de
restaurar os registradores. Hoje sempre devolve o mesmo quadro. Na fase 3, a
troca de processo vai ser só o escalonador devolver o quadro de outro
processo: não precisa de uma rotina separada de troca de contexto.

**Portas de interrupção, não de "trap".** IF é desligada na entrada, então o
kernel não é interrompido no meio de outra interrupção. É mais simples de
raciocinar e o kernel é pequeno o bastante para isso não atrasar nada.

**PIC.** Remapeado para 0x20-0x2F (os vetores 8-15 originais colidem com
exceções da CPU), tudo mascarado, cada driver habilita a sua linha. IRQ 7 e 15
espúrias são reconhecidas pelo registrador de serviço e não recebem EOI.

**Tela de erro.** Nome da exceção em português, código de erro, todos os
registradores e CR0/CR2/CR3, e um pedido para fotografar: no G470 a foto é a
única ferramenta de depuração.

**Texto em Latin-1 também no modo texto.** O kernel é compilado com
`-fexec-charset=ISO-8859-1`, então as mensagens podem ter acentos. A fonte
da placa VGA segue a página de código 437; o driver traduz o que existe nela
(ç, é, á, ó, ...) e troca pela letra sem acento o que não existe (ã, õ).

## D008. Memória física e alocador do kernel: como foi pensado (H06)

**Até onde o kernel gerencia.** O kernel é de 32 bits sem PAE, então nunca
enxerga memória acima de 4 GB (no G470, com ~8 GB, metade fica de fora; a
tela mostra os dois números). Indo além: na fase 3, com a paginação, os
programas vão morar a partir de 0x40000000 (PRD 4), e o kernel vai ficar
mapeado 1:1 (endereço virtual = físico) abaixo disso em todos os espaços de
endereço, para poder ler e escrever qualquer página física sem truques. A
faixa 0x38000000-0x3FFFFFFF fica reservada para o kernel mapear coisas que
estão em endereços físicos altos (framebuffer de vídeo, registradores do
USB). Por isso o gerenciador de páginas só entrega memória abaixo de
**0x38000000 (896 MB)**. É muito mais do que o SaguiOS precisa, e em troca
o kernel nunca precisa de mapeamento temporário para tocar numa página.

**Bitmap de páginas de 4 KB.** Um bit por página, 1 = ocupada. Começa tudo
ocupado; liberamos só as regiões "livres" do E820 (arredondadas para dentro,
para nunca entregar meia página de uma região reservada); depois marcamos de
novo como ocupados o primeiro 1 MB (BIOS, tabelas, nossas estruturas de
boot), o kernel com a BSS e o ramdisk. 896 MB / 4 KB = 229.376 bits = 28 KB
de bitmap, que fica na BSS. A busca olha 32 páginas de uma vez (uma palavra
inteira cheia = nada livre ali) e continua de onde parou da última vez.

**`kmalloc` em duas faixas.**
- *Pequenos (até 2 KB):* oito classes de tamanho em potência de 2 (16 a
  2048 bytes, cabeçalho incluído). Cada classe tem uma lista de blocos
  livres; quando esvazia, o alocador pega uma página e a corta em blocos
  daquela classe. Liberar devolve o bloco à lista da classe, em tempo
  constante. As páginas das classes não voltam para o bitmap: a memória de
  estruturas pequenas do kernel tende a ser reutilizada pela mesma classe.
- *Grandes (mais de 2 KB):* páginas contíguas direto do bitmap, devolvidas ao
  bitmap no `kfree`.

Cada bloco tem um cabeçalho de 8 bytes com uma assinatura e a classe (ou o
número de páginas). O `kfree` confere a assinatura e troca por outra ao
liberar: liberar duas vezes ou liberar um ponteiro que não veio do `kmalloc`
é detectado e registrado, em vez de corromper as listas em silêncio.

Outras opções pensadas: lista única de blocos com divisão e junção de
vizinhos (gasta menos memória, mas cada `kfree` precisa percorrer a lista e
um erro corrompe tudo) e alocador por "buddy" (mais complexo do que um
kernel deste tamanho precisa).

## D009. Teclado ABNT2: como foi pensado (H07)

**Do byte à tecla.** O driver só lê a porta 0x60 na IRQ 1 e entrega bytes a
uma máquina de estados pequena: prefixo 0xE0 (tecla estendida), soltar
(bit 7), modificadores (Shift, Ctrl, Alt, AltGr = Alt da direita, Caps Lock,
Num Lock) e, por último, a tradução pelo mapa. Bytes de resposta do teclado
(0xFA, 0xFE) e bytes que o controlador marca como vindos do mouse são
ignorados, porque na fase 4 os dois dividem o mesmo controlador.

**Um código só para tudo.** O buffer guarda inteiros de 16 bits: 1-255 é um
caractere Latin-1 já pronto (com acento), e de 0x100 em diante são teclas sem
caractere (setas, Home, F1...). Ctrl+letra vira o código de controle (Ctrl+C
= 3), como em terminais tradicionais. Assim o terminal, o editor e o jogo
leem do mesmo jeito.

**Mapa como tabela de pares.** `mapa[scan][0/1]` = sem e com Shift. As teclas
mortas são o próprio acento marcado com um bit alto. Caps Lock inverte o
Shift só nas letras e no ç. A tecla morta fica guardada; a próxima tecla
procura numa tabela pequena de combinações (acento, letras base, resultados).
Sem combinação, sai o acento e depois a letra, como pede o PRD; acento +
espaço dá o próprio acento.

**Teste.** O teste digita pelo monitor do QEMU, que nomeia teclas pela
posição física (a tecla do ';' americano é o ç no ABNT2), cobrindo todas as
linhas da tabela do PRD 4.4. Detalhe encontrado: o nome `altgr` do QEMU não
gera o Alt da direita; o certo é `alt_r`.

## D010. Plano da fase 3: espaços de endereço, processos e chamadas (H10 a H16)

**Mapa de endereços virtuais (igual em todos os processos):**

| Faixa | Uso |
| --- | --- |
| 0x00000000-0x37FFFFFF | Kernel: mapeamento 1:1 da memória física (só anel 0). A página 0 fica sem mapeamento, para pegar ponteiro nulo. |
| 0x38000000-0x3FFFFFFF | Kernel: janelas para dispositivos em endereços físicos altos (vídeo, USB), fases 4 a 6. |
| 0x40000000-0xBFFFFFFF | Programa do usuário: código a partir de 0x40000000 (PRD 4), dados, área que cresce com `aumentar_memoria`, e a pilha no topo. |

As tabelas de páginas do kernel (0 a 0x3FFFFFFF) são criadas uma vez no boot,
e todo diretório de processo aponta para as mesmas tabelas. Assim o kernel
aparece igual em todo espaço de endereço sem copiar nada, e uma janela nova
(ex.: o framebuffer) vale para todos os processos na hora.

Páginas de 4 KB em tudo, inclusive no kernel. Páginas de 4 MB gastariam
menos tabelas (1 MB de tabelas hoje), mas uma página grande que cubra regiões
de tipos diferentes de memória é um caso que o manual da Intel deixa
indefinido, e o mapa do G470 tem regiões reservadas no meio da RAM
(0x20000000 e 0x40000000).

**Chamadas de sistema reiniciáveis.** Como toda interrupção passa pelo
despachante que devolve o quadro a retomar (D007), uma chamada que precisa
esperar (ler de um pipe vazio, esperar um filho, ler do teclado) não precisa
de uma pilha "congelada" no meio do kernel: ela marca o processo como
bloqueado, **volta o EIP do processo 2 bytes** (o tamanho de `int 0x80`) e
devolve o quadro de outro processo. Quando o processo for acordado, ele
executa o `int 0x80` de novo, com os mesmos registradores, e desta vez a
condição está satisfeita. Chamadas que não devem ser repetidas (`dormir`)
gravam o resultado antes de bloquear e não voltam o EIP. Consequência: o
kernel nunca é interrompido no meio de uma chamada (a porta 0x80 desliga as
interrupções), e não existe troca de contexto dentro do kernel, só na saída
de uma interrupção. É bem menos código e menos lugar para erro.

**Herança de descritores.** Não há `fork`. `executar` cria o processo direto
a partir do ELF, e o filho herda só os descritores 0, 1 e 2 do pai (entrada,
saída e erro), além da pasta atual. O shell monta redirecionamentos e pipes
assim: guarda a própria saída com `duplicar`, coloca o arquivo ou o pipe no
lugar, chama `executar` e restaura. Herdar só 0-2 evita o problema clássico
de um processo segurar sem querer a ponta de escrita de um pipe e o leitor
nunca ver o fim do arquivo.

**Terminal.** A entrada 0 lê o teclado em modo "linha" (o kernel edita a
linha, ecoa, Backspace funciona, Ctrl+D no começo da linha = fim de arquivo)
ou em modo "bruto" (cada tecla; teclas especiais viram ESC + uma letra), que
o shell usa para ter histórico e completar com Tab.

**Chamadas extras além do PRD 4.5** (registradas aqui porque o PRD lista o
mínimo): `aumentar_memoria` (a memória dos programas, usada pelo `malloc`
da libsagui), `pasta_atual` (para o `pwd`), `renomear` (para o `mv`),
`modo_terminal` (linha/bruto, para o shell) e `limpar_tela`.

## D011. Processos e escalonador: como foi pensado (H11)

**Troca de contexto sem rotina própria.** Graças ao despachante que devolve o
quadro a retomar (D007), trocar de processo é: guardar o ponteiro do quadro
atual no processo, escolher outro, trocar o CR3 se o espaço for diferente,
ajustar o `esp0` da TSS (pilha do kernel do próximo programa no anel 3) e
devolver o quadro dele. O `iret` no fim do trecho comum faz o resto.

**O processo 0 é o próprio boot.** Em vez de criar uma tarefa ociosa, o
código que terminou a inicialização vira o processo 0. Ele só é escolhido
quando ninguém mais está pronto, e fica em `hlt`.

**Tarefas do kernel começam por um quadro montado à mão.** O `iret` para o
anel 0 desempilha só EIP, CS e EFLAGS; os dois campos seguintes do quadro
(`esp_usuario`, `ss_usuario`), que só existem para o anel 3, ficam no topo
da pilha quando a função começa. Usamos isso de propósito: um vira o
endereço de retorno (para uma rotina que encerra a tarefa) e o outro, o
argumento da função.

**Round-robin com fatia de 2 tiques (20 ms)**, procurando o próximo pronto
na tabela a partir do atual, dando a volta. Se o ocioso está rodando e alguém
fica pronto (ex.: uma tecla chegou), a troca acontece no tique seguinte, sem
esperar a fatia.

**Fim de processo em duas etapas.** Ao terminar, a memória do programa e os
arquivos são liberados na hora, mas a pilha do kernel e a entrada na tabela
ficam (estado zumbi) até o pai recolher o código de saída com `esperar`. Não
dá para liberar a pilha do kernel na hora: estamos rodando nela até o
escalonador devolver o quadro de outro processo. Órfãos passam para o
processo 0 e são recolhidos pelo próprio escalonador.

## D012. Anel 3 e chamadas de sistema: como foi pensado (H12)

**Entrar no anel 3 é só um quadro diferente.** O processo novo recebe um
quadro com CS e SS de usuário (seletores com nível 3), a pilha do programa e
o ponto de entrada. Quando o escalonador o escolhe, o mesmo `iret` de sempre
troca de anel. O EFLAGS começa com IOPL=0: `cli`, `sti`, `in` e `out` no
anel 3 geram proteção geral.

**A TSS só serve para uma coisa:** dizer à CPU qual pilha do kernel usar
quando um programa no anel 3 é interrompido. O escalonador atualiza o
`esp0` a cada troca.

**Erro de programa x erro do kernel.** O despachante olha o nível de
privilégio do CS salvo no quadro: exceção vinda do anel 3 encerra só aquele
processo, com uma linha explicando o motivo; vinda do anel 0, tela vermelha.

**Validação de ponteiros.** Todo ponteiro vindo de um programa passa por
`usuario_ok`: o intervalo inteiro tem que estar na área dos programas
(0x40000000 a 0xBFFFFFFF) e cada página tem que estar mapeada com o bit de
usuário (e de escrita, quando o kernel vai escrever). Textos são copiados
para o kernel conferindo cada página que o texto atravessa. Só depois disso
o kernel lê ou escreve, direto pelo endereço virtual, já que o CR3 em uso é
o do próprio processo.

**Testes sem carregador ELF.** A H12 vem antes do carregador (H13). Os
programas de teste são três trechos de assembly independentes de posição,
dentro do kernel, que o autoteste copia para a página 0x40000000 de um
espaço novo.

## D013. Carregador ELF e libsagui: como foi pensado (H13)

**O carregador só precisa dos segmentos.** Dos cabeçalhos ELF, o kernel lê
apenas a lista de segmentos PT_LOAD: endereço, bytes do arquivo, bytes na
memória e se é gravável. Cada segmento ganha páginas novas no espaço do
processo (somente leitura se o ELF não pedir escrita) e os bytes são
copiados pelo endereço físico, aproveitando o mapeamento 1:1 do kernel.
Seções, símbolos e relocação são ignorados: os programas são estáticos e
ligados em 0x40000000. Tudo é conferido contra o tamanho do arquivo e contra
os limites da área dos programas antes de qualquer cópia; um arquivo
estranho é recusado com `ERRO_FORMATO`.

**Argumentos na pilha.** Os textos são copiados para o kernel antes de
criar o processo (o espaço do pai pode mudar), e depois gravados no topo da
pilha do filho, seguidos do vetor de ponteiros, de argv e de argc. A
`_inicio` da libsagui lê `[esp]` e `[esp+4]`.

**libsagui.** Uma função por chamada de sistema; texto e memória; `aloca`
com blocos contíguos numa área que cresce com `aumentar_memoria` (busca do
primeiro que couber, juntando vizinhos livres durante a busca, um desenho
diferente do `kmalloc` do kernel de propósito: os dois têm necessidades
diferentes); `imprimir` com o mesmo método do formatador do kernel;
`ler_linha` byte a byte, para não consumir dados de um pipe que pertencem
ao próximo leitor.

**Build.** Cada `userland/programas/X.c` vira `/bin/X` e cada
`userland/testes/X.c` vira `/testes/X`. O ramdisk é montado a partir de uma
cópia de `rootfs/` com esses programas, então a pasta `rootfs/` do
repositório só tem os arquivos escritos à mão.

## D014. Camada gravável e arquivos abertos: como foi pensado (H14)

**Árvore na RAM com cópia na escrita.** No boot, cada entrada do SGFS vira
um nó numa árvore (pasta -> lista de filhos, em ordem alfabética, para o
`ls` sair ordenado sem ordenar). Um arquivo aponta para seus bytes dentro do
ramdisk; na primeira escrita ganha uma cópia própria no heap do kernel, que
cresce dobrando de tamanho. Criar, apagar e renomear só mexem na árvore.
Assim a "camada gravável por cima do SGFS" do PRD não precisa de duas
estruturas (uma de leitura e uma de escrita) combinadas a cada busca: há
uma só, e o SGFS é só o ponto de partida.

**Apagar um arquivo aberto.** O nó sai da pasta na hora, mas os dados ficam
até o último descritor fechar (o nó conta quantos arquivos abertos apontam
para ele).

**Três níveis: descritor -> arquivo aberto -> nó.** O descritor é só um
índice na tabela do processo. O "arquivo aberto" guarda tipo (nó, terminal,
ponta de pipe), modo e posição, e conta referências, porque `duplicar` e
`executar` fazem dois descritores (às vezes de processos diferentes)
compartilharem a mesma posição. É o que faz `echo a > x` seguido de outro
comando escrever no lugar certo.

**Terminal como arquivo.** Entrada 0 e saídas 1 e 2 do primeiro programa
são um arquivo aberto do tipo terminal; todos os outros herdam. O modo linha
edita no kernel; o modo bruto entrega tecla a tecla (D010).

**Proteções.** Não se remove pasta com conteúdo, nem a raiz, nem a pasta em
que algum processo está. Mover uma pasta para dentro dela mesma é recusado.

## D015. Pipes: como foi pensado (H15)

**Buffer circular de 512 bytes por pipe**, pequeno de propósito: qualquer
transferência real cruza a borda muitas vezes, e o teste de 20.000 bytes
exercita o bloqueio dezenas de vezes.

**Um canal só para leitores e escritores.** Os dois lados dormem no próprio
pipe. Ler ou escrever acorda todos, e cada um repete a chamada (D010) e
confere a condição de novo. Acordar alguém que ainda não pode continuar
custa uma volta a mais, mas dispensa duas filas e a lógica de quem acorda
quem.

**Escrita parcial.** Com espaço para só parte dos dados, o kernel escreve o
que cabe e devolve quanto foi; não bloqueia no meio. A `escrever_tudo` da
libsagui insiste no resto. Assim nenhuma chamada fica "pela metade" dentro
do kernel, o que combina com as chamadas reiniciáveis.

**Fim de arquivo** = pipe vazio e nenhum escritor. **Pipe quebrado** =
escrever sem nenhum leitor. Os dois dependem de contar as pontas abertas,
por isso o filho herda só 0, 1 e 2 (D010) e o shell fecha suas cópias das
pontas logo depois de criar os processos.

## D016. O shell sagsh: como foi pensado (H16)

**Só é interno o que precisa mudar o próprio shell:** `cd`/`entrar`,
`pwd`/`onde` (a pasta é do processo do shell), `clear`/`limpar`, `ajuda`,
`reiniciar` e `sair`. Todo o resto é programa em `/bin`, um arquivo `.c`
por comando. Os apelidos em português são resolvidos por uma tabela no
shell, que também alimenta o `ajuda` e o completar com Tab, então um nome
novo aparece nos três lugares de uma vez.

**Pipes e redirecionamento sem fork.** Para cada etapa de um pipeline, o
shell coloca em 0 e 1 o que aquela etapa deve usar (arquivo, ponta de pipe
ou o terminal guardado), chama `executar` e passa para a próxima; no fim
restaura 0 e 1 e fecha as pontas que sobraram nele. Só depois disso espera
os filhos: se esperasse cada um logo após criá-lo, o primeiro poderia
travar com o pipe cheio antes de o segundo existir.

**Edição da linha no próprio shell**, com o terminal em modo bruto:
histórico de 32 linhas (setas), Tab que completa comandos (com apelidos) ou
nomes de arquivos (acrescentando `/` em pastas), Ctrl+C descarta a linha,
Ctrl+L limpa a tela. A tela de texto apaga com `\b`, então trocar a linha
pelo histórico é "apagar n caracteres e escrever a nova".

**Segundo plano (`&`).** O shell não espera e mostra o PID. Antes de cada
prompt recolhe, sem bloquear (`esperar` com a opção nova), os filhos que já
terminaram, e avisa. Assim `contador 10 &` roda enquanto outros comandos
são usados, que é a demonstração de multitarefa pedida no PRD.

**O kernel reabre o shell** se ele for fechado com `sair`.

**Um detalhe do compilador.** O primeiro `falha divisao` não dividia: o
GCC troca `1 / x` por comparações (o resultado só pode ser -1, 0 ou 1) e a
CPU nunca executa a divisão. O programa agora divide 12345 por zero.

## D017. Vídeo VESA: como foi pensado (H17)

**Buffer de trás sempre em 32 bits (0x00RRGGBB).** A placa pode usar 24 ou
32 bits e qualquer ordem de cores (o VBE informa a posição de cada canal).
Em vez de cada rotina de desenho conhecer o formato, tudo é desenhado num
buffer na RAM com um formato só, e a conversão acontece na cópia para a
placa. Quando o formato da placa já é igual (o caso do QEMU e o esperado no
G470), a cópia é um `memcpy` por linha.

**Copiar só o que mudou.** As rotinas de desenho aumentam um "retângulo
sujo"; `video_mostrar` copia só ele. Um caractere digitado no terminal
copia 8x16 pixels, e não a tela inteira de 3 MB.

**O framebuffer entra pela janela do kernel** (0x38000000, D010): como as
tabelas dessa faixa são compartilhadas, a tela fica acessível em qualquer
processo sem trocar nada.

**O cursor do mouse é desenhado na cópia**, por cima, direto na placa, e
nunca no buffer de trás. Mover o cursor é só refazer, a partir do buffer de
trás, os pixels de onde ele estava. Assim nada que é desenhado precisa saber
que existe um cursor.

**Sem gráfico, volta ao texto.** Se o estágio 2 não achou um modo aceitável,
ou o `4F02` falhou, `vesa_ativo` fica 0 e o console continua no modo texto
(aviso na tela a partir da H18).

## D018. Fonte e terminal gráfico: como foi pensado (H18)

**A fonte é desenhada em texto.** `tools/fonte/sagui8x16.txt` tem cada
caractere como uma grade de `#` e `.`, legível e editável por qualquer
pessoa. O `tools/mkfonte.py` converte para C no build. Estilo: traços de 2
pixels, letras em 7 colunas (a 8ª separa), maiúsculas com 10 linhas,
minúsculas com 7, descendentes de 2.

**As acentuadas são compostas, não desenhadas uma a uma.** O gerador junta
a letra base com um acento de 2 linhas: nas maiúsculas, nas linhas 0 e 1 da
célula (que a fonte deixa livres); nas minúsculas, logo acima da altura do
"x"; no i, sobre um "ı" sem ponto; a cedilha embaixo. São 60 letras do
Latin-1 a partir de 7 peças, e todas ficam coerentes entre si.

**Terminal = grade de células.** Cada célula guarda caractere e cores, para
o cursor poder ser desenhado (cores trocadas) e apagado sem perder o que
estava embaixo. Rolar = mover os pixels uma linha de texto no buffer de trás.

**Sequências de controle próprias e mínimas:** `ESC [ D` (esquerda),
`ESC [ C` (direita) e `ESC [ K` (apagar até o fim da linha), nos dois motores
(texto e gráfico). É o mínimo para o shell editar no meio da linha, pedido
do Eli no teste da fase 3.

**Programa em segundo plano escrevendo enquanto o shell edita.** O terminal
sabe quem o pôs em modo bruto. Se outro processo escreve nesse meio-tempo,
o texto começa numa linha nova e o shell recebe `ESC R`, que o faz
redesenhar o prompt e o que já estava digitado (também pedido do Eli).

**Testes leem a tela gráfica pela própria fonte.** Como a grade é exata e a
fonte é nossa, o harness reconhece cada célula de 8x16 da captura comparando
com os bitmaps da fonte: é uma leitura exata, sem OCR, e por isso os testes
das fases anteriores continuam valendo no modo gráfico.

**Boot reordenado.** Interrupções, memória e paginação são ligadas antes de
qualquer texto, porque o terminal gráfico precisa do `kmalloc` e da janela do
kernel. As mensagens são impressas depois, já no modo gráfico.

## D019. Mouse PS/2: como foi pensado (H19)

**Mesmo controlador do teclado, portas separadas na leitura.** O i8042
avisa no bit 5 do estado se o byte que está esperando é do mouse. O driver
do teclado passou a **parar sem ler** quando vê esse bit (antes ele lia e
descartava, o que roubaria pacotes do mouse), e o do mouse só lê bytes com
esse bit ligado. A inicialização do mouse é feita com as interrupções
desligadas, para nenhum dos dois tratadores pegar as respostas (0xFA) que o
próprio driver está esperando.

**Pacotes de 3 bytes com ressincronização.** O bit 3 do primeiro byte é
sempre 1; se um byte se perder, o driver descarta até achar um byte que
possa ser o primeiro. Pacotes com os bits de estouro são ignorados (o valor
do deslocamento não é confiável).

**O cursor é do vídeo, não do mouse.** O driver só informa a posição; o
desenho da seta é feito na cópia para a tela (D017), por cima de tudo.

**Fila de eventos.** Cada pacote vira um evento (posição, deslocamento,
botões e quais mudaram) numa fila, com um canal para quem quiser dormir
esperando por ele. Na fase 5 o gerenciador de janelas consome essa fila.

## D020. Um dono só para o controlador do teclado e do touchpad (H19-fix)

**O que aprendemos no G470.** O controlador i8042 tem uma fila de saída de
um único byte, compartilhada pelo teclado e pelo touchpad. Se um byte não
for lido, nada mais passa. Na H19, o teclado e o mouse liam o controlador
cada um por conta própria, e o teclado passou a deixar na fila os bytes do
mouse para o driver do mouse. No G470 a interrupção do mouse não chegou, e
o byte parado travou o teclado.

**Decisão.** Um só módulo (`i8042.c`) lê a fila e entrega cada byte a quem
é de direito. Ele esvazia a fila na IRQ 1, na IRQ 12 e a cada tique do
relógio. A terceira é a rede de segurança: mesmo que uma interrupção não
chegue no hardware real, nenhum byte fica parado mais de 10 ms. Custa uma
leitura de porta a cada tique.

**Configuração defensiva.** A fila é esvaziada antes de pedir o byte de
configuração; o valor lido é conferido (bits reservados), com um valor
seguro se falhar; a interrupção e a tradução do teclado ficam **sempre**
ligadas; a IRQ 12 só é ligada se o touchpad responder; se ele não responder
(nem depois de um reset), a porta do mouse é desligada. O aviso na tela
mostra os números (configuração lida e gravada, respostas do touchpad) para
que uma foto diga o que o hardware respondeu.

## D021. Plano da fase 5: janelas e programas gráficos (H20 a H28)

**O kernel desenha a área de trabalho e as molduras; os programas desenham
o conteúdo.** Isso é o que o PRD pede (seção 4) e mantém os programas
gráficos como programas comuns no anel 3.

**Memória compartilhada para o conteúdo da janela.** `criar_janela` aloca
páginas físicas contíguas para os pixels da janela e as mapeia no espaço do
programa (a partir de 0xA0000000). O programa desenha direto nelas, sem
chamada de sistema por pixel, e chama `atualizar_janela` para pedir ao
kernel que mostre. O kernel lê as mesmas páginas pelo mapeamento 1:1 (D008).
Essas páginas levam uma marca na tabela de páginas (um bit livre para o
sistema) para que destruir o espaço do processo não as libere: quem as
devolve é o gerenciador de janelas, ao fechar a janela.

**Eventos por janela.** Cada janela tem uma fila: clique (com contagem de
cliques, para o duplo clique), botão solto, movimento, tecla e "fechar". O
kernel só entrega a cada programa os eventos das janelas dele.
`proximo_evento` pode esperar (dorme no canal da janela) ou não (o jogo
precisa continuar se não houver tecla).

**Gerenciador como tarefa do kernel.** Uma tarefa do kernel acorda a cada
tique, consome a fila do mouse e a do teclado, cuida de arrastar, focar e
fechar janelas, entrega os eventos e redesenha. A composição é feita só no
retângulo que mudou: fundo (de uma cópia pronta da área de trabalho), as
janelas de baixo para cima, a barra superior por último.

**Modo gráfico x terminal de tela cheia.** Com a área de trabalho no ar, o
console de tela cheia deixa de desenhar (o texto do kernel vai só para a
serial), e o shell passa a rodar dentro do programa Terminal. Os testes das
fases 2 a 4 continuam rodando com uma opção de build (`--console`) que
mantém o terminal de tela cheia.

## D022. Gerenciador de janelas: detalhes (H21)

**Arrastar, focar, fechar ficam no kernel.** Clique na barra de título
começa a arrastar; clique em qualquer lugar traz a janela para cima e dá o
foco; clique no X manda "fechar" ao programa, que decide terminar. O kernel
não mata o programa: assim um editor pode perguntar "salvar antes de
fechar?" (H25).

**Quem recebe o quê.** Clique na área de conteúdo vai para a janela, com
coordenadas relativas a ela; o soltar e os movimentos com botão apertado vão
para a mesma janela que recebeu o clique (mesmo que o mouse saia dela), o
que permite arrastar coisas dentro de um programa; movimento sem botão vai
para a janela com foco, se o mouse estiver sobre ela; teclas vão para a
janela com foco. Duplo clique é detectado pelo kernel (dois cliques em
450 ms no mesmo lugar) e chega como `cliques = 2`.

**Tarefa do kernel que dorme de verdade.** O gerenciador acorda a cada tique
para consumir as filas do mouse e do teclado. Para dormir ele bloqueia e cede
a vez na hora, por uma interrupção de software só do kernel (0x81); sem isso
o processo 0 nunca mais rodava (erro 11).

## D023. Calculadora em ponto fixo (H26)

Os números são inteiros de 64 bits que guardam o valor vezes 10.000 (quatro
casas decimais). Soma e subtração são diretas; multiplicação divide o
produto por 10.000; divisão multiplica o dividendo por 10.000 antes. Antes
de multiplicar, o programa confere se o resultado cabe (dividindo o maior
inteiro possível por um dos fatores) e avisa "Número grande demais". Num x86
de 32 bits, `/` e `%` entre inteiros de 64 bits viram chamadas a rotinas da
biblioteca do compilador, que não usamos; a calculadora faz a divisão longa
bit a bit, como no papel. Efeito colateral bom: `0,1 + 0,2` dá exatamente
`0,3`, sem o erro de arredondamento do ponto flutuante.

## D024. Hub USB no QEMU (fase 6)

**O que o PRD pede:** testar no QEMU com um hub virtual entre o controlador
EHCI e o pendrive, porque no G470 (chipset Intel série 6) cada controlador
EHCI tem um hub interno de alta velocidade ("rate matching hub") e todo
dispositivo fica atrás dele.

**O que o QEMU 8.2 permite:** o `usb-hub` do QEMU é um hub de velocidade
plena (USB 1.1) e o QEMU recusa ligá-lo numa porta de alta velocidade do EHCI
("speed mismatch"); a opção de controladores companheiros (UHCI) levaria o
hub e o pendrive para o controlador UHCI, que o G470 nem tem. Não existe hub
de alta velocidade no QEMU.

**Decisão:** o suporte a hub é escrito pela especificação USB 2.0
(capítulo 11: descritor de hub, energia, estado e reset das portas,
mudanças de conexão) e é usado sempre que um dispositivo de classe hub
aparecer, inclusive para a detecção de conexão e remoção. No QEMU os testes
ligam o pendrive direto na porta do EHCI; o caminho do hub só é exercitado
no G470, e o roteiro da fase 6 pede atenção a ele. Dispositivos de
velocidade plena ou baixa atrás de um hub (que exigiriam "split
transactions") não são suportados: pendrives são de alta velocidade.

## D025. USB: como foi pensado (H29 a H31)

**Síncrono e sem interrupções do controlador.** Cada transferência monta os
descritores (qTD), pendura uma cabeça de fila (QH) no anel assíncrono, espera
o controlador terminar e tira a QH (confirmando com o "doorbell" da
especificação). Só uma transferência por vez, com as interrupções da CPU
desligadas durante a espera. É mais lento que um driver com filas e
interrupções, mas para um pendrive de dados é suficiente, e não existe
nenhuma corrida entre o kernel e o controlador para depurar sem serial no
G470.

**O tempo vem do próprio controlador.** Com as interrupções desligadas o
relógio do sistema para; os atrasos exigidos pela especificação USB (50 ms
de reset de porta, por exemplo) e os limites de espera usam o contador de
microquadros do EHCI (FRINDEX, 8 por milissegundo).

**Memória para o controlador.** O EHCI lê e escreve a memória por endereço
físico. O driver separa uma página para a QH-cabeça, a QH de trabalho, o
pacote de setup e até 8 qTDs, e 64 KB contíguos de buffer. Como o kernel
enxerga a memória física 1:1 (D008), o mesmo número serve para a CPU e para
o controlador.

**Alternância de dados (data toggle).** Cada endpoint bulk alterna 0/1 a cada
pacote. Como a QH é recriada a cada transferência, o driver guarda a
alternância de cada endpoint e a calcula para cada qTD (cada qTD, menos o
último, leva um múltiplo do tamanho de pacote). Depois de um STALL e do
CLEAR_FEATURE, a alternância volta a 0, como manda a especificação.

**Pedindo o controlador à BIOS.** A BIOS do G470 usa o EHCI para ler o
pendrive de boot e emular teclado USB. Antes de reiniciar o controlador, o
driver segue o procedimento de passagem de controle da especificação EHCI
(semáforos da capacidade USBLEGSUP) e desliga as interrupções SMI. Como o
SaguiOS já está todo na RAM, não há nada a perder.

**Conexão a quente por consulta.** Uma tarefa do kernel olha, duas vezes por
segundo, o bit "conexão mudou" das portas do controlador e de cada hub.

## D026. FAT32: como foi pensado (H32, H33)

**Caminhos com nomes longos de verdade.** Cada nome é procurado na pasta
juntando as entradas de nome longo (LFN, em UTF-16) que antecedem a entrada
curta 8.3, conferidas pela soma de verificação da especificação. Sem nome
longo válido, vale o nome curto (respeitando as marcas de minúsculas que o
Windows grava). O texto do SaguiOS é Latin-1, que é exatamente o começo do
Unicode: a conversão é direta; o que não couber vira '_'. A comparação de
nomes ignora maiúsculas e minúsculas, inclusive acentuadas.

**Gravação.** Todo arquivo ou pasta criado ganha nome longo e um nome curto
único gerado (NOME~1.EXT). Clusters novos são procurados a partir da dica de
"próximo livre" do setor FSInfo; as duas cópias da FAT são sempre gravadas
juntas; o número de clusters livres no FSInfo é atualizado ao desmontar.
Datas e horas vêm do relógio CMOS.

**Cache pequena e explícita.** Os setores da FAT e das pastas passam por uma
cache de 16 setores, gravada no fim de cada operação (criar, escrever,
apagar...) e no desmontar. Os dados dos arquivos, quando cobrem setores
inteiros, vão direto para o pendrive. Assim, mesmo se o pendrive for
arrancado, o que foi feito até a operação anterior já está gravado.

## D027. Textos em UTF-8 vindos do pendrive (H32)

O SaguiOS guarda e mostra texto em Latin-1 (PRD 4.4), mas os arquivos que
chegam num pendrive gravado no Windows atual ou no Linux costumam estar em
UTF-8, em que "ã" ocupa dois bytes. O kernel não mexe no conteúdo (lê os
bytes como estão). Quem mostra texto converte: a libsagui tem
`utf8_para_latin1`, que troca cada sequência UTF-8 válida de um caractere
que exista no Latin-1 pelo byte correspondente e deixa o resto como está
(um texto que já seja Latin-1 quase nunca forma sequências UTF-8 válidas, então
não é estragado). O `cat` converte enquanto copia (guardando um pedaço de
sequência que tenha ficado no fim de um bloco) e o Editor converte ao abrir.
O que o SaguiOS grava continua em Latin-1, que o Bloco de Notas do Windows
abre como "ANSI".

## D028. Estruturas EHCI no formato de 64 bits e todas as partições (correção da fase 6)

O EHCI tem dois formatos de qTD/QH: o de 32 bits e o de 64 bits (apêndice B
da especificação), e quem escolhe é o controlador (bit 0 do HCCPARAMS), não o
sistema. Em vez de dois formatos no código, o SaguiOS usa sempre o tamanho
maior (qTD de 64 bytes, QH de 96) com as palavras altas zeradas: num
controlador de 32 bits as palavras a mais são simplesmente ignoradas, e num de
64 bits o endereço alto 0 está certo porque toda a memória usada fica abaixo de
896 MB (D008). Custa poucos bytes por fila e elimina um caminho que só o
hardware real exercitaria.

Partições: um pendrive pode ter o SaguiOS no começo (0xDA) e uma FAT32 depois,
que foi o que o Eli fez. As quatro entradas do MBR são examinadas; vale a
primeira FAT32 com BPB válido.

Também (erro 19): **um anel assíncrono por controlador.** Com dois EHCI (o
G470 tem dois), um anel compartilhado é percorrido pelos dois e a mesma
transferência sai nos dois barramentos. Cada controlador ganhou a sua página
de estruturas; o buffer de dados segue compartilhado, porque só uma
transferência acontece por vez.

## D029. Bip sem travar ninguém (B3)

O alto-falante do PC fica ligado à saída do canal 2 do PIT. O kernel
programa esse canal como onda quadrada (modo 3) com divisor 1.193.182 / Hz e
liga os bits 0 e 1 da porta 0x61. Para o som não travar quem pede, a chamada
`SIS_BIP` recebe uma lista de notas (frequência e duração), copia para uma
fila no kernel e volta na hora; quem toca é o tique do relógio (10 ms), que
desconta a nota atual e começa a próxima. Uma lista nova substitui a que
estiver tocando (num jogo, o efeito mais novo importa mais), a não ser que o
programa peça para enfileirar. A duração tem a resolução do tique: 10 ms.

Como saber se tocou num PC onde não dá para ouvir (testes, ou um notebook
que não liga o sinal ao alto-falante): na primeira nota do boot o kernel lê a
saída do canal 2 (bit 5 da porta 0x61) durante 5 ms medidos pelo contador do
canal 0 e registra no log quantas vezes ela virou. Medir por voltas de laço
não serve: no QEMU as leituras de porta são muito mais rápidas que no PC, e a
primeira versão viu 0 viradas.

## D030. Teclas seguradas para jogos (B4)

Os eventos de janela só avisam quando uma tecla é apertada (com a repetição
automática do teclado depois de meio segundo). Um jogo em primeira pessoa
precisa saber se a seta *continua* apertada, e andar e girar ao mesmo tempo.
O driver de teclado passou a manter um mapa de 256 bits, um por código de
varredura (128 a 255 para as teclas estendidas, como as setas), ligado ao
apertar e desligado ao soltar. A chamada `SIS_TECLAS_SEGURADAS` copia esse
mapa, mas só para o processo dono da janela em foco; os outros recebem tudo
zerado, para um programa em segundo plano não ler o que se digita em outra
janela. Ctrl e espaço sozinhos (que não geram caractere) também aparecem no
mapa, o que permite atirar com eles.

## Sagui 3D (B4)

Um jogo em primeira pessoa com visual 2,5D, escrito para este projeto em
`userland/sagui3d/` (seis arquivos, ~2.400 linhas, binário de 57 KB). As
fases são arquivos de texto em `/jogos/sagui3d/`. Abaixo, como cada parte
foi pensada.

### Projeção, ordem de desenho, recorte e o "olho de peixe"

Parti de semelhança de triângulos. Com o olho na origem olhando para a
frente, um ponto a uma distância `f` à frente e `l` para o lado aparece na
tela a `l * FOCO / f` pixels do centro; uma altura `h` (acima do olho)
aparece `h * FOCO / f` pixels acima do horizonte. `FOCO` = 176 pixels numa
vista de 320 dá uns 85 graus de visão.

O efeito "olho de peixe" aparece quando se usa a distância em linha reta até
a parede: nas bordas da tela essa distância é maior que no centro e uma
parede reta encolhe dos lados. Aqui `f` é sempre a distância medida *ao
longo da direção do olhar* (a coordenada "fundo" depois de girar o mundo
para a câmera), que é exatamente o que a semelhança de triângulos pede. Com
ela, os pontos de uma parede reta caem numa reta na tela.

A ordem de desenho saiu da escolha de só ter salas **convexas**. De dentro
de uma sala convexa, cada direção bate em exatamente um lado dela; então os
lados de uma sala nunca se cobrem na tela e podem ser desenhados em qualquer
ordem. Para o que está além, cada coluna da tela guarda uma "fresta": a
faixa de linhas ainda não pintada. Começando pela sala do sagui, cada lado
virado para ele pinta, nas colunas que ocupa, o teto acima e o chão abaixo;
se for parede, pinta a parede e fecha a coluna; se for passagem, pinta só os
degraus (o pedaço entre os dois tetos e entre os dois chãos) e estreita a
fresta para o vão. A sala vizinha é então desenhada só naquelas colunas e
só dentro da fresta. Com isso, não existe ordenação de paredes por
distância: o que está na frente é desenhado primeiro e o que está atrás só
aparece pelo buraco que ficou. O desenho para quando todas as frestas fecham
(ou depois de 32 salas em cadeia, como proteção).

Recorte: um lado com uma ponta atrás do olho é cortado no plano a meio palmo
à frente, só para achar as colunas que ele ocupa. Dentro dessas colunas, a
posição exata da parede em cada coluna vem do cruzamento do raio daquela
coluna com o segmento (reta contra reta), que dá o "fundo" e a posição ao
longo da parede para escolher a coluna da textura. As distâncias na tela são
limitadas a ±4096 pixels antes de somar ao centro (a primeira versão deixava
uma faixa da tela sem desenho quando uma parede quase encostava no olho,
porque o número não cabia em 32 bits).

Chão e teto: numa linha da tela a `d` pixels do horizonte, o plano de altura
`h` está a `fundo = (h - olho) * FOCO / d`. `FOCO / d` é calculado uma vez
por linha; com o fundo e a direção de cada coluna sai o ponto do mapa e o
pixel da textura. **Decisão: chão e teto continuam texturizados**, porque o
QEMU, que é bem mais lento que o G470, desenhou entre 80 e 110 quadros por
segundo; não foi preciso trocar por cor sólida.

Tudo é inteiro com 16 bits de fração (os programas não usam a FPU, que o
kernel não salva na troca de processos). Multiplicações passam por 64 bits;
a divisão de 64 por 32 bits usa a própria instrução `idiv`, conferindo antes
se o resultado cabe. O seno sai de uma tabela de um quarto de volta montada
na abertura do jogo girando um vetor de um passo pequeno 1024 vezes.

### Salas com alturas diferentes

Cada sala tem a altura do chão, a do teto, a luz (0 a 15) e as texturas de
piso, forro e paredes; o forro pode ser "céu" (salas a céu aberto, com o
contorno das árvores da mata no horizonte, que gira com o sagui). Dois
lados que usam os mesmos dois pontos, um em cada sala, formam uma passagem.
A diferença de chão vira um degrau desenhado; uma escada é uma fileira de
salas estreitas com o chão 16 palmos mais alto a cada uma; uma plataforma é
uma sala mais alta. O sagui sobe até 24 palmos de uma vez e o olho
acompanha o chão subindo devagar (e descendo um pouco mais rápido). Formas
côncavas, como uma galeria com um pilar no meio ou um pátio com um tablado,
são feitas juntando salas convexas em volta. A luz de cada sala escurece
com a distância na direção de um verde quase preto, como a sombra dentro da
mata.

O arquivo de fase é texto: `ponto`, `sala` (propriedades e a lista de
pontos), `lado` (textura própria de uma parede), `secreta` (passagem que
parece parede de cipó), e uma linha para cada coisa (`sagui`, `banana`,
`cacho`, `pote`, `porteira`, `macaco`). O jogo confere cada sala (convexa,
pontos definidos) e diz a linha do erro. `tools/sagui3d_mapa.py` faz as
mesmas verificações e desenha a planta vista de cima, para desenhar fases.

### Texturas e personagens

Os personagens e objetos são pixel art escrita como texto no código, uma
letra por pixel com uma paleta própria: o macaco-prego (24x32, com o boné
escuro que dá nome à espécie, rosto creme e rabo enrolado; tonto, os olhos
viram espirais e três estrelinhas giram na cabeça), a banana, o cacho
dourado, a casca, a jabuticaba, o pote de jabuticabas, o coração e o sagui
do SaguiOS. O estilingue é desenhado pela metade e espelhado, com as
mãozinhas cinzentas do sagui; o elástico é traçado em linha reta, solto ou
puxado com a semente no meio. A porteira é montada em código: mourões,
placa com a banana e, fechada, três tábuas com uma trava em diagonal.

As texturas de 32x32 são geradas por regras na abertura do jogo, o que
economiza espaço e deixa cada regra explicada: pedra em fiadas de 8 pixels
com três pedras de larguras sorteadas por fiada (somando 32 para emendar
sem costura) e juntas desencontradas, borda clara em cima e escura embaixo
e musgo em algumas; cipó como a pedra com duas ramas em onda (pela tabela
de seno) e folhas alternadas; madeira em quatro tábuas com veios e pregos;
grama com florzinhas amarelas e brancas; terra com pedrinhas; lajota em
placas com rejunte; folhas (teto de mata fechada) e rocha.

### Como os macacos-prego decidem para onde andar

A cada 4 tiques (30 ms cada), cada macaco que está quieto ou caçando olha
para o sagui: se ele está a menos de 640 palmos e a linha reta entre os dois
passa só por passagens com vão na altura dos olhos (o mesmo passeio de sala
em sala que o desenho usa, saindo de cada sala convexa pelo primeiro lado
que a linha cruza), o macaco o vê e guarda onde o viu. Caçando, anda em
linha reta até o último lugar onde o viu, um pouco mais devagar que o sagui
andando. Se bate numa parede, desvia 45 graus para um lado sorteado por meio
segundo e tenta de novo, o que basta para contornar quinas e pilares sem um
mapa de caminhos. Chegando lá sem ver o sagui, fica de guarda.

Encostar no sagui tira uma vida: o macaco "pega uma banana do bolso" e sai
correndo com ela por uns segundos, e o sagui fica um tempo sem poder ser
pego de novo. Cada semente faz o macaco parar um instante; a terceira o
deixa tonto (estrelinhas) e depois ele foge para longe do sagui até voltar
para a mata (some do mapa e conta como "espantado"). Pisar numa casca de
banana faz o macaco deslizar um pouco, girar e ficar tonto uns segundos,
e depois ele volta a caçar. Ninguém se machuca.

### O que foi feito diferente de propósito

- O tema e as regras são do SaguiOS: bananas como mecânica central (pontos,
  vidas a cada cinco, cascas como armadilha, porteira que pede um mínimo de
  bananas, um cacho dourado escondido por fase), estilingue de jabuticaba,
  macacos que roubam bananas em vez de atacar.
- A barra de status não tem retrato do personagem: só ícones (banana com
  "atuais/mínimo da porteira", cascas, sementes, corações, fase e pontos).
- Os inimigos não morrem: ficam tontos e fogem.
- Os mapas são polígonos convexos com passagens descobertas pelos pontos em
  comum, num arquivo de texto próprio; as fases são áreas da Mata Atlântica
  (uma clareira, um rio seco, um templo) com céu e árvores.
- O motor desenha coluna por coluna com frestas, sem nenhuma ordenação de
  paredes; o escurecimento vai para o verde da mata, não para o preto.
- Nenhum código, tutorial ou repositório de jogo foi consultado: as contas
  partiram da geometria descrita acima.

## D031. Configurações no pendrive, montado sozinho quando é do SaguiOS (H37)

O SaguiOS roda da RAM e nunca grava no disco interno, então as configurações
moram no pendrive de dados, em `/usb/.saguios/configuracoes.txt`: texto
simples, `chave = valor`, reescrito inteiro a cada mudança (são poucas linhas,
e um desligamento no meio não deixa linha pela metade). O kernel guarda a
tabela na memória e os programas leem e gravam por duas chamadas de sistema;
assim o próprio kernel (a área de trabalho, o relógio) também usa os valores.

Para as configurações valerem desde o boot, um pendrive que **já tem** a pasta
`.saguios` é montado sozinho ao ser conectado: ela só existe se o usuário já
montou esse pendrive e mudou alguma configuração, então montar não surpreende
ninguém. Os outros pendrives continuam esperando o usuário montar, como na
1.0. A espiada para saber se a pasta existe é só leitura (desmonta sem gravar).

Mudanças feitas sem pendrive valem até desligar; se um pendrive for montado
depois, elas são gravadas junto na próxima mudança.

## D032. Aparência e ícones em colunas (H38)

A aba Aparência troca o degradê do fundo (Brasil, Mata, Céu, Noite, Terra) e
mostra ou esconde o sagui do canto. O kernel guarda o fundo pronto num
buffer; quando as configurações mudam (um contador de "geração" aumenta), a
tarefa das janelas redesenha o fundo e a tela. O tamanho dos ícones, previsto
no PRD, ficou de fora: com 8 ou 9 ícones, ícones maiores não cabem em 768
linhas. Em vez disso, os ícones voltaram ao espaçamento folgado da 1.0 e
continuam numa segunda coluna quando não cabem mais na primeira.

Acertar o relógio grava direto no relógio da placa-mãe (CMOS), com o bit SET
ligado durante a escrita para o chip não avançar no meio, no mesmo formato
(BCD ou binário, 12 ou 24 horas) que ele já usa. O fuso horário é guardado
nas configurações para o relógio automático pela internet (fase 4).

## D033. Som: HD Audio por consulta, misturador no tique (H40 a H42)

**Driver.** O controlador HD Audio conversa com o codec por duas filas na
memória (CORB para os comandos, RIRB para as respostas). O driver não usa
interrupções: manda o comando e espera o ponteiro do RIRB andar. Um detalhe
achado no QEMU: com o aviso de resposta (bit 0 do RIRBCTL) desligado, o
controlador do QEMU para de ler a fila depois de cada resposta; com ele ligado
e as interrupções globais desligadas, o aviso só marca um registrador, que o
driver limpa a cada resposta. Funciona igual nos dois casos no hardware real.

O caminho do som é descoberto pelo grafo do codec, sem tabela de modelos: para
cada pino que a configuração do fabricante diz ser linha, alto-falante ou fone
e estar ligado a algo, uma busca em profundidade pelas listas de conexão acha
um DAC. No caminho, os seletores recebem a conexão certa, os amplificadores
ficam sem mudo no passo de 0 dB (alto sem distorcer), o pino liga a saída (e o
reforço de fone) e, se tiver, o amplificador externo (EAPD), que muitos
notebooks precisam para o alto-falante tocar.

**Misturador.** Um anel de 64 KB que o DMA lê sem parar, em 48 kHz, 16 bits,
estéreo. A cada tique (10 ms), o kernel escreve o pedaço que falta para ficar
60 ms à frente da posição de leitura: soma as fontes dos programas e a fonte de
notas, aplica o volume e corta em 16 bits. Cada programa tem um anel próprio de
~340 ms e manda quadros sem esperar (a chamada devolve quantos cabem), então
nenhum programa trava por causa do som.

**O bip pela placa.** As listas de notas do B3 (comando `bip`, Sagui Pega
Banana, Sagui 3D) viram ondas quadradas suaves na placa quando ela existe; sem
placa, continuam no alto-falante do PC. Os jogos ganharam a placa de som sem
mudar uma linha.

**Testes.** O QEMU grava a saída da placa num `.wav`; o teste mede a frequência
(cruzamentos do zero) e a mistura (algoritmo de Goertzel para cada frequência).
O canto do sagui (`/sons/sagui.wav`) é sintetizado por `tools/mksom.py`.

## D034. Rede: tudo por consulta, e a pendência do G470 (H43 a H47)

A placa Intel PRO/1000 (a das máquinas virtuais) trabalha com dois anéis de
descritores na memória; o driver não usa interrupções: a tarefa da rede acorda
a cada tique (10 ms), tira o que chegou e cuida dos relógios (DHCP, ARP, TCP).
Com 10 ms de folga, o atraso extra é pequeno e o código fica muito mais simples
e previsível. A pilha guarda os endereços IPv4 "na ordem da rede" (o primeiro
número no byte mais baixo), o que evita converter em cada comparação.

O DNS fica fora do kernel (na `libsagui`), como manda o PRD: é só uma pergunta
UDP para o servidor que o DHCP informou. Os nomes "comprimidos" das respostas
são pulados sem montar o texto, porque só o endereço interessa.

**H46 (rede no G470) ficou pendente.** Depende da foto do comando `pci` no
notebook: se o chip de rede tiver manual público, ganha driver próprio; se não,
o plano B é um adaptador USB-Ethernet da classe CDC (especificação pública). O
adaptador USB que o QEMU simula é de velocidade plena (USB 1.1) e o driver EHCI
do SaguiOS só fala com dispositivos de alta velocidade, então esse caminho não
dá para testar no QEMU como o PRD supunha: ele será testado com o adaptador de
verdade.

## D035. Números aleatórios sem RDRAND (H50)

A criptografia depende de números que ninguém consiga prever, e o processador
do G470 (Sandy Bridge, 2011) não tem a instrução RDRAND. A fonte de incerteza é
o próprio hardware: o kernel lê o contador de ciclos da CPU (RDTSC) em toda
interrupção (relógio, teclado, touchpad, USB) e, no boot, em 32 tiques
seguidos do relógio; o instante exato em ciclos varia por motivos físicos
(osciladores independentes, atrasos de barramento, uma pessoa digitando). A
data do relógio da placa e, se existir, o RDRAND também entram.

Os valores são misturados num poço de 16 palavras com as mesmas operações do
ChaCha (somar, girar, ou-exclusivo). Para entregar números, o poço vira a
chave de um ChaCha20 escrito no kernel; o primeiro bloco de cada pedido
substitui a chave ("apagar a chave depois de usar"), então nem quem lesse a
memória depois refaria os números já entregues. É o mesmo princípio dos
geradores dos sistemas grandes, em tamanho de projeto de estudo: não há
estimativa formal de entropia nem bloqueio até "juntar o suficiente". No
boot, antes das primeiras chaves, já entraram dezenas de medições.

## D036. Criptografia e TLS com subagentes, e como entram no SaguiOS (H50 a H54)

As partes mais exigentes da fase 4 (a biblioteca de criptografia e o cliente
TLS 1.3 com a validação de certificados) foram escritas por subagentes no
Claude Fable 5.2, com as mesmas regras de autoria do projeto: só as RFCs e as
normas do NIST, nenhum código de biblioteca, comentários em português. Cada
um registrou como pensou cada parte em `userland/cripto/LEIA-ME.md` e
`userland/tls/LEIA-ME.md`, e cada um tem o próprio teste (vetores oficiais;
servidores `openssl` locais; Wikipédia e example.com de verdade).

Os dois foram escritos sem depender do sistema: recebem um "ambiente" de
funções (mandar, receber sem esperar, esperar, relógio, números aleatórios,
data). Assim são testados no Linux da VPS e, no SaguiOS, a ponte
(`userland/libsagui/tls_ponte.c`) liga essas funções ao socket TCP, ao gerador
do kernel e ao relógio da placa convertido para UTC pelo fuso. A biblioteca
entra na `libsagui`; o ligador só copia para cada programa o que ele usa.

Um ajuste feito na integração: a âncora de confiança passou a ser o par
**nome + chave** de uma raiz, e não os bytes exatos do certificado. Servidores
mandam autoridades "assinadas em cruz" por raízes mais antigas (o example.com,
via Cloudflare, manda o SSL.com 2022 assinado pela antiga AAA, que já saiu da
lista da Mozilla); a mesma autoridade, com a mesma chave, está na nossa lista.

## D037. SSH: biblioteca separada, servidores conhecidos e o que fica para o Eli conferir (H56 a H58)

Como o TLS, o cliente SSH 2 (`userland/ssh`) foi escrito por um subagente no
Claude Fable 5.2 só a partir das RFCs (4251 a 4254, 4344, 6668, 8709, 8731,
8332, 4648), sem depender do sistema: recebe um "ambiente" de funções e é
testado no Linux da VPS contra o `sshd` do OpenSSH (77 conferências em
`tests/h56_h58_ssh.py`: as quatro combinações de cifra e chave do servidor,
corrupção de pacotes, trocas de chave no meio da sessão, 2,7 MB de saída
conferidos por SHA-256, servidor que cai). No SaguiOS entra na `libsagui`, e
dois programas usam: `ssh` e `ssh-chave`.

Escolhas:
- **Cifras:** `aes128-ctr`/`aes256-ctr` com `hmac-sha2-256` (o PRD aceitava
  esta ou `aes256-gcm`; a CTR com HMAC é a que todo servidor ainda aceita).
  Chave do servidor `ssh-ed25519` ou `rsa-sha2-256`.
- **Servidores conhecidos** em `<pasta>/conhecidos` (linhas
  `nome tipo base64`, como o `known_hosts` sem os nomes embaralhados). Na
  primeira conexão, mostra a impressão digital e pergunta; se a chave mudar,
  recusa com aviso forte e diz qual linha apagar.
- `<pasta>` é `/usb/.saguios/ssh` se houver pendrive configurado; sem ele,
  `/tmp/ssh` (e o programa avisa que a chave some ao desligar).
- **Teclas:** o Terminal manda ESC + letra; o `ssh` traduz para o xterm e
  acompanha o pedido de "modo de aplicação" das setas (ESC [ ? 1 h). Outras
  sequências (respostas do próprio Terminal, como a posição do cursor que o
  vim pergunta) passam como estão.
- O Terminal ganhou `ESC [ n b` (repetir a última letra), que o ncurses usa
  para desenhar barras: sem ela, o htop saía com colunas tortas.

O que os testes automáticos não cobrem, e vai no roteiro:
- **Login por senha aceito.** O `sshd` de teste roda sem ser root e não
  consegue conferir senhas; o teste só cobre a senha recusada. A primeira
  prova é o Eli entrar num servidor dele.
- **Redimensionar.** A janela do Terminal do SaguiOS tem tamanho fixo
  (94 x 28); a mudança de tamanho (`window-change`) está pronta e testada na
  biblioteca (de 43 x 132 para 30 x 100), e passa a ser usada quando o
  Terminal puder mudar de tamanho.

## D038. Sagui Navegador: o que ele entende, e como é testado (H59 a H63)

O navegador é um programa só (`userland/navegador`), em cinco partes:
descompactação (DEFLATE/zlib/gzip), imagens (PNG e JPEG), leitor de HTML,
layout e a janela. Nada de JavaScript, de CSS externo nem de fontes: a ideia
é ler páginas de texto (Wikipédia, documentação, README) bem, e não imitar um
navegador completo.

**HTML.** Leitura tolerante a erros com as regras mais comuns dos
navegadores (parágrafos que fecham sozinhos, `li`/`dt`/`dd`/`tr`/`td`, tags
de fechamento sem par ignoradas, texto cru em `script`/`style`/`textarea`).
Entidades com nome (as do Latin-1 e as tipográficas mais usadas) e
numéricas. Texto em UTF-8, Latin-1 ou Windows-1252 vira Latin-1 (a fonte do
SaguiOS); o que não existe vira o parecido (aspas curvas viram retas,
travessão vira hífen) ou `?`.

**Layout.** Blocos (p, div, h1-h6, listas, citações, pre, hr, tabelas),
texto corrido com quebra por palavra, imagens na linha, tabelas com largura
de coluna pela largura mínima e preferida das células, e caixas que
**flutuam à direita** (as "infobox" e as figuras "thumb" da Wikipédia, e
`float: right`/`align=right`). h1 sai em letra dobrada; h2 tem um traço
embaixo; links em azul sublinhado.

**CSS suportado** (no atributo `style` e em `<style>` com seletores simples
`tag`, `.classe`, `#id`, `tag.classe` e listas com vírgula; o resto é
ignorado): `display: none`, `visibility: hidden`, `color`,
`background`/`background-color` (só a cor), `text-align: center/right`,
`font-weight: bold`, `text-decoration: underline/line-through`. Atributos
antigos: `bgcolor`, `color` (em `font`), `align`, `hidden`, `width`/`height`
de imagens, `border` de tabelas. Uma folha própria esconde o que só funciona
com JavaScript ou com o CSS do site (menus da Wikipédia, botões de
"mover para a barra lateral" etc.).

**Formulários.** Só GET, só campos de texto/busca, ocultos e o botão de
enviar (é o que a busca da Wikipédia precisa). O texto vai em UTF-8.

**Teste.** O mesmo código de layout e desenho é compilado também no
computador de desenvolvimento (com um `sagui.h` simulado) e desenha a página
inteira; a janela no QEMU tem de mostrar exatamente os mesmos pixels. Assim
a "captura de referência" não é uma foto aprovada uma vez, e sim o próprio
programa rodando fora do SaguiOS: se o layout mudar, os dois mudam juntos, e
o que o teste confere é que o SaguiOS desenha o que o layout manda (imagens
que chegaram pela rede, rolagem, recorte). As imagens e o HTML também têm
testes próprios (H59 a H61), contra o Pillow e árvores conferidas à mão.

## D039. O compilador C do SaguiOS: frente, representação intermediária e fundo (H64 a H66)

O `cc` (`userland/compilador`, umas 6000 linhas) foi escrito de cabeça, a
partir do rascunho público do padrão C11, do manual da Intel (codificação
das instruções), da especificação ELF e da System V ABI do i386. Nenhum
código de compilador, montador, ligador ou tutorial foi consultado. O
raciocínio de cada parte está em `userland/compilador/LEIA-ME.md`; a
linguagem, em `docs/c-do-sagui.md`. Aqui ficam as decisões que mexem com o
resto do sistema.

**Duas metades, uma fronteira.** A frente (C) entrega uma representação
intermediária própria (`ri.h`): funções como listas de instruções de três
operandos sobre temporários de 32 ou 64 bits, mais os dados globais. O
fundo (x86, montador, ligador) só conhece isso. Uma linguagem em português
precisa só de uma frente nova. `cc --ri` mostra a lista.

**Simples antes de rápido.** Sem alocação de registradores: cada temporário
tem uma casa na pilha, com dois atalhos (resultado usado logo em seguida
fica em EAX; comparação + salto vira `cmp`/`jcc`). O código sai de 3 a 5
vezes mais lento que o do GCC e com tamanho parecido. Bastou: o shell, o
Terminal, o Editor, a Calculadora e a Banana recompilados pelo `cc`
passaram nos testes da versão 1.0.

**Erros para quem vai se corrigir sozinho.** Um erro por compilação, com
`arquivo:linha:coluna`, a linha, um `^` e uma dica; nomes da biblioteca
padrão do C ganham a dica do equivalente na libsagui (`printf` ->
`imprimir`). Não há avisos; por isso o que o GCC só avisa e costuma ser
erro de verdade (ponteiro recebendo inteiro, função não declarada) aqui é
recusado.

**Como o programa encontra a libsagui.** O build passou a gerar
`build/userland/libsagui.o`: todos os objetos da libsagui juntos num objeto
ELF relocável (`ld -r`), que vai para a imagem em `/sistema/lib/libsagui.o`
(200 KB). O ligador do `cc` lê esse arquivo, segue as relocações a partir
de `_inicio` e leva para o programa só as seções alcançadas. Os cabeçalhos
em `/sistema/inc` são cópias exatas de `userland/inc/*.h` e de
`kernel/inc/abi.h`: o compilador entende o que eles usam do GCC
(`__attribute__`, ignorado; `__builtin_va_list`). Assim não existe uma
segunda descrição da biblioteca para ficar desatualizada.

**Inteiros de 64 bits com ajuda.** `long long` é aceito (a Calculadora
usa). Soma, subtração e comparação são geradas direto; multiplicação,
divisão e deslocamento chamam `userland/libsagui/longo64.c`, novo, com a
divisão longa bit a bit (o SaguiOS não usa a libgcc).

**Pilha.** O carregador dá 64 KB de pilha a cada programa, pouco para um
analisador recursivo. O `cc` pede 1 MB com `aloca` e troca o ESP enquanto
compila (dez linhas de assembly em `cc.c`); o kernel não mudou.

**O mesmo núcleo dentro e fora.** O compilador fala com o mundo por uma
tabela de funções (`CcAmbiente`), como o TLS e o SSH. Os testes compilam o
núcleo para o Linux e conferem que o `cc` rodando dentro do SaguiOS produz
executáveis byte a byte iguais. Para rodar os 50 programas de teste em
milissegundos antes do QEMU, há uma libsagui de teste em que só as chamadas
de sistema são trocadas pelas do Linux (o ELF de 32 bits em 0x40000000
também carrega lá).

**Tamanho.** O `cc` tem 98 KB (compilado com `-Os`) e a biblioteca 200 KB;
a imagem foi de 78% para 99,3% da meta de 1,44 MB (`prd.md`, seção 1). A
meta não é obrigatória, mas a próxima fase vai passar dela; fica registrado.

**Fora da primeira versão:** ponto flutuante, vetores de tamanho variável,
campos de bits, assembly dentro do C, avisos, mais de um erro por vez. A
lista completa está no manual.

## D040. Visual moderno: vidro só nas barras, desfoque por recorte e efeitos automáticos (H74 a H77)

(D039 fica para o compilador, feito em paralelo.)

- **Onde há vidro:** nas barras de título, na barra de cima, na doca, no menu
  e nos balões. O conteúdo das janelas continua opaco (é desenhado pelos
  programas, e vidro atrás de texto atrapalha a leitura).
- **Desfoque:** média de caixa de 13 x 13 em duas passadas com somas
  corridas (`kernel/gui/vidro.c`). O custo não depende do raio e cada pixel
  custa poucas somas e três divisões.
- **Só onde a tela mudou:** o gerenciador já recompunha só o retângulo sujo.
  O vidro precisa do que está atrás e em volta dele. Por isso, quando o
  retângulo sujo encosta num vidro, ele cresce para cobrir o vidro inteiro
  mais o raio do desfoque (`expandir_vidro`). Assim a borda do desfoque nunca
  lê pixels de um quadro velho.
- **Sombra:** um degradê de 12 pixels, quadrático, deslocado 4 pixels para
  baixo. A área suja de uma janela passou a incluir a sombra.
- **Cantos:** uma tabela de 8 linhas diz quantos pixels pular em cada ponta
  (raio 8). Os botões de fechar e minimizar ficaram redondos.
- **Doca:** na parte de baixo, no meio, com os programas fixos (Arquivos,
  Terminal, Navegador, Editor, Configurações), uma linha e depois os
  programas abertos. Um ponto amarelo marca os que estão abertos, e o nome
  aparece num balão. Clicar traz a janela do programa (restaurando se estiver
  minimizada) ou abre o programa.
- **Menu do sagui:** a cabecinha na barra de cima abre a lista de programas,
  mais "Desligar...". O ícone de som mostra o volume e abre Configurações >
  Som.
- **Animações:** abrir, fechar, minimizar e restaurar desenham um contorno
  que vai de um retângulo a outro em 10 tiques (100 ms). A janela que está
  aparecendo só é desenhada quando o contorno chega. Um contorno custa quase
  nada, ao contrário de redimensionar os pixels da janela a cada quadro.
- **Tema claro e escuro:** valem para o que o sistema desenha (barras,
  doca, menu, molduras). Os programas usam as cores do kit (`gui.h`), que já
  são claras; fazer cada programa trocar de cor precisaria de uma chamada
  nova, que fica para depois.
- **Efeitos automáticos (H77):** na partida, o gerenciador mede quanto custa
  recompor a tela inteira com vidro (média de 4 quadros). Acima de 40 ms, o
  computador é lento e fica com os efeitos leves: sem desfoque, sem sombra e
  sem animação, com as barras na cor sólida. Em Configurações > Aparência
  dá para escolher automático, completos ou leves. No QEMU (sem aceleração),
  o quadro inteiro custou 17 ms.

**Redimensionar e maximizar (H78, pedido do Eli).** O conteúdo das janelas é
desenhado pelo programa num tamanho fixo. Por isso o programa precisa
**pedir** para ser redimensionável (`jan_redimensionavel`). O kernel troca a
memória da janela por uma do tamanho da tela (até os 4 MB que cada janela tem
no espaço do programa), no mesmo endereço, e passa a mandar `EV_REDIMENSIONAR`
com o tamanho novo. Enquanto a borda é arrastada, só um contorno com as
medidas acompanha o mouse: o programa recebe o tamanho uma vez, ao soltar.
Assim um programa lento não deixa o arrasto travado. Maximizar ocupa a tela
entre a barra de cima e a doca e guarda o tamanho de antes; arrastar uma
janela maximizada a devolve ao tamanho anterior. Os programas que não pedem
(Calculadora, jogos) continuam fixos e sem o botão. O Terminal avisa o
programa de dentro com a resposta do xterm `ESC [ 8 ; linhas ; colunas t`, só
se ele pediu, pelo modo próprio `ESC [ ? 7727 h` (o `ssh` pede e repassa ao
servidor com `window-change`). Um modo próprio evita que o sagsh receba essa
sequência no meio da linha de comando.

## D041. O núcleo é do zero; aplicativos podem usar código livre de terceiros, às claras

**Contexto (14/11/2026).** O SaguiOS ficou público e recebeu pull requests da
comunidade (#1, #3, #4, #6, #7), feitos com ajuda de IA. A revisão do maior
deles (o navegador Cipó, #6) achou alguns trechos adaptados de projetos
conhecidos, que a IA reproduziu "de memória" (libwebp, font-rs, V8, xz e
tabelas do FFmpeg e do FAAD2). Todos têm licenças livres compatíveis com a
GPL 3: o problema não era legal, e sim a regra do projeto ("nada de terceiros")
e a falta dos créditos. O Eli não vai ter tempo de evoluir o sistema sozinho e
quer a comunidade participando; ele deixou esta decisão com o Claude.

**Decisão.**
- **O núcleo continua 100% do zero:** kernel, drivers, sistema de arquivos,
  rede, TLS e criptografia, SSH, janelas, `libsagui`, shell, compilador e os
  programas básicos. É a identidade do projeto, e a história do "sistema
  escrito do zero" continua verdadeira.
- **Nos aplicativos, do zero é o padrão**, mas código livre de terceiros pode
  entrar como exceção: licença compatível com a GPL 3, pasta própria em
  `terceiros/` com a licença original e um `ORIGEM.md`, crédito em
  `CREDITOS.md` e no README ("O que não foi escrito aqui").
- Código reproduzido de memória por uma IA conta como código de terceiros.

**Por quê.** É o que todos os sistemas usados de verdade fazem (as
distribuições Linux juntam milhares de projetos, cada um com a sua licença;
Windows e macOS têm listas de "avisos de terceiros"). Proteger o núcleo mantém
o que torna o SaguiOS único; abrir os aplicativos, às claras, deixa a
comunidade trazer coisas boas sem medo de uma regra rígida demais. Com IA,
escrever do zero ficou barato, então o padrão continua sendo escrever aqui:
código de fora fica para o que não faz sentido reescrever.

**Consequências.** `AGENTS.md`, `CONTRIBUTING.md` e o modelo de pull request
foram atualizados. Do Cipó (#6), os trechos de terceiros podem entrar, desde
que separados em `terceiros/` e creditados, depois de conferida a licença
exata de cada um.

## D042. Ponto flutuante nos programas, pelo SSE2 (H80)

A regra "sem ponto flutuante" da versão 1 era uma simplificação: para os
programas usarem float e double, o kernel precisa guardar os registradores
de ponto flutuante de cada processo. Agora guarda: a cada troca de processo,
FXSAVE do que sai e FXRSTOR do que entra (512 bytes, alinhados em 16), e
cada processo novo começa com a FPU zerada. Salvar sempre, e não só quando o
programa usou, é mais simples e custa pouco. O kernel continua compilado sem
ponto flutuante (-mgeneral-regs-only), para nunca estragar esses registradores.

Os programas são compilados com `-msse2 -mfpmath=sse`: as contas vão pelo
SSE2, que arredonda cada operação para double exato (IEEE 754). A FPU x87, a
outra opção, calcula com 80 bits por dentro e só arredonda ao guardar na
memória, então a mesma conta dá resultados levemente diferentes conforme o
compilador guarda ou não os valores; o teste da H80 pegou isso, e o
JavaScript do Cipó exige double exato. O G470, o VirtualBox e o QEMU têm SSE2.

## D043. O Cipó: o navegador da 2.0 com o motor do pull request #6 (H82)

**Contexto.** O pull request #6, de Robson de Lima Barbosa, trouxe o "Cipó":
um navegador muito mais completo que o da 2.0 (CSS moderno, JavaScript,
fontes, Canvas, SVG, WebGL, vídeo), mas escrito sobre a versão 1.0 — com
outra pilha de rede, outro TLS, som pela AC97, mudanças nas chamadas de
sistema e tudo dentro da `libsagui`. A revisão achou também trechos
reproduzidos de memória de projetos conhecidos (D041). O nome Cipó veio do
#3 (M4R4G0N).

**Decisão: o motor entra, por cima da 2.0.**
- **Onde mora:** o motor é aplicativo, então fica em `userland/navegador/`
  (`motor/`, `fontes/`, `js/`, `graficos/`) e não na `libsagui`, que é
  núcleo e é ligada em todos os programas. As declarações que o #6 punha no
  `sagui.h` estão em `userland/navegador/cipo.h`. O programa continua sendo
  `/bin/navegador`; `/bin/cipo` é uma cópia com o nome novo. O tocador de
  vídeo do #6 mora em `userland/video/` e vira `/bin/video`.
- **O que entrou do #6:** o motor de páginas (HTML em árvore, cascata CSS com
  seletores compostos, `var()`, `@media`, `!important`; bloco, inline,
  float, flex, grid, tabela, `position`, `transform`, `z-index`, gradientes,
  bordas arredondadas), a quebra de linha do Unicode (UAX #14), as fontes
  TrueType, CFF, WOFF e WOFF2 (com Brotli, RFC 7932) e as DejaVu do sistema
  em `/fontes`, o motor de JavaScript com DOM (classes, async, geradores,
  Proxy, BigInt, typed arrays, eventos, timers, fetch/XHR, cookies,
  iframes), o Canvas 2D, o SVG, o WebGL e o GLSL em software, as imagens GIF
  e WebP, os cookies do HTTP e o tocador de MP4 (H.264 e AAC).
- **O que é da 2.0 por baixo:** a rede, o HTTP e o TLS 1.3 (`http_pedir`;
  a libsagui ganhou só `http_pedir_completo`, que mostra os cabeçalhos de
  cada resposta, para o Set-Cookie e o fetch), a janela redimensionável, a
  barra, o histórico e os favoritos do navegador da 2.0, o PNG, o JPEG
  (também progressivo) e o DEFLATE da 2.0, e o som HD Audio (`som_abrir` e
  `som_enviar`; o tocador converte o áudio para 48 kHz).
- **Como o JavaScript conversa com o motor:** como no #6, os scripts rodam
  sobre o HTML e devolvem o HTML do DOM depois do "load"; o motor lê esse
  HTML. Cliques, teclas, timers e rolagem rodam no JavaScript, e se o DOM
  mudou a página é relida no mesmo ponto da rolagem ("página viva").

**O que ficou de fora do #6, e por quê.** O TLS, a rede, a AC97 e o
`desligar` do #6 (a 2.0 já tem os seus, escritos aqui, e são núcleo); as
mudanças na ABI e na GUI do kernel; a compactação LZMA do ramdisk e o filtro
BCJ (`kernel/lib/lzma.c`, derivado do xz: é kernel, e a 2.0 não tem teto de
tamanho — a imagem foi de 1,7 MB para cerca de 5 MB, com 1,3 MB de fontes);
`entrega/`, o `tools/video/yt_base.py`, o gerador `tools/video/h264gen/` e
as regras especiais para YouTube, Bing e reCAPTCHA. O leitor de HTML da 2.0
(`html.c`) continua no repositório e na H61, mas a janela agora usa o motor;
o `pagina.c` (layout da 2.0) saiu.

**Os trechos de terceiros.** Cada um foi separado do código do Cipó numa
pasta de `terceiros/`, com a licença original conferida e um `ORIGEM.md`:
`font-rs` (o rasterizador por área acumulada; Apache 2.0), `libwebp-vp8` (o
WebP com perdas e as suas tabelas; BSD + patentes WebM), `v8-numeros` (o
`toString` em outras bases; BSD), `ffmpeg-h264-tabelas` (CABAC e CAVLC; LGPL
2.1 ou posterior) e `faad2-aac-huffman` (os livros de Huffman do AAC; GPL 2
**ou posterior**, conferido no COPYING e no copyright do Debian, com o
preâmbulo do FAAD2 e a frase de crédito que ele exige). Todas são
compatíveis com a GPL 3, então nada precisou ser reescrito. Dados: as fontes
DejaVu (licença Bitstream Vera), as tabelas do Unicode (Unicode License v3)
e o dicionário da RFC 7932 (BSD do IETF Trust), em `CREDITOS.md`. Uma
função de datas do #6 que seguia um algoritmo conhecido (dias desde 1970)
foi trocada por um laço simples escrito aqui.

**Mudanças no código do #6.** Além de mudar de lugar: o PNG/JPEG passam pelos
decodificadores da 2.0 e as imagens chegam com alfa (PNG transparente sobre
o fundo da página); o WOFF usa o DEFLATE da 2.0; dois erros do layout
corrigidos (linha vazia dentro de `pre` e `<br><br>` contava a altura em
dobro; um float no começo do texto não era contornado pela primeira linha);
`motor_ancora` e `motor_imagens_total` para os links `#âncora` e o registro.

**Ponto flutuante e pilha.** O motor usa double pelo SSE2 (D042). Nos
testes da H82 o `baixar https://...` (H53) passou a cair com "proteção
geral" num `movdqa` da curva 25519: o `inicio.asm` deixava a pilha 8 bytes
fora do alinhamento de 16 que o gcc supõe na entrada de `principal`, e com
SSE2 o gcc guarda valores de 16 bytes na pilha com instruções que exigem o
alinhamento; um quadro maior no HTTP mudou o caminho e o erro apareceu. O
`inicio.asm` agora desce mais 8 bytes antes de empilhar os argumentos (vale
para todos os programas). Com isso apareceu o mesmo erro do outro lado no
`cc`, que troca para uma pilha própria de 1 MB com o topo vindo do `aloca`
(alinhado só em 8): o topo agora é alinhado em 16. Os objetos do Cipó e do
tocador são compilados também com `-mstackrealign`, por segurança. Outros
testes mudaram por causa da imagem maior e da pasta `/fontes` na raiz: a H23
conta 11 itens na raiz, e a H34 cria a FAT32 depois do fim da imagem (antes
começava em 2 MB, e a imagem passou de 5 MB).

**Testes.** A H62 continua comparando a janela, pixel a pixel, com o mesmo
código compilado no computador — agora o motor do Cipó com as fontes DejaVu
(as cores procuradas na captura mudaram: links `#0B57D0`, borda dos campos
`#767676`). A H82 (`tests/h82_cipo.py`) confere uma página de CSS moderno
(flex, grid, gradiente, float, tabela, transform, CSS externo, `@font-face`,
SVG, GIF, WebP e PNG transparente) pixel a pixel, o GIF contra o Pillow, o
WebP perto do Pillow, 120 páginas estragadas no motor com verificação de
memória, e uma página de JavaScript (as verificações do teste do #6, mais
`toString` em outras bases, cookies, fetch com POST, Canvas 2D, WebGL, timer
e clique). `tests/h82_video.py` só testa a decodificação de vídeo e o som
quando a máquina dos testes tem ffmpeg para gerar o MP4. A H63 (internet)
mudou num ponto: o artigo da Wikipédia agora aparece como nos outros
navegadores, com a busca do topo escondida atrás de uma lupa (o campo surge
por JavaScript), então a busca é feita na página Especial:Pesquisar, que
tem o campo à vista. A Wikipédia responde 429 ("respeite a política de
robôs") quando muitos pedidos chegam seguidos com o User-Agent
"SaguiOS/2.0" (um artigo pede dezenas de imagens); um User-Agent com
contato, como a política pede, fica para depois.

**Pendências.** O vídeo baixa o MP4 inteiro antes de tocar (o HTTP da 2.0
entrega o corpo de uma vez); o `<video>` dentro da página só abre o tocador
quando o endereço é o próprio MP4; WebGL, GLSL, iframes e fontes WOFF2 entram
compilados mas só têm o teste básico (o WebGL limpando a tela); um float
dentro de um bloco com borda pinta o fundo uns pixels abaixo do texto dele.

## D044. Resolução nativa no G470: aproveitar o modo da BIOS, não refazer (H84)

**O problema.** O VESA da BIOS do G470 oferece no máximo 1024x768; o painel
tem 1366x768. A placa (Intel HD Graphics 3000, Sandy Bridge, 8086:0116) estica
a imagem com o "panel fitter", e o texto fica borrado.

**Decisão.** Um driver pequeno (`kernel/dispositivos/video_nativo.c`), escrito
só a partir dos manuais públicos da Intel (PRM da Sandy Bridge, volume 1
parte 1 e volume 3 partes 1 a 3), que **não refaz o modo**: relógios (PLLs),
tempos, FDI, transcodificador e a porta LVDS ficam como a BIOS deixou. Num
notebook a BIOS já manda o sinal na resolução nativa para o painel; o driver
só (1) lê o tamanho nativo nos tempos do pipe ligado (HTOTAL/VTOTAL), (2)
muda o tamanho de origem do pipe (PIPESRC) para ele, (3) desliga o panel
fitter (PF_CTL, PF_WIN_POS, PF_WIN_SZ) e (4) muda o passo do plano primário
(DSPSTRIDE, 1366 x 4 arredondado para múltiplo de 64 = 5504 bytes), com a
imagem começando no mesmo lugar do framebuffer VESA (DSPSURF/DSPLINOFF não
mudam; a escrita no DSPSURF só "arma" a mudança). Os ~4 MB da imagem nova
precisam estar mapeados na GTT pela BIOS (que mapeia toda a memória roubada,
de no mínimo 32 MB): o driver confere entrada por entrada e, se faltar
alguma, desiste em vez de preencher a GTT. Preencher a GTT exigiria escolher
memória para a placa e cuidar de cache; não vale o risco para 4 MB que a BIOS
já deu.

**Segurança.** Desligado por padrão: só age com `video.nativo = sim` no
pendrive de dados (sem o pendrive, volta o VESA). Só age se tudo conferir:
placa 8086 em 00:02.0 com ID 0106, 0116 ou 0126; BARs abaixo de 4 GB; um
único pipe ligado; porta LVDS e energia do painel ligadas; tempos com cara de
painel (640 a 1920 por 480 a 1200); origem do pipe igual ao modo VESA; plano
ligado, linear, 32 bits BGRX, com o passo do VESA e mostrando o endereço do
VESA. Depois de escrever, relê: pipe ainda ligado, PIPESRC e DSPSTRIDE
aceitos, panel fitter desligado e o contador de quadros andando; se algo
falhar, devolve os valores da BIOS. Tudo o que é lido e escrito vai para o
diário (`registro VIDEO`).

**O que não é certeza.** O manual não deixa claro se o PIPESRC pode mudar com
o pipe ligado na Sandy Bridge; desligar o pipe exigiria refazer a sequência
inteira (FDI, transcodificador do PCH), que é justamente o que se quis
evitar. Se o G470 não aceitar, o diário mostra e o driver desfaz.

**Interface.** O vídeo ganhou a troca de resolução com o sistema já ligado
(`video_reservar` antes de mexer na placa, `video_adotar` depois), e o
terminal do kernel e o mouse se ajustam. A interface gráfica já lia
`video_largura()`/`video_altura()`; com 1366 a janela maximizada cabe nos
4 MB de memória por janela (1362 x ~680 pixels x 4 < 4 MB).

**Como foi testado sem o G470.** O QEMU não tem a placa Intel. O teste
`h84_video_nativo.py` prova que nada muda sem a configuração e com ela numa
placa que não é Intel (o diário diz o motivo), e, com `video.nativo =
teste-qemu`, troca a placa VGA do QEMU para 1366x768 pela interface "Bochs
VBE" (documentada pelo Bochs) com o sistema já ligado: é o mesmo caminho do
lado do SaguiOS, e prova a interface em 1366x768. O estágio 2 também ganhou
uma opção de build só para testes (`--vesa-largo`) que prefere um modo VESA
largo de 768 linhas (o QEMU oferece 1280x768).

## D045. O visual do Robson (#1) como opção, junto do vidro da 2.0 (H81)

O pull request #1 (Robson de Lima Barbosa) foi escrito sobre a 1.0, antes do
vidro, da doca e do redimensionar da 2.0. Juntar como estava apagaria a 2.0;
por isso as ideias e o código dele foram trazidos para o código atual, com
ele como autor do commit:

- **Ícones**: `tools/mkarte.py` (dele) descreve cada ícone com formas e gera
  ARGB no build. Ganhou um segundo tamanho (40x40, para a doca) e os ícones
  que só existem na 2.0 (Configurações, Cipó, programa instalado). O padrão
  para um ícone novo está no começo do arquivo.
- **Letras suavizadas** em todo o sistema (kernel e libsagui): um pixel
  apagado com dois vizinhos acesos em "L" recebe 1/3 da cor da letra.
  Os botões dos programas ganharam o degradê com cantos cortados dele.
- **Fundos**: "Com sol" (desenhado por código, dele) e "M. Atlântica" (a
  imagem feita por IA que ele trouxe, `rootfs/arte/mata-atlantica.sgi`, no
  formato de `tools/mkfundo.py`). O padrão continua o degradê "Brasil" pedido
  na H20; a imagem é uma escolha. Se o arquivo faltar, o "Com sol" entra no
  lugar.
- **Temas**: além de escuro e claro (H76), "Mata", com a cara do #1 (barra
  verde quase opaca, filete amarelo, botões redondos vermelho, amarelo e
  verde, título forte), e "Azul", com as cores do #7. Os dois somam campos ao
  `tema_t` em vez de outro caminho de desenho: o vidro, as sombras e os
  efeitos leves continuam valendo para todos.
- **Menus do botão direito**: um único menu de contexto serve a ícones, à
  área vazia, à barra de título e aos programas. A chamada `SIS_MENU_JANELA`
  (77) e o evento `EV_MENU` (7) são as do #1, com números novos (os dele
  colidiam com a 2.0). Terminal, Editor e Arquivos têm menus próprios; no
  Terminal, "Colar" é o primeiro item, para não perder o costume do botão
  direito que colava.
- **Barra de tarefas** como opção à doca (`aparencia.barra = tarefas`).
- **Mouse a 200 pacotes por segundo** e o relógio CMOS lido uma vez por
  segundo, com as interrupções desligadas por menos tempo.

Ficou de fora o maximizar do #1: a 2.0 já tinha o seu (H78), com a memória
das janelas certa para 1366x768.

## D046. Espaço dos discos: uma pergunta a mais no info_sistema, e a FAT contada (H83)

O comando `df` (apelido `espaco`) precisa de total, usado e livre de cada
lugar onde se grava. Em vez de uma chamada de sistema nova, entrou mais um
tipo no `info_sistema`, que já serve memória, processos e versão:
`INFO_DISCOS_ESPACO` (7) devolve um `info_espaco_t` para "/" e outro para
"/usb", se houver pendrive montado.

- **"/"** não é um disco: é a árvore de arquivos na memória (o ramdisk mais
  o que foi gravado depois, na RAM). O "usado" é a soma dos tamanhos dos
  arquivos da árvore; o "livre" é a memória livre, porque é lá que um
  arquivo novo vai parar; o total é a soma dos dois.
- **"/usb"** vem da FAT32. O setor FSInfo guarda uma contagem de clusters
  livres, mas a especificação da Microsoft diz que ela é só uma dica: outro
  sistema pode ter gravado no pendrive sem atualizá-la. Então o kernel conta
  os clusters livres na FAT de verdade, uma vez por montagem, na primeira
  vez que alguém pergunta (contar ao montar atrasaria toda montagem de
  pendrive grande, mesmo sem ninguém querer saber); depois, alocar e liberar
  clusters já mantêm a conta em dia. A FAT é lida em blocos de 64 setores.
  Se a conta difere do FSInfo, uma linha no log diz quanto era e quanto é.

## D047. MP3 do zero, pela norma (H86)

O Tocador precisa tocar MP3, e o decodificador (`userland/tocador/mp3.c`) foi
escrito aqui, a partir da descrição da norma ISO/IEC 11172-3 (MPEG-1 áudio,
Layer III) e da ISO/IEC 13818-3 (as taxas de 16, 22,05 e 24 kHz do MPEG-2),
sem consultar nem reproduzir nenhum decodificador existente. É um aplicativo,
e a D041 permitiria código livre de terceiros, mas um MP3 cabe em pouco mais de mil linhas
e dá para explicar etapa por etapa; não havia motivo para a exceção.

- **O que vem da norma como dado** (`mp3_tabelas.h`): os códigos de Huffman,
  as faixas de fator de escala, a pré-ênfase, os coeficientes da janela de
  síntese e as contagens de fatores do MPEG-2. São números dos anexos, iguais
  em qualquer decodificador; estão em `CREDITOS.md`. O resto (as janelas e os
  cossenos da IMDCT, a matriz do banco de síntese, n^(4/3), as potências de
  2) é calculado pelas fórmulas ao abrir o primeiro arquivo, com cosseno e
  raízes próprios (não há libc).
- **Simples antes de rápido.** As árvores de Huffman são percorridas bit a
  bit, a IMDCT é a soma direta da fórmula e o banco de síntese é a receita da
  figura da norma, só aproveitando que o cosseno se repete (32 das 64 linhas
  da matriz). Em float pelo SSE2 (D042). No QEMU, 3 s de música estéreo
  decodificam em cerca de 0,3 s; se o G470 precisar, dá para acelerar depois.
- **Como se sabe que está certo.** `tests/h86_mp3.py` compila o mesmo `mp3.c`
  no PC de build e compara o som com o da libmpg123, em arquivos feitos na
  hora pela libmp3lame (as duas só como ferramentas de teste, como o QEMU): a
  diferença máxima é de 1 unidade em 16 bits (SNR perto de 97 dB). O estéreo
  de intensidade, que a libmp3lame não gera, é conferido com quadros montados
  à mão pelo teste.
- **Ficou de fora:** o "MPEG-2.5" (8, 11,025 e 12 kHz), que não é da ISO; o
  "formato livre" (taxa de bits fora da tabela); a conferência do CRC (os
  quadros com CRC tocam, o CRC é pulado); e o corte do silêncio do começo e
  do fim (a informação "gapless" do quadro LAME). Os blocos mistos estão
  escritos pela norma, mas nenhum codificador à mão os gera: não foram
  testados.

## D048. Todo o userland realinha a pilha para o SSE2 (H63)

O D042 fez os programas usarem SSE2, e o D043 já tinha corrigido a pilha
inicial e as pilhas trocadas do Cipó e do tocador. Faltava o outro lado: a
`libsagui`, onde ficam o TLS e o x25519, continuava compilada sem
`-mstackrealign`. Na segunda conexão HTTPS, feita com a pilha trocada à mão
pelo motor, o `x25519` recebia uma pilha alinhada só em 8 bytes e o `movdqa`
que guarda um vetor de 16 bytes dava proteção geral.

Agora o `UCFLAGS` leva `-mstackrealign` para todo o userland. Cada função
alinha a própria pilha ao entrar, então qualquer pilha trocada à mão (motor,
JavaScript, compilador ou um programa futuro) é segura, e o Cipó e o tocador
não precisam mais do flag separado. O kernel continua sem SSE e sem esse custo.

## D049. Programas úteis: comandos de texto, pacotes, captura, imagens, música e lixeira (H87)

**Pedido.** Deixar o sistema mais completo para o uso do dia a dia, sem
código de terceiros novo: tudo foi escrito para o SaguiOS, e o que já existia
no Cipó e no tocador de vídeo foi reaproveitado pelo Makefile, sem copiar.

**Comandos de texto.** `head`, `tail`, `sort` (estável, por mesclagem; -r, -n,
-u), `uniq` (-c, -d), `find` (-name com * e ?, -type f|d), `du` (-s),
`hexdump` (como o `hexdump -C`) e `less`. O `less` usa o que o Terminal já
entende: tela alternativa (`ESC [ ? 1049 h`), sem quebra de linha
(`ESC [ ? 7 l`) e o aviso de tamanho (`ESC [ ? 7727 h`, H78), então se ajusta
à janela. Vindo de um pipe, o teclado não chega a ele, e ele só imprime.

**Pacotes.** `tar` no formato ustar (POSIX.1-1988), sem compressão, e
`zip`/`unzip` no formato básico da PKWARE (sem ZIP64 nem senha). A compressão
é um DEFLATE novo na `libsagui` (`deflate.c`, `compactar.h`): LZ77 com tabela
de dispersão de 3 bytes, até 48 tentativas por posição, e um bloco só com os
códigos de Huffman fixos da RFC 1951. É mais simples que os códigos próprios
de cada bloco e comprime menos (o texto da história do Linux foi de 1579 para
1032 bytes), mas é DEFLATE válido: o teste abre os pacotes com o `zipfile` e
o `tarfile` do Python. O `unzip` usa o descompressor do Cipó
(`navegador/descompactar.c`), confere o CRC-32 de cada arquivo e recusa
caminhos com ".." ou começando por "/"; o `tar xf` também.

**Captura de tela.** A tecla Print Screen (E0 37) virou `TECLA_IMPRIMIR`. O
gerenciador de janelas a trata antes da janela em foco e abre o
`/bin/captura`, que lê a tela por uma chamada nova, `SIS_LER_TELA` (78): ela
copia o buffer de trás (a tela montada, sem o cursor) para o programa, se o
espaço couber. Qualquer programa pode ler a tela, como no X11 sem extensões;
o SaguiOS não tem usuários separados, então não há a quem esconder. O PNG é
RGB de 8 bits, filtro "nenhum" e IDAT comprimido pelo DEFLATE novo. Com
pendrive montado vai para `/usb/Capturas`; sem, para `/capturas` (na memória,
some ao desligar).

**Visualizador.** Usa a porta única de imagens do Cipó (`imagens.c`: PNG,
JPEG, GIF e WebP) e lê BMP de 24 e 32 bits. Ao reduzir, cada pixel da tela é
a média dos pixels que ele cobre (guardada até mudar o zoom ou a janela); ao
ampliar, pixel mais próximo. As imagens da pasta são ordenadas pelo nome,
porque a FAT do pendrive lista na ordem em que foram gravadas.

**Música.** Toca WAV (PCM de 8 ou 16 bits) e M4A/MP4 com AAC-LC, ligando o
`mp4.c` e o `aac.c` do tocador de vídeo. O laço não espera a placa: entrega o
que ela aceita e volta aos eventos. Não há MP3: seria um decodificador novo
inteiro, e ficou de fora.

**Lixeira.** "Apagar" no Arquivos move para a lixeira do mesmo disco
(`/.lixeira` ou `/usb/.lixeira`), porque renomear entre a memória e o pendrive
não existe (o `sis_renomear` recusa com ERRO_ACESSO; o `mv` copia e apaga). O `.origens` de cada lixeira guarda o caminho original de
cada item, para restaurar. Nomes começando com ponto não aparecem no
Arquivos. O `rm` do terminal continua apagando de vez, como no Unix.

**Configuração de rede.** A aba Rede já tinha DHCP ou manual (H47). Ganhou o
botão "Testar conexão": resolve example.com pelo DNS e abre uma conexão TCP na
porta 80, com o tempo de cada passo.

## D050. Revisão do visual: bordas suavizadas, ícones e Configurações (H88)

**Pedido.** O usuário apontou bordas com pontas, vidro mal feito, as
Configurações feias, falta de ícones (só texto) e texto saindo dos botões. As
capturas de todos os programas (`tests/vistoria.py`) confirmaram:

- Os cantos redondos pulavam pixels inteiros (escada). No modo de efeitos
  leves, que o QEMU escolhe sozinho, ainda havia uma moldura preta quadrada em
  volta do canto redondo: daí as pontas. Os cantos de baixo eram retos.
- Os botões de fechar, minimizar e maximizar eram linhas de pixels inteiros, e
  o X e o círculo saíam em escada.
- Nos efeitos leves não havia sombra: a janela era um retângulo chapado.
- O menu do sagui e a lateral das Configurações eram só texto. "Sobre o
  computador" encostava nas bordas do botão; "M. Atlântica", "Escuro
  (trocar)" e a frase dos efeitos saíam para fora dos botões.

**O que mudou.**

- `kernel/inc/suave.h`: retângulo de cantos redondos, contorno e traço com a
  borda suavizada (4 x 4 amostras por pixel, contas em oitavos de pixel e só
  com inteiros, porque o kernel não usa ponto flutuante). O kernel e a
  `libsagui` usam o mesmo arquivo.
- Vidro (`vidro.c`): o canto tem uma tabela de cobertura (8 x 8 amostras) em
  vez de "pixels a pular". Ganhou também uma linha clara na beira de cima e um
  degradê que clareia o terço de cima. A sombra fica também nos efeitos leves
  (custa só a faixa em volta da janela). Os cantos de baixo das janelas são
  feitos guardando o fundo dos dois cantos antes de pintar a janela e
  misturando de volta depois de copiar o conteúdo.
- Botões da barra de título desenhados com `suave.h`. O título é cortado antes
  dos botões.
- Ícones: `tools/mkarte.py` ganhou monitor, visualizador, música e desligar,
  e o tamanho 20 para todos. Com `--programas`, gera os ícones de 20x20 para a
  `libsagui` (`userland/inc/icones.h`), mais 17 da interface (relógio,
  aparência, som, rede, lixeira, tocar e outros). Pelo `--gc-sections`, cada
  programa leva só os ícones que usa.
- Menu do sagui: ícone em cada item, linha de 30 pixels, destaque redondo e
  uma linha separando "Sobre" e "Desligar". A doca usa os ícones novos.
- `gui.c`: botões com cantos redondos, degradê, contorno fino e ícone
  opcional (`botao_t.icone`). O campo `ativo` marca a opção escolhida. O texto
  que não cabe termina em "..". Caixas, listas e barras de rolagem também
  ficaram com cantos redondos. Funções novas: `g_ret_redondo`,
  `g_contorno_redondo`, `g_icone` e `g_texto_cortado`.
- Configurações: lateral com ícone e nome em cada aba, e a aba aberta num
  cartão branco com filete verde. O título da aba tem ícone. Os efeitos viram
  três botões (Automático, Completos, Leves). A nota de baixo diz claramente
  que, sem pendrive, os ajustes valem até desligar.
- Arquivos, Música, Visualizador e Desligar: botões com ícones. O "Sobre"
  mostra o tamanho da imagem em MB; antes era a porcentagem de um disquete de
  1,44 MB, que dava "431%".

**Compatibilidade.** As posições clicáveis das abas e dos botões das
Configurações não mudaram. O menu do sagui passou a ter linhas de 30 pixels,
então os testes que clicam nele foram ajustados. O `-Wno-missing-field-initializers`
entra no `UCFLAGS` porque `botao_t` ganhou campos no fim: quem escreve
`{ x, y, l, a, texto }` deixa o resto em zero, que é o certo.

## D051. Menu Iniciar com todos os programas, busca e categorias (H89)

**Contexto.** O menu do sagui listava 13 programas numa coluna. Programas que
existem em `/bin` e abrem janela (Chuva de letras, Triângulos 3D, Componentes,
Sagui Code) não apareciam em lugar nenhum da interface. O pedido foi um menu
como o dos outros sistemas: todos os programas, barra de pesquisa e
categorias.

**Decisão.**
- O menu continua no kernel (`kernel/gui/janelas.c`), no mesmo botão do
  sagui. Ele tem uma caixa de busca, uma fileira de categorias e uma grade de
  5 x 4 ícones de 40 pixels, com o nome em até duas linhas. No rodapé ficam
  Sobre e Desligar.
- O catálogo (`catalogo[]`) dá a cada programa nome, caminho, categoria,
  ícone e palavras extras para a busca ("navegador" acha o Cipó, "processos"
  acha o Sagui Monitor). As categorias são Todos, Internet, Escritório, Mídia,
  Jogos, Sistema, Programação e Instalados. Instalados só aparece quando há
  programas no pendrive, que entram pelo `area_icone_nome`.
- Enquanto o menu está aberto, o teclado vai para a busca e não para a
  janela em foco. A busca ignora maiúsculas e acentos e compara texto Latin-1,
  porque o kernel compila com `-fexec-charset=ISO-8859-1` e o teclado
  entrega Latin-1. As setas escolhem, Enter abre e Esc fecha.
- O Sagui Code roda dentro do Terminal (`terminal -e /bin/saguicode`). O
  Vídeo fica fora do menu, porque precisa de um arquivo; ele continua abrindo
  pelo Arquivos.
- O vidro do menu ficou quase opaco (250 de 256). Com 238, o texto das
  janelas de trás atrapalhava a leitura da grade.
- Ícones novos no `tools/mkarte.py`: lupa, chuva, triângulos, código e
  componentes.

**Compatibilidade.** Os testes que clicavam em itens do menu antigo (`h74_h77`,
`h78`, `h86`, `h88`) agora clicam na grade. O item k fica em
`(28 + (k % 5) * 108 + 54, 134 + (k // 5) * 92 + 30)`.

## D052. Laço de seleção na área de trabalho (H90)

**Origem.** O laço translúcido foi feito pelo Robson no commit 0b3c462 (ramo
`area-lixeira`, sobre a 1.0). O commit foi junto do pull request do Cipó (#6),
de que só o navegador entrou na 2.1, e nunca chegou ao main.

**Decisão.** Ele foi refeito sobre o código atual, só com o que o main tem
hoje:
- arrastar no vazio da área de trabalho desenha um retângulo translúcido na cor
  do filete do tema, por cima do fundo e por baixo das janelas;
- ao soltar, os ícones que ele toca ficam marcados com um destaque redondo e
  translúcido; com Shift, eles se somam aos que já estavam marcados;
- um clique num ícone marca só ele, e clicar no vazio desmarca.

**Ficou de fora.** Arrastar ícones em grupo e mandar ícones para uma lixeira
existiam na versão de 0b3c462. O main não tem nenhum dos dois: os ícones ficam
numa grade fixa, e a lixeira (H87) é de arquivos, no Arquivos. São mudanças
separadas.

**Menu Iniciar.** Os chips das categorias passavam da borda do painel. O menu
foi para 620 pixels, a sobra da fileira agora é repartida entre os chips
(`chip_folga`) e a grade fica centralizada.

## D053. Arquivos: laço, vários marcados, "Novo" no menu e nome editado na linha (H91)

**Pedido.** Queria a seleção translúcida também no Arquivos, as opções de
criar no menu do botão direito (pasta, arquivo de texto e outros) e o nome
escrito na própria linha do item, sem janela de diálogo. Isso vale para criar
e para renomear.

**Na lista da `libsagui`.** Tudo é opcional, e quem não liga continua igual
(Música, Tocador, Componentes).
- `lista_t.marc` (um byte por item) liga a seleção de vários itens:
  - clique marca um, Ctrl+clique alterna, Shift+clique marca a faixa e
    Ctrl+A marca todos;
  - arrastar a partir do vazio desenha o laço translúcido. O vazio é abaixo
    dos itens ou depois do fim do nome. O laço marca as linhas cujo nome ele
    toca;
  - um clique sem arrastar no fim da linha seleciona a linha, e o duplo clique
    abre, como antes. Os testes antigos clicam no meio da linha.
- `lista_editar(j, l, i, marcar)` abre a edição do nome na própria linha. O
  texto fica numa caixa branca de contorno verde, e os primeiros `marcar`
  caracteres ficam marcados: digitar troca a parte marcada.
  - Enter termina e devolve `LISTA_EDITOU`;
  - Esc desiste e devolve `LISTA_DESISTIU`;
  - um clique fora também termina;
  - "/" não entra no nome.

**No Arquivos.**
- O menu do botão direito no vazio tem Nova pasta, Novo arquivo de texto,
  Nova página HTML, Atualizar e Voltar.
- Num item, o menu tem Abrir (com o nome do programa), Abrir no Editor,
  Renomear, Apagar e os "Novo".
- Com vários marcados, o menu oferece "Apagar os N itens...".
- Criar dá um nome livre ("Nova pasta", "Nova pasta (2)"...) e já abre a
  edição na linha. Nos arquivos, só o nome fica marcado, sem a extensão: quem
  digita "notas" tem "notas.txt".
- A página HTML nasce com um esqueleto mínimo.
- F2 e o menu Renomear usam a mesma edição.
- O renomear recusa:
  - nome vazio (fica o nome antigo);
  - nome que começa com ponto, porque esses ficam escondidos;
  - nome que já existe na pasta.
- Apagar, Apagar de vez e Restaurar valem para todos os marcados, com uma
  pergunta só.

**Mudança de comportamento.** O botão "Nova pasta" não abre mais a janela que
pedia o nome. O `h23_arquivos` passou a esperar a edição na linha.
