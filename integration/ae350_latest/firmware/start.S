/* Freestanding RV32 entry for the official Emb_TCM memory layout.
 * The reset instruction is fetched at 0x80000000. No cache/ILM enable.
 */
.section .text.start,"ax",@progbits
.globl _start
_start:
    /* First observable action: no stack, CSR or peripheral setup required. */
    li t0,0xF800001c
    li t1,0xAE350001
    sw t1,0(t0)
    fence iorw,iorw
    csrw mie, zero
    csrci mstatus, 8
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop
    la sp, __stack_top
    la t0, trap_entry
    csrw mtvec, t0
    li t0,0xF800001c
    li t1,0xAE350002
    sw t1,0(t0)
    fence iorw,iorw
    la t0, __data_load
    la t1, __data_start
    la t2, __data_end
1:  bgeu t1,t2,2f
    lw t3,0(t0)
    sw t3,0(t1)
    addi t0,t0,4
    addi t1,t1,4
    j 1b
2:  la t1,__bss_start
    la t2,__bss_end
3:  bgeu t1,t2,4f
    sw zero,0(t1)
    addi t1,t1,4
    j 3b
4:  fence iorw,iorw
    li t0,0xF800001c
    li t1,0xAE350003
    sw t1,0(t0)
    fence iorw,iorw
    call main
5:  j 5b
.balign 4
trap_entry:
    /* Framed diagnostic record; do not depend on the faulting stack. */
    li t0,0xF800001c
    li t1,0xDEAD3500
    sw t1,0(t0)
    fence iorw,iorw
    csrr t1,mcause
    csrr t2,mepc
    csrr t3,mtval
    sw t1,0(t0)
    fence iorw,iorw
    sw t2,0(t0)
    fence iorw,iorw
    sw t3,0(t0)
    fence iorw,iorw
6:  j 6b
