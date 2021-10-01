

x86  在pci枚举的时候通过端口进行， 

```c
#define PORT_PCI_CMD           0x0cf8
#define PORT_PCI_DATA          0x0cfc

pci_config_writew(bdf, PCI_MEMORY_BASE, addr >> PCI_MEMORY_SHIFT);

void pci_config_writew(u16 bdf, u32 addr, u16 val)
{
    /* PORT_PCI_CMD 选择pci设备，通过bdf选择 
     * bit[31]:是否是能pci配置功能，1表示使能
     * bit[30:24]:保留位
     * bit[23:16]:pci总线号
     * bit[15:11]:选定总线的设备号
     * bit[10:8]:功能号
     * bit[7:2]:表示所选总线，设备号，功能号对应的pci设备的寄存器地
     * bit[1:0]:保留
     */
    outl(0x8000 0000 | (bdf << 8) | (addr & 0xfc), PORT_PCI_CMD);
    
    outw(val, PORT_PCI_DATA + (addr & 2));
}
```

写端口之后会调用对应端口的operation，更新pci的bar

