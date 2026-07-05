	.globl .main
	.globl hdump
  .globl .write
  .globl .calloc
  .globl .odm_initialize
  .globl .odm_mount_class
	.globl .odm_get_first
	.globl .odm_get_next
  .globl .odm_terminate
  .globl .exit
  .globl writebuf
  .globl usage
  .globl main_start
  .globl _adata

  .text
.main:
  stwu 1,-32(1)
  mflr 0
  stw 0,4(1)

  bl main_start
  nop
  .long writebuf

main_start:
  stw 20,8(1)
  stw 21,12(1)
  stw 22,16(1)
  stw 23,20(1)
  stw 24,24(1)
  mflr 25
  lwz 25,4(25)
  stw 25,28(1)

  /* Check arg count = 3 */
  cmplwi 3, 3
  bne usage

  /* Get args from 0(r4) and 4(r4) */
  lwz 20, 0(4)
  lwz 21, 4(4)

  /* Initialize odm */
  bl .odm_initialize
  nop

  /* Mount requested class */
  mr 3, 21
  bl .odm_mount_class
  nop

  /* Save class symbol */
  mr 22, 3

  /* Allocate space for object */
  lwz 3, 8(22)
  li 4, 1
  bl .calloc
  nop

  mr 24, 3

  /* do odm_get_first */
  mr 3, 22
  mr 4, 20
  mr 5, 24
  bl .odm_get_first
  nop

  /* while record */
while:
  cmplwi 3, 0
  beq end_while

  /* write */
  mr 3, 24
  lwz 4, 8(22)
  lwz 5, 28(1)
  bl hdump
  nop

  /* odm_get_next */
  mr 3, 22
  mr 4, 24
  bl .odm_get_next
  nop
  b while
  /* end while */

end_while:
  /* odm_terminate */
  bl .odm_terminate
  nop
  /* exit(0) */
  xor 3,3,3
  bl .exit
  nop
  .long 0
  .long 4
  .ascii "main"

  .align 4
  /* hdump(addr, len, writebuf) */
hdump:
  stwu 1,-32(1)
  mflr 0
  stw 0,4(1)
  stw 3,8(1)
  stw 4,12(1)
  stw 5,28(1)
  stw 20,16(1)
  stw 21,20(1)
  stw 22,24(1)

  /* address */
  mr 20,3
  /* len */
  mr 21,4
  /* offset */
  xor 22,22,22

row:
  /* Buffer address */
  lwz 4,28(1)

  cmplwi 22,0
  beq row_heading
  li 3,10
  stb 3,0(4)
  li 3,1
  li 5,1
  bl .write
  nop

row_heading:
  /* Buffer address */
  lwz 4,28(1)

  /* print offset */
  srwi 3,22,8
  bl tohex
  addi 4,4,2
  mr 3,22
  bl tohex
  addi 4,4,-2
  li 3,58
  stb 3,4(4)
  li 3,32
  stb 3,5(4)
  li 5,6
  li 3,1
  bl .write
  nop

  /* if offset > len, goto term */
column:
  cmpw 22,21
  bge term

  /* Buffer address */
  lwz 4,28(1)

  /* print 8 bits _xy */
  li 5,32
  stw 5,0(4)

  addi 4,4,1
  lbz 3,0(20)
  addi 20,20,1
  bl tohex
  nop
  addi 4,4,-1

  li 3,1
  li 5,3
  bl .write
  nop

  /* increase offset */
  addi 22,22,1

  /* 16 boundary?, goto row */
  li 4,15
  and. 5,4,22
  beq row
  .long 0
  .long 5
  .ascii "hdump"

  .align 4
term:
  /* Buffer address */
  lwz 4,28(1)

  li 3,10
  stb 3,0(4)
  li 3,1
  li 5,1
  bl .write
  nop

  lwz 22,24(1)
  lwz 21,20(1)
  lwz 20,16(1)
  lwz 5,4(1)
  mtlr 5
  lwz 1,0(1)
  blr
  .long 0
  .long 4
  .ascii "term"

  .align 4
  /* Convert the value in r3 to two hex digits and save in [01](r4) */
tohex:
  mr 5,3
  bl tohex1
  nop
  stb 3,0(4)
  srwi 3,5,4
  bl tohex1
  nop
  stb 3,1(4)
  blr
  .long 0
  .long 5
  .ascii "tohex"

  .align 4
  /* Convert the value in r3 to one hex digit */
tohex1:
  li 4,15
  and 3,3,4
  cmplwi 3,9
  bgt tohex1letter
  addi 3,3,48
  blr
  .long 0
  .long 6
  .ascii "tohex1"

  .align 4
tohex1letter:
  addi 3,3,87
  blr
  .long 0
  .long 13
  .ascii "tohex1letter"

usage:
  li 3,1
  bl .exit
  nop
  blr

  .data
writebuf:
  .long 0
  .long 0
  .long 0
  .long 0

  .toc
  /* return code */
  .long 0
