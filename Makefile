# Makefile do SaguiOS
#
# Alvos principais:
#   make            -> gera build/saguios.img (imagem para pendrive)
#   make vdi        -> gera também build/saguios.vdi (VirtualBox)
#   make test       -> roda todos os testes automáticos no QEMU
#   make rodar      -> abre o sistema no QEMU (precisa de janela; na VPS use os testes)
#   make run        -> abre no QEMU como um PC completo: 768 MB, rede e1000,
#                      som intel-hda e USB 2.0 (EHCI). O som sai pelo
#                      PulseAudio; troque com SOM=alsa (ou SOM=none)
#   make entrega FASE=N -> copia .img, .vdi e SHA-256 para entrega/faseN/
#   make vazio      -> alvo vazio (critério da H01)
#   make limpar     -> apaga build/

# O GCC do sistema em modo 32 bits e freestanding faz o papel do compilador
# cruzado (ver docs/decisoes.md, D001).
CC      := gcc
AS      := nasm
LD      := ld
PY      := python3

CFLAGS  := -m32 -march=i686 -std=gnu11 -O2 -g \
           -ffreestanding -fno-builtin -nostdlib -fno-pic -fno-pie \
           -fno-stack-protector -fno-asynchronous-unwind-tables \
           -mgeneral-regs-only -fno-omit-frame-pointer \
           -Wall -Wextra -Werror=implicit-function-declaration \
           -finput-charset=UTF-8 -fexec-charset=ISO-8859-1 \
           -DSAGUI_KERNEL -Ikernel/inc
LDFLAGS := -m elf_i386 -nostdlib --no-dynamic-linker -z noexecstack --no-warn-rwx-segments

B       := build

KERNEL_C   := $(shell find kernel -name '*.c' | sort)
KERNEL_ASM := $(shell find kernel -name '*.asm' | sort)
KERNEL_OBJ := $(patsubst %.c,$(B)/%.o,$(KERNEL_C)) $(patsubst %.asm,$(B)/%.asm.o,$(KERNEL_ASM)) \
              $(B)/gerado/fonte8x16.o $(B)/gerado/arte_icones.o
KERNEL_HDR := $(shell find kernel -name '*.h')
ROOTFS     := $(shell find rootfs -type f | sort)

# Programas de usuário: libsagui + um .c por programa em userland/programas/
# (vão para /bin) e em userland/testes/ (vão para /testes).
# H80: os programas podem usar ponto flutuante (o kernel salva a FPU de cada
# processo); o kernel continua com -mgeneral-regs-only. As contas vão pelo
# SSE2 (double exato, IEEE 754, como o JavaScript pede), e não pela FPU x87,
# que calcula com 80 bits por dentro e dá resultados que mudam conforme o
# compilador guarda ou não os valores na memória.
# -mstackrealign: com o SSE2, o gcc guarda valores de 16 bytes na pilha com
# instruções que exigem alinhamento. Cada função alinha a própria pilha ao
# entrar, então uma pilha trocada à mão (motor, JavaScript, compilador) não
# derruba o programa num movdqa, mesmo quando quem chama é código da libsagui.
# H88: botao_t ganhou campos no fim (ícone, ativo); quem escreve { x, y, l, a,
# texto } deixa o resto em zero, que é o certo, então o aviso fica desligado.
UCFLAGS := -m32 -march=i686 -msse2 -mfpmath=sse -std=gnu11 -O2 \
           -mstackrealign \
           -ffreestanding -fno-builtin -nostdlib -fno-pic -fno-pie \
           -fno-stack-protector -fno-asynchronous-unwind-tables \
           -ffunction-sections -fdata-sections \
           -Wall -Wextra -Werror=implicit-function-declaration \
           -Wno-missing-field-initializers \
           -finput-charset=UTF-8 -fexec-charset=ISO-8859-1 \
           -Iuserland/inc -idirafter kernel/inc
ULDFLAGS := -m elf_i386 -nostdlib -T userland/programa.ld --gc-sections -z max-page-size=4096 \
            -z noexecstack --no-warn-rwx-segments
# A criptografia (H50-H52) e o TLS (H53) entram na libsagui; só o que cada
# programa usa vai para ele (--gc-sections).
CRIPTO_OBJ := $(patsubst userland/%.c,$(B)/userland/%.o,$(wildcard userland/cripto/*.c) $(wildcard userland/tls/*.c) $(wildcard userland/ssh/*.c))
LIBSAGUI_OBJ := $(CRIPTO_OBJ) $(B)/userland/fonte8x16.o $(B)/userland/icones.o \
                $(patsubst userland/libsagui/%.c,$(B)/userland/libsagui/%.o,$(wildcard userland/libsagui/*.c)) \
                $(patsubst userland/libsagui/%.asm,$(B)/userland/libsagui/%.asm.o,$(wildcard userland/libsagui/*.asm))
# (userland/programas/*.h: código dividido entre dois programas, como o motor
# de expressões do sed e do expr, H91.)
USER_HDR  := $(wildcard userland/inc/*.h) $(wildcard userland/cripto/*.h) $(wildcard userland/tls/*.h) $(wildcard userland/ssh/*.h) \
            $(wildcard userland/programas/*.h) kernel/inc/abi.h
PROGRAMAS := $(patsubst userland/programas/%.c,$(B)/userland/bin/%,$(wildcard userland/programas/*.c))
# O Sagui 3D (B4) tem vários arquivos, numa pasta própria.
SAGUI3D_OBJ := $(patsubst userland/sagui3d/%.c,$(B)/userland/sagui3d/%.o,$(wildcard userland/sagui3d/*.c))
PROGRAMAS += $(B)/userland/bin/sagui3d
# O navegador (fase 6 da 2.0), que na 2.1 é o Cipó (H82): o motor de páginas,
# as fontes, o JavaScript e os gráficos ficam em subpastas, e o código de
# terceiros que ele usa fica em terceiros/ (D041, D043).
NAVEGADOR_C   := $(wildcard userland/navegador/*.c userland/navegador/*/*.c) \
                 terceiros/font-rs/acumulador.c terceiros/libwebp-vp8/vp8.c terceiros/v8-numeros/numero_base.c
NAVEGADOR_OBJ := $(patsubst %.c,$(B)/nav/%.o,$(NAVEGADOR_C))
NAVEGADOR_HDR := $(wildcard userland/navegador/*.h userland/navegador/*/*.h terceiros/*/*.h) $(B)/gerado/js_base.h
PROGRAMAS += $(B)/userland/bin/navegador $(B)/userland/bin/cipo
# O tocador de vídeo (H82): MP4 com H.264 e AAC, do pull request #6.
VIDEO_OBJ := $(patsubst %.c,$(B)/nav/%.o,$(wildcard userland/video/*.c))
PROGRAMAS += $(B)/userland/bin/video
# H87: o visualizador de imagens usa os decodificadores do Cipó, o tocador de
# música o MP4 e o AAC do tocador de vídeo, e o unzip o DEFLATE do Cipó.
IMAGENS_OBJ := $(patsubst %.c,$(B)/nav/%.o,userland/navegador/imagens.c userland/navegador/png.c \
                 userland/navegador/jpeg.c userland/navegador/descompactar.c terceiros/libwebp-vp8/vp8.c)
VISUALIZADOR_OBJ := $(B)/nav/userland/visualizador/visualizador.o $(IMAGENS_OBJ)
MUSICA_OBJ := $(B)/nav/userland/musica/musica.o $(B)/nav/userland/video/mp4.o $(B)/nav/userland/video/aac.o
UNZIP_OBJ := $(B)/nav/userland/unzip/unzip.o $(B)/nav/userland/navegador/descompactar.o
PROGRAMAS += $(B)/userland/bin/visualizador $(B)/userland/bin/musica $(B)/userland/bin/unzip
# O decodificador de MP3 do Tocador (H86), escrito do zero (D047), e o
# mp3info, o comando que o testa. (A janela do Tocador usa o mesmo mp3.o.)
MP3INFO_OBJ := $(B)/userland/tocador/mp3info.o $(B)/userland/tocador/mp3.o
PROGRAMAS += $(B)/userland/bin/mp3info
# O Sagui Code (fase 8): o núcleo do agente (ia.c e agente.c) com duas
# interfaces, o Terminal (saguicode) e a janela (saguicode-janela, H89).
SAGUICODE_NUCLEO := $(B)/userland/saguicode/ia.o $(B)/userland/saguicode/agente.o
SAGUICODE_OBJ := $(SAGUICODE_NUCLEO) $(B)/userland/saguicode/saguicode.o
SAGUICODE_JANELA_OBJ := $(SAGUICODE_NUCLEO) $(B)/userland/saguicode/janela.o
PROGRAMAS += $(B)/userland/bin/saguicode $(B)/userland/bin/saguicode-janela
# O Tocador de músicas (H87): a janela e, quando chegar, o decodificador de MP3.
TOCADOR_OBJ := $(B)/userland/tocador/tocador.o $(B)/userland/tocador/mp3.o
PROGRAMAS += $(B)/userland/bin/tocador
# O compilador C do SaguiOS (fase 7 da versão 2.0): o comando cc, a libsagui
# num objeto só (o que o cc liga aos programas) e os cabeçalhos que ele lê.
COMPILADOR_OBJ := $(patsubst userland/compilador/%.c,$(B)/userland/compilador/%.o,$(wildcard userland/compilador/*.c))
PROGRAMAS += $(B)/userland/bin/cc
CC_LIB := $(B)/userland/libsagui.o
CC_INC := userland/inc/sagui.h userland/inc/gui.h userland/inc/http.h kernel/inc/abi.h
PROG_TESTE := $(patsubst userland/testes/%.c,$(B)/userland/testes/%,$(wildcard userland/testes/*.c))

.PHONY: all vazio vdi test rodar run entrega limpar dashboard

all: $(B)/saguios.img

vazio:
	@echo "SaguiOS: alvo vazio ok"

# ---------- carregador de boot ----------
$(B)/boot/mbr.bin: boot/mbr.asm boot/cabecalho.inc
	@mkdir -p $(dir $@)
	$(AS) -f bin -I boot/ -o $@ $<

$(B)/boot/estagio2.bin: boot/estagio2.asm $(wildcard boot/*.inc)
	@mkdir -p $(dir $@)
	$(AS) -f bin -I boot/ -o $@ $<

# ---------- kernel ----------
$(B)/kernel/%.o: kernel/%.c $(KERNEL_HDR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(B)/kernel/%.asm.o: kernel/%.asm
	@mkdir -p $(dir $@)
	$(AS) -f elf32 -I kernel/arch/ -o $@ $<

# A fonte é desenhada em texto (tools/fonte/) e vira C no build.
$(B)/gerado/fonte8x16.c: tools/fonte/sagui8x16.txt tools/mkfonte.py
	@mkdir -p $(dir $@)
	$(PY) tools/mkfonte.py $< $@

$(B)/gerado/fonte8x16.o: $(B)/gerado/fonte8x16.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Os ícones (H81, do Robson) são descritos em tools/mkarte.py e viram C no build.
$(B)/gerado/arte_icones.c: tools/mkarte.py
	@mkdir -p $(dir $@)
	$(PY) tools/mkarte.py $@

$(B)/gerado/arte_icones.o: $(B)/gerado/arte_icones.c kernel/inc/arte.h
	$(CC) $(CFLAGS) -c -o $@ $<

# H88: os mesmos ícones, em 20x20, para os programas (userland/inc/icones.h).
$(B)/gerado/icones_programas.c: tools/mkarte.py
	@mkdir -p $(dir $@)
	$(PY) tools/mkarte.py --programas $@

$(B)/userland/icones.o: $(B)/gerado/icones_programas.c userland/inc/icones.h
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -c -o $@ $<

$(B)/kernel.elf: $(KERNEL_OBJ) kernel/ligacao.ld
	$(LD) $(LDFLAGS) -T kernel/ligacao.ld -o $@ $(KERNEL_OBJ)

$(B)/kernel.bin: $(B)/kernel.elf
	objcopy -O binary $< $@

# ---------- programas de usuário ----------
$(B)/userland/%.o: userland/%.c $(USER_HDR)
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -c -o $@ $<

$(B)/userland/fonte8x16.o: $(B)/gerado/fonte8x16.c
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -c -o $@ $<

$(B)/userland/libsagui/%.asm.o: userland/libsagui/%.asm
	@mkdir -p $(dir $@)
	$(AS) -f elf32 -o $@ $<

$(B)/userland/bin/%: $(B)/userland/programas/%.o $(LIBSAGUI_OBJ) userland/programa.ld
	@mkdir -p $(dir $@)
	$(LD) $(ULDFLAGS) -o $@.elf $< $(LIBSAGUI_OBJ)
	objcopy --strip-all $@.elf $@

$(B)/userland/sagui3d/%.o: userland/sagui3d/%.c userland/sagui3d/jogo.h $(USER_HDR)
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -c -o $@ $<

$(B)/userland/bin/sagui3d: $(SAGUI3D_OBJ) $(LIBSAGUI_OBJ) userland/programa.ld
	@mkdir -p $(dir $@)
	$(LD) $(ULDFLAGS) -o $@.elf $(SAGUI3D_OBJ) $(LIBSAGUI_OBJ)
	objcopy --strip-all $@.elf $@

# -fno-math-errno: sqrt vira a instrução do SSE2 (não há libm).
NAV_CFLAGS := $(UCFLAGS) -fno-math-errno -Iuserland/navegador -I$(B)/gerado

$(B)/nav/%.o: %.c $(NAVEGADOR_HDR) $(USER_HDR)
	@mkdir -p $(dir $@)
	$(CC) $(NAV_CFLAGS) -c -o $@ $<

# O JavaScript embutido (Promise, classes do DOM...) vira uma string em C.
$(B)/gerado/js_base.h: userland/navegador/js/js_base.js tools/mkjsbase.py
	@mkdir -p $(dir $@)
	$(PY) tools/mkjsbase.py $< $@

$(B)/userland/bin/navegador: $(NAVEGADOR_OBJ) $(LIBSAGUI_OBJ) userland/programa.ld
	@mkdir -p $(dir $@)
	$(LD) $(ULDFLAGS) -o $@.elf $(NAVEGADOR_OBJ) $(LIBSAGUI_OBJ)
	objcopy --strip-all $@.elf $@

$(B)/nav/userland/video/%.o: userland/video/%.c $(wildcard userland/video/*.h terceiros/*/*.h) $(USER_HDR)
	@mkdir -p $(dir $@)
	$(CC) $(NAV_CFLAGS) -c -o $@ $<

$(B)/userland/bin/video: $(VIDEO_OBJ) $(LIBSAGUI_OBJ) userland/programa.ld
	@mkdir -p $(dir $@)
	$(LD) $(ULDFLAGS) -o $@.elf $(VIDEO_OBJ) $(LIBSAGUI_OBJ)
	objcopy --strip-all $@.elf $@

$(B)/nav/userland/visualizador/%.o: userland/visualizador/%.c $(USER_HDR) userland/inc/compactar.h $(wildcard userland/video/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(NAV_CFLAGS) -c -o $@ $<

$(B)/nav/userland/musica/%.o: userland/musica/%.c $(USER_HDR) userland/inc/compactar.h $(wildcard userland/video/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(NAV_CFLAGS) -c -o $@ $<

$(B)/nav/userland/unzip/%.o: userland/unzip/%.c $(USER_HDR) userland/inc/compactar.h $(wildcard userland/video/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(NAV_CFLAGS) -c -o $@ $<

$(B)/userland/bin/visualizador: $(VISUALIZADOR_OBJ) $(LIBSAGUI_OBJ) userland/programa.ld
	@mkdir -p $(dir $@)
	$(LD) $(ULDFLAGS) -o $@.elf $(VISUALIZADOR_OBJ) $(LIBSAGUI_OBJ)
	objcopy --strip-all $@.elf $@

$(B)/userland/bin/musica: $(MUSICA_OBJ) $(LIBSAGUI_OBJ) userland/programa.ld
	@mkdir -p $(dir $@)
	$(LD) $(ULDFLAGS) -o $@.elf $(MUSICA_OBJ) $(LIBSAGUI_OBJ)
	objcopy --strip-all $@.elf $@

$(B)/userland/bin/unzip: $(UNZIP_OBJ) $(LIBSAGUI_OBJ) userland/programa.ld
	@mkdir -p $(dir $@)
	$(LD) $(ULDFLAGS) -o $@.elf $(UNZIP_OBJ) $(LIBSAGUI_OBJ)
	objcopy --strip-all $@.elf $@

# /bin/cipo é o mesmo programa com o nome novo (H82).
$(B)/userland/bin/cipo: $(B)/userland/bin/navegador
	cp $< $@

# -mstackrealign: o MP3 faz contas em float com SSE2, e o gcc pode guardar
# valores alinhados em 16 bytes na pilha (como no navegador).
$(B)/userland/tocador/%.o: userland/tocador/%.c $(wildcard userland/tocador/*.h) $(USER_HDR)
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -mstackrealign -c -o $@ $<

$(B)/userland/bin/tocador: $(TOCADOR_OBJ) $(LIBSAGUI_OBJ) userland/programa.ld
	@mkdir -p $(dir $@)
	$(LD) $(ULDFLAGS) -o $@.elf $(TOCADOR_OBJ) $(LIBSAGUI_OBJ)
	objcopy --strip-all $@.elf $@

$(B)/userland/bin/mp3info: $(MP3INFO_OBJ) $(LIBSAGUI_OBJ) userland/programa.ld
	@mkdir -p $(dir $@)
	$(LD) $(ULDFLAGS) -o $@.elf $(MP3INFO_OBJ) $(LIBSAGUI_OBJ)
	objcopy --strip-all $@.elf $@

$(B)/userland/saguicode/%.o: userland/saguicode/%.c userland/saguicode/ia.h userland/saguicode/agente.h $(USER_HDR)
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -c -o $@ $<

$(B)/userland/bin/saguicode: $(SAGUICODE_OBJ) $(LIBSAGUI_OBJ) userland/programa.ld
	@mkdir -p $(dir $@)
	$(LD) $(ULDFLAGS) -o $@.elf $(SAGUICODE_OBJ) $(LIBSAGUI_OBJ)
	objcopy --strip-all $@.elf $@

$(B)/userland/bin/saguicode-janela: $(SAGUICODE_JANELA_OBJ) $(LIBSAGUI_OBJ) userland/programa.ld
	@mkdir -p $(dir $@)
	$(LD) $(ULDFLAGS) -o $@.elf $(SAGUICODE_JANELA_OBJ) $(LIBSAGUI_OBJ)
	objcopy --strip-all $@.elf $@

# -Os: o cc vai dentro da imagem, e o tamanho pesa mais que a velocidade.
$(B)/userland/compilador/%.o: userland/compilador/%.c $(wildcard userland/compilador/*.h) $(USER_HDR)
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -Os -c -o $@ $<

$(B)/userland/bin/cc: $(COMPILADOR_OBJ) $(LIBSAGUI_OBJ) userland/programa.ld
	@mkdir -p $(dir $@)
	$(LD) $(ULDFLAGS) -o $@.elf $(COMPILADOR_OBJ) $(LIBSAGUI_OBJ)
	objcopy --strip-all $@.elf $@

# A libsagui inteira num objeto relocável: cada função continua na sua seção,
# e o ligador do cc só leva para o programa as que ele usa.
$(CC_LIB): $(LIBSAGUI_OBJ)
	@mkdir -p $(dir $@)
	$(LD) -m elf_i386 -r -z noexecstack -o $@ $(LIBSAGUI_OBJ)
	objcopy --strip-unneeded -R .comment -R .note.GNU-stack $@

$(B)/userland/testes/%: $(B)/userland/testes/%.o $(LIBSAGUI_OBJ) userland/programa.ld
	@mkdir -p $(dir $@)
	$(LD) $(ULDFLAGS) -o $@.elf $< $(LIBSAGUI_OBJ)
	objcopy --strip-all $@.elf $@

# ---------- ramdisk (SGFS) ----------
# O ramdisk é montado a partir de uma cópia do rootfs/ com os programas em
# /bin e os programas de teste em /testes.
$(B)/ramdisk.sgfs: $(ROOTFS) $(PROGRAMAS) $(PROG_TESTE) $(CC_LIB) $(CC_INC) docs/c-do-sagui.md tools/mksgfs.py
	rm -rf $(B)/rootfs
	cp -r rootfs $(B)/rootfs
	$(if $(PROGRAMAS),cp $(PROGRAMAS) $(B)/rootfs/bin/)
	$(if $(PROG_TESTE),mkdir -p $(B)/rootfs/testes && cp $(PROG_TESTE) $(B)/rootfs/testes/)
	mkdir -p $(B)/rootfs/sistema/lib $(B)/rootfs/sistema/inc $(B)/rootfs/sistema/ajuda
	cp $(CC_LIB) $(B)/rootfs/sistema/lib/libsagui.o
	cp $(CC_INC) $(B)/rootfs/sistema/inc/
	cp docs/c-do-sagui.md $(B)/rootfs/sistema/ajuda/   # o manual que o Sagui Code manda para a IA (H71)
	$(PY) tools/mksgfs.py $(B)/rootfs $@

# ---------- imagem final ----------
$(B)/saguios.img: $(B)/boot/mbr.bin $(B)/boot/estagio2.bin $(B)/kernel.bin $(B)/ramdisk.sgfs tools/mkimagem.py
	$(PY) tools/mkimagem.py --mbr $(B)/boot/mbr.bin --estagio2 $(B)/boot/estagio2.bin \
	    --kernel $(B)/kernel.bin --kernel-elf $(B)/kernel.elf --ramdisk $(B)/ramdisk.sgfs \
	    --saida $@ --vesa
	cd $(B) && sha256sum saguios.img > saguios.img.sha256

$(B)/saguios.vdi: $(B)/saguios.img
	rm -f $@ $(B)/vdi.raw
	cp $< $(B)/vdi.raw
	# disco virtual com tamanho múltiplo de 1 MB (ver D004)
	truncate -s %1M $(B)/vdi.raw
	qemu-img convert -f raw -O vdi $(B)/vdi.raw $@
	rm -f $(B)/vdi.raw
	cd $(B) && sha256sum saguios.vdi > saguios.vdi.sha256

vdi: $(B)/saguios.vdi

test: $(B)/saguios.img
	$(PY) tests/rodar_todos.py

rodar: $(B)/saguios.img
	qemu-system-i386 -m 512M -drive format=raw,file=$< -serial stdio

# Do pull request #7: o sistema com as placas que ele sabe usar, como no
# G470. "snapshot=on": o que for gravado no disco some ao fechar o QEMU (o
# SaguiOS roda na memória; a imagem fica como saiu do make).
SOM ?= pa
run: $(B)/saguios.img
	qemu-system-i386 -m 768M -drive format=raw,file=$<,snapshot=on -serial stdio \
	    -nic user,model=e1000 \
	    -audiodev $(SOM),id=som -device intel-hda -device hda-duplex,audiodev=som \
	    -device usb-ehci,id=ehci

entrega: $(B)/saguios.img $(B)/saguios.vdi
	@test -n "$(FASE)" || (echo "use: make entrega FASE=N" && false)
	mkdir -p entrega/fase$(FASE)
	cp $(B)/saguios.img $(B)/saguios.vdi entrega/fase$(FASE)/
	cd entrega/fase$(FASE) && sha256sum saguios.img saguios.vdi > SHA256SUMS.txt
	@cat entrega/fase$(FASE)/SHA256SUMS.txt

dashboard:
	$(PY) tools/status.py atualizar

limpar:
	rm -rf $(B)
