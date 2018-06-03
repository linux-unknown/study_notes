# arm64 VMSA

## Virtual address (VA)

​	An address used in an instruction, as a data or instruction address, is a Virtual Address (VA).
- In AArch64 state, the VA has a maximum address width of either **48 bits** or, when ARMv8.2-LVA is implemented and the 64KB translation granule is used, **52 bits**.


### Translation stage with a single VA range
For a translation stage that supports a single VA range, a 48-bit VA 	width gives a VA range of 0x0000000000000000 to 0x0000FFFFFFFFFFFF.

If ARMv8.2-LVA is implemented and the 64KB translation granule is used, for a translation regime that supports a single VA range, the 52-bit VA width gives a VA range of 0x0000000000000000 to 0x000FFFFFFFFFFFFF

### Translation stage with two VA ranges

For a translation stage that supports two VA subranges, **one at the bottom of the full 64-bit address range**, and **one at the top**, as follows:

- The bottom VA range runs up from address 0x0000000000000000.

  With a maximum VA width of 48 bits this gives a VA range of **0x0000000000000000 to 0x0000FFFFFFFFFFFF**.With a maximum VA width of 52 bits this gives a VA range of 	**0x0000000000000000 to 0x000FFFFFFFFFFFFF**.

- The top VA subrange runs up to address 0xFFFFFFFFFFFFFFFF.
  With a maximum VA width of 48 bits this gives a VA range of **0xFFFF000000000000 to 0xFFFFFFFFFFFFFFFF** .With a maximum VA width of 52 bits this gives a VA range of **0xFFF0000000000000 to 0xFFFFFFFFFFFFFFFF**. Reducing the VA width for this subrange increases the bottom address of the range.

A 48-bit VA range corresponds to an address space of 256TB. A 52-bit VA range corresponds to an address space of 4PB.

## Physical address (PA)

​	The address of a location in a physical memory map. That is, an output address from the PE to the
memory system.

***

页表的最后一级定义了：

- 物理地址的高位

- 要访问得在的属性和访问权限

***

### The VMSAv8-64 translation table format

AArch64 use the VMSAv8-64 translation table format。This format uses **64-bit descriptor entries** in the translation tables.

The VMSAv8-64 translation table format provides:

- Up to **four levels** of address lookup.
- A translation granule size of *4KB, 16KB, or 64KB*.
- Input addresses of:
  ​	— Up to 52 bits if ARMv8.2-LVA is implemented and the 64KB translation granule is used.
  ​	— Otherwise, up to **48 bits**.
- Output addresses of:
  ​	— Up to 52 bits if ARMv8.2-LPA is implemented and the 64KB translation granule is used.
  ​	— Otherwise, up to **48 bits**
***
### Controlling address translation stages

If a stage of address translation supports two VA ranges then that stage of translation **provides a TTBR_ELx for each VA range**, and the stage of address translation has:

 - A single TCR_ELx.
 - A TTBR_ELx for each VA range. **TTBR0_ELx points to the translation tables for the address range that starts at 0x0000000000000000**, and **TTBR1_ELx points to the translation tables for the address range that ends at 0xFFFFFFFFFFFFFFFF**.

*Otherwise, a single TTBR_ELx holds the address of the translation table that must be used for the first lookup for the stage of address translation*

####SCTLR_ELx:System Control Register

**SCTLR_ELx.EE**: Endianness of data accesses at ELx(x > 0)

- 0              little-endian
- 1              big-endian


**SCTLR_ELx.M**:

- 0              stage 1 address translation disabled

- 1              stage 1 address translation enable


| Translation stage                        | Controlled from | Controlling register     |
| :--------------------------------------- | :-------------- | :----------------------- |
| Secure EL3 stage 1                       | EL3             | SCTLR_EL3.{EE, M}        |
| Secure EL1&0 stage 1                     | Secure EL1      | SCTLR_EL1.{EE, M}        |
| Non-secure EL2 stage 1 or Non-secure EL2&^a^ stage 1 | EL2             | SCTLR_EL2.{EE, M}        |
| Non-secure EL1&0 stage 2                 | EL2             | SCTLR_EL2.EE, HCR_EL2.VM |
| Non-secure EL1&0 stage 1                 | Non-secure EL1  | SCTLR_EL1.{EE, M}        |



```c
/*
 * Drivers should NOT use these either.
 */
#define __pa(x)			__virt_to_phys((unsigned long)(x))
#define __va(x)			((void *)__phys_to_virt((phys_addr_t)(x)))
#define pfn_to_kaddr(pfn)	__va((pfn) << PAGE_SHIFT)
#define virt_to_pfn(x)      __phys_to_pfn(__virt_to_phys(x))

/*
 *  virt_to_page(k)	convert a _valid_ virtual address to struct page *
 *  virt_addr_valid(k)	indicates whether a virtual address is valid
 */
#define ARCH_PFN_OFFSET		((unsigned long)PHYS_PFN_OFFSET)
```

