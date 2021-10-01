# Domain XML format

[TOC]

## 元素和属性概述

### General metadata

所有virtual machine需要的根元素是domain，它有两个属性：

- **type**：指定用来运行domain的hypervisor，值由driver指定，可以是xen，kvm，qemu，lxc
- **id**：唯一的整型值来标识运行的guest machine，Inactive machine没有id值

有下面的主要元素：

- **name：virtual** machine的段名字
- **uuid：virtual** machine全球唯一的标志

```xml
<domain type='kvm' id='1'>
  <name>MyGuest</name>
  <uuid>4dea22b3-1d52-d8f3-2516-782e98ab3fa0</uuid>
</domain>
```



### Operating system booting

有很多不同的方式来引导虚拟机，每种方法各有利弊。

#### BIOS bootloader

支持完full virtualization的hypervisors 可以通过BIOS引导。 在这种情况下，BIOS具有启动顺序优先级（软盘，硬盘，cdrom，网络），该优先级确定从何处获取/查找启动映像。

```xml
...
<os firmware='efi'>
  <type>hvm</type>
  <boot dev='hd'/>
  <boot dev='cdrom'/>
  <smbios mode='sysinfo'/>
</os>
...
```

- **firmware**：可选值为bios和efi，普通用户无需关心。
- **type**：指定虚拟机中启动的操作系统的类型。hvm表示 full virtualization。linux表示支持Xen 3 hypervisor guest ABI。可选的属性arch支持虚拟化的CPU架构，machine指机器类型。
- **boot：dev**可以是fd，hd，cdrom或network，用于指定要考虑的下一个引导设备。引导元素可以重复多次以设置引导设备的优先级列表以依次尝试。
- **smbios**：如何填充SMBIOS信息给guest。mode属性必须指定，可以是emulate（让hypervisor生成所有值），host（从host的SMBIOS复制Block0和Block1.除了UUID），sysinfo（使用sysinfo元素中的值），如果没有被指定，hypervisor 是默认被使用。

### SMBIOS System Information 

一些hypervisors 允许控制向guest显示的系统信息（例如，SMBIOS字段可以由虚拟机管理程序填充，并可以通过访客中的dmidecode命令进行检查）。 可选的sysinfo元素涵盖所有此类信息。

```xml
...
<os>
  <smbios mode='sysinfo'/>
  ...
</os>
<sysinfo type='smbios'>
  <bios>
    <entry name='vendor'>LENOVO</entry>
  </bios>
  <system>
    <entry name='manufacturer'>Fedora</entry>
    <entry name='product'>Virt-Manager</entry>
    <entry name='version'>0.9.4</entry>
  </system>
  <baseBoard>
    <entry name='manufacturer'>LENOVO</entry>
    <entry name='product'>20BE0061MC</entry>
    <entry name='version'>0B98401 Pro</entry>
    <entry name='serial'>W1KS427111E</entry>
  </baseBoard>
  <chassis>
    <entry name='manufacturer'>Dell Inc.</entry>
    <entry name='version'>2.12</entry>
    <entry name='serial'>65X0XF2</entry>
    <entry name='asset'>40000101</entry>
    <entry name='sku'>Type3Sku1</entry>
  </chassis>
  <oemStrings>
    <entry>myappname:some arbitrary data</entry>
    <entry>otherappname:more arbitrary data</entry>
  </oemStrings>
</sysinfo>
<sysinfo type='fwcfg'>
  <entry name='opt/com.example/name'>example value</entry>
  <entry name='opt/com.coreos/config' file='/tmp/provision.ign'/>
</sysinfo>
```

sysinfo元素有一个强制元素type，type决定子元素的布局。支持的值是smbios和fwcfg

#### smbios

每一个sysinfo的子元素被命名为SMBIOS block。

这是SMBIOS的block 0

​	vendor：BIOS Vendor的名称

​	version：BIOS 版本

#### fwcfg

我们暂时没有用到

### CPU Allocation

```xml
<domain>
  ...
  <vcpu placement='static' cpuset="1-4,^3,6" current="1">2</vcpu>
  <vcpus>
    <vcpu id='0' enabled='yes' hotpluggable='no' order='1'/>
    <vcpu id='1' enabled='no' hotpluggable='yes'/>
  </vcpus>
  ...
</domain>
```

#### vcpu

该元素的内容定义分配给guest OS的最大虚拟CPU的数量，必须在1和hypervisor支持的最大CPU之间。

- **couset**：可选属性cpuset是用逗号分隔的物理CPU编号列表，默认情况下可以将domain process和 virtual CPU固定到这些物理CPU编号。 （注意：domain process和virtual CPU的固定策略可以由cputune分别指定。如果指定了cputune的属性emulatorpin，则将忽略vcpu在此处指定的cpuset。类似地，对于指定了vcpupin的虚拟CPU， 由cpuset指定的cpuset将被忽略；对于未指定vcpupin的虚拟CPU，每个虚拟CPU将固定到由cpuset指定的物理CPU）。 该列表中的每个元素要么是单个CPU编号，一定范围的CPU编号，要么是插入号，后跟一个要从先前范围排除的CPU编号

- **current**：可选属性current可用于指定是否应启用少于最大虚拟CPU数量的虚拟机

- **placement**：可选的属性placement可用于指示 domain process的CPU放置模式。 该值可以是“ static”或“ auto”，但是如果指定了cpuset，则默认为numatune的placement或 static。 使用“自动”表示domain process将固定到advisory nodeset从numad（mumad是Linux一个进程）查询到的，并且如果指定了属性cpuset的值将被忽略。 如果未同时指定cpuset和Placement或placement为“static”，但未指定cpuset，则domain process将固定到所有可用的物理CPU

#### vcpus

vcpus元素允许控制各个vCPU的状态。 id属性指定libvirt在其他地方使用的vCPU id，例如vCPU固定，调度程序信息和NUMA分配。请注意，在guest中看到的vCPU ID在某些情况下可能与libvirt ID不同。有效ID是从0到vcpu元素减去1设置的最大vCPU计数。enabled属性允许控制vCPU的状态。有效值为yes和no。 hotpluggable控制在使能时在启动CPU的情况下是否可以热插拔指定的vCPU。请注意，所有禁用的vCPU必须是可热插拔的。有效值为yes和no。 order可以指定添加online vCPU的顺序。对于需要一次插入多个vCPU的虚拟机管理程序/平台，可以在需要立即启用的所有vCPU中重复执行该命令。无需指定顺序，然后以任意顺序添加vCPU。如果使用了order info，则必须将其用于所有在线vCPU。Hypervisors 可能会在某些操作期间清除或更新ordering 信息，以确保有效的配置。请注意，系统管理程序可能会创建不同于引导vCPU的可热插拔vCPU，因此可能需要进行特殊的初始化。Hypervisors 可能要是能的vCPU在boot时，，不可热插拔的vCPU从ID 0开始开始聚类。还可能要求vCPU 0始终存在且不可热插拔。请注意，可能需要为单个CPU提供状态才能支持可寻址vCPU热插拔，并且并非所有虚拟机管理程序都支持此功能。对于QEMU，需要满足以下条件。 vCPU 0需要启用且不可热插拔。

### CPU Tuning

```xml
<domain>
  ...
  <cputune>
    <vcpupin vcpu="0" cpuset="1-4,^2"/>
    <vcpupin vcpu="1" cpuset="0,1"/>
    <vcpupin vcpu="2" cpuset="2,3"/>
    <vcpupin vcpu="3" cpuset="0,4"/>
  </cputune>
  ...
</domain>
```



#### cputune

可选的cputune元素提供有关domain的CPU可调参数的详细信息。

#### vcpupin

可选的vcpupin元素指定将domain vCPU固定到哪个主机的物理CPU。 如果省略此选项，并且未指定元素vcpu的属性cpuset，则默认情况下，vCPU固定到所有物理CPU。 它包含两个必需的属性，属性vcpu指定vCPU ID，属性cpuset与元素vcpu的属性cpuset相同。 

### Memory Allocation

```xml
<domain>
  ...
  <maxMemory slots='16' unit='KiB'>1524288</maxMemory>
  <memory unit='KiB'>524288</memory>
  <currentMemory unit='KiB'>524288</currentMemory>
  ...
</domain>
```

#### memory

引导时为guest虚拟机分配的最大内存。内存分配包括可能在启动时指定或以后热插拔的其他可能的存储设备。此值的单位由可选属性unit确定，默认为“ KiB”。有效单位是“ b”或“字节”代表字节，“ KB”代表千字节，“ k”或“ KiB”代表千字节，“ MB”代表兆字节 ，“ M”或“ MiB”表示兆字节，“ GB”表示千兆字节，“ G”或“ GiB”表示千兆字节，“ TB”表示兆字节，或者“ T”或“ TiB”表示TB。但是，该值将由libvirt舍入到最接近的千字节，并且可能会进一步舍入到hypervisor支持的粒度。一些hypervisors 还强制执行最低要求，例如4000KiB。如果为guest 配置了NUMA，则可以省略存储元素。在crash的情况下，可选属性dumpCore可用于控制是否将guest 内存包括在生成的coredump中（值“ on”，“ off”）

#### currentMemory

guest的实际内存分配。 该值可以小于最大分配，以允许动态增加客户机内存。 如果省略此参数，则默认为与memory元素相同的值。unit属性的行为与 memory相同。

#### maxMemory

guest的运行时最大内存分配。 通过memory元素或者NUMA cell size配置指定的初始内存可以通过内存热插拔增加，但是会被限制maxMemory的大小。 unit属性的行为与<memory>相同。 slot属性指定可用于向guest添加内存的插槽数。 范围是特定于hypervisor 的。 请注意，由于对齐了通过内存热插拔添加的内存块，因此可能无法实现此元素指定的完整大小分配。

### Memory Backing

```xml
<domain>
  ...
  <memoryBacking>
    <hugepages>
      <page size="1" unit="G" nodeset="0-3,5"/>
      <page size="2" unit="M" nodeset="4"/>
    </hugepages>
    <locked/>
    <source type="file|anonymous|memfd"/>
    <access mode="shared|private"/>
  </memoryBacking>
  ...
</domain>
```

可选的“ memoryBacking”元素可能包含几个元素，这些元素会影响host pages如何备份虚拟内存页面。

The optional `memoryBacking` element may contain several elements that influence how virtual memory pages are backed by host pages.

#### hugepages

这告诉hypervisor ，guset应该使用大页面而不是正常的本机页面大小分配其内存。 从1.2.5开始，可以为每个numa节点设置更大的页面。 介绍page元素。 它具有一个强制性的属性size，该size指定应使用哪些大页面（在支持不同大小的大页面的系统上特别有用）。 size属性的默认单位是千字节（1024的倍数）。 如果要使用其他单位，请使用可选的unit属性。 对于具有NUMA的系统，可选nodeset属性可能会派上用场，因为它将把guest的NUMA节点与特定的大页面大小相关联。 在示例代码段中，除第四个节点外，每个NUMA节点都使用1 GB的大页面。

#### source

使用type属性，可以提供“file”以利用文件memorybacking或保留默认的值“anonymous”。 从4.10.0开始，您可以选择“ memfd”支持。 （仅QEMU / KVM）

#### access

使用mode属性，指定memory是shared或者private。该属性可以被per numa节点的memAcess覆盖。

### IOThreads Allocation 

IOThread是专用的事件循环线程，用于支持的磁盘设备执行块I/O请求，以提高可伸缩性，尤其是在具有多个LUN的SMP host/guset上

```xml
<domain>
  ...
  <iothreads>4</iothreads>
  ...
</domain>
```
```xml
<domain>
  ...
  <iothreadids>
    <iothread id="2"/>
    <iothread id="4"/>
    <iothread id="6"/>
    <iothread id="8"/>
  </iothreadids>
  ...
</domain>
```

### iothreads

此可选元素的内容定义了要分配给domain以供受支持的目标存储设备使用的IOThreads的数量。 每个host CPU只能有1个或2个IOThread。 每个IOThread可能分配了多个受支持的设备。 从1.2.8开始

#### iothreadids

可选的iothreadids元素提供了特别定义domain的IOThread ID的能力。 默认情况下，IOThread ID的编号从1到为该domain定义的iothreads的顺序编号。 id属性用于定义IOThread ID。 id属性必须是一个大于0的正整数。如果定义的iothreadids比该domain定义的iothreads少，那么libvirt将顺序填充从1开始的iothreadids，以避免任何预定义的id。 如果定义的iothreadids多于为该域定义的iothreads，则iothreads值将相应调整。 

### CPU model and topology

```xml
...
<cpu match='exact'>
  <model fallback='allow'>core2duo</model>
  <vendor>Intel</vendor>
  <topology sockets='1' dies='1' cores='2' threads='1'/>
  <cache level='3' mode='emulate'/>
  <feature policy='disable' name='lahf_lm'/>
</cpu>
...
```

#### cpu

cpu元素是描述guset CPU要求的主要容器。 它的match属性指定提供给guest的虚拟CPU与这些requirements匹配的严格程度。 从0.7.6开始，如果拓扑是cpu中的唯一元素，则可以省略match属性。 match属性的可能值为

- **minum**：没有使用到
- **exact**：提供给guset的虚拟CPU应该与规格完全匹配。 如果不支持这种CPU，libvirt将拒绝启动domian。
- **strict**：没有用到。

从0.8.5开始，match属性可以省略，并且默认为**exact**。 有时，hypervisor 无法创建与libvirt传递的规范完全匹配的虚拟CPU。 从3.2.0开始，可以使用可选的check属性来请求检查虚拟CPU是否符合规范的特定方式。 通常，在启动domain时忽略此属性并坚持使用默认值是安全的。 domain启动后，libvirt将自动将check属性更改为最佳支持值，以确保在将domian迁移到另一台主机时虚拟CPU不会更改。 可以使用以下值：

- **none**：没有用到
- **partial**：没有用到
- **full**：将根据CPU规范检查hypervisor 创建的guset CPU，除非两个CPU匹配，否则不会启动domain。

从0.9.10开始，可以使用可选的mode属性来简化将guest CPU配置为尽可能靠近主机CPU的过程。 模式属性的可能值为：

- **custom**
  在这种模式下，cpu元素描述了应该呈现给guest的CPU。 如果未指定mode属性，则该值为默认设置。 通过此模式，无论guest启动到哪个主机，持久性guset都将看到相同的硬件。

#### model

model元素的内容指定guest请求的CPU模型。 可用CPU模型及其定义的列表可在libvirt数据目录的cpu_map.xml文件中找到。 如果hypervisor 无法使用确切的CPU模型，则libvirt会自动回退到hypervisor 支持的最接近的模型，同时保留CPU功能列表。 从0.9.10开始，可以使用可选的fallback属性来禁止此行为，在这种情况下，尝试启动请求不受支持的CPU模型的domain的尝试将失败。 fallback属性支持的值为：allow（这是默认值）和forbid。 可选的vendor_id属性（自0.10.0开始）可用于设置guest看到的供应商ID。 它必须正好是12个字符长。 如果未设置，则使用主机的供应商ID。 典型的可能值为“ AuthenticAMD”和“ GenuineIntel”。

#### topology
topology元素指定虚拟CPU的请求拓扑给guest。 socket，die，core和thread这四个属性接受非零的正整数值。 它们分别指每个NUMA节点的CPU socket 数，每个socket的die数，每个die的core数以及每个core的thread数。 die属性是可选的，如果省略，则默认为1，而其他属性都是必需的。 Hypervisors 可能要求cpu元素指定的vCPU的最大数量等于topology产生的vcpus的数量。

#### feature

cpu元素可以包含零个或多个元素，这些元素用于微调所选CPU模型提供的功能。 可以在与CPU型号相同的文件中找到已知功能名称的列表。 每个feature元素的含义取决于其policy属性，该属性必须设置为以下值之一：

- disable：该feature不会被virtual CPU支持
- require：guset创建将失败，除非host CPU支持该功能或hypervisor 能够模拟该功能。
- force：virtual CPU将会支持feature，不管host CPU是否支持
- forbid：guest创建将会失败，如果host CPU不支持的话。

从0.8.5开始，policy属性可以省略，默认为require。

#### numa

```xml
...
<cpu>
  ...
  <numa>
    <cell id='0' cpus='0-3' memory='512000' unit='KiB' discard='yes'/>
    <cell id='1' cpus='4-7' memory='512000' unit='KiB' memAccess='shared'/>
  </numa>
  ...
</cpu>
...
```

每个``cell``元素都指定一个NUMA单元或NUMA节点。 ``cpus``指定属于节点的CPU或CPU范围。从6.5.0开始，对于qemu驱动程序，如果仿真器二进制文件在每个单元中支持不连续的cpus范围，则在每个``cell``中声明的所有CPU的总和将与在``vcpu``元素中声明的最大虚拟CPU数量匹配。这是通过将所有剩余的CPU填充到第一个NUMA单元中来完成的。鼓励用户提供完整的NUMA拓扑，其中NUMA CPU的总和与vcpus中声明的最大虚拟CPU数量匹配，以使domain在qemu和libvirt版本之间保持一致。 memory指定节点内存（以千字节为单位）（即1024字节的块）。从6.6.0开始，cpus属性是可选的，如果省略，将创建一个无CPU的NUMA节点。从1.2.11开始，可以使用附加的``unit``属性来定义 `memory`指定内存的unit。从1.2.7的所有cell都应具有id属性，以防在代码中需要引用某个单元格，否则，将以从0开始的递增顺序为这些cell分配id。不建议混合使用和不使用id属性的cell，因为这样做可能会导致不良行为。从1.2.9版本开始，可选属性memAccess可以控制将内存映射为“shared”还是“private”。这仅对支持hugepages-backed的内存和nvdimm模块有效。每个``cell``元素可以具有一个可选的discarding属性，该属性可以微调给定numa节点的discarding功能，如[Memory Backing](https://libvirt.org/formatdomain.html#elementsMemoryBacking).中所述。接受的值为yes和no。从4.4.0开始

当前，guest NUMA 规范仅适用于QEMU / KVM和Xen。

### Hypervisor features

Hypervisors 可能允许打开/关闭某些CPU /计算机功能。

```xml
<features>
  <pae/>
  <acpi/>
  <apic/>
  <hap/>
  <privnet/>
  <hyperv>
    <relaxed state='on'/>
    <vapic state='on'/>
    <spinlocks state='on' retries='4096'/>
    <vpindex state='on'/>
    <runtime state='on'/>
    <synic state='on'/>
    <stimer state='on'>
      <direct state='on'/>
    </stimer>
    <reset state='on'/>
    <vendor_id state='on' value='KVM Hv'/>
    <frequencies state='on'/>
    <reenlightenment state='on'/>
    <tlbflush state='on'/>
    <ipi state='on'/>
    <evmcs state='on'/>
  </hyperv>
  <kvm>
    <hidden state='on'/>
    <hint-dedicated state='on'/>
  </kvm>
  <xen>
    <e820_host state='on'/>
    <passthrough state='on' mode='share_pt'/>
  </xen>
  <pvspinlock state='on'/>
  <gic version='2'/>
  <ioapic driver='qemu'/>
  <hpt resizing='required'>
    <maxpagesize unit='MiB'>16</maxpagesize>
  </hpt>
  <vmcoreinfo state='on'/>
  <smm state='on'>
    <tseg unit='MiB'>48</tseg>
  </smm>
  <htm state='on'/>
  <ccf-assist state='on'/>
  <msrs unknown='ignore'/>
  <cfpc value='workaround'/>
  <sbbc value='workaround'/>
  <ibs value='fixed-na'/>
</features>
```

所有列在features元素内的feature，省略可切换feature tag的是会被关闭。 可以通过询问 [capabilities XML](https://libvirt.org/formatcaps.html)L和 [domain capabilities XML](https://libvirt.org/formatdomaincaps.html),L来找到可用的feature，但是fully virtualized domains的常见集合是：

#### pae

物理地址扩展模式，允许32位的guest寻址大于4GB的内存

#### acpi

ACPI对于电源管理很有用，例如，对于KVM guest虚拟机，它需要正常关机才能正常工作。

#### apic

APIC运行编程IRQ管理，

#### hap

根据state属性（值的on，off），启用或禁用硬件辅助分页（Hardware Assisted Paging）。 如果hypervisor 检测到硬件辅助分页的可用性，则默认值为打开。

### Events configuration

有时有必要覆盖对各种事件采取的默认操作。 并非所有hypervisors 都支持所有事件和操作。 可以通过调用libvirt API virDomainReboot，virDomainShutdown或virDomainShutdownFlags来采取这些措施。 使用virsh reboot或virsh  shutdown也会触发该事件。

```xml
...
<on_poweroff>destroy</on_poweroff>
<on_reboot>restart</on_reboot>
<on_crash>restart</on_crash>
<on_lockfailure>poweroff</on_lockfailure>
...
```

以下元素集合允许指定action，当生命周期guest OS触发了操作。一个常见的用例是强制把reboot当作poweroff，当在初始OS安装的时候。这样首次安装后允许VM被重新配置。

#### on_poweroff

此元素的内容指定guest请求关闭电源时要执行的操作。

#### on_reboot

指定guest请求reboot是要执行的操作

#### on_crash

指定guest请求crash是要执行的操作

这些状态中的每一个都允许相同的四个可能的动作。

#### destroy
该domain将完全终止，并释放所有资源。
#### restart
domain将被终止，然后使用相同的配置重新启动。
#### preserve
该domain将被终止，其资源将保留以进行分析。
#### rename-restart
该domain将被终止，然后使用新名称重新启动。

QEMU/KVM支持on_poweroff和on_reboot事件，处理destroy和restart操作。针对on_reboot事件的preserve动作被视为destroy，而针对on_poweroff事件的rename-restart动作被视为restart事件。

自0.8.4开始，on_crash事件支持这些其他操作。

#### coredump-destroy
崩溃的domain的核心将被转储，然后该domian将被完全终止并释放所有资源
#### coredump-restart
崩溃的dmian的核心将被转储，然后使用相同的配置重新启动domain

### Devices

```xml
...
<devices>
  <emulator>/usr/lib/xen/bin/qemu-dm</emulator>
</devices>
...
```

#### emulator
emulator元素的内容指定了设备模型仿真器二进制文件的完全限定路径。[capabilities XML](https://libvirt.org/formatcaps.html) 指定用于每种特定域类型/体系结构组合的推荐默认emulator 。

为了帮助用户识别他们关心的设备，每个设备都可以直接具有alias元素，然后该子元素具有name属性，用户可以存储设备的标识符。 标识符必须具有“ ua-”前缀，并且在domain中必须唯一。 此外，标识符必须仅包含以下字符：[a-zA-Z0-9_-]。 从3.9.0开始

```xml
<devices>
  <disk type='file'>
    <alias name='ua-myDisk'/>
  </disk>
  <interface type='network' trustGuestRxFilters='yes'>
    <alias name='ua-myNIC'/>
  </interface>
  ...
</devices>
```

任何看起来像disk的设备，软盘，硬盘，cdrom，paravirtualized driver ，通过disk元素指定

```xml
...
<devices>
  <disk type='file' device='disk'>
    <driver name='qemu' type='qcow2' queues='4'/>
    <source file='/var/lib/libvirt/images/domain.qcow'/>
    <backingStore type='file'>
      <format type='qcow2'/>
      <source file='/var/lib/libvirt/images/snapshot.qcow'/>
      <backingStore type='block'>
        <format type='raw'/>
        <source dev='/dev/mapper/base'/>
        <backingStore/>
      </backingStore>
    </backingStore>
    <target dev='vdd' bus='virtio'/>
  </disk>

  <disk type='nvme' device='disk'>
    <driver name='qemu' type='raw'/>
    <source type='pci' managed='yes' namespace='1'>
      <address domain='0x0000' bus='0x01' slot='0x00' function='0x0'/>
    </source>
    <target dev='vde' bus='virtio'/>
  </disk>
    
  <disk type='block' device='cdrom'>
    <driver name='qemu' type='raw'/>
    <target dev='hdd' bus='ide' tray='open'/>
    <readonly/>
  </disk>
</devices>


...
```

#### disk

disk 元素是主要的容器用来描述disk和支持的属性如下：

- **type**：值可以是：file，block，dir，network，volume，nvme。
- **device**：指示如何将disk 暴露给guset OS。 此属性的可能值为“ floppy”，“ disk”，“ cdrom”和“ lun”，默认为“ disk”。

#### source

disk source的表示取决于disk  type属性值，如下所示（file，block，dir，network等是disk type的值）：

- **file**：file属性指定保存磁盘的文件的标准路径。

- **block**：dev属性指定充当磁盘的**主机设备**的标准路径。

- **dir**：dir属性指定充当磁盘的标准路径。

- **network**：protocol属性指定协议来访问请求的image。 可能的值是“ nbd”，“ iscsi”，“ rbd”，“ sheepdog”，“ gluster”，“ vxhs”，“ http”，“ https”，“ ftp”，ftps”或“ tftp”。

  对于任何协议处理nbd，一个附加的属性name是强制的，用来指定哪一个volume/image将会被用到。

#### target

target元素控制disk暴露于guest OS的bus/device。 dev属性指示“logical”设备名称。在guest OS中，实际的设备名称不保证映射到设备名称。按设备顺序为准（Treat it as a device ordering hint）。可选的bus属性指定了要模拟的磁盘设备的类型。可能的值是特定于驱动程序的，从1.1.2开始，典型值为“ ide”，“ scsi”，“ virtio”，“ xen”，“ usb”，“ sata”或“ sd”，“ sd”。如果省略，则根据设备名称的样式推断总线类型（例如，如果设备名称为sda，通常被暴露为SCSI bus）。可选属性tray指示可移动磁盘（例如CDROM或软盘）的托盘状态，该值可以是“open”或“closed”，默认为“closed”。注意，可以在domain运行时更新tray的值。可选属性removable设置USB磁盘的可移动标志，其值可以为“on”或“off”，默认为“off”

#### driver

可选driver元素允许指定和hypervisor driver有关的更详细的信息来提供disk。

- **cache**：cache属性控制cache机制，可能的值为default, none, writethrough, writeback, directsync（像writethrough，但是bypass了host page cache）和unsafe。
- **name**：如果hypervisor 支持多个backend drivers，name属性选择primary backend driver name，然后type属性提供sub-type，qemu支支持qemu，但是支持多个types，包括 raw, bochs, qcow2, 和qed。

#### address

如果存在，则address元素将磁盘与控制器一个槽绑定在一起（尽管可以明确指定实际的<controller>设备，但libvirt通常可以通过libvirt推断出该设备）。 type属性是强制性的，通常是“ pci”或“ drive”。 对于“ pci”控制器，必须存在bus，slot和function附加属性，以及可选的domain和multifunction属性。 多功能默认为“off”； 其他任何值都需要QEMU 0.1.3和libvirt 0.9.7。 对于drive控制器，附加的属性`controller`, `bus`, `target` (**libvirt 0.9.11**), and `unit` 是可以用的，每一个的默认值 0。