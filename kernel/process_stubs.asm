; -----------------------------------------------------------------------------
; ClaudeOS process stubs
; -----------------------------------------------------------------------------
; void process_switch(uint32_t *old_esp, uint32_t new_esp)
; - Saves callee-saved context on current stack
; - Stores current ESP through old_esp
; - Loads new_esp, restores context from new stack, returns into it
;
; Stack layout expected on a saved process stack (lowest address at top):
;   [esp + 0]  edi
;   [esp + 4]  esi
;   [esp + 8]  ebx
;   [esp +12]  ebp
;   [esp +16]  return EIP
; -----------------------------------------------------------------------------

bits 32
section .text

global process_switch

process_switch:
    mov eax, [esp + 4]      ; old_esp*
    mov edx, [esp + 8]      ; new_esp

    push ebp
    push ebx
    push esi
    push edi

    mov [eax], esp
    mov esp, edx

    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
