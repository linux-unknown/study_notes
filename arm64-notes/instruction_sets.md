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



### ERET

