# minios tiny の Forth（アセンブリだけ）
#
# 古典的な「間接スレッデッドコード」の Forth。
#   rsi = 命令ポインタ（次に実行する言葉の場所）
#   rsp = データスタック（数を積む）
#   rbp = リターンスタック（言葉から戻る場所を積む）
#
# 辞書: 言葉は連結リストでつながる。1 つの言葉の形は
#   [前の言葉へのリンク 8 バイト][長さ 1 バイト（上位ビットは印）][名前][8 バイト境界まで詰め物]
#   [コードフィールド 8 バイト: 機械語の番地][パラメータ: Forth で書いた言葉なら、呼ぶ言葉の並び]
# アセンブリで書いた言葉は、コードフィールドが自分の機械語を指す。
# Forth で書いた言葉は、コードフィールドが DOCOL を指し、その後ろに呼ぶ言葉のコードフィールドの番地が並ぶ。
    .code64

    .set F_IMMED,   0x80                # 即時の言葉（コンパイル中でもその場で実行する）
    .set F_HIDDEN,  0x20                # 隠れている（定義の途中の言葉）
    .set F_LENMASK, 0x1f                # 名前の長さ（31 文字まで）

# 次の言葉へ進む
    .macro NEXT
    lodsq
    jmp *(%rax)
    .endm

# リターンスタックに積む・降ろす
    .macro PUSHRSP reg
    lea -8(%rbp), %rbp
    mov \reg, (%rbp)
    .endm
    .macro POPRSP reg
    mov (%rbp), \reg
    lea 8(%rbp), %rbp
    .endm

    .set link, 0

# Forth で書く言葉（アセンブリの中で、呼ぶ言葉の並びとして書く）
    .macro defword name, label, flags=0
    .section .data
    .align 8
name_\label :
    .quad link
    .set link, name_\label
    .byte \flags + (22f - 21f)
21: .ascii "\name"
22: .align 8
\label :
    .quad DOCOL
    .endm

# アセンブリで書く言葉
    .macro defcode name, label, flags=0
    .section .data
    .align 8
name_\label :
    .quad link
    .set link, name_\label
    .byte \flags + (22f - 21f)
21: .ascii "\name"
22: .align 8
\label :
    .quad code_\label
    .text
code_\label :
    .endm

# 変数（番地を積む言葉）
    .macro defvar name, label, initial=0
    defcode \name, \label
    push $var_\label
    NEXT
    .section .data
    .align 8
var_\label :
    .quad \initial
    .endm

# ---- 起動 ----

    .text
    .global main
main:
    cld
    mov %rsp, var_S0                    # データスタックの底（boot.s が用意したスタック）
    mov $return_stack_top, %rbp
    mov $banner, %rsi
    call puts
    mov $cold_start, %rsi
    NEXT

    .section .data
    .align 8
cold_start:
    .quad QUIT

# Forth で書いた言葉を呼ぶ: 戻る場所をリターンスタックに積み、パラメータの先頭から実行する
    .text
    .align 8
DOCOL:
    PUSHRSP %rsi
    lea 8(%rax), %rsi
    NEXT

# ---- スタック ----

    defcode "drop", DROP
    pop %rax
    NEXT

    defcode "swap", SWAP
    pop %rax
    pop %rbx
    push %rax
    push %rbx
    NEXT

    defcode "dup", DUP
    push (%rsp)
    NEXT

    defcode "over", OVER
    push 8(%rsp)
    NEXT

    defcode "rot", ROT                  # ( a b c -- b c a )
    pop %rax
    pop %rbx
    pop %rcx
    push %rbx
    push %rax
    push %rcx
    NEXT

    defcode "-rot", NROT                # ( a b c -- c a b )
    pop %rax
    pop %rbx
    pop %rcx
    push %rax
    push %rcx
    push %rbx
    NEXT

    defcode "?dup", QDUP                # 0 でなければ複製する
    mov (%rsp), %rax
    test %rax, %rax
    jz 1f
    push %rax
1:  NEXT

    defcode ">r", TOR
    pop %rax
    PUSHRSP %rax
    NEXT

    defcode "r>", FROMR
    POPRSP %rax
    push %rax
    NEXT

    defcode "depth", DEPTH              # スタックに積まれている数
    mov var_S0, %rax
    sub %rsp, %rax
    shr $3, %rax
    push %rax
    NEXT

# ---- 計算 ----

    defcode "+", ADD
    pop %rax
    add %rax, (%rsp)
    NEXT

    defcode "-", SUB
    pop %rax
    sub %rax, (%rsp)
    NEXT

    defcode "*", MUL
    pop %rax
    pop %rbx
    imul %rbx, %rax
    push %rax
    NEXT

    defcode "/mod", DIVMOD              # ( a b -- 余り 商 )
    pop %rbx
    test %rbx, %rbx
    jz divide_by_zero
    pop %rax
    cqo
    idiv %rbx
    push %rdx
    push %rax
    NEXT

    defcode "negate", NEGATE
    negq (%rsp)
    NEXT

    defcode "1+", INCR
    incq (%rsp)
    NEXT

    defcode "1-", DECR
    decq (%rsp)
    NEXT

# 比較の結果: 真は -1（全部のビットが立つ）、偽は 0
    .macro compare cond
    pop %rax
    pop %rbx
    cmp %rax, %rbx
    set\cond %al
    movzbq %al, %rax
    neg %rax
    push %rax
    NEXT
    .endm

    defcode "=", EQU
    compare e
    defcode "<>", NEQU
    compare ne
    defcode "<", LT
    compare l
    defcode ">", GT
    compare g

    defcode "0=", ZEQU
    pop %rax
    test %rax, %rax
    setz %al
    movzbq %al, %rax
    neg %rax
    push %rax
    NEXT

    defcode "and", AND
    pop %rax
    and %rax, (%rsp)
    NEXT

    defcode "or", OR
    pop %rax
    or %rax, (%rsp)
    NEXT

    defcode "xor", XOR
    pop %rax
    xor %rax, (%rsp)
    NEXT

    defcode "invert", INVERT
    notq (%rsp)
    NEXT

# ---- メモリ ----

    defcode "!", STORE                  # ( 値 番地 -- )
    pop %rbx
    pop %rax
    mov %rax, (%rbx)
    NEXT

    defcode "@", FETCH                  # ( 番地 -- 値 )
    pop %rbx
    push (%rbx)
    NEXT

    defcode "c!", STOREBYTE
    pop %rbx
    pop %rax
    mov %al, (%rbx)
    NEXT

    defcode "c@", FETCHBYTE
    pop %rbx
    movzbq (%rbx), %rax
    push %rax
    NEXT

    defvar "state", STATE               # 0 = 実行中、1 = コンパイル中
    defvar "here", HERE, dict_space     # 次に言葉を書き足す場所
    defvar "s0", S0                     # データスタックの底
    defvar "base", BASE, 10             # 数を読み書きするときの基数

# ---- 実行の流れ（コンパイルした言葉の中で使う） ----

    defcode "lit", LIT                  # 次のセルを数として積む
    lodsq
    push %rax
    NEXT

    defcode "exit", EXIT                # 呼び出し元に戻る
    POPRSP %rsi
    NEXT

    defcode "branch", BRANCH            # 次のセルのぶんだけ飛ぶ（セルの場所からの相対）
    add (%rsi), %rsi
    NEXT

    defcode "0branch", ZBRANCH          # 0 なら飛ぶ
    pop %rax
    test %rax, %rax
    jz code_BRANCH
    lodsq
    NEXT

    defcode "execute", EXECUTE          # ( コードフィールドの番地 -- ) その言葉を実行する
    pop %rax
    jmp *(%rax)

# ---- 入出力 ----

    defcode "key", KEY                  # 次の 1 文字（打った行から）
    call _key
    push %rax
    NEXT

    defcode "emit", EMIT
    pop %rax
    call _emit
    NEXT

    defcode "cr", CR
    mov $'\n', %al
    call _emit
    NEXT

    defcode "space", SPACE
    mov $' ', %al
    call _emit
    NEXT

    defcode ".", DOT                    # 数を出して、空白を 1 つ
    pop %rax
    call _print_num
    mov $' ', %al
    call _emit
    NEXT

    defcode ".s", DOTS                  # スタックの中身を、底から順に出す: <数> a b c
    mov $'<', %al
    call _emit
    mov var_S0, %rax
    sub %rsp, %rax
    shr $3, %rax
    call _print_num
    push %rsi
    mov $str_dots, %rsi
    call puts
    pop %rsi
    mov var_S0, %rbx
1:  sub $8, %rbx
    cmp %rsp, %rbx
    jb 2f
    mov (%rbx), %rax
    call _print_num
    mov $' ', %al
    call _emit
    jmp 1b
2:  NEXT

    defcode "words", WORDS              # 辞書の言葉を、新しい順に並べる
    push %rsi
    mov var_LATEST, %rbx
1:  test %rbx, %rbx
    jz 3f
    movzbl 8(%rbx), %ecx
    test $F_HIDDEN, %cl
    jnz 2f
    and $F_LENMASK, %ecx
    lea 9(%rbx), %rdi
4:  mov (%rdi), %al
    call _emit
    inc %rdi
    dec %ecx
    jnz 4b
    mov $' ', %al
    call _emit
2:  mov (%rbx), %rbx
    jmp 1b
3:  mov $'\n', %al
    call _emit
    pop %rsi
    NEXT

    defcode "bye", BYE                  # QEMU の終了装置（isa-debug-exit）に書く。無ければ止まる
    mov $0, %al
    out %al, $0xf4
1:  cli
    hlt
    jmp 1b

# ---- 外側のインタプリタとコンパイラ ----

    defcode "word", WORD                # ( -- 番地 長さ ) 次の単語を読む
    call _word
    push %rdi
    push %rcx
    NEXT

    defcode "find", FIND                # ( 番地 長さ -- 言葉 か 0 )
    pop %rcx
    pop %rdi
    call _find
    push %rax
    NEXT

    defcode ">cfa", TCFA                # ( 言葉 -- コードフィールドの番地 )
    pop %rdi
    call _tcfa
    push %rdi
    NEXT

    defcode "create", CREATE            # ( 番地 長さ -- ) 新しい言葉の頭（リンクと名前）を書き足す
    pop %rcx
    pop %rbx
    call _create
    NEXT

    defcode ",", COMMA                  # ( 値 -- ) 辞書に 1 セル書き足す
    pop %rax
    call _comma
    NEXT

    defcode "[", LBRAC, F_IMMED         # 実行に切り替える
    movq $0, var_STATE
    NEXT

    defcode "]", RBRAC                  # コンパイルに切り替える
    movq $1, var_STATE
    NEXT

    defcode "hidden", HIDDEN            # ( 言葉 -- ) 隠す・現す
    pop %rdi
    xorb $F_HIDDEN, 8(%rdi)
    NEXT

    defcode "immediate", IMMEDIATE, F_IMMED  # 最後に定義した言葉を即時の言葉にする
    mov var_LATEST, %rdi
    xorb $F_IMMED, 8(%rdi)
    NEXT

    defcode "'", TICK                   # ( -- コードフィールドの番地 ) 次の単語の言葉を探す
    call _word
    call _find
    test %rax, %rax
    jz 1f
    mov %rax, %rdi
    call _tcfa
    push %rdi
    NEXT
1:  jmp undefined_word

# : 名前 ... ; で新しい言葉を定義する
    defword ":", COLON
    .quad WORD, CREATE                  # 名前を読んで頭を作る
    .quad LIT, DOCOL, COMMA             # コードフィールド: DOCOL
    .quad LATEST, FETCH, HIDDEN         # 定義の途中は隠す（自分自身を呼んだつもりで古い定義を呼ばないように）
    .quad RBRAC, EXIT                   # コンパイルに切り替える

    defword ";", SEMICOLON, F_IMMED
    .quad LIT, EXIT, COMMA              # 最後に exit を書き足す
    .quad LATEST, FETCH, HIDDEN         # 現す
    .quad LBRAC, EXIT                   # 実行に戻る

    defcode "interpret", INTERPRET      # 単語を 1 つ読んで、実行するかコンパイルする
    call _word
    call _find
    test %rax, %rax
    jz 5f                               # 辞書に無い → 数かもしれない
    mov %rax, %rdi
    movb 8(%rdi), %bl                   # 印
    call _tcfa                          # rdi = コードフィールドの番地
    mov %rdi, %rax
    test $F_IMMED, %bl
    jnz 4f                              # 即時の言葉はいつでも実行する
    cmpq $0, var_STATE
    je 4f
    call _comma                         # コンパイル中: 呼ぶ言葉として書き足す
    NEXT
4:  jmp *(%rax)                         # 実行する
5:  call _number                        # rax = 数、rcx = 読めなかった文字数
    test %rcx, %rcx
    jnz undefined_word
    cmpq $0, var_STATE
    je 6f
    mov %rax, %rbx                      # コンパイル中: lit と数を書き足す
    mov $LIT, %rax
    call _comma
    mov %rbx, %rax
    call _comma
    NEXT
6:  push %rax
    NEXT

    defcode "?stack", QSTACK            # 積んだ数より多く降ろしていたら、直して知らせる
    mov var_S0, %rax
    cmp %rax, %rsp
    jbe 1f
    mov %rax, %rsp
    push %rsi
    mov $str_underflow, %rsi
    call puts
    pop %rsi
1:  NEXT

    defcode "rsp!", RSPRESET            # リターンスタックを空にする
    mov $return_stack_top, %rbp
    NEXT

    defword "quit", QUIT                # 外側のループ: ずっと interpret を繰り返す
    .quad RSPRESET
1:  .quad INTERPRET, QSTACK, BRANCH
2:  .quad 1b - 2b                       # branch の飛び先は、このセルからの相対

# ---- 困ったとき ----

# 辞書にも数にも無い単語: 「? 単語」を出し、その行の残りを捨てて、実行に戻る
undefined_word:
    mov $str_undefined, %rsi
    call puts
    mov $word_buffer, %rsi
    call puts
    mov $'\n', %al
    call _emit
    jmp abort_line

divide_by_zero:
    mov $str_divzero, %rsi
    call puts
abort_line:
    mov var_input_len, %rax
    mov %rax, var_input_pos
    movq $0, var_STATE
    mov var_S0, %rsp
    mov $cold_start, %rsi
    NEXT

# ---- 下請け（アセンブリの関数。rsi・rbp・rsp は壊さない） ----

# _emit: al を 1 文字出す（改行の前に \r を足す）
_emit:
    cmp $'\n', %al
    jne 1f
    push %rax
    mov $'\r', %al
    call putc
    pop %rax
1:  jmp putc

# _key: 打った行から次の 1 文字を返す。行を使い切ったら「 ok」を出して、次の行を読む
_key:
    mov var_input_pos, %rax
    cmp var_input_len, %rax
    jb 2f
    cmpq $0, var_STATE                  # コンパイル中（: の途中で改行した）は ok を出さない
    jne 1f
    cmpq $0, var_input_started
    je 1f
    push %rsi
    mov $str_ok, %rsi
    call puts
    pop %rsi
1:  movq $1, var_input_started
    push %rsi
    push %rdi                           # _word が単語を書き込んでいる途中かもしれない
    mov $input_buffer, %rdi
    call read_line                      # rax = 長さ（行末に改行を足す）
    pop %rdi
    pop %rsi
    movb $'\n', input_buffer(%rax)
    inc %rax
    mov %rax, var_input_len
    movq $0, var_input_pos
    xor %eax, %eax
2:  movzbq input_buffer(%rax), %rbx
    inc %rax
    mov %rax, var_input_pos
    mov %rbx, %rax
    ret

# _word: 空白と改行を飛ばして単語を 1 つ読み、word_buffer に入れる（0 で終える）。rdi = 番地、rcx = 長さ
_word:
1:  call _key
    cmp $' ', %al
    jbe 1b                              # 空白、改行、制御文字を飛ばす
    cmp $'\\', %al                      # \ から行末まではコメント
    jne 3f
2:  call _key
    cmp $'\n', %al
    jne 2b
    jmp 1b
3:  mov $word_buffer, %rdi
4:  cmp $word_buffer + 31, %rdi
    jae 5f
    stosb
5:  call _key
    cmp $' ', %al
    ja 4b
    movb $0, (%rdi)
    sub $word_buffer, %rdi
    mov %rdi, %rcx
    mov $word_buffer, %rdi
    ret

# _find: rdi・rcx の名前の言葉を、新しい順に探す（大文字と小文字は区別しない）。rax = 言葉か 0
_find:
    push %rsi
    mov var_LATEST, %rdx
1:  test %rdx, %rdx
    jz 4f
    movzbl 8(%rdx), %eax
    and $(F_HIDDEN | F_LENMASK), %eax   # 隠れている言葉は長さが合わなくなり、見つからない
    cmp %ecx, %eax
    jne 3f
    xor %r8d, %r8d                      # 1 文字ずつ比べる
2:  cmp %rcx, %r8
    je 5f
    movzbl 9(%rdx,%r8), %eax
    movzbl (%rdi,%r8), %ebx
    or $0x20, %al                       # 英字なら小文字に（記号は変わらないものだけ比べるので十分）
    or $0x20, %bl
    cmp %al, %bl
    jne 3f
    inc %r8
    jmp 2b
3:  mov (%rdx), %rdx
    jmp 1b
4:  xor %eax, %eax
    pop %rsi
    ret
5:  mov %rdx, %rax
    pop %rsi
    ret

# _tcfa: rdi の言葉のコードフィールドの番地を rdi に返す
_tcfa:
    movzbl 8(%rdi), %eax
    and $F_LENMASK, %eax
    lea 9 + 7(%rdi,%rax), %rdi
    and $~7, %rdi
    ret

# _number: rdi・rcx の文字列を base の基数で数にする。rax = 数、rcx = 読めなかった文字数（0 なら全部読めた）
_number:
    xor %eax, %eax
    xor %r9d, %r9d                      # 負の数か
    test %rcx, %rcx
    jz 5f
    cmpb $'-', (%rdi)
    jne 1f
    cmp $1, %rcx                        # "-" だけは数ではない
    je 5f
    mov $1, %r9d
    inc %rdi
    dec %rcx
1:  movzbl (%rdi), %ebx
    sub $'0', %ebx
    cmp $10, %ebx
    jb 2f
    movzbl (%rdi), %ebx
    or $0x20, %ebx                      # a〜z を 10〜35 に
    sub $'a', %ebx
    js 4f
    add $10, %ebx
2:  cmp var_BASE, %rbx
    jae 4f
    imul var_BASE, %rax
    add %rbx, %rax
    inc %rdi
    dec %rcx
    jnz 1b
4:  test %r9, %r9
    jz 5f
    neg %rax
5:  ret

# _comma: rax を辞書に書き足す
_comma:
    mov var_HERE, %rdi
    stosq
    mov %rdi, var_HERE
    ret

# _create: rbx・rcx の名前で、新しい言葉の頭を書き足す（コードフィールドはまだ）
_create:
    push %rsi
    mov var_HERE, %rdi
    mov %rdi, %rdx                      # 新しい言葉の場所
    mov var_LATEST, %rax
    stosq                               # リンク
    mov %cl, %al
    stosb                               # 長さ
    mov %rbx, %rsi
    rep movsb                           # 名前
    add $7, %rdi                        # 8 バイト境界に揃える
    and $~7, %rdi
    mov %rdx, var_LATEST
    mov %rdi, var_HERE
    pop %rsi
    ret

# _print_num: rax を base の基数で出す（負なら - を付ける）
_print_num:
    push %rsi
    mov $num_buffer + 70, %rsi
    movb $0, (%rsi)
    mov %rax, %r8
    test %rax, %rax
    jns 1f
    neg %rax
1:  xor %edx, %edx
    divq var_BASE
    cmp $10, %dl
    jb 2f
    add $'a' - 10 - '0', %dl
2:  add $'0', %dl
    dec %rsi
    mov %dl, (%rsi)
    test %rax, %rax
    jnz 1b
    test %r8, %r8
    jns 3f
    dec %rsi
    movb $'-', (%rsi)
3:  call puts
    pop %rsi
    ret

# ---- 最後の言葉（ここより後に言葉を定義しない） ----

    defcode "latest", LATEST
    push $var_LATEST
    NEXT

    .section .data
    .align 8
var_LATEST:
    .quad link                          # 辞書の一番新しい言葉（この行より後に言葉を定義しない）
var_input_len:     .quad 0
var_input_pos:     .quad 0
var_input_started: .quad 0

    .section .rodata
banner:        .asciz "\nminios tiny forth\n"
str_ok:        .asciz " ok\n"
str_dots:      .asciz "> "
str_undefined: .asciz "? "
str_underflow: .asciz "stack underflow\n"
str_divzero:   .asciz "division by zero\n"

    .section .bss
    .align 8
input_buffer:  .skip 260
word_buffer:   .skip 32
num_buffer:    .skip 72
return_stack:  .skip 8192
return_stack_top:
    .align 8
dict_space:    .skip 65536              # 新しく定義した言葉を書き足す場所

    .section .note.GNU-stack, "", @progbits
