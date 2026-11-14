#!/usr/bin/env python3
"""H50 - biblioteca de criptografia (userland/cripto).

Critério: a biblioteca compila com as opções do SaguiOS (32 bits, sem libc,
sem libgcc, sem ponto flutuante) sem ficar devendo símbolo nenhum, e cada
algoritmo dá exatamente o resultado dos vetores oficiais: SHA-2 (FIPS 180-4),
HMAC (RFC 4231), HKDF (RFC 5869), AES (FIPS 197), CTR (SP 800-38A), GCM,
ChaCha20-Poly1305 (RFC 8439), X25519 (RFC 7748) e Ed25519 (RFC 8032). RSA e
ECDSA (só conferência) são testados com chaves e assinaturas feitas na hora
pelo comando openssl da máquina, e com assinaturas adulteradas, que têm de ser
recusadas.

Este teste não usa o QEMU: a biblioteca é ligada a um programa de teste
(tests/cripto/teste_cripto.c) que roda aqui mesmo, em 32 bits. O openssl e os
módulos hashlib/hmac do Python entram só como ferramentas de conferência.
"""
import glob
import hashlib
import hmac
import os
import random
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "cripto"))
sys.dont_write_bytecode = True     # não deixar __pycache__ em tests/cripto
from qemu import FalhaTeste, verificar, RAIZ, SAIDA  # noqa: E402
import vetores as V  # noqa: E402

FONTES = os.path.join(RAIZ, "userland", "cripto")
PASTA = os.path.join(SAIDA, "h50-cripto")
PROGRAMA = os.path.join(PASTA, "teste_cripto")

# As mesmas opções com que os programas do SaguiOS são compilados. Com
# -nostdlib e -ffreestanding o compilador não pode contar com libc; e
# qualquer divisão de 64 bits apareceria como chamada a __udivdi3.
OPCOES = ["-m32", "-march=i686", "-std=gnu11", "-O2", "-ffreestanding", "-fno-builtin", "-nostdlib",
          "-fno-pic", "-fno-pie", "-fno-stack-protector", "-mgeneral-regs-only",
          "-Wall", "-Wextra", "-Werror"]

# Maior quadro de pilha aceito para uma função sozinha. A biblioteca promete
# ficar abaixo de 16 KB na cadeia de chamadas mais funda; a função mais gulosa
# é a ecdsa_verificar (dois contextos de Montgomery, uns 4 KB), e abaixo dela
# só há funções pequenas.
PILHA_MAX = 6144

sorte = random.Random(50)       # sempre os mesmos "sorteios": teste repetível


def hx(b):
    return b.hex() if b else "-"


def de_hex(s):
    return bytes.fromhex(s)


def aleatorio(n):
    return bytes(sorte.getrandbits(8) for _ in range(n))


def executar(args, entrada=None):
    r = subprocess.run(args, input=entrada, capture_output=True)
    if r.returncode != 0:
        raise FalhaTeste(f"{' '.join(args[:3])}... falhou: {r.stderr.decode(errors='replace')[-400:]}")
    return r.stdout


def openssl(*args, entrada=None):
    return executar(["openssl", *args], entrada)


def rodar(linhas):
    """Manda os comandos ao programa de teste e devolve uma resposta por comando."""
    r = subprocess.run([PROGRAMA], input=("\n".join(linhas) + "\n").encode(), capture_output=True, timeout=300)
    if r.returncode != 0:
        raise FalhaTeste(f"o programa de teste terminou com código {r.returncode}")
    saida = r.stdout.decode().split("\n")[:-1]
    if len(saida) != len(linhas):
        raise FalhaTeste(f"esperava {len(linhas)} respostas, vieram {len(saida)}")
    return saida


def conferir(casos, mensagem):
    """casos: lista de (comando, resposta esperada). Roda todos de uma vez."""
    respostas = rodar([c for c, _ in casos])
    for (comando, esperado), veio in zip(casos, respostas):
        if veio != esperado:
            raise FalhaTeste(f"{mensagem}\n   comando:  {comando[:200]}\n   esperado: {esperado[:200]}\n   veio:     {veio[:200]}")
    verificar(True, f"{mensagem} ({len(casos)} casos)")


# ------------------------------------------------------------ compilação

def compilar():
    os.makedirs(PASTA, exist_ok=True)
    for velho in glob.glob(os.path.join(PASTA, "*.o")) + glob.glob(os.path.join(PASTA, "*.su")):
        os.remove(velho)
    objetos = []
    for fonte in sorted(glob.glob(os.path.join(FONTES, "*.c"))):
        obj = os.path.join(PASTA, os.path.basename(fonte)[:-2] + ".o")
        executar(["gcc", *OPCOES, "-fstack-usage", "-c", "-o", obj, fonte])
        objetos.append(obj)
    verificar(len(objetos) >= 6, f"biblioteca compila com as opções do SaguiOS, sem avisos ({len(objetos)} arquivos)")

    # Nenhum símbolo pode ficar para "alguém de fora" resolver: nem memcpy,
    # nem __udivdi3, nem nada.
    definidos, pedidos = set(), set()
    for linha in executar(["nm", *objetos]).decode().split("\n"):
        partes = linha.split()
        if len(partes) >= 2 and partes[-2] == "U":
            pedidos.add(partes[-1])
        elif len(partes) == 3:
            definidos.add(partes[2])
    faltam = sorted(pedidos - definidos)
    verificar(not faltam, "biblioteca não depende de libc nem de libgcc" + (f": faltam {faltam}" if faltam else ""))

    maior, dono = 0, ""
    for su in glob.glob(os.path.join(PASTA, "*.su")):
        for linha in open(su):
            partes = linha.rstrip("\n").split("\t")
            if len(partes) >= 2 and int(partes[1]) > maior:
                maior, dono = int(partes[1]), partes[0].split(":")[-1]
    verificar(0 < maior <= PILHA_MAX, f"nenhuma função usa mais de {PILHA_MAX} bytes de pilha (a maior: {dono}, {maior})")

    executar(["gcc", "-m32", "-std=gnu11", "-no-pie", "-O1", "-Wall", "-Wextra", "-I", FONTES, "-o", PROGRAMA,
              os.path.join(RAIZ, "tests", "cripto", "teste_cripto.c"), *objetos])
    verificar(os.path.exists(PROGRAMA), "programa de teste ligado à biblioteca (32 bits)")


# ------------------------------------------------------------ resumos, HMAC, HKDF

def testar_resumos():
    casos = []
    for v in V.SHA:
        casos.append((f"resumo {v['tipo']} {hx(de_hex(v['dados']))}", v["resumo"]))
        casos.append((f"resumo_pedacos {v['tipo']} {hx(de_hex(v['dados']))}", v["resumo"]))
    conferir(casos, "SHA-256, SHA-384 e SHA-512: exemplos da FIPS 180-4")
    conferir([(f"resumo_repetido {t} 61 1000000", r) for t, r in V.SHA_MILHAO.items()],
             "SHA-2: um milhão de letras 'a'")

    # Todos os tamanhos em volta das bordas de bloco (onde mora o erro de
    # enchimento), conferidos com o hashlib do Python.
    casos = []
    for n in list(range(0, 270)) + [511, 512, 513, 1000, 4096, 10007]:
        dados = aleatorio(n)
        for t in ("sha256", "sha384", "sha512"):
            certo = hashlib.new(t, dados).hexdigest()
            casos.append((f"resumo {t} {hx(dados)}", certo))
            casos.append((f"resumo_pedacos {t} {hx(dados)}", certo))
    conferir(casos, "SHA-2 em todos os tamanhos de 0 a 269 bytes, inteiro e aos pedaços")


def hkdf_python(tipo, sal, material, info, tam):
    """HKDF escrito direto da RFC 5869, com o hmac do Python, para conferir o SHA-384."""
    prk = hmac.new(sal, material, tipo).digest()
    okm, t, i = b"", b"", 1
    while len(okm) < tam:
        t = hmac.new(prk, t + info + bytes([i]), tipo).digest()
        okm += t
        i += 1
    return prk, okm[:tam]


def testar_hmac_hkdf():
    casos = []
    for v in V.RFC4231:
        for t in ("sha256", "sha384"):
            casos.append((f"hmac {t} {v['chave']} {v['dados']}", v[t]))
    # O caso 5 da RFC só dá os 16 primeiros bytes da saída.
    respostas = rodar([c for c, _ in casos])
    for (comando, esperado), veio in zip(casos, respostas):
        verificar(veio.startswith(esperado) and len(veio) in (64, 96), f"HMAC RFC 4231: {comando[:11]} chave de {len(comando.split()[2]) // 2} bytes")

    casos = []
    for n in (0, 1, 55, 56, 63, 64, 65, 111, 112, 127, 128, 129, 300):
        for nk in (0, 1, 32, 64, 65, 128, 129, 200):
            chave, dados = aleatorio(nk), aleatorio(n)
            for t in ("sha256", "sha384", "sha512"):
                casos.append((f"hmac {t} {hx(chave)} {hx(dados)}", hmac.new(chave, dados, t).hexdigest()))
    conferir(casos, "HMAC com chaves e mensagens de vários tamanhos, conferido com o Python")

    conferir([(f"hkdf sha256 {hx(de_hex(v['sal']))} {v['ikm']} {hx(de_hex(v['info']))} {v['tam']}", f"{v['prk']} {v['okm']}")
              for v in V.RFC5869], "HKDF-SHA256: RFC 5869, casos 1 a 3")
    casos = []
    for t in ("sha256", "sha384"):
        tam_resumo = hashlib.new(t).digest_size
        for tam in (1, tam_resumo - 1, tam_resumo, tam_resumo + 1, 100, 255 * tam_resumo):
            sal, material, info = aleatorio(sorte.randrange(0, 80)), aleatorio(sorte.randrange(1, 80)), aleatorio(sorte.randrange(0, 80))
            prk, okm = hkdf_python(t, sal, material, info, tam)
            casos.append((f"hkdf {t} {hx(sal)} {hx(material)} {hx(info)} {tam}", f"{prk.hex()} {okm.hex()}"))
    conferir(casos, "HKDF-SHA256 e HKDF-SHA384 em vários tamanhos de saída")
    veio = rodar([f"hkdf sha256 00 01 - {255 * 32 + 1}", f"hkdf sha384 00 01 - {255 * 48 + 1}"])
    verificar(all(r.endswith(" FALHA") for r in veio), "HKDF recusa saída maior que 255 blocos")


# ------------------------------------------------------------ AES

def testar_aes():
    conferir([(f"aes {v['chave']} {v['claro']}", v["cifrado"]) for v in V.AES],
             "AES-128 e AES-256: FIPS 197")
    veio = rodar(["aes 000102030405060708090a0b0c0d0e0f1011121314151617 00112233445566778899aabbccddeeff", "aes 00 00"])
    verificar(veio == ["FALHA", "FALHA"], "AES recusa chave de tamanho errado (192 bits não é oferecido)")
    conferir([(f"aes_ctr {v['chave']} {v['contador']} {v['claro']}", v["cifrado"]) for v in V.AES_CTR],
             "AES-CTR: SP 800-38A F.5.1 e F.5.5")

    # Contra o openssl: tamanhos quebrados e o contador dando a volta.
    casos = []
    for bits in (128, 256):
        for n in (1, 15, 16, 17, 31, 32, 33, 47, 48, 100, 1000):
            chave, contador, dados = aleatorio(bits // 8), aleatorio(16), aleatorio(n)
            if n == 100:
                contador = b"\xff" * 16          # vira para zero no segundo bloco
            if n == 1000:
                contador = contador[:12] + b"\xff\xff\xff\xfe"
            certo = openssl("enc", f"-aes-{bits}-ctr", "-K", chave.hex(), "-iv", contador.hex(), "-nosalt", entrada=dados)
            casos.append((f"aes_ctr {chave.hex()} {contador.hex()} {dados.hex()}", certo.hex()))
    conferir(casos, "AES-CTR igual ao openssl, inclusive aos pedaços e com o contador virando")


def testar_gcm():
    cifrar, decifrar, ruins = [], [], []
    for v in V.GCM:
        k, n, a, p, c, t = v["chave"], v["nonce"], hx(de_hex(v["extra"])), hx(de_hex(v["claro"])), hx(de_hex(v["cifrado"])), v["marca"]
        cifrar.append((f"gcm_cifrar {k} {n} {a} {p}", f"{c} {t}"))
        decifrar.append((f"gcm_decifrar {k} {n} {a} {c} {t}", p))
        # Um bit trocado em cada lugar: marca, texto cifrado, dados extras, nonce.
        marca = bytearray(de_hex(t))
        for i in (0, 7, 15):
            m2 = bytearray(marca)
            m2[i] ^= 1 << (i % 8)
            ruins.append((f"gcm_decifrar {k} {n} {a} {c} {m2.hex()}", "FALHA"))
        if v["cifrado"]:
            c2 = bytearray(de_hex(v["cifrado"]))
            c2[len(c2) // 2] ^= 0x80
            ruins.append((f"gcm_decifrar {k} {n} {a} {c2.hex()} {t}", "FALHA"))
        if v["extra"]:
            a2 = bytearray(de_hex(v["extra"]))
            a2[0] ^= 1
            ruins.append((f"gcm_decifrar {k} {n} {a2.hex()} {c} {t}", "FALHA"))
            ruins.append((f"gcm_decifrar {k} {n} - {c} {t}", "FALHA"))
        n2 = bytearray(de_hex(n))
        n2[-1] ^= 1
        ruins.append((f"gcm_decifrar {k} {n2.hex()} {a} {c} {t}", "FALHA"))
    conferir(cifrar, "AES-GCM cifrar: casos de teste 1-5 e 13-16 da especificação")
    conferir(decifrar, "AES-GCM decifrar os mesmos casos")
    conferir(ruins, "AES-GCM recusa marca, texto, extras ou nonce adulterados (e zera a saída)")


# ------------------------------------------------------------ ChaCha20 e Poly1305

def testar_chacha():
    v = V.CHACHA_BLOCO
    conferir([(f"chacha_bloco {v['chave']} {v['contador']} {v['nonce']}", v["saida"])], "ChaCha20, função de bloco: RFC 8439 2.3.2")
    v = V.CHACHA_CIFRA
    conferir([(f"chacha {v['chave']} {v['contador']} {v['nonce']} {v['claro']}", v["cifrado"]),
              (f"chacha {v['chave']} {v['contador']} {v['nonce']} {v['cifrado']}", v["claro"])],
             "ChaCha20 cifrar e decifrar: RFC 8439 2.4.2")
    casos = []
    for n in (1, 63, 64, 65, 127, 128, 129, 500):
        chave, nonce, dados, contador = aleatorio(32), aleatorio(12), aleatorio(n), sorte.randrange(0, 1 << 31)
        iv = contador.to_bytes(4, "little") + nonce
        certo = openssl("enc", "-chacha20", "-K", chave.hex(), "-iv", iv.hex(), "-nosalt", entrada=dados)
        casos.append((f"chacha {chave.hex()} {contador} {nonce.hex()} {dados.hex()}", certo.hex()))
    conferir(casos, "ChaCha20 igual ao openssl em tamanhos quebrados")

    conferir([(f"poly1305 {v['chave']} {hx(de_hex(v['dados']))}", v["marca"]) for v in V.POLY],
             "Poly1305: RFC 8439 2.5.2 e os 11 vetores do apêndice A.3 (casos de borda)")

    cifrar, decifrar, ruins = [], [], []
    for v in V.AEAD:
        k, n, a, p, c, t = v["chave"], v["nonce"], v["extra"], v["claro"], v["cifrado"], v["marca"]
        cifrar.append((f"cp_cifrar {k} {n} {a} {p}", f"{c} {t}"))
        decifrar.append((f"cp_decifrar {k} {n} {a} {c} {t}", p))
        for i in (0, 8, 15):
            m2 = bytearray(de_hex(t))
            m2[i] ^= 0x10
            ruins.append((f"cp_decifrar {k} {n} {a} {c} {m2.hex()}", "FALHA"))
        c2 = bytearray(de_hex(c))
        c2[-1] ^= 1
        ruins.append((f"cp_decifrar {k} {n} {a} {c2.hex()} {t}", "FALHA"))
        a2 = bytearray(de_hex(a))
        a2[3] ^= 4
        ruins.append((f"cp_decifrar {k} {n} {a2.hex()} {c} {t}", "FALHA"))
        ruins.append((f"cp_decifrar {k} {n} - {c} {t}", "FALHA"))
        n2 = bytearray(de_hex(n))
        n2[0] ^= 1
        ruins.append((f"cp_decifrar {k} {n2.hex()} {a} {c} {t}", "FALHA"))
    conferir(cifrar, "ChaCha20-Poly1305 cifrar: RFC 8439 2.8.2 e A.5")
    conferir(decifrar, "ChaCha20-Poly1305 decifrar os mesmos casos")
    conferir(ruins, "ChaCha20-Poly1305 recusa marca, texto, extras ou nonce adulterados (e zera a saída)")


def testar_aead_ida_e_volta():
    """Tamanhos em volta das bordas de bloco: o que um cifra o outro abre; e,
    se o módulo 'cryptography' existir na máquina, os bytes batem com os dele."""
    try:
        from cryptography.hazmat.primitives.ciphers.aead import AESGCM, ChaCha20Poly1305
    except ImportError:
        AESGCM = ChaCha20Poly1305 = None
    comandos, dados = [], []
    for n in (0, 1, 15, 16, 17, 31, 32, 33, 63, 64, 65, 255, 1500, 16384):
        for na in (0, 5, 13, 16, 40):
            claro, extra, nonce = aleatorio(n), aleatorio(na), aleatorio(12)
            for nome, chave in (("gcm", aleatorio(16)), ("gcm", aleatorio(32)), ("cp", aleatorio(32))):
                comandos.append(f"{nome}_cifrar {chave.hex()} {nonce.hex()} {hx(extra)} {hx(claro)}")
                dados.append((nome, chave, nonce, extra, claro))
    cifrados = rodar(comandos)
    voltas, comparados = [], 0
    for (nome, chave, nonce, extra, claro), resposta in zip(dados, cifrados):
        cifrado, marca = resposta.split(" ")
        voltas.append((f"{nome}_decifrar {chave.hex()} {nonce.hex()} {hx(extra)} {cifrado} {marca}", hx(claro)))
        if AESGCM:
            classe = AESGCM if nome == "gcm" else ChaCha20Poly1305
            certo = classe(chave).encrypt(nonce, claro, extra)
            if certo.hex() != (cifrado if cifrado != "-" else "") + marca:
                raise FalhaTeste(f"{nome}: resultado diferente do módulo cryptography ({len(claro)} bytes)")
            comparados += 1
    conferir(voltas, "AES-GCM e ChaCha20-Poly1305: ida e volta em tamanhos de 0 a 16384 bytes")
    if AESGCM:
        verificar(comparados == len(dados), f"os mesmos {comparados} casos batem com o módulo cryptography do Python")
    else:
        print("  (pulado: módulo cryptography do Python não instalado; a comparação extra não rodou)")


# ------------------------------------------------------------ X25519 e Ed25519

def bytes_finais_der(pem_args, n):
    """Os n últimos bytes da forma DER de uma chave: é onde fica a chave crua."""
    return openssl(*pem_args, "-outform", "DER")[-n:]


def testar_x25519():
    conferir([(f"x25519 {v['escalar']} {v['u']}", v["saida"]) for v in V.X25519], "X25519: RFC 7748 5.2")
    conferir([(f"x25519_repetido {n}", V.X25519_LACO[n]) for n in (1, 1000)],
             "X25519: teste em laço da RFC 7748 (1 e 1000 voltas)")
    d = V.X25519_DH
    conferir([(f"x25519_base {d['a']}", d["A"]), (f"x25519_base {d['b']}", d["B"]),
              (f"x25519 {d['a']} {d['B']}", d["K"]), (f"x25519 {d['b']} {d['A']}", d["K"])],
             "X25519: troca de chaves da RFC 7748 6.1 (os dois lados chegam ao mesmo segredo)")

    k = aleatorio(32).hex()
    nove = "09" + "00" * 31
    base, com_bit, acima_de_p = rodar([f"x25519_base {k}", f"x25519 {k} 09{'00' * 30}80", f"x25519 {k} f6{'ff' * 30}7f"])
    verificar(base == rodar([f"x25519 {k} {nove}"])[0], "X25519: x25519_base é a multiplicação pelo ponto 9")
    verificar(com_bit == base, "X25519: o bit 255 do ponto recebido é ignorado")
    verificar(acima_de_p == base, "X25519: um ponto escrito acima de p vale o seu resto (p + 9 = 9)")
    veio = rodar([f"x25519 {k} {'00' * 32}", f"x25519 {k} 01{'00' * 31}", f"x25519 {k} ec{'ff' * 30}7f"])
    verificar(veio == ["FALHA"] * 3, "X25519: pontos de ordem pequena (0, 1, p-1) dão zero e são recusados")

    # Contra o openssl: duas chaves geradas por ele, segredo derivado por ele.
    a_pem, b_pem, b_pub = (os.path.join(PASTA, n) for n in ("xa.pem", "xb.pem", "xb-pub.pem"))
    openssl("genpkey", "-algorithm", "x25519", "-out", a_pem)
    openssl("genpkey", "-algorithm", "x25519", "-out", b_pem)
    openssl("pkey", "-in", b_pem, "-pubout", "-out", b_pub)
    a = bytes_finais_der(["pkey", "-in", a_pem], 32)
    b = bytes_finais_der(["pkey", "-in", b_pem], 32)
    pub_b = bytes_finais_der(["pkey", "-in", b_pem, "-pubout"], 32)
    segredo = openssl("pkeyutl", "-derive", "-inkey", a_pem, "-peerkey", b_pub)
    conferir([(f"x25519_base {b.hex()}", pub_b.hex()), (f"x25519 {a.hex()} {pub_b.hex()}", segredo.hex())],
             "X25519: chave pública e segredo iguais aos do openssl")


ORDEM_ED = 2 ** 252 + 27742317777372353535851937790883648493


def testar_ed25519():
    casos = []
    for v in V.RFC8032:
        m = hx(de_hex(v["mensagem"]))
        casos.append((f"ed_publica {v['semente']}", v["publica"]))
        casos.append((f"ed_assinar {v['semente']} {m}", v["assinatura"]))
        casos.append((f"ed_verificar {v['publica']} {m} {v['assinatura']}", "OK"))
    conferir(casos, "Ed25519: chave pública, assinatura e conferência dos 5 vetores da RFC 8032")

    ruins = []
    for v in V.RFC8032:
        m, a, p = de_hex(v["mensagem"]), de_hex(v["assinatura"]), de_hex(v["publica"])
        for i in (0, 31, 32, 63):            # R e S adulterados
            a2 = bytearray(a)
            a2[i] ^= 1
            ruins.append((f"ed_verificar {p.hex()} {hx(m)} {a2.hex()}", "FALHA"))
        m2 = bytearray(m or b"\x00")          # mensagem adulterada (ou com um byte a mais)
        m2[0] ^= 0x40
        ruins.append((f"ed_verificar {p.hex()} {m2.hex()} {a.hex()}", "FALHA"))
        p2 = bytearray(p)                     # outra chave
        p2[5] ^= 2
        ruins.append((f"ed_verificar {p2.hex()} {hx(m)} {a.hex()}", "FALHA"))
        # S + L é a "mesma" assinatura escrita de outro jeito: a RFC manda recusar.
        s = int.from_bytes(a[32:], "little") + ORDEM_ED
        ruins.append((f"ed_verificar {p.hex()} {hx(m)} {(a[:32] + s.to_bytes(32, 'little')).hex()}", "FALHA"))
    # Chave pública que não é ponto da curva (y = 2 não tem x), e y escrito acima de p.
    v = V.RFC8032[0]
    ruins.append((f"ed_verificar 02{'00' * 31} - {v['assinatura']}", "FALHA"))
    ruins.append((f"ed_verificar ee{'ff' * 30}7f - {v['assinatura']}", "FALHA"))
    conferir(ruins, "Ed25519 recusa assinatura, mensagem ou chave adulteradas, S fora da faixa e chave inválida")

    # Contra o openssl: como a assinatura não tem sorteio, tem de sair idêntica.
    pem, arq, sig = (os.path.join(PASTA, n) for n in ("ed.pem", "ed-msg.bin", "ed-sig.bin"))
    casos = []
    for n in (1, 2, 32, 100, 2000):      # (o openssl não assina arquivo vazio)
        openssl("genpkey", "-algorithm", "ed25519", "-out", pem)
        semente = bytes_finais_der(["pkey", "-in", pem], 32)
        publica = bytes_finais_der(["pkey", "-in", pem, "-pubout"], 32)
        msg = aleatorio(n)
        open(arq, "wb").write(msg)
        openssl("pkeyutl", "-sign", "-inkey", pem, "-rawin", "-in", arq, "-out", sig)
        assinatura = open(sig, "rb").read()
        casos.append((f"ed_publica {semente.hex()}", publica.hex()))
        casos.append((f"ed_assinar {semente.hex()} {hx(msg)}", assinatura.hex()))
        casos.append((f"ed_verificar {publica.hex()} {hx(msg)} {assinatura.hex()}", "OK"))
    conferir(casos, "Ed25519: chaves e assinaturas idênticas às do openssl")


# ------------------------------------------------------------ RSA

def primo_provavel(n):
    """Miller-Rabin com 24 bases sorteadas."""
    if n < 4 or n % 2 == 0:
        return n in (2, 3)
    d, s = n - 1, 0
    while d % 2 == 0:
        d, s = d // 2, s + 1
    for _ in range(24):
        x = pow(sorte.randrange(2, n - 1), d, n)
        if x in (1, n - 1):
            continue
        for _ in range(s - 1):
            x = x * x % n
            if x == n - 1:
                break
        else:
            return False
    return True


def gerar_rsa(bits, e):
    """Uma chave RSA feita aqui mesmo, para termos o expoente privado em mãos
    e podermos fabricar blocos malformados."""
    def primo(nbits):
        while True:
            p = sorte.getrandbits(nbits) | (3 << (nbits - 2)) | 1
            if (p - 1) % e != 0 and primo_provavel(p):
                return p
    while True:
        p, q = primo(bits // 2), primo(bits - bits // 2)
        n = p * q
        if p != q and n.bit_length() == bits:
            return n, pow(e, -1, (p - 1) * (q - 1))


CABECALHOS = {  # DigestInfo da RFC 8017, seção 9.2, nota 1
    "sha256": "3031300d060960864801650304020105000420",
    "sha384": "3041300d060960864801650304020205000430",
    "sha512": "3051300d060960864801650304020305000440",
}


def bloco_pkcs1(tipo, resumo, k):
    t = de_hex(CABECALHOS[tipo]) + resumo
    return b"\x00\x01" + b"\xff" * (k - len(t) - 3) + b"\x00" + t


def mgf1(tipo, semente, n):
    saida, c = b"", 0
    while len(saida) < n:
        saida += hashlib.new(tipo, semente + c.to_bytes(4, "big")).digest()
        c += 1
    return saida[:n]


def bloco_pss(tipo, resumo, bits_em, sal):
    """EMSA-PSS-ENCODE da RFC 8017, seção 9.1.1."""
    tam = (bits_em + 7) // 8
    h = hashlib.new(tipo, b"\x00" * 8 + resumo + sal).digest()
    db = b"\x00" * (tam - len(sal) - len(h) - 2) + b"\x01" + sal
    mascarado = bytearray(a ^ b for a, b in zip(db, mgf1(tipo, h, len(db))))
    mascarado[0] &= 0xFF >> (8 * tam - bits_em)
    return bytes(mascarado) + h + b"\xbc"


def nbytes(n):
    return n.to_bytes((n.bit_length() + 7) // 8, "big")


def testar_exp_modular():
    casos = []
    for bits in (8, 31, 32, 33, 64, 255, 521, 1024, 2048, 4096):
        for _ in range(3):
            m = sorte.getrandbits(bits) | (1 << (bits - 1)) | 1
            if m == 1:
                m = 3
            b, e = sorte.randrange(0, m), sorte.getrandbits(sorte.choice((1, 17, 64, bits)))
            tam = (bits + 7) // 8
            casos.append((f"exp_modular {hx(nbytes(b))} {hx(nbytes(e))} {m.to_bytes(tam, 'big').hex()}",
                          pow(b, e, m).to_bytes(tam, "big").hex()))
    m = (1 << 4096) - 1         # o maior módulo possível, com a maior base
    casos.append((f"exp_modular {nbytes(m - 1).hex()} 010001 {nbytes(m).hex()}", pow(m - 1, 65537, m).to_bytes(512, "big").hex()))
    conferir(casos, "potência modular (Montgomery) igual ao pow() do Python, de 8 a 4096 bits")
    veio = rodar(["exp_modular 02 03 10", "exp_modular 11 03 11", f"exp_modular 02 03 01{'00' * 512}01"])
    verificar(veio == ["FALHA"] * 3, "potência modular recusa módulo par, base não reduzida e módulo acima de 4096 bits")


def testar_rsa_openssl():
    msg = aleatorio(300)
    arq, sig = os.path.join(PASTA, "rsa-msg.bin"), os.path.join(PASTA, "rsa-sig.bin")
    open(arq, "wb").write(msg)
    for bits, tipos in ((2048, ("sha256", "sha384", "sha512")), (2049, ("sha256",)), (3072, ("sha384",)), (4096, ("sha256", "sha512"))):
        pem = os.path.join(PASTA, f"rsa{bits}.pem")
        openssl("genpkey", "-algorithm", "RSA", "-pkeyopt", f"rsa_keygen_bits:{bits}", "-out", pem)
        n = openssl("rsa", "-in", pem, "-noout", "-modulus").decode().strip().split("=")[1].lower()
        if len(n) % 2:
            n = "0" + n
        e = "010001"
        bons, ruins = [], []
        for tipo in tipos:
            resumo = hashlib.new(tipo, msg).digest()
            outro = next(t for t in ("sha256", "sha384", "sha512") if t != tipo)
            openssl("dgst", f"-{tipo}", "-sign", pem, "-out", sig, arq)
            s1 = open(sig, "rb").read()
            openssl("dgst", f"-{tipo}", "-sign", pem, "-sigopt", "rsa_padding_mode:pss", "-sigopt", "rsa_pss_saltlen:digest", "-out", sig, arq)
            s2 = open(sig, "rb").read()
            openssl("dgst", f"-{tipo}", "-sign", pem, "-sigopt", "rsa_padding_mode:pss", "-sigopt", "rsa_pss_saltlen:10", "-out", sig, arq)
            s3 = open(sig, "rb").read()
            bons.append((f"rsa_pkcs1 {tipo} {n} {e} {resumo.hex()} {s1.hex()}", "OK"))
            bons.append((f"rsa_pss {tipo} {n} {e} {resumo.hex()} {s2.hex()}", "OK"))
            bons.append((f"rsa_pkcs1 {tipo} 0000{n} 0000{e} {resumo.hex()} {s1.hex()}", "OK"))    # zeros à esquerda na chave
            for nome, s in (("rsa_pkcs1", s1), ("rsa_pss", s2)):
                r2 = bytearray(resumo)
                r2[0] ^= 1
                ruins.append((f"{nome} {tipo} {n} {e} {r2.hex()} {s.hex()}", "FALHA"))             # outra mensagem
                for i in (0, len(s) // 2, len(s) - 1):
                    x = bytearray(s)
                    x[i] ^= 1
                    ruins.append((f"{nome} {tipo} {n} {e} {resumo.hex()} {x.hex()}", "FALHA"))     # assinatura adulterada
                n2 = bytearray(de_hex(n))
                n2[-2] ^= 4
                ruins.append((f"{nome} {tipo} {n2.hex()} {e} {resumo.hex()} {s.hex()}", "FALHA"))  # outra chave
                ruins.append((f"{nome} {tipo} {n} 03 {resumo.hex()} {s.hex()}", "FALHA"))           # outro expoente
                ruins.append((f"{nome} {tipo} {n} {e} {resumo.hex()} {s[:-1].hex()}", "FALHA"))    # um byte a menos
                ruins.append((f"{nome} {tipo} {n} {e} {resumo.hex()} 00{s.hex()}", "FALHA"))       # um byte a mais
                ruins.append((f"{nome} {outro} {n} {e} {hashlib.new(outro, msg).hexdigest()} {s.hex()}", "FALHA"))  # outro resumo
            ruins.append((f"rsa_pss {tipo} {n} {e} {resumo.hex()} {s1.hex()}", "FALHA"))            # PKCS#1 lida como PSS
            ruins.append((f"rsa_pkcs1 {tipo} {n} {e} {resumo.hex()} {s2.hex()}", "FALHA"))          # PSS lida como PKCS#1
            ruins.append((f"rsa_pss {tipo} {n} {e} {resumo.hex()} {s3.hex()}", "FALHA"))            # sal de outro tamanho
            ruins.append((f"rsa_pkcs1 {tipo} {n} {e} {resumo.hex()} {n}", "FALHA"))                 # assinatura = n
        conferir(bons, f"RSA {bits} bits: assinaturas PKCS#1 v1.5 e PSS do openssl aceitas")
        conferir(ruins, f"RSA {bits} bits: adulteradas, trocadas ou de outro tipo são recusadas")


def testar_rsa_fabricadas():
    """Com uma chave feita aqui temos o expoente privado: dá para assinar blocos
    montados de propósito com defeito, como faria quem tenta forjar."""
    for bits, e in ((1024, 3), (1537, 65537)):
        n, d = gerar_rsa(bits, e)
        k = (bits + 7) // 8
        nh, eh = nbytes(n).hex(), nbytes(e).hex()

        def assinar(bloco):
            return pow(int.from_bytes(bloco, "big"), d, n).to_bytes(k, "big").hex()

        bons, ruins = [], []
        for tipo in ("sha256", "sha384", "sha512"):
            resumo = hashlib.new(tipo, aleatorio(50)).digest()
            tam = len(resumo)
            certo = bloco_pkcs1(tipo, resumo, k)
            bons.append((f"rsa_pkcs1 {tipo} {nh} {eh} {resumo.hex()} {assinar(certo)}", "OK"))
            cab = de_hex(CABECALHOS[tipo])
            defeitos = {
                "tipo de bloco 02": b"\x00\x02" + certo[2:],
                "primeiro byte 01": b"\x01" + certo[1:],
                "um FF trocado": certo[:10] + b"\xfe" + certo[11:],
                "enchimento curto com lixo no fim": b"\x00\x01" + b"\xff" * 8 + b"\x00" + cab + resumo + b"\x00" * (k - 11 - len(cab) - tam),
                "sem o NULL dos parâmetros": b"\x00\x01" + b"\xff" * (k - len(cab) - tam - 1) + b"\x00"
                                             + bytes([0x30, cab[1] - 2, 0x30, 0x0b]) + cab[4:15] + cab[17:] + resumo,
                "sem cabeçalho": b"\x00\x01" + b"\xff" * (k - tam - 3) + b"\x00" + resumo,
                "sem o 00 separador": b"\x00\x01" + b"\xff" * (k - len(cab) - tam - 2) + cab + resumo,
            }
            for nome, bloco in defeitos.items():
                if len(bloco) != k:
                    raise FalhaTeste(f"teste mal montado: {nome}")
                ruins.append((f"rsa_pkcs1 {tipo} {nh} {eh} {resumo.hex()} {assinar(bloco)}", "FALHA"))

            if k < 2 * tam + 2 + 1:
                continue                    # chave pequena demais para PSS com este resumo
            sal = aleatorio(tam)
            em = bloco_pss(tipo, resumo, bits - 1, sal)
            bons.append((f"rsa_pss {tipo} {nh} {eh} {resumo.hex()} {assinar(em)}", "OK"))
            ndb = len(em) - tam - 1
            db = bytearray(a ^ b for a, b in zip(em[:ndb], mgf1(tipo, em[ndb:ndb + tam], ndb)))
            db[0] &= 0xFF >> (8 * len(em) - (bits - 1))

            def remontar(db_novo):
                m = bytearray(a ^ b for a, b in zip(db_novo, mgf1(tipo, em[ndb:ndb + tam], ndb)))
                m[0] &= 0xFF >> (8 * len(em) - (bits - 1))
                return bytes(m) + em[ndb:]

            sem_um = bytearray(db)
            sem_um[ndb - tam - 1] = 2
            zero_sujo = bytearray(db)
            zero_sujo[max(0, ndb - tam - 3)] |= 0x10
            defeitos = {
                "byte final diferente de BC": em[:-1] + b"\xbd",
                "separador 01 trocado": remontar(sem_um),
                "enchimento de zeros sujo": remontar(zero_sujo),
                "sal de outro tamanho": bloco_pss(tipo, resumo, bits - 1, sal[:-1]),
                "sal trocado": bloco_pss(tipo, hashlib.new(tipo, b"outra").digest(), bits - 1, sal),
            }
            if (bits - 1) % 8:
                defeitos["bit de cima ligado"] = bytes([em[0] | 0x80]) + em[1:]
            for nome, bloco in defeitos.items():
                valor = int.from_bytes(bloco, "big")
                if valor < n:
                    ruins.append((f"rsa_pss {tipo} {nh} {eh} {resumo.hex()} {assinar(bloco)}", "FALHA"))
        conferir(bons, f"RSA {bits} bits, e={e}: blocos montados pela RFC 8017 em Python são aceitos")
        conferir(ruins, f"RSA {bits} bits, e={e}: blocos com defeito de formato assinados de verdade são recusados")

    n, d = gerar_rsa(1016, 65537)
    resumo = hashlib.sha256(b"x").digest()
    s = pow(int.from_bytes(bloco_pkcs1("sha256", resumo, 127), "big"), d, n).to_bytes(127, "big").hex()
    conferir([(f"rsa_pkcs1 sha256 {nbytes(n).hex()} 010001 {resumo.hex()} {s}", "FALHA"),
              (f"rsa_pkcs1 sha256 {nbytes(n).hex()} 01 {resumo.hex()} {bloco_pkcs1('sha256', resumo, 127).hex()}", "FALHA")],
             "RSA recusa chave menor que 1024 bits e expoente 1")


# ------------------------------------------------------------ ECDSA

CURVAS = {
    "p256": dict(nome="prime256v1", bytes=32,
                 n=0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551),
    "p384": dict(nome="secp384r1", bytes=48,
                 n=0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC7634D81F4372DDF581A0DB248B0A77AECEC196ACCC52973),
}


def ler_der(der):
    """SEQUENCE { INTEGER r, INTEGER s } -> (r, s)."""
    pos = 2 if der[1] < 0x80 else 3
    numeros = []
    for _ in range(2):
        tam = der[pos + 1]
        numeros.append(int.from_bytes(der[pos + 2:pos + 2 + tam], "big"))
        pos += 2 + tam
    return numeros


def inteiro_der(v):
    b = v.to_bytes(v.bit_length() // 8 + 1, "big")      # sempre com espaço para o bit de sinal
    return b"\x02" + bytes([len(b)]) + b


def montar_der(r, s):
    corpo = inteiro_der(r) + inteiro_der(s)
    return b"\x30" + (bytes([len(corpo)]) if len(corpo) < 128 else b"\x81" + bytes([len(corpo)])) + corpo


def testar_ecdsa():
    arq, sig = os.path.join(PASTA, "ec-msg.bin"), os.path.join(PASTA, "ec-sig.bin")
    for curva, c in CURVAS.items():
        tam, ordem = c["bytes"], c["n"]
        bons, ruins = [], []
        for rodada in range(6):
            pem = os.path.join(PASTA, f"ec-{curva}.pem")
            openssl("ecparam", "-name", c["nome"], "-genkey", "-noout", "-out", pem)
            ponto = bytes_finais_der(["ec", "-in", pem, "-pubout"], 1 + 2 * tam)
            if ponto[0] != 4:
                raise FalhaTeste("não achei o ponto público na chave do openssl")
            # Resumos do tamanho da curva, maiores e menores que ela.
            for tipo in ("sha256", "sha384", "sha512"):
                msg = aleatorio(10 + 37 * rodada)
                open(arq, "wb").write(msg)
                openssl("dgst", f"-{tipo}", "-sign", pem, "-out", sig, arq)
                der = open(sig, "rb").read()
                resumo = hashlib.new(tipo, msg).digest()
                r, s = ler_der(der)
                p, h = ponto.hex(), resumo.hex()
                bons.append((f"ecdsa_der {curva} {p} {h} {der.hex()}", "OK"))
                bons.append((f"ecdsa {curva} {p} {h} {nbytes(r).hex()} {nbytes(s).hex()}", "OK"))
                # (r, n - s) também é assinatura válida da mesma mensagem: é
                # uma propriedade do ECDSA, e exercita outro caminho da conta.
                bons.append((f"ecdsa_der {curva} {p} {h} {montar_der(r, ordem - s).hex()}", "OK"))

                h2 = bytearray(resumo)
                h2[3] ^= 0x20
                ruins.append((f"ecdsa_der {curva} {p} {h2.hex()} {der.hex()}", "FALHA"))                    # outra mensagem
                ruins.append((f"ecdsa_der {curva} {p} {h} {montar_der(r ^ 2, s).hex()}", "FALHA"))          # r adulterado
                ruins.append((f"ecdsa_der {curva} {p} {h} {montar_der(r, s ^ (1 << 70)).hex()}", "FALHA"))  # s adulterado
                ruins.append((f"ecdsa_der {curva} {p} {h} {montar_der(s, r).hex()}", "FALHA"))              # r e s trocados
                if rodada == 0:
                    ruins.append((f"ecdsa_der {curva} {p} {h} {montar_der(0, s).hex()}", "FALHA"))          # r = 0
                    ruins.append((f"ecdsa_der {curva} {p} {h} {montar_der(r, 0).hex()}", "FALHA"))          # s = 0
                    ruins.append((f"ecdsa_der {curva} {p} {h} {montar_der(ordem, s).hex()}", "FALHA"))      # r = n
                    ruins.append((f"ecdsa_der {curva} {p} {h} {montar_der(r, s + ordem).hex()}", "FALHA"))  # s acima de n
                    ruins.append((f"ecdsa_der {curva} {p} {h} {der.hex()}00", "FALHA"))                     # lixo depois do DER
                    ruins.append((f"ecdsa_der {curva} {p} {h} {der[:-1].hex()}", "FALHA"))                  # DER cortado
                    ruins.append((f"ecdsa_der {curva} {p} {h} 31{der[1:].hex()}", "FALHA"))                 # não é SEQUENCE
                    fora = bytearray(ponto)
                    fora[-1] ^= 1
                    ruins.append((f"ecdsa_der {curva} {fora.hex()} {h} {der.hex()}", "FALHA"))              # ponto fora da curva
                    ruins.append((f"ecdsa_der {curva} 02{p[2:]} {h} {der.hex()}", "FALHA"))                 # formato comprimido
                    ruins.append((f"ecdsa_der {curva} {p[:-2]} {h} {der.hex()}", "FALHA"))                  # ponto cortado
                    ruins.append((f"ecdsa_der {curva} 04{'00' * (2 * tam)} {h} {der.hex()}", "FALHA"))      # (0, 0)
                    outra = "p384" if curva == "p256" else "p256"
                    ruins.append((f"ecdsa_der {outra} {p} {h} {der.hex()}", "FALHA"))                       # curva errada
            if rodada:
                # A chave da rodada anterior não confere a assinatura desta.
                ruins.append((f"ecdsa_der {curva} {anterior} {h} {der.hex()}", "FALHA"))
            anterior = ponto.hex()
        conferir(bons, f"ECDSA {curva.upper()}: assinaturas do openssl aceitas (SHA-256, 384 e 512; DER e r,s soltos)")
        conferir(ruins, f"ECDSA {curva.upper()}: adulteradas, de outra chave ou malformadas são recusadas")


# ------------------------------------------------------------ utilidades

def testar_utilidades():
    a = aleatorio(40)
    casos = [(f"diferentes {a.hex()} {a.hex()}", "0"), ("diferentes - -", "0")]
    for i in (0, 1, 20, 38, 39):
        for bit in (0, 7):
            b = bytearray(a)
            b[i] ^= 1 << bit
            casos.append((f"diferentes {a.hex()} {b.hex()}", "1"))
    casos.append((f"diferentes {'00' * 40} {'ff' * 40}", "1"))
    conferir(casos, "comparação em tempo constante: iguais dá 0, qualquer bit diferente dá 1")
    conferir([(f"zerar {a.hex()}", "copiou " + "00" * 40)], "copiar e zerar")

    # A comparação não pode ter saída antecipada: no código de máquina, o
    # único desvio condicional admitido é o do laço que percorre os bytes.
    asm = executar(["objdump", "-d", "--no-show-raw-insn", os.path.join(PASTA, "utilidades.o")]).decode()
    funcao = asm.split("<cri_diferentes>:")[1].split("\n\n")[0]
    desvios = re.findall(r"\bj(?!mp)[a-z]+\b", funcao)
    verificar(len(desvios) <= 2 and "call" not in funcao,
              f"cri_diferentes não tem saída antecipada (desvios condicionais: {len(desvios)}, só os do laço)")


def main():
    compilar()
    testar_utilidades()
    testar_resumos()
    testar_hmac_hkdf()
    testar_aes()
    testar_gcm()
    testar_chacha()
    testar_aead_ida_e_volta()
    testar_x25519()
    testar_ed25519()
    testar_exp_modular()
    testar_rsa_openssl()
    testar_rsa_fabricadas()
    testar_ecdsa()
    return 0


if __name__ == "__main__":
    sys.exit(main())
