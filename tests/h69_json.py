#!/usr/bin/env python3
"""H69 (parte 1) - leitor e escritor de JSON da libsagui.

Critério: documentos válidos (textos com escapes, \\u com pares substitutos,
acentos em UTF-8, números negativos, com fração e expoente, aninhamento,
listas e objetos vazios, e uma resposta de verdade no formato da API de
conversa com chamada de ferramentas) são lidos e escritos de novo sem mudar o
significado (conferido com o json do Python); documentos inválidos são
recusados com motivo; 300 documentos estragados não derrubam o leitor (com
verificação de memória). Roda no computador, com o sagui.h simulado.
"""
import json
import os
import random
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import FalhaTeste, RAIZ, SAIDA  # noqa: E402

PASTA = os.path.join(SAIDA, "h69-json")
sorte = random.Random(69)


def verificar(cond, msg, detalhe=""):
    if not cond:
        raise FalhaTeste(f"{msg}: {detalhe}" if detalhe else msg)
    print("  ok:", msg)


VALIDOS = [
    '{}', '[]', 'null', 'true', '0', '-12', '"texto"', '  {"a" : [1, 2, {"b": null}], "c": false}  ',
    '{"acento": "ação \\u00e9 \\u2014 😀 \\ud83d\\ude00", "escapes": "\\"\\\\\\/\\b\\f\\n\\r\\t\\u0001"}',
    '[1.5, -0.25, 3e10, 2E-3, -7.0e+2, 123456789012]',
    '[[[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]]]]',
    '{"id":"gen-1","choices":[{"index":0,"message":{"role":"assistant","content":null,'
    '"tool_calls":[{"id":"call_1","type":"function","function":{"name":"escrever_arquivo",'
    '"arguments":"{\\"caminho\\":\\"/usb/ola.c\\",\\"conteudo\\":\\"int principal(void) {\\\\n  imprimir(\\\\\\"ol\\u00e1\\\\\\\\n\\\\\\");\\\\n}\\"}"}}]},'
    '"finish_reason":"tool_calls"}],"usage":{"prompt_tokens":1200,"completion_tokens":85,"cost":0.00123}}',
]
INVALIDOS = ['', '{', '[1,]', '{"a" 1}', '{"a":1,}', "{'a':1}", '"sem fim', 'tru', '[1 2]', '"\\x"', '01x', '{"a":1} lixo',
             '"\x01"', '-', '[' * 200 + ']' * 200]


def main():
    if os.path.exists(PASTA):
        shutil.rmtree(PASTA)
    os.makedirs(PASTA)
    # O json.h inclui "sagui.h" da mesma pasta: copia para cá junto com o simulado.
    for f in (os.path.join(RAIZ, "userland", "inc", "json.h"), os.path.join(RAIZ, "userland", "libsagui", "json.c"),
              os.path.join(RAIZ, "tests", "navegador", "simulado", "sagui.h"), os.path.join(RAIZ, "tests", "json", "teste_json.c")):
        shutil.copy(f, PASTA)
    prog = os.path.join(PASTA, "teste_json")
    r = subprocess.run(["gcc", "-m32", "-std=gnu11", "-O1", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover", "-Wall", "-Wextra",
                        "-Werror", "-Wno-format-truncation", "-o", prog, os.path.join(PASTA, "teste_json.c"),
                        os.path.join(PASTA, "json.c")], capture_output=True, text=True)
    verificar(r.returncode == 0, "json.c compila (com verificação de memória)", r.stderr[-1500:])
    amb = dict(os.environ, ASAN_OPTIONS="detect_leaks=1")

    def rodar(texto, *args):
        caminho = os.path.join(PASTA, "entrada.json")
        with open(caminho, "wb") as f:
            f.write(texto if isinstance(texto, bytes) else texto.encode())
        return subprocess.run([prog, caminho, *args], capture_output=True, env=amb, timeout=30)

    for t in VALIDOS:
        r = rodar(t)
        verificar(r.returncode == 0, f"válido lido: {t[:50]!r}", r.stdout.decode(errors="replace") + r.stderr.decode(errors="replace")[-800:])
        volta = json.loads(r.stdout.decode())
        verificar(volta == json.loads(t), "  e escrito de novo com o mesmo significado")
    r = rodar(VALIDOS[-1], "usage")
    verificar(r.returncode == 0, "campo de objeto encontrado")
    for t in INVALIDOS:
        r = rodar(t)
        verificar(r.returncode == 1 and r.stdout.startswith(b"ERRO: "), f"inválido recusado: {t[:30]!r} ({r.stdout.decode(errors='replace').strip()[:60]})")
    # Um documento grande gerado pelo Python.
    grande = {"lista": [{"n": i, "nome": f"item {i} ção", "par": i % 2 == 0, "nada": None} for i in range(3000)]}
    r = rodar(json.dumps(grande, ensure_ascii=False))
    verificar(r.returncode == 0 and json.loads(r.stdout.decode()) == grande, "documento de 3000 objetos ida e volta")
    # Estragados.
    bases = [t.encode() for t in VALIDOS]
    for k in range(300):
        b = bytearray(sorte.choice(bases))
        if k % 2:
            b = b[:sorte.randrange(len(b) + 1)]
        for _ in range(sorte.randrange(0, 8)):
            if b:
                b[sorte.randrange(len(b))] = sorte.choice(b'{}[]",:\\u0 \xff')
        r = rodar(bytes(b))
        if r.returncode not in (0, 1):
            raise FalhaTeste(f"documento estragado {k} derrubou o leitor: {r.stderr.decode(errors='replace')[-600:]}")
    verificar(True, "300 documentos estragados sem erro de memória nem vazamento")


if __name__ == "__main__":
    try:
        main()
        print("PASSOU")
    except FalhaTeste as e:
        print("FALHOU:", e)
        sys.exit(1)
