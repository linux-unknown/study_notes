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

