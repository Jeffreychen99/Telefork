.intel_syntax noprefix
.text


.global _getrax
_getrax:
    ret


.global _getrdx
_getrdx:
    mov rax,rdx
    ret

.global _getrcx
_getrcx:
    mov rax,rcx
    ret

.global _getrbx
_getrbx:
    mov rax,rbx
    ret