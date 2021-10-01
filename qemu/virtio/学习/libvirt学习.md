# libvirt

Libvirt 是一组软件的汇集，提供了管理虚拟机和其它虚拟化功能（如：存储和网络接口等）的便利途径。这些软件包括：一个长期稳定的 C 语言 API、**一个守护进程（libvirtd）**和**一个命令行工具（virsh）**。Libvirt 的主要目标是提供一个单一途径以管理多种不同虚拟化方案以及虚拟化主机，包括：[KVM/QEMU](https://wiki.archlinux.org/index.php/QEMU)，[Xen](https://wiki.archlinux.org/index.php/Xen)，[LXC](https://wiki.archlinux.org/index.php/LXC)，[OpenVZ](http://openvz.org/) 或 [VirtualBox](https://wiki.archlinux.org/index.php/VirtualBox) [hypervisors](https://wiki.archlinux.org/index.php/Category:Hypervisors) 

交互框图如下：

![基本的交互框架如图](C:\Users\ffelixwu\Downloads\2019011515073164.png)

## libvirt几个重要概念

1. 节点（Node）：一个物理机器，上面可能运行着多个虚拟客户机。Hypervisor和Domain都运行在Node之上。
2. Hypervisor：也称虚拟机监控器（VMM），如KVM、Xen、VMware、Hyper-V等，是虚拟化中的一个底层软件层，它可以虚拟化一个节点让其运行多个虚拟客户机（不同客户机可能有不同的配置和操作系统）。
3. 域（Domain）：是在Hypervisor上运行的一个客户机操作系统实例。域也被称为实例（instance，如亚马逊的AWS云计算服务中客户机就被称为实例）、客户机操作系统（guest OS）、虚拟机（virtual machine），它们都是指同一个概念。

------

在了解了节点、Hypervisor和域的概念之后，用一句话概括libvirt的目标，就是：**为了安全高效的管理节点上的各个域，而提供一个公共的稳定的软件层**。当然，这里的管理，既包括本地的管理，也包含远程的管理。具体地讲，libvirt的管理功能主要包含如下五个部分：

1. 域的管理：包括对节点上的域的各个生命周期的管理，如：启动、停止、暂停、保存、恢复和动态迁移。也包括对多种设备类型的热插拔操作，包括：磁盘、网卡、内存和CPU，当然不同的Hypervisor上对这些热插拔的支持程度有所不同。

2. 远程节点的管理：只要物理节点上运行了libvirtd这个守护进程，远程的管理程序就可以连接到该节点进程管理操作，经过认证和授权之后，所有的libvirt功能都可以被访问和使用。libvirt支持多种网络远程传输类型，如SSH、TCP套接字、Unix domain socket、支持TLS的加密传输等。假设使用最简单的SSH，则不需要额外配置工作，比如：example.com节点上运行了libvirtd，而且允许SSH访问，在远程的某台管理机器上就可以用如下的命令行来连接到example.com上，从而管理其上的域。

   virsh -c qemu+ssh://root@example.com/system

3. 存储的管理：任何运行了libvirtd守护进程的主机，都可以通过libvirt来管理不同类型的存储，如：创建不同格式的客户机镜像（**qcow2、raw、qde、vmdk**等）、挂载NFS共享存储系统、查看现有的LVM卷组、创建新的LVM卷组和逻辑卷、对磁盘设备分区、挂载iSCSI共享存储，等等。当然libvirt中，对存储的管理也是支持远程管理的。

4. 网络的管理：任何运行了libvirtd守护进程的主机，都可以通过libvirt来管理物理的和逻辑的网络接口。包括：列出现有的网络接口卡，配置网络接口，创建虚拟网络接口，网络接口的桥接，VLAN管理，NAT网络设置，为客户机分配虚拟网络接口，等等。

5. 提供一个稳定、可靠、高效的应用程序接口（API）以便可以完成前面的4个管理功能。

------

libvirt主要由三个部分组成，分别是：**应用程序编程接口（API）库、一个守护进程（libvirtd）和一个默认命令行管理工具**（virsh）。

- 应用程序接口（API）是为了其他虚拟机管理工具（如virsh、virt-manager等）提供虚拟机管理的程序库支持。
- libvirtd守护进程负责执行对节点上的域的管理工作，在用各种工具对虚拟机进行管理之时，这个守护进程一定要处于运行状态中，而且这个守护进程可以分为两种：一种是root权限的libvirtd，其权限较大，可以做所有支持的管理工作；一种是普通用户权限的libvirtd，只能做比较受限的管理工作。
- virsh是libvirt项目中默认的对虚拟机管理的一个命令行工具

