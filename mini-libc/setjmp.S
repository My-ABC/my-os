/* SPDX-License-Identifier: MIT */
/* mini-libc setjmp/longjmp for freestanding i386.
 *
 * jmp_buf layout (6 ints, see include/setjmp.h):
 *   [0] ebx  [1] esi  [2] edi  [3] ebp  [4] esp  [5] eip
 *
 * setjmp saves the callee-saved registers plus the stack pointer and
 * return address, and returns 0. longjmp restores them and resumes at
 * the instruction after the original `call setjmp`, making setjmp
 * "return" a second time with the given value (0 is coerced to 1,
 * per C11). Caller-saved registers (eax/ecx/edx) are not preserved —
 * the caller must treat them as clobbered, same as setjmp semantics.
 *
 * Stack trick: at setjmp entry [esp]=retaddr, [esp+4]=env. We save
 * esp_entry+4 (pointing at env, i.e. the caller's post-call esp) and
 * eip=[esp_entry]. longjmp restores esp to that value and jumps
 * directly to the saved eip — a `ret` would wrongly pop the env arg.
 */

    .intel_syntax noprefix
    .text
    .globl setjmp
    .globl longjmp

/* int setjmp(jmp_buf env)
 * entry: [esp]=ret [esp+4]=env */
setjmp:
    mov eax, [esp + 4]        /* env */
    mov [eax + 0],  ebx
    mov [eax + 4],  esi
    mov [eax + 8],  edi
    mov [eax + 12], ebp
    lea edx, [esp + 4]        /* caller's post-call esp */
    mov [eax + 16], edx
    mov edx, [esp]            /* return address */
    mov [eax + 20], edx
    xor eax, eax              /* first return: 0 */
    ret

/* void longjmp(jmp_buf env, int val)
 * entry: [esp]=ret [esp+4]=env [esp+8]=val — never returns */
longjmp:
    mov ecx, [esp + 4]        /* env */
    mov eax, [esp + 8]        /* val */
    test eax, eax
    jnz 1f
    mov eax, 1                /* longjmp(env, 0) returns 1 */
1:
    mov ebx, [ecx + 0]
    mov esi, [ecx + 4]
    mov edi, [ecx + 8]
    mov ebp, [ecx + 12]
    mov esp, [ecx + 16]
    jmp dword ptr [ecx + 20]  /* resume after `call setjmp` */
