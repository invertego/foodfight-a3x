	.text
	.long   0x41535321
	jmp	initialize
	.asciz	"Food Fight"
	.align	32
	.long	0	//let ASSFIX set this.
	.ascii	"FUDw"
	.byte	0	//reserved
	#include "../crt0.s"
	TARGET_A3X = 1
	#include "rom.s"

	.section .rodata
	.align  4
	.global PlayfieldTiles
	.global ObjectTiles
PlayfieldTiles:
	.incbin "playfield.bin"
ObjectTiles:
	.incbin "object.bin"
