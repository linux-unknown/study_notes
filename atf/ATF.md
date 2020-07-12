# ATF firmware-design基本翻译

[TOC]

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

该阶段从平台的EL3的reset vector开始执行，reset address是platform相关的，但是通常是在Trusted ROM区域。BL1 data段在运行时被复制到trusted SRAM

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

BL1不期望接受到exception，除了SMC exception。后面BL1会安装一个简单的stub。该期望接受有限制的SMC类型（通过通用功能寄存器X0/R0中的function ID来确定）。

   - ``BL1_SMC_RUN_IMAGE``: 该SMC通过BL2产生，用来使BL1传递控制给EL3运行时软件（This SMC is raised by BL2 to make BL1 pass control to EL3 Runtime Software.）。

   - 所有在Firmware Update Design Guide的BL1 SMC Interface章节列出的SMC只支持AArch64。

其他的SMC将导致错误发生。

### CPU初始化

BL1调用reset_handler函数，按顺序调用CPU特定的reset hander函数（见PU specific operations
   framework）

### Control register setup (for AArch64)

   -  ``SCTLR_EL3``. Instruction cache is enabled by setting the ``SCTLR_EL3.I``bit. Alignment and stack alignment checking is enabled by setting the``SCTLR_EL3.A`` and ``SCTLR_EL3.SA`` bits. Exception endianness is set to little-endian by clearing the ``SCTLR_EL3.EE`` bit.
   -  ``SCR_EL3``. The register width of the next lower exception level is set to AArch64 by setting the ``SCR.RW`` bit. The ``SCR.EA`` bit is set to trap both External Aborts and SError Interrupts in EL3. The ``SCR.SIF`` bit is also set to disable instruction fetches from Non-secure memory when in secure state.
   -  ``CPTR_EL3``. Accesses to the ``CPACR_EL1`` register from EL1 or EL2, or the ``CPTR_EL2`` register from EL2 are configured to not trap to EL3 by clearing the ``CPTR_EL3.TCPAC`` bit. Access to the trace functionality is configured not to trap to EL3 by clearing the ``CPTR_EL3.TTA`` bit. Instructions that access the registers associated with Floating Point and Advanced SIMD execution are configured to not trap to EL3 by clearing the ``CPTR_EL3.TFP`` bit.
   -  ``DAIF``. The SError interrupt is enabled by clearing the SError interrupt mask bit.
   -  ``MDCR_EL3``. The trap controls, ``MDCR_EL3.TDOSA``, ``MDCR_EL3.TDA`` and ``MDCR_EL3.TPM``, are set so that accesses to the registers they control do not trap to EL3. AArch64 Secure self-hosted debug is disabled by setting the ``MDCR_EL3.SDD`` bit. Also ``MDCR_EL3.SPD32`` is set to disable AArch32 Secure self-hosted privileged debug from S-EL1.

### 平台初始化

在ARM platform，BL1执行下面的平台初始化：

-  Enable the Trusted Watchdog.
-  Initialize the console.
-  Configure the Interconnect to enable hardware coherency.
-  Enable the MMU and map the memory it needs to access.
-  Configure any required platform storage to load the next bootloader image (BL2).
-  If the BL1 dynamic configuration file, ``TB_FW_CONFIG``, is available, then load it to the platform defined address and make it available to BL2 via``arg0``.

### 固件升级检测和执行

在执行完platform setup之后，BL1调用bl1_plat_get_next_image_id来决定Firmware Update是否需要或者正常启动。如果platform code返回BL2_IMAGE_ID则执行下面章节描述的正常启动，否则BL1假设需要Firmware Update并且执行传递给Firmware Update中的第一个image。无论哪种情况，BL1都会通过调用bl1_plat_get_image_desc()来检索下一个image的描述符。 image包含一个entry_point_info_t结构，BL1使用该结构初始化下一个图像的执行状态。

### BL2 image的加载和执行

在正常的启动流程中，BL1执行下如下：

1. BL1从primary CPU打印下面的字符串提示BL1阶段执行成功


​		"Booting Trusted Firmware"

2. BL1 从platform storage load BL2 raw binary镜像，从一个平台指定的基地址。在load之前，BL1调用bl1_plat_handle_pre_image_load，这允许平台更新或者使用image信息。如果BL2 image不存在或者没有足够的trusted SRAM,则会打印下面的错误信息：

​		"Failed to load BL2 firmware."

3. BL1调用bl1_plat_handle_pre_image_load，该函数在image load之后再次用于平台的进一步措施。该函数必须为BL2填充必要的参数，也许包含memory layout，memory layout的描述在后面的章节。

4. BL1 转移控制给BL2在Secure EL1（AArch64）



## BL2

BL1加载并且传递控制给EL2在Secure EL1（AArch64）。BL2被链接和加载到一个平台特定的基地址（更多的信息见后面的文档）。BL2实现的功能如下：

### 架构初始化

对于AArch64，BL2执行TF-A和常规软件后续阶段所需的最小体系结构初始化。EL1和EL0通过清除CPACR.FPEN位可以访问Floating Point and Advanced SIMD寄存器。

### 平台初始化

在ARM平台，BL2执行下面的平台初始化：

-  Initialize the console.
-  Configure any required platform storage to allow loading further bootloader images.
-  Enable the MMU and map the memory it needs to access.
-  Perform platform security setup to allow access to controlled components.
-  Reserve some memory for passing information to the next bootloader image EL3 Runtime Software and populate it.
-  Define the extents of memory available for loading each subsequent bootloader image.
-  If BL1 has passed TB_FW_CONFIG dynamic configuration file in arg0, then parse it.

### Image loading in BL2

BL2的image加载机制依赖于LOAD_IMAGE_V2编译选项。如果该flag被禁止，从EL2 generic code调用各自的load_blxx函数加载BLxx image。如果被使能，BL2 generic code基于platform提供的   可加载image列表加载image。BL2将平台提供的可执行image列表传递给下一个移交BL映像。默认该flag是禁止的对于AArch64。

平台提供的可加载image列表可能包含动态配置文件。该文件被加载和解析在bl2_plat_handle_post_image_load函数中。这些配置文件可以被作为参数传递到下一级的boot loader阶段，通过在该函数中更新相应的entrypoint信息。

SCP_BL2（系统控制处理器固件）image加载

一些系统有一个分开的System Control Processor(SCP)用于power，clock，reset和system control。BL2加载可选的SCP_EL2 image从platform storage到一个平台特定的secure memory 区域。SCP_BL2后续的处理是平台特定的。

### EL3运行时软件image加载

BL2加载EL3运行时image从platform storage到平台特定的地址在trusted SRAM中。如果没有足够的内存来加载image或者确实image，将导致一个错误发生。如果LOAD_IMAGE_V2被禁止，并且如果image加载成，BL2更新trusted SRAM使用的数量和被EL3运行时软件可用的数量。该信息被保存在平台特定的地址处。

### AArch64 BL32（Secure-EL1 Payload）image加载

BL2加载可选的BL32 image从platform storage到平台特定的secure memory区域。该image执行在secure world。

### BL33（Non-trusted Firmware）image加载

BL2加载BL33 image（比如UEFI或者其他test或boot软件）从platform storage到平台定义的non-secure内存。

一旦安全状态初始化完成，BL2就会依赖EL3运行时软件将控制权传递给BL33。因此，BL2使用normal world软件image的entrypoint和Saved Program Status Register （SPSR）填充特定于平台的内存区域。 entrypoint是BL33映像的加载地址。 SPSR是根据PSCI PDD第5.13节的规定确定的。 此信息将传递到EL3运行时软件。

### AArch64 BL31（EL3运行时软件）执行

EL2继续执行如下：

1. BL2传递控制给BL1通过SMC，并且提供BL3的entrypoint给BL1。该exception被BL1安装的exception handler处理。
2. BL1关闭MMU并且刷新cache，清楚SCTLR_EL3.M/I/C 位刷新data cache到the point of coherency并且无效TLB。
3. BL1传递控制给BL31到指定的EL3 entrypoint。 

## BL31

该阶段的Image是被BL2加载，并且BL1传递控制给BL3在EL3下。BL31独自执行在trusted SRAM。BL31被链接和加载到一个平台特定的地址处。BL31实现的功能如下：

### 架构初始化

当前，BL31执行和BL1相似的架构初始化，就系统寄存器而言。由于BL1代码在ROM中，BL31架构初始化可以覆盖之前被BL1初始化的。

BL31初始化per-CPU data framework，该框架提供了对经常访问per-CPU数据的缓存，这些数据针对在不同CPU上的快速并发操作进行了优化。该buffer包含了per-CPU上下文的指针，crash buffer，CPU reset和power down操作，PSCI data，platform data等等。

然后替换BL1设置的exception vectors。BL31 exception vectors为处理SMC提供了更详尽的支持，因为这是访问BL31实施的运行时服务的唯一机制。BL31在将控制权传递给所需的SMC处理程序例程之前，检查每个SMC的有效性（如SMC调用约定PDD所指定）。

BL31使用平台提供的系统计数器的时钟频率对CNTFRQ_EL0寄存器进行编程。

### 平台初始化

BL31执行详细的平台初始化，这使normal world软件能够正确运行。

在 Arm platform包括如下：

-  Initialize the console.
-  Configure the Interconnect to enable hardware coherency.
-  Enable the MMU and map the memory it needs to access.
-  Initialize the generic interrupt controller.
-  Initialize the power controller device.
-  Detect the system topology.

### 运行时服务初始化

BL31负责初始化运行时服务。其中之一是PSCI。

作为PSCI初始化的一部分，BL31探测系统拓扑。它也初始化数据结构，这些数据结构实现了状态机用于追踪powe domain nodes的状态，电源状态可以是OFF，RUN或者RETENTION。