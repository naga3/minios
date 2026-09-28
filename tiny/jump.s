# ジャンプ: 横から見た 1 画面のステージを、ロボットが歩いて跳ぶ。
# 左右（矢印か A・D）で歩き、Z・スペース・上（か W）で跳ぶ。長く押すと高く跳ぶ。? の箱は下から叩くと使った箱になる。
# 穴に落ちるとやり直し。Esc でシェルに戻る
#
# ステージは 16 × 16 点のマスで 20 × 12 マス（画面の y = 8 から。1 行目は文字）。マスも絵もスプライト（sprite.s）で描く。
# 1 秒に 50 コマ（タイマー 2 回ごと）。位置と速さは 1/16 点の単位で持ち、なめらかに加速・減速する
    .code64
    .set TILE, 16
    .set MAP_W, 20
    .set MAP_H, 12
    .set TOP, 8                         # ステージを描き始める画面の y
    .set FRAME, 2                       # 何回のタイマー（10ms）で 1 コマ進むか
    .set SKY, 16 + 2 * 36 + 4 * 6 + 5   # 空の色
    # 速さ（1/16 点の単位、1 コマあたり）
    .set ACCEL, 3                       # 歩き出し
    .set FRICTION, 4                    # 止まるとき
    .set MAX_RUN, 40                    # 歩く速さの上限（2.5 点）
    .set GRAVITY, 6                     # 重力
    .set JUMP_V, 116                    # 跳んだ瞬間の上向きの速さ（高さは 4 マスほど）
    .set MAX_FALL, 112                  # 落ちる速さの上限（1 マスより小さく。すり抜けないように）
    # 当たりの四角: 絵の 16 × 16 のうち、左から HB_L 点目から幅 HB_W 点、高さは 16 点
    .set HB_L, 3
    .set HB_W, 10
    # キーの番号（kbd.s の keys の表）
    .set SC_W, 0x11
    .set SC_A, 0x1e
    .set SC_D, 0x20
    .set SC_Z, 0x2c
    .set SC_SPACE, 0x39
    .set SC_UP, 0xc8
    .set SC_LEFT, 0xcb
    .set SC_RIGHT, 0xcd

    .text
    .global jump_main

jump_main:
    push %rbx
    push %r12
    xor %eax, %eax                      # カーソルを隠す
    call con_cursor
new_life:
    call setup
play:
    mov ticks, %rax                     # 次のコマの時刻まで、CPU を休める
    cmp next_frame, %rax
    jae 1f
    hlt
    jmp play
1:  add $FRAME, %rax
    mov %rax, next_frame
2:  call getc_nowait                    # 貯まった文字を読む。Esc なら終わり、ほかは捨てる（シェルに残らないように）。
    test %al, %al                       # Esc は押しっぱなしの表ではなく、こちらで見る（1 コマより短く押して離しても取りこぼさない）
    jz 3f
    cmp $27, %al
    je quit
    jmp 2b
3:
    call update                         # 動かす（穴に落ちたら 1）
    test %eax, %eax
    jnz miss
    call draw
    jmp play
miss:
    mov $0x040f, %ax                    # 赤地に白
    call con_color
    mov $17, %al
    mov $12, %ah
    call con_goto
    mov $str_miss, %rsi
    call con_puts
    mov ticks, %rbx                     # 1 秒見せて、やり直す（その間も Esc は効く）
    add $100, %rbx
1:  call getc_nowait
    cmp $27, %al
    je quit
    hlt
    cmp %rbx, ticks
    jb 1b
    jmp new_life
quit:
    call getc_nowait
    test %al, %al
    jnz quit
    mov $1, %al
    call con_cursor
    mov $0x0107, %ax                    # シェルの色に戻して消す
    call con_color
    call con_clear
    pop %r12
    pop %rbx
    ret

# setup: ステージを初めの形に戻し、ロボットを左に置く
setup:
    mov $stage, %rsi                    # ? の箱は叩くと変わるので、写しを使う
    mov $level, %rdi
    mov $MAP_W * MAP_H, %ecx
    rep movsb
    movq $(2 * TILE) << 4, px
    movq $(9 * TILE) << 4, py
    movq $0, vx
    movq $0, vy
    movq $0, anim
    movb $0, facing
    movb $0, on_ground
    movb $1, jump_held                  # 押しっぱなしのキーで、いきなり跳ばないように
    mov ticks, %rax
    mov %rax, next_frame
    call draw
    ret

# update: キーを見て速さを決め、動かして、ぶつかったら押し戻す。穴に落ちたら eax = 1
update:
    movzbl keys + SC_LEFT, %eax         # 左右: 押している向きへ加速。どちらも（両方）押していなければ止まっていく
    or keys + SC_A, %al
    movzbl keys + SC_RIGHT, %ecx
    or keys + SC_D, %cl
    mov vx, %rdx
    cmp %al, %cl
    je 3f
    test %cl, %cl
    jz 2f
    add $ACCEL, %rdx                    # 右
    cmp $MAX_RUN, %rdx
    jle 1f
    mov $MAX_RUN, %rdx
1:  movb $0, facing
    jmp 6f
2:  sub $ACCEL, %rdx                    # 左
    cmp $-MAX_RUN, %rdx
    jge 1f
    mov $-MAX_RUN, %rdx
1:  movb $1, facing
    jmp 6f
3:  test %rdx, %rdx                     # 0 に近づける
    jz 6f
    jl 4f
    sub $FRICTION, %rdx
    jge 6f
    xor %edx, %edx
    jmp 6f
4:  add $FRICTION, %rdx
    jle 6f
    xor %edx, %edx
6:  mov %rdx, vx
    movzbl keys + SC_Z, %eax            # 跳ぶ: 地面に立っていて、押した瞬間だけ（押しっぱなしでは続けて跳ばない）
    or keys + SC_SPACE, %al
    or keys + SC_UP, %al
    or keys + SC_W, %al
    mov vy, %rdx
    test %al, %al
    jz 7f
    cmpb $0, jump_held
    jne 7f
    cmpb $0, on_ground
    je 7f
    mov $-JUMP_V, %rdx
7:  mov %al, jump_held
    mov $GRAVITY, %ecx                  # 重力。上っている途中で離したら 3 倍（小さく跳ぶ）
    test %rdx, %rdx
    jns 8f
    test %al, %al
    jnz 8f
    imul $3, %ecx
8:  add %rcx, %rdx
    cmp $MAX_FALL, %rdx
    jle 9f
    mov $MAX_FALL, %rdx
9:  mov %rdx, vy
    mov vx, %rax                        # 横に動かしてから押し戻し、次に縦
    add %rax, px
    call collide_x
    mov vy, %rax
    add %rax, py
    call collide_y
    incq anim
    mov py, %rax                        # ステージの下より落ちた
    sar $4, %rax
    cmp $MAP_H * TILE, %rax
    jge 10f
    xor %eax, %eax
    ret
10: mov $1, %eax
    ret

# collide_x: 横に動いた先が壁（マス）なら、そのマスの手前まで戻して止める
collide_x:
    mov vx, %rax
    test %rax, %rax
    jz 9f
    mov px, %rdi
    sar $4, %rdi
    add $HB_L, %rdi                     # 当たりの左端
    test %rax, %rax
    js 1f
    add $HB_W - 1, %rdi                 # 右へ動いているなら右端
1:  mov py, %rsi                        # 頭・胴・足の 3 点を見る
    sar $4, %rsi
    call solid
    test %eax, %eax
    jnz 2f
    add $7, %rsi
    call solid
    test %eax, %eax
    jnz 2f
    add $8, %rsi
    call solid
    test %eax, %eax
    jnz 2f
    ret
2:  and $-TILE, %rdi                    # ぶつかったマスの左端
    cmpq $0, vx
    jl 3f
    sub $HB_L + HB_W, %rdi              # 右へ動いていた: マスの左に並ぶ
    jmp 4f
3:  add $TILE - HB_L, %rdi              # 左へ動いていた: マスの右に並ぶ
4:  shl $4, %rdi
    mov %rdi, px
    movq $0, vx
9:  ret

# collide_y: 縦に動いた先がマスなら戻して止める。落ちていたなら着地、上っていたなら頭を打つ（? の箱を叩く）。
# 最後に、足の 1 点下がマスなら「立っている」
collide_y:
    mov px, %rdi
    sar $4, %rdi
    add $HB_L, %rdi                     # rdi = 当たりの左端
    mov py, %rsi
    sar $4, %rsi                        # rsi = 頭
    mov vy, %rax
    test %rax, %rax
    jz 5f
    js 3f
    add $15, %rsi                       # 落ちている: 足もと
    call solid2
    test %eax, %eax
    jz 5f
    and $-TILE, %rsi                    # マスの上に乗る
    sub $16, %rsi
    shl $4, %rsi
    mov %rsi, py
    movq $0, vy
    jmp 5f
3:  call solid2                         # 上っている: 頭
    test %eax, %eax
    jz 5f
    push %rdi
    add $HB_W / 2, %rdi                 # 頭の真ん中の上のマスを叩く
    call bump
    pop %rdi
    or $TILE - 1, %rsi                  # マスの下に戻る
    inc %rsi
    shl $4, %rsi
    mov %rsi, py
    movq $0, vy
5:  mov py, %rsi
    sar $4, %rsi
    add $16, %rsi
    call solid2
    mov %al, on_ground
    ret

# bump: 点 (rdi, rsi) のマスが ? の箱なら、使った箱にする
bump:
    call tile_at
    test %rax, %rax
    jz 1f
    cmpb $'?', (%rax)
    jne 1f
    movb $'U', (%rax)
1:  ret

# solid2: 点 (rdi, rsi) か (rdi + HB_W - 1, rsi) がマスなら eax = 1（当たりの左右の端を見る）
solid2:
    call solid
    test %eax, %eax
    jnz 1f
    push %rdi
    add $HB_W - 1, %rdi
    call solid
    pop %rdi
1:  ret

# solid: 点 (rdi, rsi)（ステージの中の位置）が通れないなら eax = 1。ステージの左右の外は壁、上下の外は空
solid:
    call tile_at
    test %rax, %rax
    jz 2f
    movzbl (%rax), %eax
    cmp $'.', %al
    je 1f
    cmp $'C', %al                       # 雲は飾り
    je 1f
    mov $1, %eax
    ret
1:  xor %eax, %eax
    ret
2:  xor %eax, %eax
    cmp $0, %rdi
    jl 3f
    cmp $MAP_W * TILE, %rdi
    jl 4f
3:  mov $1, %eax
4:  ret

# tile_at: 点 (rdi, rsi) のマスの番地を rax に。ステージの外なら 0
tile_at:
    push %rdi
    push %rsi
    sar $4, %rdi
    sar $4, %rsi
    xor %eax, %eax
    cmp $MAP_W, %rdi                    # 負の数は符号なしで比べると大きい
    jae 1f
    cmp $MAP_H, %rsi
    jae 1f
    imul $MAP_W, %rsi, %rax
    add %rdi, %rax
    add $level, %rax
1:  pop %rsi
    pop %rdi
    ret

# draw: 空、マス、ロボット、1 行目の文字を描いて、帰線を待って写す
draw:
    xor %edi, %edi
    mov $TOP, %esi
    mov $MAP_W * TILE, %edx
    mov $MAP_H * TILE, %ecx
    mov $SKY, %r8d
    call fill_rect
    xor %r8d, %r8d                      # マスは反転しない
    xor %r12d, %r12d                    # r12 = 行
1:  xor %ebx, %ebx                      # rbx = 列
2:  imul $MAP_W, %r12, %rax
    movzbl level(%rbx,%rax), %eax
    call tile_picture
    test %rdx, %rdx
    jz 3f
    mov %rbx, %rdi
    shl $4, %rdi
    mov %r12, %rsi
    shl $4, %rsi
    add $TOP, %rsi
    call draw_sprite
3:  inc %rbx
    cmp $MAP_W, %rbx
    jne 2b
    inc %r12
    cmp $MAP_H, %r12
    jne 1b
    mov $pic_jump, %rdx                 # ロボット: 宙にいれば跳ぶ絵、歩いていれば 2 枚を交互に
    cmpb $0, on_ground
    je 4f
    mov $pic_stand, %rdx
    cmpq $0, vx
    je 4f
    testb $4, anim
    jz 4f
    mov $pic_walk, %rdx
4:  mov px, %rdi
    sar $4, %rdi
    mov py, %rsi
    sar $4, %rsi
    add $TOP, %rsi
    movzbl facing, %r8d
    call draw_sprite
    xor %edi, %edi                      # 1 行目（跳ぶと上にはみ出すので、毎回描き直す）
    xor %esi, %esi
    mov $MAP_W * TILE, %edx
    mov $TOP, %ecx
    xor %r8d, %r8d
    call fill_rect
    mov $0x000f, %ax
    call con_color
    xor %eax, %eax
    call con_goto
    mov $str_title, %rsi
    call con_write
    call wait_vsync
    call present
    ret

# tile_picture: al のマスの文字の絵を rdx に。空なら 0
tile_picture:
    mov $tiles, %rdx
1:  cmpb $0, (%rdx)
    je 2f
    cmp (%rdx), %al
    je 3f
    add $9, %rdx
    jmp 1b
2:  xor %edx, %edx
    ret
3:  mov 1(%rdx), %rdx
    ret

    .section .rodata
str_title: .asciz "JUMP  arrows  Z/space:jump  Esc:quit"
str_miss:  .asciz " MISS "

# ステージ: 1 文字 1 マス。. 空、G 草の地面、# 土、B れんが、? 箱（叩くと U）、C 雲（通れる）
stage:
    .ascii "...................."
    .ascii "..CC.........CC....."
    .ascii "...................."
    .ascii "...................."
    .ascii "...........???......"
    .ascii "...................."
    .ascii "...................."
    .ascii "....B?B........BBB.."
    .ascii "...................."
    .ascii "........GG.........."
    .ascii "GGGGGGGG##GG..GGGGGG"
    .ascii "############..######"

# マスの文字 → 絵
tiles:
    .ascii "G"
    .quad pic_grass
    .ascii "#"
    .quad pic_dirt
    .ascii "B"
    .quad pic_brick
    .ascii "?"
    .quad pic_question
    .ascii "U"
    .quad pic_used
    .ascii "C"
    .quad pic_cloud
    .byte 0

# 絵（16 × 16）。色の文字は sprite.s を見る。ロボットは右向きに描く（左向きは反転）
pic_stand:
    .ascii ".....777777....."
    .ascii "...77ffffff77..."
    .ascii "..7ffffffffff7.."
    .ascii "..7ffff000000f7."
    .ascii "..7fff0bb00bb0f7"
    .ascii "..7ffff000000f7."
    .ascii "..7ffffffffff7.."
    .ascii "...77ffffff77..."
    .ascii ".....c7777c....."
    .ascii "....19999991...."
    .ascii "...7199ee9917..."
    .ascii "...7199999917..."
    .ascii "....19999991...."
    .ascii "....11111111...."
    .ascii "....88....88...."
    .ascii "...888....888..."
pic_walk:
    .ascii ".....777777....."
    .ascii "...77ffffff77..."
    .ascii "..7ffffffffff7.."
    .ascii "..7ffff000000f7."
    .ascii "..7fff0bb00bb0f7"
    .ascii "..7ffff000000f7."
    .ascii "..7ffffffffff7.."
    .ascii "...77ffffff77..."
    .ascii ".....c7777c....."
    .ascii "....19999991...."
    .ascii "...7199ee9917..."
    .ascii "...7199999917..."
    .ascii "....19999991...."
    .ascii "....11111111...."
    .ascii "...88......88..."
    .ascii "..888.......888."
pic_jump:
    .ascii ".....777777....."
    .ascii "...77ffffff77..."
    .ascii "..7ffffffffff7.."
    .ascii "..7ffff000000f7."
    .ascii "..7fff0bb00bb0f7"
    .ascii "..7ffff000000f7."
    .ascii "..7ffffffffff7.."
    .ascii "...77ffffff77..."
    .ascii ".....c7777c..7.."
    .ascii "....19999991.7.."
    .ascii "...7199ee991...."
    .ascii "...7199999917..."
    .ascii "....19999991...."
    .ascii "....11111111...."
    .ascii ".....88..88....."
    .ascii "....888..888...."
pic_grass:
    .ascii "aaaaaaaaaaaaaaaa"
    .ascii "aaaaaaaaaaaaaaaa"
    .ascii "2aa2a22aa2aa2a2a"
    .ascii "2222222222222222"
    .ascii "nnnnnnnnnnnnnnnn"
    .ascii "nnnknnnnnnnnknnn"
    .ascii "nnnnnnnnnnnnnnnn"
    .ascii "nknnnnnnknnnnnnn"
    .ascii "nnnnnnnnnnnnnnnn"
    .ascii "nnnnnknnnnnnnnkn"
    .ascii "nnnnnnnnnnnnnnnn"
    .ascii "nnknnnnnnnnnnnnn"
    .ascii "nnnnnnnnnknnnnnn"
    .ascii "nnnnnnnnnnnnnnnn"
    .ascii "nnnnnknnnnnnnnnn"
    .ascii "nnnnnnnnnnnnnnnn"
pic_dirt:
    .ascii "nnnnnnnnnnnnnnnn"
    .ascii "nnnnnnnnnnknnnnn"
    .ascii "nnnnnnnnnnnnnnnn"
    .ascii "nnknnnnnnnnnnnnn"
    .ascii "nnnnnnnnnnnnnnnn"
    .ascii "nnnknnnnnnnnknnn"
    .ascii "nnnnnnnnnnnnnnnn"
    .ascii "nknnnnnnknnnnnnn"
    .ascii "nnnnnnnnnnnnnnnn"
    .ascii "nnnnnknnnnnnnnkn"
    .ascii "nnnnnnnnnnnnnnnn"
    .ascii "nnknnnnnnnnnnnnn"
    .ascii "nnnnnnnnnknnnnnn"
    .ascii "nnnnnnnnnnnnnnnn"
    .ascii "nnnnnknnnnnnnnnn"
    .ascii "nnnnnnnnnnnnnnnn"
pic_brick:
    .ascii "0000000000000000"
    .ascii "rrrrrrr0rrrrrrr0"
    .ascii "rrrrrrr0rrrrrrr0"
    .ascii "rrrrrrr0rrrrrrr0"
    .ascii "0000000000000000"
    .ascii "rrr0rrrrrrr0rrrr"
    .ascii "rrr0rrrrrrr0rrrr"
    .ascii "rrr0rrrrrrr0rrrr"
    .ascii "0000000000000000"
    .ascii "rrrrrrr0rrrrrrr0"
    .ascii "rrrrrrr0rrrrrrr0"
    .ascii "rrrrrrr0rrrrrrr0"
    .ascii "0000000000000000"
    .ascii "rrr0rrrrrrr0rrrr"
    .ascii "rrr0rrrrrrr0rrrr"
    .ascii "rrr0rrrrrrr0rrrr"
pic_question:
    .ascii "oooooooooooooook"
    .ascii "o0oooooooooooo0k"
    .ascii "oooooooooooooook"
    .ascii "ooooooffffoooook"
    .ascii "oooooffooffooook"
    .ascii "oooooffooffooook"
    .ascii "ooooooooffoooook"
    .ascii "oooooooffooooook"
    .ascii "oooooooffooooook"
    .ascii "oooooooooooooook"
    .ascii "oooooooffooooook"
    .ascii "oooooooffooooook"
    .ascii "oooooooooooooook"
    .ascii "oooooooooooooook"
    .ascii "o0oooooooooooo0k"
    .ascii "kkkkkkkkkkkkkkkk"
pic_used:
    .ascii "kkkkkkkkkkkkkkkk"
    .ascii "k0nnnnnnnnnnnn0k"
    .ascii "knnnnnnnnnnnnnnk"
    .ascii "knnnnnnnnnnnnnnk"
    .ascii "knnnnnnnnnnnnnnk"
    .ascii "knnnnnnnnnnnnnnk"
    .ascii "knnnnnnnnnnnnnnk"
    .ascii "knnnnnnnnnnnnnnk"
    .ascii "knnnnnnnnnnnnnnk"
    .ascii "knnnnnnnnnnnnnnk"
    .ascii "knnnnnnnnnnnnnnk"
    .ascii "knnnnnnnnnnnnnnk"
    .ascii "knnnnnnnnnnnnnnk"
    .ascii "knnnnnnnnnnnnnnk"
    .ascii "k0nnnnnnnnnnnn0k"
    .ascii "kkkkkkkkkkkkkkkk"
pic_cloud:
    .ascii "................"
    .ascii "................"
    .ascii "................"
    .ascii "................"
    .ascii "......ffff......"
    .ascii "....ffffffff...."
    .ascii "...ffffffffff..."
    .ascii "..ffffffffffff.."
    .ascii ".ffffffffffffff."
    .ascii "ffffffffffffffff"
    .ascii "ffffffffffffffff"
    .ascii ".ffffffffffffff."
    .ascii "..77ffffffff77.."
    .ascii "................"
    .ascii "................"
    .ascii "................"

    .section .bss
    .align 8
px:         .skip 8                     # ロボットの絵の左上（ステージの中、1/16 点の単位）
py:         .skip 8
vx:         .skip 8                     # 速さ（1/16 点 / コマ）
vy:         .skip 8
anim:       .skip 8                     # コマの数（歩く絵の切り替え）
next_frame: .skip 8
facing:     .skip 1                     # 0 右向き、1 左向き
on_ground:  .skip 1
jump_held:  .skip 1                     # 前のコマで跳ぶキーを押していた
level:      .skip MAP_W * MAP_H         # 今のステージ（stage の写し）

    .section .note.GNU-stack, "", @progbits
