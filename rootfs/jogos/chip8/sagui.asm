; sagui.asm - um sagui que quica pela tela (exemplo do SaguiOS, H92).
;
; Programa de CHIP-8 escrito para o SaguiOS. O sagui anda na diagonal e
; muda de direção quando bate numa borda; cada batida faz um bip e soma 1
; no contador do canto. Para montar: chip8asm sagui.asm
;
; Registros: va, vb = posição do sagui; vc, vd = passo em x e em y (1 ou
; 255, que somado dá -1); ve = batidas; v0 a v4 = contas.

inicio:
    cls
    ld va, 10
    ld vb, 5
    ld vc, 1
    ld vd, 1
    ld ve, 0
    call mostrar_contador
    ld i, sagui
    drw va, vb, 8

laco:
    ld v4, 2                ; espera 2 quadros (1/30 s)
    ld dt, v4
espera:
    ld v4, dt
    se v4, 0
    jp espera

    ld i, sagui             ; apaga o sagui (desenhar de novo no mesmo lugar apaga)
    drw va, vb, 8
    add va, vc
    add vb, vd
    sne va, 0               ; bateu na esquerda: vai para a direita
    call bateu_x
    sne va, 56              ; bateu na direita (64 - 8): vai para a esquerda
    call bateu_x
    sne vb, 0
    call bateu_y
    sne vb, 24              ; 32 - 8
    call bateu_y
    ld i, sagui
    drw va, vb, 8
    jp laco

; Inverte o passo (1 <-> 255) e conta a batida.
bateu_x:
    ld v0, 0
    sub v0, vc              ; 0 - 1 = 255 e 0 - 255 = 1
    ld vc, v0
    jp contar
bateu_y:
    ld v0, 0
    sub v0, vd
    ld vd, v0
contar:
    ld v0, 4
    ld st, v0               ; um bip curto
    call mostrar_contador   ; apaga o número velho...
    add ve, 1
    call mostrar_contador   ; ...e desenha o novo
    ret

; Desenha (ou apaga, se já estiver lá) as batidas em 3 dígitos, no canto.
mostrar_contador:
    ld i, digitos
    ld b, ve                ; centena, dezena e unidade na memória
    ld v2, [i]              ; v0, v1, v2 = os três dígitos
    ld v3, 50
    ld v4, 26
    ld f, v0
    drw v3, v4, 5
    add v3, 5
    ld f, v1
    drw v3, v4, 5
    add v3, 5
    ld f, v2
    drw v3, v4, 5
    ret

; O sagui, 8x8: os tufos brancos das orelhas, os olhos e a boca.
sagui:
    db %#......#
    db %##.##.##
    db %.######.
    db %.#.##.#.
    db %.######.
    db %..#..#..
    db %...##...
    db %..####..

digitos:
    db 0, 0, 0
