# ATF firmware-design基本翻译

Trusted Firmware-A (TF-A)  实现了ARM参考平台Trusted Board Boot Requirements (TBBR) Platform Design Document 的一个子集。当平台上电时，TBB序列开始，并一直运行到将控制权移交给在DRAM中运行在normal world的固件的阶段。 这是冷启动路径。

TF-A也实现了 Power State Coordination Interface既PSCI，作为一个运行时的服务。PSCI是一个从normal world软件到实现电源管理用例的一个结构（比如econdary CPU boot,hotplug and idle).Normal world软件通过SMC(Secure Monitor Call)指令可以访问TF-A运行时服务。

TF-A实现了一个框架来配置和映射在任一安全状态下产生的中断。详细的中断管理框架和实现见TF-A Interrupt Management Design guide。

TF-A也实现了一个库用来设置和映射转换表。详见Xlat_tables design。

TF-A支持AArch64和Aarch32 执行状态。

## code boot

当platform物理上电，code boot path开始。如果COLD_BOOT_SINGLE_CPU=0，其中一个CPU从reset状态释放，被选择为primary CPU，其余的CPU被当作secondary CPU。primary CPU通过平台特有方法选择。cold boot path主要被primary CPU执行。而不是所有CPU执行CPU的基本初始化。secondary cpu保持在一个平台特定的安全状态，知道primary cpu充分的执行初始化，然后boot他们。

TF-A实现的cold boot path依赖于执行状态。AArch64 分为5个步骤（按照执行顺序）

Boot loader stage 1（BL1）：AP Trusted ROM

Boot loader stage 2（BL2）：Trusted Boot Firmware

Boot loader stage 3-1（BL31）：EL3 Runtime Software

Boot loader stage 3-2（BL32）：Secure-EL1 payload（可选的）

Boot loader stage 3-3（BL33）：Non-trusted Firmware

Aarch32略去



Arm development platforms (Fixed Virtual Platforms (FVPs) and Juno) 实现了一个下面内存区域的组合，每一个bootloader阶段使用一个或多个这些内存区域

- 可以从non-secure和secure状态访问的区域，比如non-trusted SRAM，ROM，DRAM

- 只能从secure状态访问的区域，比如trusted SRAM和ROM，FVP也实现了trusted DRAM，是通过静态配置的。另外，FVP和Juno development platform 通过配置TrustZone Controller (TZC) 来创建DRAM的一个区域只能被secure状态访问。

## BL1

该阶段从平台的EL3的reset vector开始执行，reset address是platform相关的，但是通常是在Trusted ROM区域。BL1 打他段在运行时被复制到trusted SRAM

在 Arm development platforms,BL1代码从reset vector开始执行，reset vector通过常量BL1_RO_BASE定义。BL1 data段被复制到trusted SRAM的顶部，被常量BL1_RW_BASE定义的地方

### 确定boot path

无论什么时候CPU从reset release，BL1需要区分是warm boot还是cold boot。这通过平台特定的机制，见plat_get_my_entrypoint()，在warm boot的情形下，cpu期望从单独的entrypoint继续执行。在cold boot情形下，secondary CPU被放在一个平台特定的安全状态（见plat_secondary_cold_boot_setup()），同时primary CPU执行剩下的cold boot path，描述如下：

### 架构初始化

BL1执行最小化的架构初始化，如下

#### Execption vectors

BL1设置简单的exception vectors，为synchronous and asynchronous exceptions。当收到一个exception默认的行为是在通用寄存器X0/R0填充一个状态码，然后调用plat_report_exception()，状态码是下面的之一：

对于Aarch64：

       0x0 : Synchronous exception from Current EL with SP_EL0
       0x1 : IRQ exception from Current EL with SP_EL0
       0x2 : FIQ exception from Current EL with SP_EL0
       0x3 : System Error exception from Current EL with SP_EL0
       0x4 : Synchronous exception from Current EL with SP_ELx
       0x5 : IRQ exception from Current EL with SP_ELx
       0x6 : FIQ exception from Current EL with SP_ELx
       0x7 : System Error exception from Current EL with SP_ELx
       0x8 : Synchronous exception from Lower EL using aarch64
       0x9 : IRQ exception from Lower EL using aarch64
       0xa : FIQ exception from Lower EL using aarch64
       0xb : System Error exception from Lower EL using aarch64
       0xc : Synchronous exception from Lower EL using aarch32
       0xd : IRQ exception from Lower EL using aarch32
       0xe : FIQ exception from Lower EL using aarch32
       0xf : System Error exception from Lower EL using aarch32




