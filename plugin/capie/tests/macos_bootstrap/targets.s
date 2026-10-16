.text
.p2align 4
.globl _target0
_target0:
.rept 14
nop
.endr
movq %rdi,%rax
addq %rsi,%rax
addq %rdx,%rax
addq %rcx,%rax
addq %r8,%rax
addq %r9,%rax
addq 8(%rsp),%rax
cvttsd2si %xmm0,%r10
addq %r10,%rax
retq
.text
.p2align 4
.globl _target1
_target1:
.rept 14
nop
.endr
movq %rdi,%rax
movq $7,(%rdi)
movq $8,8(%rdi)
movq $9,16(%rdi)
retq
.text
.p2align 4
.globl _target2
_target2:
.rept 14
nop
.endr
movq $7,%rax
retq
.text
.p2align 4
.globl _target3
_target3:
.rept 14
nop
.endr
movq $7,%rax
retq
.text
.p2align 4
.globl _target4
_target4:
.rept 14
nop
.endr
movq $7,%rax
retq
.text
.p2align 4
.globl _target5
_target5:
.rept 14
nop
.endr
movq $7,%rax
retq
.text
.p2align 4
.globl _target6
_target6:
.rept 14
nop
.endr
movq $7,%rax
retq
.text
.p2align 4
.globl _target7
_target7:
.rept 14
nop
.endr
movq $7,%rax
retq
.text
.p2align 4
.globl _target8
_target8:
.rept 14
nop
.endr
movq $7,%rax
retq
.text
.p2align 4
.globl _target9
_target9:
.rept 14
nop
.endr
movq $7,%rax
retq
.text
.p2align 4
.globl _target10
_target10:
.rept 14
nop
.endr
movq $7,%rax
retq
.text
.p2align 4
.globl _target11
_target11:
.rept 14
nop
.endr
movq $7,%rax
retq
.text
.p2align 4
.globl _target12
_target12:
.rept 14
nop
.endr
movq $7,%rax
retq
.text
.p2align 4
.globl _target13
_target13:
.rept 14
nop
.endr
movq $7,%rax
retq
.text
.p2align 4
.globl _target14
_target14:
.rept 14
nop
.endr
movq $7,%rax
retq
