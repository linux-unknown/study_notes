# aarch64 常用指令

###  STP

>Store Pair of SIMD&FP registers. This instruction stores a pair of SIMD&FP registers to memory. The address used for the store is calculated from a base register value and an immediate offset

**post-index mode**: STP \<Dt1\>,\<Dt2\>, [\<Xn|SP\>], #\<imm\>

[SP | Xn] = Dt1

[(SP | Xn) + 8] = Dt2

Xn | SP =  Xn |SP + imm



**pre-index mode**: STP \<Xt1>,\<Xt2>, [\<Xn|SP\>, #\<imm>]!

Xn | SP = (Xn + imm) | (SP + imm)

[SP | Xn] = Dt1

[(SP | Xn) + 8] = Dt2



**base mode:**STP \<Xt1> \<Xt2> [<Xn|SP, {, #imm}]

[(SP | Xn) + imm] = Dt1

[(SP | Xn) + imm + 8] = Dt2

*[Xn] 表示Xn寄存器的值作为地址处的内存。*



### RET

> Return from subroutine branches unconditionally to an address in a register, with a hint that this is a subroutine  return

 RET {\<Xn\>}

\<Xn\>	Is the 64-bit name of the general-purpose register holding the address to be branched to, 
		encoded in  the "Rn" field. **Defaults to X30 if absent.**

### ERET

> Exception Return using the ELR and SPSR for the current Exception level. When executed, the PE restores PSTATE from the SPSR, and branches to the address held in the ELR.



### ADRP

> Form PC-relative address to 4KB page adds an immediate value that is shifted left by 12 bits, to the PC value to form a PC-relative address, with the bottom 12 bits masked out, and writes the result to the destination register.

ADRP \<Xd\>, \<label\>

Assembler symbols
**\<Xd\>**	Is the 64-bit name of the general-purpose destination register, encoded in the "Rd" field.
**\<label\>**	Is the program label whose 4KB page address is to be calculated. Its offset from the page address of  this instruction, in the range +/-4GB, is encoded as "immhi:immlo" times 4096.

**Operation**
 bits(64) base = PC[]; 

 base\<11:0\> = Zeros(12); 

X[d] = base + imm; 

ADRP 对labl的地址采用PC + offset的方式，所以可以实现位置无关代码

ADRP是页对其的，所以齐只能获得lable的[63:13]地址。要获得完整的地址，一般如下：
```assembly
adrp	x1, handle_arch_irq
ldr x1, [x1, #:lo12:handle_arch_irq]
```

###  CBNZ

> Compare and Branch on Nonzero compares the value in a register with zero, and conditionally branches to a label  at a PC-relative offset if the comparison is not equal. It provides a hint that this is not a subroutine call or return.  This instruction does not affect the condition flags.

CBNZ \<Xt\>, \<label\>

如果xt寄存器不为0，则跳转到lable



### TBZ

> Test bit and Branch if Zero compares the value of a test bit with zero, and conditionally branches to a label at a  PC-relative offset if the comparison is equal. It provides a hint that this is not a subroutine call or return. This  instruction does not affect condition flags.

TBZ \<R\>\<t\>, #\<imm\>, \<label\>

如果寄存器t和imm相与为0，则跳转到label