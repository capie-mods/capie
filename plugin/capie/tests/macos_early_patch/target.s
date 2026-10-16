.text
.globl _owned_target
.p2align 4
_owned_target:
.rept 14
nop
.endr
movl $7,%eax
ret
