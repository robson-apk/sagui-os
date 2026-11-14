#!/usr/bin/env python3
"""H64 a H66 - o compilador C do SaguiOS (frente, fundo e o comando cc).

Critérios:
  H64: o manual docs/c-do-sagui.md existe, com a lista do que fica de fora,
       e os programas completos que ele mostra compilam.
  H65: os programas de tests/compilador/erros/ (70, o PRD pede 50) dão
       exatamente a mensagem esperada: arquivo:linha:coluna, o texto do erro,
       a linha do código com o ^ no lugar e a dica; e o compilador termina
       com código diferente de zero. Os 50 programas de
       tests/compilador/validos/ compilam sem erro.
  H66: os 50 programas válidos, compilados e ligados à libsagui pelo
       compilador do SaguiOS, rodam DENTRO do SaguiOS (QEMU) e escrevem a
       saída esperada, com o código de saída esperado.
  cc:  o comando cc, dentro do SaguiOS, compila programas vindos do pendrive
       (um deles em dois arquivos, com cabeçalho próprio); os executáveis
       saem byte a byte iguais aos feitos fora, rodam, e um programa com
       erro recebe a mensagem com linha, coluna e dica.

O mesmo código de userland/compilador é compilado para o Linux (com o
"ambiente" de tests/compilador/cc_linux.c) para as partes que não precisam do
QEMU. Antes do QEMU há ainda uma passada rápida: os 50 programas rodam no
próprio Linux, ligados a uma libsagui em que só a camada de chamadas de
sistema foi trocada (tests/compilador/chamadas_linux.c).

As saídas esperadas dos programas válidos (*.esperado) foram conferidas
contra os mesmos programas compilados pelo GCC:
    python3 tests/h64_h66_compilador.py --conferir-gcc
Para regravar as mensagens esperadas depois de mudar um texto do compilador
(e conferir o resultado no git diff):
    python3 tests/h64_h66_compilador.py --regravar
"""
import glob
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, FalhaTeste, verificar, criar_pendrive, pendrive_qemu, imagem_console, mtype, RAIZ  # noqa: E402

AQUI = os.path.join(RAIZ, "tests", "compilador")
B = os.path.join(RAIZ, "build", "compilador")
CC = os.path.join(B, "cc_linux")
LIB = os.path.join(RAIZ, "build", "userland", "libsagui.o")
LIB_LINUX = os.path.join(B, "libsagui-linux.o")
INC = ["-I", os.path.join(RAIZ, "userland", "inc"), "-I", os.path.join(RAIZ, "kernel", "inc")]
UCFLAGS = ("-m32 -march=i686 -std=gnu11 -O2 -ffreestanding -fno-builtin -nostdlib -fno-pic -fno-pie "
           "-fno-stack-protector -fno-asynchronous-unwind-tables -mgeneral-regs-only -ffunction-sections "
           "-fdata-sections -finput-charset=UTF-8 -fexec-charset=ISO-8859-1").split() + \
          ["-I" + os.path.join(RAIZ, "userland", "inc"), "-idirafter", os.path.join(RAIZ, "kernel", "inc")]
# Compilados DENTRO do SaguiOS (os outros 50 são compilados fora e só rodam lá).
DA_VERSAO_1 = ["wc", "cat", "calculadora", "banana"]
NO_SAGUIOS = ["v17_struct_valor", "v26_recursao", "v34_longo64", "v37_include", "v50_interpretador"]


def rodar(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, **kw)


def preparar():
    """O compilador para o Linux e a libsagui com as chamadas do Linux."""
    os.makedirs(B, exist_ok=True)
    fontes = sorted(f for f in glob.glob(os.path.join(RAIZ, "userland", "compilador", "*.c")) if not f.endswith("/cc.c"))
    r = rodar(["gcc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-fno-builtin", "-o", CC, os.path.join(AQUI, "cc_linux.c")] + fontes)
    verificar(r.returncode == 0 and not r.stderr, "o núcleo do compilador compila para o Linux sem avisos" + r.stderr.decode()[:400])
    r = rodar(["gcc"] + UCFLAGS + ["-c", "-o", os.path.join(B, "chamadas_linux.o"), os.path.join(AQUI, "chamadas_linux.c")])
    if r.returncode != 0:
        raise FalhaTeste("chamadas_linux.c não compilou: " + r.stderr.decode()[:400])
    objs = [o for o in sorted(glob.glob(os.path.join(RAIZ, "build", "userland", "libsagui", "*.o")))
            if os.path.basename(o) not in ("chamadas.o", "inicio.asm.o", "tls_ponte.o", "http.o", "rede.o")]
    r = rodar(["ld", "-m", "elf_i386", "-r", "-z", "noexecstack", "-o", LIB_LINUX] + objs + [os.path.join(B, "chamadas_linux.o")])
    if r.returncode != 0:
        raise FalhaTeste("a libsagui de teste não ligou: " + r.stderr.decode()[:400])


def argumentos(fonte):
    m = re.search(r"^// args: (.*)$", open(fonte, encoding="utf-8").read(), re.M)
    return m.group(1) if m else ""


def separar_args(texto):
    return [a or b for a, b in re.findall(r'"([^"]*)"|(\S+)', texto)]


def rodar_no_linux(programa, args):
    r = rodar([programa] + separar_args(args), timeout=20, cwd=B)
    return r.stdout + f"[codigo {r.returncode}]\n".encode()


def erros(regravar):
    pasta = os.path.join(AQUI, "erros")
    fontes = sorted(glob.glob(os.path.join(pasta, "e*.c")))
    falhas = []
    for f in fontes:
        nome = os.path.basename(f)
        r = rodar([CC, nome] + INC + ["-L", LIB, "-o", os.path.join(B, "erro.bin")], cwd=pasta)
        esperado = f[:-2] + ".esperado"
        if regravar:
            open(esperado, "wb").write(r.stderr)
        quer = open(esperado, "rb").read() if os.path.exists(esperado) else b"(falta o arquivo .esperado)"
        if r.returncode != 1 or r.stdout or r.stderr != quer:
            falhas.append(f"{nome}: código {r.returncode}\n--- veio:\n{r.stderr.decode()}--- esperado:\n{quer.decode()}")
            continue
        texto = r.stderr.decode()
        # Forma da mensagem: posição, linha com ^, dica (os dois erros do ligador não têm posição).
        if not texto.startswith("cc: erro: "):
            if not re.match(rf"{re.escape(nome)}:\d+:\d+: erro: .+\n +\d+ \| .*\n +\| *\t*\^\n", texto):
                falhas.append(f"{nome}: a mensagem não tem a forma arquivo:linha:coluna + linha + ^:\n{texto}")
        if "\ndica: " not in texto:
            falhas.append(f"{nome}: a mensagem não traz dica:\n{texto}")
    if falhas:
        raise FalhaTeste(f"{len(falhas)} de {len(fontes)} programas com erro não deram a mensagem esperada:\n" + "\n".join(falhas[:5]))
    verificar(len(fontes) >= 50, f"H65: {len(fontes)} programas com erro dão a mensagem esperada (posição, linha com ^ e dica) e código 1")


def validos(regravar):
    pasta = os.path.join(AQUI, "validos")
    fontes = sorted(glob.glob(os.path.join(pasta, "v*.c")))
    feitos = {}
    for f in fontes:
        nome = os.path.basename(f)[:-2]
        saida = os.path.join(B, nome + ".bin")
        r = rodar([CC, f] + INC + ["-L", LIB, "-o", saida])
        if r.returncode != 0 or r.stderr:
            raise FalhaTeste(f"{nome} devia compilar sem erro:\n{r.stderr.decode()}")
        d = open(saida, "rb").read()
        if d[:4] != b"\x7fELF" or d[16] != 2 or d[18] != 3:
            raise FalhaTeste(f"{nome}: a saída não é um executável ELF de 32 bits")
        feitos[nome] = (f, d)
    verificar(len(fontes) >= 50, f"H65: {len(fontes)} programas válidos compilam e viram executáveis ELF do SaguiOS")

    # O Editor do SaguiOS grava em Latin-1; os arquivos vindos de fora são UTF-8. Dá no mesmo.
    latin = os.path.join(B, "latin1.c")
    open(latin, "wb").write(open(feitos["v15_textos"][0], encoding="utf-8").read().encode("latin-1"))
    r = rodar([CC, latin] + INC + ["-L", LIB, "-o", latin + ".bin"])
    verificar(r.returncode == 0 and open(latin + ".bin", "rb").read() == feitos["v15_textos"][1],
              "um arquivo gravado em Latin-1 (como o Editor do SaguiOS grava) dá o mesmo executável que em UTF-8")

    # Passada rápida no Linux, com a libsagui de teste.
    falhas = []
    for nome, (f, _) in feitos.items():
        prog = os.path.join(B, nome + ".linux")
        r = rodar([CC, f] + INC + ["-L", LIB_LINUX, "-o", prog])
        if r.returncode != 0:
            raise FalhaTeste(f"{nome} não ligou com a libsagui de teste:\n{r.stderr.decode()}")
        os.chmod(prog, 0o755)
        veio = rodar_no_linux(prog, argumentos(f))
        esperado = f[:-2] + ".esperado"
        if regravar:
            open(esperado, "wb").write(veio)
        if not os.path.exists(esperado) or veio != open(esperado, "rb").read():
            falhas.append(nome)
    if falhas:
        raise FalhaTeste("no Linux, saída diferente da esperada em: " + ", ".join(falhas))
    verificar(True, f"os {len(feitos)} programas, ligados à libsagui com as chamadas do Linux, dão a saída esperada")
    return feitos


def manual():
    """Os programas completos que o manual mostra têm de compilar."""
    texto = open(os.path.join(RAIZ, "docs", "c-do-sagui.md"), encoding="utf-8").read()
    blocos = [b for b in re.findall(r"```c\n(.*?)```", texto, re.S) if "int principal(" in b]
    for i, b in enumerate(blocos):
        f = os.path.join(B, f"manual{i + 1}.c")
        open(f, "w", encoding="utf-8").write(b)
        r = rodar([CC, f] + INC + ["-L", LIB, "-o", os.path.join(B, f"manual{i + 1}.bin")])
        if r.returncode != 0:
            raise FalhaTeste(f"o exemplo {i + 1} do manual não compila:\n{r.stderr.decode()}")
    faltam = [s for s in ("fica de fora", "libsagui", "principal", "imprimir") if s not in texto]
    verificar(len(blocos) >= 3 and not faltam, f"H64: docs/c-do-sagui.md existe e os {len(blocos)} programas completos dele compilam")


def conferir_gcc():
    """Compila cada programa válido também com o GCC e compara as saídas."""
    pasta = os.path.join(AQUI, "validos")
    diferentes, sem_gcc = [], []
    for f in sorted(glob.glob(os.path.join(pasta, "v*.c"))):
        nome = os.path.basename(f)[:-2]
        obj, prog = os.path.join(B, nome + ".gcc.o"), os.path.join(B, nome + ".gcc")
        r = rodar(["gcc"] + UCFLAGS + ["-w", "-c", "-o", obj, f])
        if r.returncode == 0:
            r = rodar(["ld", "-m", "elf_i386", "-nostdlib", "-T", os.path.join(RAIZ, "userland", "programa.ld"), "--gc-sections",
                       "-z", "noexecstack", "--no-warn-rwx-segments", "-o", prog, obj, LIB_LINUX])
        if r.returncode != 0:
            sem_gcc.append(nome)      # ex.: divisão de 64 bits, que o GCC pede à libgcc
            continue
        if rodar_no_linux(prog, argumentos(f)) != open(f[:-2] + ".esperado", "rb").read():
            diferentes.append(nome)
    print("  sem versão do GCC (não ligam sem a libgcc):", ", ".join(sem_gcc) or "nenhum")
    verificar(not diferentes, "as saídas esperadas são as dos mesmos programas compilados pelo GCC" + (": diferem " + ", ".join(diferentes) if diferentes else ""))


def saida_do_programa(texto):
    """Do que saiu na serial durante um comando: (saída do programa, código)."""
    m = re.search(r"-> processo (\d+)\n(.*)\[SAGUI\] processo \1 \([^)]*\) terminou com codigo (-?\d+)", texto, re.S)
    if not m:
        return None, None
    return m.group(2), int(m.group(3))


def no_saguios(feitos):
    arquivos = {}
    curtos = {}
    for nome, (f, d) in feitos.items():
        curtos[nome] = nome[:3]                       # v01 ... v50
        arquivos["bin/" + nome[:3]] = d
    pasta = os.path.join(AQUI, "validos")
    for nome in NO_SAGUIOS:
        arquivos[f"src/{nome}.c"] = open(os.path.join(pasta, nome + ".c"), "rb").read()
    arquivos["src/v37_cabecalho.h"] = open(os.path.join(pasta, "v37_cabecalho.h"), "rb").read()
    # Um programa em dois arquivos e um com erro (com acento na linha do erro).
    arquivos["src/parte1.c"] = ('#include "sagui.h"\nint dobro(int x);\nextern int base;\n'
                                'int principal(int argc, char **argv)\n{\n    (void)argv;\n'
                                '    imprimir("%d\\n", dobro(base + argc));\n    return 0;\n}\n')
    arquivos["src/parte2.c"] = 'int base = 20;\nint dobro(int x) { return x * 2; }\n'
    arquivos["src/ruim.c"] = ('#include "sagui.h"\n\nint principal(int argc, char **argv)\n{\n'
                              '    int maçãs = argc;   /* não pode */\n    return 0;\n}\n')
    for nome in DA_VERSAO_1:
        arquivos[f"v1/{nome}.c"] = open(os.path.join(RAIZ, "userland", "programas", nome + ".c"), "rb").read()
    pen = criar_pendrive("h66-pen", rotulo="DADOS", pastas=("bin", "src", "out", "v1"), arquivos=arquivos)
    with Maquina("h66", "ide", imagem=imagem_console(), extras=["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen)) as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        out = m.comando("montar")
        verificar("montado em /usb" in out, "pendrive com os programas montado em /usb")

        falhas = []
        for nome, (f, _) in feitos.items():
            args = argumentos(f)
            out = m.comando(f"/usb/bin/{curtos[nome]}" + (" " + args if args else ""), tempo=90)
            texto, codigo = saida_do_programa(out)
            esperado = open(f[:-2] + ".esperado", "rb").read().decode("latin-1")
            if texto is None or texto + f"[codigo {codigo}]\n" != esperado:
                falhas.append(nome)
                print(f"  DIFERENTE {nome}:\n--- veio:\n{texto}[codigo {codigo}]\n--- esperado:\n{esperado}")
        if falhas:
            raise FalhaTeste(f"no SaguiOS, {len(falhas)} programas não deram a saída esperada: " + ", ".join(falhas))
        verificar(True, f"H66: os {len(feitos)} programas válidos rodam no SaguiOS com a saída e o código de saída esperados")

        # O cc dentro do SaguiOS.
        out = m.comando("ls /bin/cc /sistema/lib /sistema/inc")
        verificar("libsagui.o" in out and "sagui.h" in out and "gui.h" in out and "abi.h" in out,
                  "a imagem traz o cc, /sistema/lib/libsagui.o e os cabeçalhos em /sistema/inc")
        tempos = []
        for nome in NO_SAGUIOS:
            f = feitos[nome][0]
            ini = time.time()
            out = m.comando(f"cc /usb/src/{nome}.c -o /usb/out/{nome[:3]}", tempo=150)
            tempos.append(time.time() - ini)
            if "(cc) terminou com codigo 0" not in out:
                raise FalhaTeste(f"cc {nome}.c falhou dentro do SaguiOS:\n{out}")
            args = argumentos(f)
            out = m.comando(f"/usb/out/{nome[:3]}" + (" " + args if args else ""), tempo=90)
            texto, codigo = saida_do_programa(out)
            esperado = open(f[:-2] + ".esperado", "rb").read().decode("latin-1")
            if texto is None or texto + f"[codigo {codigo}]\n" != esperado:
                raise FalhaTeste(f"{nome} compilado dentro do SaguiOS deu saída diferente:\n{texto}")
        verificar(True, f"cc dentro do SaguiOS: {len(NO_SAGUIOS)} programas compilados lá rodam com a saída esperada "
                        f"(o mais demorado levou {max(tempos):.1f} s no QEMU)")

        out = m.comando("cc /usb/src/parte1.c /usb/src/parte2.c -o /tmp/partes", tempo=150)
        verificar("(cc) terminou com codigo 0" in out, "cc com dois arquivos .c")
        out = m.comando("/tmp/partes x y")
        verificar(saida_do_programa(out) == ("46\n", 0), "o programa de dois arquivos roda: (20 + 3) * 2 = 46")
        out = m.comando("cc /usb/src/v26_recursao.c")
        out2 = m.comando("ls /usb/src")
        verificar("(cc) terminou com codigo 0" in out and re.search(r"^v26_recursao +\d+ bytes", out2, re.M),
                  "sem -o, o executável ganha o nome do arquivo sem o .c, na mesma pasta")

        # Programas de verdade da versão 1.0 (com janela, long long, gui.h): só compilar aqui;
        # rodá-los pelos testes da 1.0 é a H68.
        for nome in DA_VERSAO_1:
            out = m.comando(f"cc /usb/v1/{nome}.c -o /usb/out/{nome[:6]}", tempo=150)
            if "(cc) terminou com codigo 0" not in out:
                raise FalhaTeste(f"cc {nome}.c (programa da versão 1.0) falhou dentro do SaguiOS:\n{out}")
        verificar(True, "cc dentro do SaguiOS compila os programas da versão 1.0: " + ", ".join(DA_VERSAO_1))

        out = m.comando("cc /usb/src/ruim.c -o /tmp/ruim", tempo=150)
        verificar("(cc) terminou com codigo 1" in out, "programa com erro: o cc termina com código 1")
        quer = ("/usb/src/ruim.c:5:11: erro: letra com acento (ou símbolo) dentro de um nome\n"
                "    5 |     int maçãs = argc;   /* não pode */\n"
                "      |           ^\n"
                "dica: nomes de variáveis e funções só podem ter letras sem acento")
        verificar(quer in out, "a mensagem no SaguiOS traz arquivo:linha:coluna, a linha (acentos em Latin-1), o ^ e a dica")
        out = m.comando("ls /tmp")
        verificar("ruim" not in out, "com erro, nenhum executável é gravado")
        out = m.comando("cc /usb/src/v01_nao_existe.c")
        verificar("não achei o arquivo" in out and "(cc) terminou com codigo 1" in out, "arquivo que não existe: mensagem clara")
        out = m.comando("cc /usb/src/v50_interpretador.c --ri | wc")
        n = re.search(r"(\d+) linhas", out)
        verificar(n and int(n.group(1)) > 300, f"cc --ri lista a representação intermediária ({n.group(1) if n else 0} linhas)")
        m.comando("desmontar")
        time.sleep(1)
    for nome in DA_VERSAO_1:
        fora = os.path.join(B, nome + ".v1.bin")
        r = rodar([CC, os.path.join(RAIZ, "userland", "programas", nome + ".c")] + INC + ["-L", LIB, "-o", fora])
        if r.returncode != 0 or mtype(pen, f"/out/{nome[:6]}") != open(fora, "rb").read():
            raise FalhaTeste(f"{nome}: o executável feito dentro do SaguiOS difere do feito fora")
    iguais = [nome for nome in NO_SAGUIOS if mtype(pen, f"/out/{nome[:3]}") == feitos[nome][1]]
    verificar(len(iguais) == len(NO_SAGUIOS),
              "os executáveis feitos pelo cc dentro do SaguiOS são byte a byte iguais aos feitos pelo mesmo compilador no Linux")


def main():
    regravar = "--regravar" in sys.argv
    preparar()
    manual()
    erros(regravar)
    feitos = validos(regravar)
    if "--conferir-gcc" in sys.argv:
        conferir_gcc()
    if "--sem-qemu" in sys.argv or regravar:
        return 0
    no_saguios(feitos)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except FalhaTeste as e:
        print("FALHOU:", e)
        sys.exit(1)
