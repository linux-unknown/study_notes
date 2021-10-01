##  Basic Facilities of a Virtio Device

[TOC]

virtio设备被发现和识别，通过一个总线特定的方式（见bus specific sections: 4.1 Virtio Over PCI Bus, 4.2 Virtio Over MMIO and 4.3 Virtio Over Channel I/O），每一个设备包含下面基本分：

- Device status field 
- Feature bits
- Device Configuration space
- One or more virtqueues

### Device status field 

在driver初始化device的过程中，驱动遵循3.1的步骤顺序。

device status field提供了这些序列已完成步骤的简单low-level指示。 经常用于在console中指示device的状态。 定义了以下位（下面以通常设置的顺序列出）：

**ACKNOWLEDGE(1)** ：指示guset OS已经发现devide并且设别到是一个有效的virtio device。

**DRIVER (2)**：指示guset OS知道如何来驱动这个device。注意，可能会有一个明显（或无限）的延时，在设置该位之前。比如Linux驱动是可以是可加载的模块。

**FAILED (128)** ：指示发生了错误在guest中，而且已经放弃了该device。这可能是一个内部错误，或者driver因为一些原因不喜欢该device，甚至一个致命的错误，在操作设备的时候。

**FEATURES_OK (8)**：指示driver已经确认了它理解的所有的feature，并且feature协商完成。

**DRIVER_OK (4)**：指示driver已经设置并且准确驱动device。

**DEVICE_NEEDS_RESET (64)** ：指示device已经遇到了无法恢复的错误。

#### Driver Requirements: Device Status Field

driver必须更新device status，设置bit来指示驱动初始化顺序步骤完成。driver不能清楚device状态位。如果driver设置FAILD位，driver在重新初始化之前必须reset device。

#### Device Requirements: Device Status Field

一旦reset，device必须初始化device status为0。

device不能消费buffer或notify driver，在driver DRIVER_OK之前。

如果device进入一个需要reset的状态，应该设置DEVICE_NEEDS_RESET。如果DRIVER_OK被设置，然后device设置DEVICE_NEEDS_RESET，device必须发送一个device 配置改变通知给drvier。

### Feature Bits

feature bits分配如下

**0 to 23** 指定device的种类

**24 to 32** 保留位，用于扩展queue和协商机制

**33及以上** 保留位，用于feature扩展

### Device Configuration Space

device configuration space通常用于很少改变或者初始化时候的参数。配置字段是可选的，他们的存在通过feature bits来指示。这个规范未来版本很可能扩展device configuration space通过在尾部添加额外的字段。

每个transport（传输）也为device configuration space提供了一个生成计数器，当可能两次访问device configuration space看到不同版本的space时，计数器将会变化。

 #### Driver Requirements: Device Configuration Space

driver不能假设从字段读取大于32 bit是原子的，也不要假定读取多个字段是原子的。

#### Virtqueues

在virtio device上传输批量数据的机制被称作virtqueue。每个device可以有0或更多的virtqueues。每个queue有一个16-bit queue大小参数，该参数设置entry的数目和表示queue的总大小。

每个virtqueue包含三部分：

- Descriptor Table 
-  Available Ring 
- Used Ring

每一部分在guest 内存中都是物理连续的，并且有不同的对齐要求。内存对齐要求和大小要求，单位是字节，每个part的总结如下：

| Virtqueue Part   | Alignment | Size               |
| ---------------- | --------- | ------------------ |
| Descriptor Table | 16        | 16∗(Queue Size)    |
| Available Ring   | 2         | 6 + 2∗(Queue Size) |
| Used Ring        | 4         | 6 + 8∗(Queue Size) |

queue大小和virtqueu buffer的最大数量有关，queue大小总是2的幂。最大的queue大小是32768，这个值通过总线特定的方式指定。

##### The Virtqueue Descriptor Table

descriptor table表示一个buffer，该buffer是driver用于device的。addr是物理地址，可以通过next组成链。每一个descriptor描述了一个buffer，该buffer或者对device是read-only或者write-only，但是一个链可以包含这两种。

实际给device提供的内容依赖于device的种类，通常以一个header开始，用于device去读，在尾部加一个status后缀，让设备来写。

```c
struct virtq_desc {
    /* Address (guest-physical). */
    le64 addr;
    /* Length. */
    le32 len;
    /* This marks a buffer as continuing via the next field. */
    #define VIRTQ_DESC_F_NEXT 1
    /* This marks a buffer as device write-only (otherwise device read-only). */
    #define VIRTQ_DESC_F_WRITE 2
    /* This means the buffer contains a list of buffer descriptors. */
    #define VIRTQ_DESC_F_INDIRECT 4
    /* The flags as indicated above. */
    le16 flags;
    /* Next field if flags & NEXT */
    le16 next;
};
```



table中descriptor的数目通过virtqueue的queue size来决定：这是最大可能的描述符链长度。

######  Indirect Descriptors

一些设备受益于同时调度大量的大请求， VIRTIO_F_INDIRECT_DESC feature允许这样。driver可以存储 indirect descriptors的table在内存的任何地方，并且插入一个descriptor在main virtqueu，这样来增加ring的能力，该descriptor表示存储该 indirect descriptors table的内存。addr和len表示e indirect table的地址和长度。

The indirect table 布局类似下面

```c
struct indirect_descriptor_table {
	/* The actual descriptors (16 bytes each) */
	struct virtq_desc desc[len / 16];
};
```

第一个 indirect descriptor坐落在indirect descriptor table（index 0），额外的通过next链起来，没有next表示descriptor的结束。一个indirect descriptor table包含readable and device-writable descriptors。

### The Virtqueue Available Ring

```c
struct virtq_avail {
#define VIRTQ_AVAIL_F_NO_INTERRUPT 1
    le16 flags;
    le16 idx;
    le16 ring[ /* Queue Size */ ];
    le16 used_event; /* Only if VIRTIO_F_EVENT_IDX */
};
```

driver使用available ring给device提供buffers：每一个ring entry表示一个descriptor chain的head。只有driver可以写，device可以读。

idx字段指示driver应该在ring中的哪里放置下一个descriptor entry，idx从0开始递增。

### Virtqueue Interrupt Suppression（抑制）

如果VIRTIO_F_EVENT_IDX feature bit没有协商，available ring中的flag字段提供了对driver提供了一个粗略的机制来通知device当buffer在使用的时候不想被打断。否则，used_event是更高性能的替代方法，其中驱动程序指定设备在中断之前可以进行多远。

这些中断抑制方法都不可靠，因为它们与设备不同步，但是它们可以用作有用的优化。

### The Virtqueue Used Ring

```c
struct virtq_used {
#define VIRTQ_USED_F_NO_NOTIFY 1
    le16 flags;
    le16 idx;
    struct virtq_used_elem ring[ /* Queue Size */];
    le16 avail_event; /* Only if VIRTIO_F_EVENT_IDX */
};
/* le32 is used here for ids for padding reasons. */
struct virtq_used_elem {
    /* Index of start of used descriptor chain. */
    le32 id;
    /* Total length of the descriptor chain which was used (written to) */
    le32 len;
};
```

used ring 是device 返回buffer的地方，一旦它们被处理完成：只有device可以写，driver可以读。

在ring中的每一个entry都是一对：id指示descriptor chain的head entry，该descriptor chain描述buffer（这个会匹配一个被guest早期放在available ring中的entry）。len表示写入buffer总的字节数。

idx字段指示 driver应该把下一个descriptor entry放到ring中的哪里，从0开始递增。

### Virtqueue Notification Suppression  

device可以抑制Notification以driver抑制interrupt相似的方式。device操作used ring中的flags或者avail_event 和driver操作available ring中的flags或者used_event 方式一样。

## General Initialization And Device Operation  

### Device Initialization

#### Driver Requirements: Device Initialization  

driver必须跟随下面的顺序初始化一个device：

1. reset the device
2. 设置ACKNOWLEDGE状态位：guet OS已经注意到该device
3. 设置DRIVER状态位：guest OS知道如何驱动该device
4. 读取device feature bits，并且将OS和driver可以理解的feature子集bits写入device。在该步骤期间，driver可能读取device-specific configuration fields来检查在accept它是否支持该device。
5. 设置FEATURES_OK状态bit，driver在该步骤之后就不能接受新的feature。
6. 重新读取device status确保FEATURES_OK 比他仍然被设置，否则device不支持我们的feature自己，并且device是不能用的。
7. 执行device-specific的设置，包括发现device的virtqueues ，可选的per-bus设置，读取并且可能的写device的virtio configuration space和填充virtqueues 。
8. 设置DRIVER_OK，此刻，device is live。

如果这些步骤的任何一个发生不可恢复的错误，driver应该设置FAILED状态bit，来指示已经放弃给device（如果需要，可以在后面reset该device来重新还是）。driver在这种情形下，不应该继续初始化。

driver不能notify device，在set DRIVER_OK  之前。

### Device Operation

由两部分的device operation：提供new buffers给device和从device那里处理used buffers。

#### Supplying Buffers to The Device  

driver提供buffers给device的virtqueues中的一个，如下：

1. driver把buffer放进descriptor table中的free descripor，如果需要则链起来
2. driver把descriptor chian的head的index放入available ring的下一个ring entry。
3. 步骤1和2可能会重复，如果批量是可能的。
4. driver执行合适的memory barrier确保在下个步骤之前device看到descriptor 他变了和available ring的更新。
5. available idx通过添加到available ring中的descriptor chain heads的数量被增加。
6. driver执行合适的memory barrier确保在检查notification suppression之前已经更新了idx字段
7. 如果notifications are not suppressed ，driver notify device 新的available buffers。

##### Placing Buffers Into The Descriptor Table  

缓冲区由零个或多个设备可读的物理连续元素组成，后跟零个或多个
物理上连续的设备可写元素（每个元素都有至少一个元素）。 该算法将其映射到descriptor table  中以形成descriptor chain ：

对每一个buffer元素，b:

1. 获取下一个free descriptor table entry，d

2. 设置d.addr为b开始的物理地址

3. 设置d.len为b的长度

4. 如果是device-writab设置d.flags为VIRTQ_DESC_F_WRITE ,否则为0

5. 如果在该元素之后，还有一个元素：

   a. 设置d.next为下一个free descriptor  元素的index

   b. 设置VIRTQ_DESC_F_NEXT bit 在 d.flags中  

##### Updating The Available Ring  

在上面的算法中descriptor chain head 是第一个d， descriptor table entry 的索引，它指向缓冲区的第一部分。 一个简单的driver实现可以执行以下操作（假定与little-endian进行了适当的转换）

```c
avail->ring[avail->idx % qsz] = head;
```

然而，通常，driver可能添加了许多descriptor chains在增加idx之前（在此时，它们变得对device可见），因此通常保持一个计数，计数driver添加了多少

```c
avail->ring[(avail->idx + added++) % qsz] = head;
```

##### Updating idx  

idx 总是增加，and wraps naturally at 65536  

```c
avail->idx += added;
```

一旦driver更新了可用的idx，它将公开descriptor及其内容。 device可以访问driver创建的descriptor chains和descriptor chains表示的内存。

##### Notifying The Device  

实际的device notification是bus-specfic，但是通常会很昂贵。因此device可以抑制此类通知在不需要的时候。

driver必须小心暴露新的idx值，在检查是否notifications是否支持。

### Receiving Used Buffers From The Device  

一旦device有被descriptor表示的used buffer，它interrupt driver。

为了最佳性能，driver也许禁止interrupt当在处理used ring，但要注意在emptying ring和reenableing interrupt之间缺少interrupt的问题。 这通常是通过在interrupt被重新enable后重新检查更多used buffer来处理的：

```c
virtq_disable_interrupts(vq);
for (;;) {
	if (vq->last_seen_used != le16_to_cpu(virtq->used.idx)) {
		virtq_enable_interrupts(vq);
		mb();
		if (vq->last_seen_used != le16_to_cpu(virtq->used.idx))
			break;
		virtq_disable_interrupts(vq);
	}

	struct virtq_used_elem *e = virtq.used->ring[vq->last_seen_used%vsz];
	process_buffer(e);
	vq->last_seen_used++;
}
```

#### Notification of Device Configuration Changes  

device的device-specificde configuration information可以被改变，一个interrupt被传递，当device-specific configuration 改变发生。

该interrupt通过device设置DEVICE_NEEDS_RESET触发。

### Device Cleanup  

## Virtio Transport Options  

virtio可以使用不同的bus，因此该标准被分为virtio通用的和bus-specific。

### Virtio Over PCI Bus 

virtio device通常实现为PCI device。

virtio device可以被实现成任何类别的PCI device：常规PCI device或者PCI Express device。确保设计符合最新的级别需求。

#### Device Requirements: Virtio Over PCI Bus  

virtio device使用Virtio Over PCI Bus必须暴露给guest符合PCI规范的interface。

#### PCI Device Discovery  

任何PCI device，Vendor ID 0x1AF4 ，PCI Device ID 0x1000到0x107F（包括0x107F）是virtio device。该范围的实际值指示哪一个virtio device被device支持。PCI Device ID通过加上0x1040给Virtio Device ID计算得到。此外，device可以使用过渡PCI设备ID范围，从0x1000到0x103F，具体取决于设备类型。

#####  Device Requirements: PCI Device Discovery  

Devices必须有PCI Vendor ID 0x1AF4，必须有PCI Device ID，PCI Device ID或者通过Virtio Device ID加上0x1043得到，或者有过度PCI Device ID，根据devie的类型，如下：

| Transitional PCI Device ID | Virtio Device                   |
| -------------------------- | ------------------------------- |
| 0x1000                     | network card                    |
| 0x1001                     | block device                    |
| 0x1002                     | memory ballooning (traditional) |
| 0x1003                     | console                         |
| 0x1004                     | SCSI host                       |
| 0x1005                     | entropy source                  |
| 0x1009                     | 9P transport                    |

例如network card device Virtio Device ID 1有PCI Device ID 0x1041或者过度PCI Device ID 0x1000。

PCI Subsystem Vendor ID和PCI Subsystem Device ID可能反应PCI Vendor ID和Device ID。

非过度device应该有PCI Device ID范围为0x1040 to 0x107f ，非过度device应该有一个PCI Revision ID  1或这更高。非过度device应该有一个PCI Subsystem Device ID of 0x40或更高。

### PCI Device Layout  

根据Virtio Structure PCI Capabilities规范，device通过I/O或memory region配置。

### Virtio Structure PCI Capabilities  

virtio device configuration 包含几个结构：

- Common configuration
- Notifications
- ISR Status
- Device-specific configuration (optional)  
- PCI configuration access  

