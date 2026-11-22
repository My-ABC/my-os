/* SPDX-License-Identifier: MIT */
/* mini-libc raw syscall wrappers for the MyOS class/sub-op ABI.
 *
 * ABI: eax = class (SYS_*), ebx = sub-operation, ecx/edx/esi = args,
 * int 0x80, return value in eax. All wrappers are cdecl: the caller
 * cleans the stack.
 */

    .intel_syntax noprefix
    .text
    .globl syscall0
    .globl syscall1
    .globl syscall2
    .globl syscall3

/* int syscall0(uint32_t klass, uint32_t op)
 * stack: [esp]=ret [esp+4]=klass [esp+8]=op */
syscall0:
    mov eax, [esp + 4]
    mov ebx, [esp + 8]
    int 0x80
    ret

/* int syscall1(uint32_t klass, uint32_t op, uint32_t a1)
 * stack: [esp]=ret [esp+4]=klass [esp+8]=op [esp+12]=a1 */
syscall1:
    mov ecx, [esp + 12]
    mov eax, [esp + 4]
    mov ebx, [esp + 8]
    int 0x80
    ret

/* int syscall2(uint32_t klass, uint32_t op, uint32_t a1, uint32_t a2)
 * stack: [esp]=ret [esp+4]=klass [esp+8]=op [esp+12]=a1 [esp+16]=a2 */
syscall2:
    mov ecx, [esp + 12]
    mov edx, [esp + 16]
    mov eax, [esp + 4]
    mov ebx, [esp + 8]
    int 0x80
    ret

/* int syscall3(uint32_t klass, uint32_t op, uint32_t a1, uint32_t a2, uint32_t a3)
 * stack: [esp]=ret [esp+4]=klass [esp+8]=op [esp+12]=a1 [esp+16]=a2 [esp+20]=a3 */
syscall3:
    mov ecx, [esp + 12]
    mov edx, [esp + 16]
    mov esi, [esp + 20]
    mov eax, [esp + 4]
    mov ebx, [esp + 8]
    int 0x80
    ret
