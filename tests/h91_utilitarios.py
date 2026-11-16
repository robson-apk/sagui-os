#!/usr/bin/env python3
"""H91 - utilitários de texto no estilo Unix (do fork SaguiOS-Extended, do
Alejandro Anuncibay, reescritos sobre o main atual).

Critério: cada programa, com entradas conhecidas num pendrive, dá a saída
esperada, lendo arquivos e a entrada (pipes do sagsh, como
"ls bin | sort -r | head -3"); os apelidos em português funcionam; erros
saem em português e com código de saída diferente de zero.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, criar_pendrive, pendrive_qemu, RAIZ  # noqa: E402

PROGRAMAS = sorted([f[:-2] for f in os.listdir(os.path.join(RAIZ, "userland", "programas")) if f.endswith(".c")]
                   + ["sagui3d", "navegador", "cipo", "video", "saguicode", "saguicode-janela", "cc", "tocador", "mp3info"])

FRUTAS = "banana 3\nmaçã 10\nabacaxi 2\nBanana 7\nuva 10\nuva 10\nkiwi 1.5\n"
NUMEROS = "".join(f"{i}\n" for i in range(1, 26))
CAMPOS = "a:b:c\nd:e:f\nsem\n1:2:3:4\n"

NOMES = ("head/inicio", "tail/fim", "sort/ordenar", "uniq/unicos", "cut/cortar",
         "tr/trocar", "seq/sequencia", "rev/inverter", "tac/de-tras", "nl/numerar",
         "diff/diferencas", "find/procurar-arquivo", "sed/editar-fluxo", "tree/arvore",
         "cal/calendario", "bc/calcular", "expr/expressao", "base64", "md5sum/md5", "od/octal", "xxd/hexa")

VELHO = "um\ndois\ntres\nquatro\ncinco\nseis\nsete\noito\nnove\ndez\nonze\ndoze\n"
NOVO = "um\nDOIS\ntres\nquatro\ncinco\nseis\nsete\noito\nnove\nnovo\ndez\nonze\n"

ARQUIVOS = {
    "frutas.txt": FRUTAS,
    "numeros.txt": NUMEROS,
    "campos.txt": CAMPOS,
    "velho.txt": VELHO,
    # O ^ é uma tecla morta no teclado ABNT2 (o QEMU não o digita sozinho):
    # as contas com potência ficam num arquivo.
    "contas.bc": "2^100\nx=0.1; x+0.2; -2^2; 7%3\nquit\n5\n",
    "novo.txt": NOVO,
    "arv/a.txt": "",
    "arv/sub1/b.txt": "bb\n",
    "arv/sub1/sub2/c.log": "",
    "arv/outra/dez.bin": "0123456789",
}
PASTAS = ("arv", "arv/sub1", "arv/sub1/sub2", "arv/outra")


def limpo(texto):
    """Tira as sequências de cor e de cursor (ESC [ ... letra)."""
    return re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", texto)


def rodar(m, comando):
    """Roda um comando e devolve só o que ele escreveu (sem o eco da linha
    digitada e sem o prompt seguinte)."""
    out = limpo(m.comando(comando))
    # O log do kernel ("[SAGUI] processo 7 ...") sai na mesma serial, entre
    # o eco da linha digitada e a saída do programa.
    out = re.sub(r"\[[A-Z][A-Z0-9_]*\] [^\n]*\n", "", out)
    out = out.split("\n", 1)[1] if "\n" in out else ""
    return out[:out.rfind("sagui:")] if "sagui:" in out else out


def codigo(m, programa):
    """O código de saída da última execução do programa (pelo log)."""
    achados = re.findall(rf"\({re.escape(programa)}\) terminou com codigo (-?\d+)", m.serial())
    return int(achados[-1]) if achados else None


def confere(m, comando, esperado, programa=None, cod=0):
    out = rodar(m, comando)
    ok = out == esperado
    if programa is not None:
        c = codigo(m, programa)
        ok = ok and c == cod
    verificar(ok, f"{comando!r} -> {esperado!r}" + ("" if ok else f" (veio {out!r})"))


def main():
    pen = criar_pendrive("h91-pen", rotulo="TEXTOS", arquivos=ARQUIVOS, pastas=PASTAS)
    extras = ["-device", "usb-ehci,id=ehci"] + pendrive_qemu(pen)
    with Maquina("h91", "ide", extras=extras) as m:
        m.esperar_serial("sagui:/$", 120)
        time.sleep(1)
        out = m.comando("ajuda")
        for nome in NOMES:
            verificar(nome in out, f"o ajuda lista {nome}")
        m.comando("montar")
        m.comando("cd /usb")
        lote1(m)
        lote2(m)
        lote3(m)
        lote4(m)
    return 0


def lote1(m):
    """head, tail, sort, uniq e cut."""
    if True:
        # ---- head
        confere(m, "head -3 numeros.txt", "1\n2\n3\n", "head")
        confere(m, "head -n 2 campos.txt frutas.txt",
                "==> campos.txt <==\na:b:c\nd:e:f\n\n==> frutas.txt <==\nbanana 3\nmaçã 10\n")
        confere(m, "head -c 5 numeros.txt", "1\n2\n3")
        confere(m, "cat numeros.txt | inicio -n 2", "1\n2\n")
        out = rodar(m, "ls /bin | sort -r | head -3")
        nomes = [l.split()[0] for l in out.splitlines() if l.strip()]
        verificar(nomes == sorted(PROGRAMAS, reverse=True)[:3], f"ls /bin | sort -r | head -3 = {nomes}")
        out = rodar(m, "head nada.txt")
        verificar("head: nada.txt: " in out and codigo(m, "head") == 1, "head de arquivo que não existe: erro e código 1")

        # ---- tail
        confere(m, "tail -3 numeros.txt", "23\n24\n25\n", "tail")
        confere(m, "tail -n +24 numeros.txt", "24\n25\n")
        confere(m, "tail -c 6 numeros.txt", "24\n25\n")
        confere(m, "cat frutas.txt | fim -n 1", "kiwi 1.5\n")

        # ---- sort
        confere(m, "sort frutas.txt", "Banana 7\nabacaxi 2\nbanana 3\nkiwi 1.5\nmaçã 10\nuva 10\nuva 10\n", "sort")
        confere(m, "sort -r numeros.txt | head -3", "9\n8\n7\n")
        confere(m, "sort -nr numeros.txt | head -3", "25\n24\n23\n")
        confere(m, "sort -k2n frutas.txt", "kiwi 1.5\nabacaxi 2\nbanana 3\nBanana 7\nmaçã 10\nuva 10\nuva 10\n")
        confere(m, "ordenar -t : -k 2 campos.txt", "sem\n1:2:3:4\na:b:c\nd:e:f\n")
        confere(m, "sort -fu frutas.txt", "abacaxi 2\nbanana 3\nBanana 7\nkiwi 1.5\nmaçã 10\nuva 10\n")
        rodar(m, "sort -c numeros.txt")
        verificar(codigo(m, "sort") == 1, "sort -c: fora de ordem devolve 1")

        # ---- uniq
        confere(m, "sort frutas.txt | uniq -c | tail -2", "      1 maçã 10\n      2 uva 10\n", "uniq")
        confere(m, "uniq -d frutas.txt", "uva 10\n")
        confere(m, "sort -f frutas.txt | unicos -i -u | head -2", "abacaxi 2\nbanana 3\n")

        # ---- cut
        confere(m, "cut -d : -f 2 campos.txt", "b\ne\nsem\n2\n", "cut")
        confere(m, "cut -d: -s -f1,3- campos.txt", "a:c\nd:f\n1:3:4\n")
        confere(m, "cortar -c 2-4 frutas.txt | head -2", "ana\naçã\n")
        rodar(m, "cut numeros.txt")
        verificar(codigo(m, "cut") == 1, "cut sem lista: uso e código 1")
        m.captura("H91-1-head-tail-sort-uniq-cut")


def lote2(m):
    """tr, seq, rev, tac e nl."""
    # ---- tr
    confere(m, "cat campos.txt | tr a-z A-Z", "A:B:C\nD:E:F\nSEM\n1:2:3:4\n", "tr")
    confere(m, "trocar [:lower:] [:upper:] < frutas.txt | head -2", "BANANA 3\nMAÇÃ 10\n")
    confere(m, "echo \"aa  bb   c\" | tr -s \" \"", "aa bb c\n")
    confere(m, "tr -d : < campos.txt | tail -1", "1234\n")
    confere(m, "tr -cd 0-9 < frutas.txt", "31027101015")
    rodar(m, "tr a")
    verificar(codigo(m, "tr") == 1, "tr com um conjunto só: uso e código 1")

    # ---- seq
    confere(m, "seq 4", "1\n2\n3\n4\n", "seq")
    confere(m, "seq 10 -3 1", "10\n7\n4\n1\n")
    confere(m, "seq -w 8 10", "08\n09\n10\n")
    confere(m, "sequencia -s , 0 0.5 2", "0.0,0.5,1.0,1.5,2.0\n")
    rodar(m, "seq x")
    verificar(codigo(m, "seq") == 1, "seq com algo que não é número: código 1")

    # ---- rev e tac
    confere(m, "echo sagui | rev", "iugas\n", "rev")
    confere(m, "inverter frutas.txt | head -2", "3 ananab\n01 ãçam\n")
    confere(m, "tac campos.txt", "1:2:3:4\nsem\nd:e:f\na:b:c\n", "tac")
    confere(m, "seq 5 | de-tras | head -2", "5\n4\n")

    # ---- nl
    confere(m, "nl campos.txt | head -2", "     1\ta:b:c\n     2\td:e:f\n", "nl")
    confere(m, "seq 3 | numerar -n rz -w 3 -s \": \" -v 10 -i 5", "010: 1\n015: 2\n020: 3\n")
    m.captura("H91-2-tr-seq-rev-tac-nl")


def lote3(m):
    """diff, find, sed e tree (e as aspas simples do sagsh)."""
    # ---- diff
    confere(m, "diff velho.txt novo.txt",
            "2c2\n< dois\n---\n> DOIS\n9a10\n> novo\n12d12\n< doze\n", "diff", 1)
    confere(m, "diff velho.txt velho.txt", "", "diff", 0)
    confere(m, "diff -u velho.txt novo.txt | head -6",
            "--- velho.txt\n+++ novo.txt\n@@ -1,5 +1,5 @@\n um\n-dois\n+DOIS\n")
    confere(m, "diferencas -i -q velho.txt novo.txt", "Os arquivos velho.txt e novo.txt são diferentes\n", "diff", 1)
    confere(m, "seq 25 | diff - numeros.txt", "", "diff", 0)
    rodar(m, "diff velho.txt nada.txt")
    verificar(codigo(m, "diff") == 2, "diff com arquivo que não existe: código 2")

    # ---- find
    confere(m, "find arv | sort", "arv\narv/a.txt\narv/outra\narv/outra/dez.bin\narv/sub1\narv/sub1/b.txt\n"
            "arv/sub1/sub2\narv/sub1/sub2/c.log\n", "find")
    confere(m, "find arv -name \"*.txt\" | sort", "arv/a.txt\narv/sub1/b.txt\n")
    confere(m, "find arv -type d -maxdepth 1 | sort", "arv\narv/outra\narv/sub1\n")
    confere(m, "procurar-arquivo arv -type f ! -name \"*.txt\" | sort", "arv/outra/dez.bin\narv/sub1/sub2/c.log\n")
    confere(m, "find arv -size +5c", "arv/outra/dez.bin\n")
    confere(m, "find arv -empty -type f | sort", "arv/a.txt\narv/sub1/sub2/c.log\n")
    rodar(m, "find arv -cor azul")
    verificar(codigo(m, "find") == 1, "find com condição desconhecida: código 1")

    # ---- sed (com aspas simples, como se escreve nos outros sistemas)
    confere(m, "sed 's/:/-/g' campos.txt", "a-b-c\nd-e-f\nsem\n1-2-3-4\n", "sed")
    confere(m, "sed s/a/X/ frutas.txt | head -2", "bXnana 3\nmXçã 10\n")
    confere(m, "sed -n 2,4p numeros.txt", "2\n3\n4\n")
    confere(m, "sed '/^[0-9]/d' campos.txt", "a:b:c\nd:e:f\nsem\n")
    confere(m, "seq 10 | sed -n '/4/,/6/p'", "4\n5\n6\n")
    confere(m, "sed '$!d' numeros.txt", "25\n")
    confere(m, "sed 's/an*/<&>/g;3q' frutas.txt", "b<an><an><a> 3\nm<a>çã 10\n<a>b<a>c<a>xi 2\n")
    confere(m, "editar-fluxo -E 's/[0-9]+/N/' frutas.txt | tail -1", "kiwi N.5\n")
    confere(m, "sed 'y/abc/ABC/;2i antes' campos.txt | head -3", "A:B:C\nantes\nd:e:f\n")
    rodar(m, "sed 's/a/b' campos.txt")
    verificar(codigo(m, "sed") == 1, "sed com s sem fim: erro e código 1")

    # ---- tree
    confere(m, "tree arv", "arv\n|-- a.txt\n|-- outra/\n|   `-- dez.bin\n`-- sub1/\n    |-- b.txt\n"
            "    `-- sub2/\n        `-- c.log\n\n3 pastas, 4 arquivos\n", "tree")
    confere(m, "arvore -d -L 1 arv", "arv\n|-- outra/\n`-- sub1/\n\n2 pastas, 0 arquivos\n")
    m.comando("limpar")
    m.comando("tree arv")
    m.captura("H91-3-diff-find-sed-tree")


def lote4(m):
    """cal, bc, expr, base64, md5sum, od e xxd."""
    # ---- cal
    confere(m, "cal 2 2028", "   fevereiro 2028\ndo se te qu qu se sá\n       1  2  3  4  5\n 6  7  8  9 10 11 12\n"
            "13 14 15 16 17 18 19\n20 21 22 23 24 25 26\n27 28 29\n\n", "cal")
    out = rodar(m, "calendario 2027 | head -4")
    verificar("2027" in out and "janeiro" in out and "março" in out and "do se te qu qu se sá  do se" in out,
              "cal 2027: o ano, três meses por linha")
    out = rodar(m, "cal")
    verificar("do se te qu qu se sá" in out and codigo(m, "cal") == 0, "cal sem argumentos: o mês de hoje")
    rodar(m, "cal 13 2020")
    verificar(codigo(m, "cal") == 1, "cal com mês 13: código 1")

    # ---- bc
    confere(m, "bc contas.bc", "1267650600228229401496703205376\n0.3\n4\n1\n", "bc")
    confere(m, "echo \"scale=3; 10/3\" | calcular", "3.333\n")
    confere(m, "echo \"0.1 + 0.2 == 0.3\" | bc", "1\n")
    confere(m, "bc -l -e \"sqrt(2)\"", "1.41421356237309504880\n")
    confere(m, "bc -e \"123456789012345678901234567890/7\"", "17636684144620811271604938270\n")
    out = rodar(m, "echo 1/0 | bc")
    verificar("divisão por zero" in out and codigo(m, "bc") == 1, "bc: divisão por zero avisa e devolve 1")

    # ---- expr
    confere(m, "expr 3 + 4 * 2", "11\n", "expr")
    confere(m, "expressao 7 / 2", "3\n")
    confere(m, "expr 2 \"<\" 10", "1\n")
    confere(m, "expr sagui : \"sa.*\"", "5\n")
    confere(m, "expr substr sagui 2 3", "agu\n")
    confere(m, "expr 3 - 3", "0\n", "expr", 1)
    rodar(m, "expr 1 / 0")
    verificar(codigo(m, "expr") == 2, "expr 1 / 0: código 2")

    # ---- base64 e md5sum
    confere(m, "echo \"Ola, sagui!\" | base64", "T2xhLCBzYWd1aSEK\n", "base64")
    confere(m, "echo T2xhIQ== | base64 -d", "Ola!")
    confere(m, "echo abc | md5sum", "0bee89b07a248e27c83fc3d5951213c1  -\n", "md5sum")
    confere(m, "md5 numeros.txt", "6f2fa9e82daa9b4e597ec0136a66fef6  numeros.txt\n")
    confere(m, "base64 numeros.txt | base64 -d | md5sum", "6f2fa9e82daa9b4e597ec0136a66fef6  -\n")
    m.comando("md5sum numeros.txt campos.txt > /tmp/lista.md5")
    confere(m, "md5sum -c /tmp/lista.md5", "numeros.txt: OK\ncampos.txt: OK\n", "md5sum", 0)

    # ---- od e xxd
    confere(m, "echo ABC | od -c", "0000000   A   B   C  \\n\n0000004\n", "od")
    confere(m, "octal -An -tx1 campos.txt | head -1", " 61 3a 62 3a 63 0a 64 3a 65 3a 66 0a 73 65 6d 0a\n")
    confere(m, "echo \"Ola, sagui!\" | xxd",
            "00000000: 4f6c 612c 2073 6167 7569 210a            Ola, sagui!.\n", "xxd")
    confere(m, "hexa -p campos.txt", "613a623a630a643a653a660a73656d0a313a323a333a340a\n")
    confere(m, "xxd campos.txt | xxd -r | md5sum", rodar(m, "md5sum - < campos.txt"))
    m.comando("limpar")
    m.comando("cal 2 2028")
    m.comando("echo \"scale=20; 1/7\" | bc")
    m.comando("echo \"Ola, sagui!\" | xxd")
    m.captura("H91-4-cal-bc-expr-base64-md5-od-xxd")


if __name__ == "__main__":
    sys.exit(main())
