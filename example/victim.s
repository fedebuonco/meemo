	.file	"victim.c"
	.text
	.globl	hp
	.data
	.align 4
	.type	hp, @object
	.size	hp, 4
hp:
	.long	80085
	.globl	money
	.align 8
	.type	money, @object
	.size	money, 8
money:
	.quad	1337
	.section	.rodata
.LC0:
	.string	"PID: %d\n"
.LC1:
	.string	"Find the hp: %d at %p\n"
.LC2:
	.string	"Find the money: %ld at %p\n"
.LC3:
	.string	"Printing every 30 seconds:"
	.text
	.globl	main
	.type	main, @function
main:
.LFB6:
	.cfi_startproc
	endbr64
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	subq	$16, %rsp
	call	getpid@PLT
	movl	%eax, %esi
	leaq	.LC0(%rip), %rax
	movq	%rax, %rdi
	movl	$0, %eax
	call	printf@PLT
	leaq	hp(%rip), %rax
	movq	%rax, -16(%rbp)
	leaq	money(%rip), %rax
	movq	%rax, -8(%rbp)
	movl	hp(%rip), %eax
	movq	-16(%rbp), %rdx
	movl	%eax, %esi
	leaq	.LC1(%rip), %rax
	movq	%rax, %rdi
	movl	$0, %eax
	call	printf@PLT
	movq	money(%rip), %rax
	movq	-8(%rbp), %rdx
	movq	%rax, %rsi
	leaq	.LC2(%rip), %rax
	movq	%rax, %rdi
	movl	$0, %eax
	call	printf@PLT
.L2:
	movl	$30, %edi
	call	sleep@PLT
	leaq	.LC3(%rip), %rax
	movq	%rax, %rdi
	call	puts@PLT
	movl	hp(%rip), %eax
	movq	-16(%rbp), %rdx
	movl	%eax, %esi
	leaq	.LC1(%rip), %rax
	movq	%rax, %rdi
	movl	$0, %eax
	call	printf@PLT
	movq	money(%rip), %rax
	movq	-8(%rbp), %rdx
	movq	%rax, %rsi
	leaq	.LC2(%rip), %rax
	movq	%rax, %rdi
	movl	$0, %eax
	call	printf@PLT
	nop
	jmp	.L2
	.cfi_endproc
.LFE6:
	.size	main, .-main
	.ident	"GCC: (Ubuntu 13.3.0-6ubuntu2~24.04) 13.3.0"
	.section	.note.GNU-stack,"",@progbits
	.section	.note.gnu.property,"a"
	.align 8
	.long	1f - 0f
	.long	4f - 1f
	.long	5
0:
	.string	"GNU"
1:
	.align 8
	.long	0xc0000002
	.long	3f - 2f
2:
	.long	0x3
3:
	.align 8
4:
