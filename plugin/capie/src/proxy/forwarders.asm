; Win64 tail-call forwarding. Preserve the undocumented ByHandle ABI too.
; Stack arguments remain at their original addresses, and x64 unwind metadata
; describes the temporary frame if the resolver throws a structured exception.
option casemap:none
EXTERN CapieResolveExport:PROC
.code

VERSION_STUB MACRO number
PUBLIC CapieVersion&number
CapieVersion&number PROC FRAME
    sub rsp, 0A8h
    .allocstack 0A8h
    .endprolog
    mov [rsp+20h], rcx
    mov [rsp+28h], rdx
    mov [rsp+30h], r8
    mov [rsp+38h], r9
    movdqu [rsp+40h], xmm0
    movdqu [rsp+50h], xmm1
    movdqu [rsp+60h], xmm2
    movdqu [rsp+70h], xmm3
    mov [rsp+88h], rax
    mov ecx, number
    call CapieResolveExport
    mov r11, rax
    mov rcx, [rsp+20h]
    mov rdx, [rsp+28h]
    mov r8, [rsp+30h]
    mov r9, [rsp+38h]
    movdqu xmm0, [rsp+40h]
    movdqu xmm1, [rsp+50h]
    movdqu xmm2, [rsp+60h]
    movdqu xmm3, [rsp+70h]
    mov rax, [rsp+88h]
    add rsp, 0A8h
    jmp r11
CapieVersion&number ENDP
ENDM

VERSION_STUB 0
VERSION_STUB 1
VERSION_STUB 2
VERSION_STUB 3
VERSION_STUB 4
VERSION_STUB 5
VERSION_STUB 6
VERSION_STUB 7
VERSION_STUB 8
VERSION_STUB 9
VERSION_STUB 10
VERSION_STUB 11
VERSION_STUB 12
VERSION_STUB 13
VERSION_STUB 14
VERSION_STUB 15
VERSION_STUB 16
END
