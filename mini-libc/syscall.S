/* SPDX-License-Identifier: MIT */
/* mini-libc raw syscall wrappers for the MyOS class/sub-op ABI.
 *
 * ABI: eax = class (SYS_*), ebx = sub-operation, ecx/edx/esi = args,
 * int 0x80, return value in eax. All wrappers are cdecl: the caller
 * cleans the stack.
 *
 * The kernel handler may clobber every register, so the wrappers save
 * and restore the callee-saved set (ebx/esi/edi/ebp) around int 0x80 —
 * otherwise GCC -O2 code that caches pointers in those registers across
 * a syscall breaks (observed as a user page fault in shell_ring3).
 */

    .intel_syntax noprefix
    .text
    .globl syscall0
    .globl syscall1
    .globl syscall2
    .globl syscall3

/* int syscall0(uint32_t klass, uint32_t op)
 * entry stack: [esp]=ret [esp+4]=klass [esp+8]=op
 * after 4 pushes (16 bytes): klass=[esp+20] op=[esp+24] */
syscall0:
    push ebp
    push edi
    push esi
    push ebx
    mov eax, [esp + 20]
    mov ebx, [esp + 24]
    int 0x80
    pop ebx
    pop esi
    pop edi
    pop ebp
    ret

/* int syscall1(uint32_t klass, uint32_t op, uint32_t a1)
 * entry: a1=[esp+12]; after pushes a1=[esp+28] */
syscall1:
    push ebp
    push edi
    push esi
    push ebx
    mov ecx, [esp + 28]
    mov eax, [esp + 20]
    mov ebx, [esp + 24]
    int 0x80
    pop ebx
    pop esi
    pop edi
    pop ebp
    ret

/* int syscall2(uint32_t klass, uint32_t op, uint32_t a1, uint32_t a2)
 * entry: a1=[esp+12] a2=[esp+16]; after pushes a1=[esp+28] a2=[esp+32] */
syscall2:
    push ebp
    push edi
    push esi
    push ebx
    mov ecx, [esp + 28]
    mov edx, [esp + 32]
    mov eax, [esp + 20]
    mov ebx, [esp + 24]
    int 0x80
    pop ebx
    pop esi
    pop edi
    pop ebp
    ret

/* int syscall3(uint32_t klass, uint32_t op, uint32_t a1, uint32_t a2, uint32_t a3)
 * entry: a1=[esp+12] a2=[esp+16] a3=[esp+20]; after pushes +16 each */
syscall3:
    push ebp
    push edi
    push esi
    push ebx
    mov ecx, [esp + 28]
    mov edx, [esp + 32]
    mov esi, [esp + 36]
    mov eax, [esp + 20]
    mov ebx, [esp + 24]
    int 0x80
    pop ebx
    pop esi
    pop edi
    pop ebp
    ret
