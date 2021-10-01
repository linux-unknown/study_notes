# qemu vhost网络中libvirt中的处理

子机xml

```xml
<interface type="bridge">
      <mac address="20:90:6F:28:84:F2"/>
      <model type="virtio"/>
      <driver name="vhost" queues="8" event_idx="on" rx_queue_size="1024" tx_queue_size="1024"/>
      <source bridge="vbr2525476"/>
      <target dev="veni_vFsp6g8y"/>
</interface>
```

source bridge 是网桥，需要在启动子机之前创建好

## tap设备创建

### qemuBuildInterfaceCommandLine

```c
static int qemuBuildInterfaceCommandLine(virQEMUDriverPtr driver, virLogManagerPtr logManager,
                              virCommandPtr cmd, virDomainDefPtr def, virDomainNetDefPtr net,
                              virQEMUCapsPtr qemuCaps, int vlan, unsigned int bootindex,
                              virNetDevVPortProfileOp vmop, bool standalone, size_t *nnicindexes,
                              int **nicindexes)
{
    int ret = -1;
    char *nic = NULL, *host = NULL;
    int *tapfd = NULL;
    size_t tapfdSize = 0;
    int *vhostfd = NULL;
    size_t vhostfdSize = 0;
    char **tapfdName = NULL;
    char **vhostfdName = NULL;
	/* actualType=VIR_DOMAIN_NET_TYPE_BRIDGE */
    virDomainNetType actualType = virDomainNetGetActualType(net);
    virNetDevBandwidthPtr actualBandwidth;
    size_t i;

    /* actualType为VIR_DOMAIN_NET_TYPE_BRIDGE */
	switch (actualType) {
    case VIR_DOMAIN_NET_TYPE_NETWORK:
    case VIR_DOMAIN_NET_TYPE_BRIDGE:
		/* tapfdSize和queue的数目一样 */
        tapfdSize = net->driver.virtio.queues;

        if (VIR_ALLOC_N(tapfd, tapfdSize) < 0 || VIR_ALLOC_N(tapfdName, tapfdSize) < 0)
            goto cleanup;

        memset(tapfd, -1, tapfdSize * sizeof(tapfd[0]));
		/* 创建tap，建立tap和网桥的连接 */
        if (qemuInterfaceBridgeConnect(def, driver, net, tapfd, &tapfdSize) < 0)
            goto cleanup;
        break;
    }
}
```

### qemuInterfaceBridgeConnect

```c
int qemuInterfaceBridgeConnect(virDomainDefPtr def, virQEMUDriverPtr driver, virDomainNetDefPtr net,
                           int *tapfd, size_t *tapfdSize)
{
    const char *brname;
    int ret = -1;
    unsigned int tap_create_flags = VIR_NETDEV_TAP_CREATE_IFUP;
    bool template_ifname = false;
    virQEMUDriverConfigPtr cfg = virQEMUDriverGetConfig(driver);
    const char *tunpath = "/dev/net/tun";

	/* 
	 * 获取网桥的名称	
	 * brname: xml中的<source bridge="vbr2525476"/> vbr2525476 
	 */
    if (!(brname = virDomainNetGetActualBridgeName(net))) {
    }

	/*  net->ifname: xml中的 <target dev="veni_vFsp6g8y"  */
    if (!net->ifname || STRPREFIX(net->ifname, VIR_NET_GENERATED_TAP_PREFIX) ||
        strchr(net->ifname, '%')) {
        VIR_FREE(net->ifname);
        if (VIR_STRDUP(net->ifname, VIR_NET_GENERATED_TAP_PREFIX "%d") < 0)
            goto cleanup;
        /* avoid exposing vnet%d in getXMLDesc or error outputs */
        template_ifname = true;
    }

    if (net->model && STREQ(net->model, "virtio"))
        tap_create_flags |= VIR_NETDEV_TAP_CREATE_VNET_HDR;

    if (virQEMUDriverIsPrivileged(driver)) { /* true */
        if (virNetDevTapCreateInBridgePort(brname, &net->ifname, &net->mac,
                                           def->uuid, tunpath, tapfd, *tapfdSize,
                                           virDomainNetGetActualVirtPortProfile(net),
                                           virDomainNetGetActualVlan(net),net->coalesce, 0, NULL,
                                           tap_create_flags) < 0) {
            virDomainAuditNetDevice(def, net, tunpath, false);
        }
   
    } 
   
    ret = 0;
}
```

### virNetDevTapCreateInBridgePort

```c
int virNetDevTapCreateInBridgePort(const char *brname, char **ifname, const virMacAddr *macaddr,
                                   const unsigned char *vmuuid, const char *tunpath,
                                   int *tapfd, size_t tapfdSize, virNetDevVPortProfilePtr virtPortProfile,
                                   virNetDevVlanPtr virtVlan, virNetDevCoalescePtr coalesce,
                                   unsigned int mtu, unsigned int *actualMTU, unsigned int flags)
{
    virMacAddr tapmac;
    char macaddrstr[VIR_MAC_STRING_BUFLEN];
    size_t i;

	/* 创建tap */
    if (virNetDevTapCreate(ifname, tunpath, tapfd, tapfdSize, flags) < 0)
        return -1;

    /* We need to set the interface MAC before adding it
     * to the bridge, because the bridge assumes the lowest
     * MAC of all enslaved interfaces & we don't want it
     * seeing the kernel allocate random MAC for the TAP
     * device before we set our static MAC.
     */
    virMacAddrSet(&tapmac, macaddr); /* 将macaddr复制到tapmac */
    
	/* 设置mac地址 */
    if (virNetDevSetMAC(*ifname, &tapmac) < 0)
        goto error;
	/* tap attach到bridge */
    if (virNetDevTapAttachBridge(*ifname, brname, macaddr, vmuuid,
                                 virtPortProfile, virtVlan, mtu, actualMTU) < 0) {
        goto error;
    }
	/* up tap设备 */
    if (virNetDevSetOnline(*ifname, !!(flags & VIR_NETDEV_TAP_CREATE_IFUP)) < 0)
        goto error;

    if (virNetDevSetCoalesce(*ifname, coalesce, false) < 0)
        goto error;

    return 0;
}
```

#### virNetDevTapCreate

```c
int virNetDevTapCreate(char **ifname, const char *tunpath, int *tapfd, size_t tapfdSize,
                       unsigned int flags)
{
    size_t i;
    struct ifreq ifr;
    int ret = -1;
    int fd;

    if (!tunpath)
        tunpath = "/dev/net/tun";

    memset(&ifr, 0, sizeof(ifr));
    for (i = 0; i < tapfdSize; i++) {
        if ((fd = open(tunpath, O_RDWR)) < 0) { }

        memset(&ifr, 0, sizeof(ifr));

		/* 用的是TAP，TAP口上有mac地址，tun口上没有 */
        ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
        /* If tapfdSize is greater than one, request multiqueue */
        if (tapfdSize > 1) {
# ifdef IFF_MULTI_QUEUE
            ifr.ifr_flags |= IFF_MULTI_QUEUE;
# else
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("Multiqueue devices are not supported on this system"));
            goto cleanup;
# endif
        }

# ifdef IFF_VNET_HDR
        if ((flags &  VIR_NETDEV_TAP_CREATE_VNET_HDR) &&
            virNetDevProbeVnetHdr(fd))
            ifr.ifr_flags |= IFF_VNET_HDR;
# endif
		/* tap 设备的名称 */
        if (virStrcpyStatic(ifr.ifr_name, *ifname) == NULL) { }
		/* 创建接口 */
        if (ioctl(fd, TUNSETIFF, &ifr) < 0) { }

        if (i == 0) {
            /* In case we are looping more than once, set other
             * TAPs to have the same name */
            VIR_FREE(*ifname);
            if (VIR_STRDUP(*ifname, ifr.ifr_name) < 0)
                goto cleanup;
        }

        if ((flags & VIR_NETDEV_TAP_CREATE_PERSIST) &&
            ioctl(fd, TUNSETPERSIST, 1) < 0) {
        }
        tapfd[i] = fd;
    }

    ret = 0;
}
```

## vhost 

### qemuBuildInterfaceCommandLine

```c
static int qemuBuildInterfaceCommandLine(virQEMUDriverPtr driver, virLogManagerPtr logManager,
                              virCommandPtr cmd, virDomainDefPtr def, virDomainNetDefPtr net,
                              virQEMUCapsPtr qemuCaps, int vlan, unsigned int bootindex,
                              virNetDevVPortProfileOp vmop, bool standalone, size_t *nnicindexes,
                              int **nicindexes)
{
    int ret = -1;
    char *nic = NULL, *host = NULL;
    int *tapfd = NULL;
    size_t tapfdSize = 0;
    int *vhostfd = NULL;
    size_t vhostfdSize = 0;
    char **tapfdName = NULL;
    char **vhostfdName = NULL;
	/* actualType=VIR_DOMAIN_NET_TYPE_BRIDGE */
    virDomainNetType actualType = virDomainNetGetActualType(net);
    virNetDevBandwidthPtr actualBandwidth;
    size_t i;

    /* actualType为VIR_DOMAIN_NET_TYPE_BRIDGE */
    if ((actualType == VIR_DOMAIN_NET_TYPE_NETWORK || actualType == VIR_DOMAIN_NET_TYPE_BRIDGE ||
         actualType == VIR_DOMAIN_NET_TYPE_ETHERNET || actualType == VIR_DOMAIN_NET_TYPE_DIRECT) &&
        !standalone) {
        /* Attempt to use vhost-net mode for these types of network device 
        * vhostfdSize也和queues的大小一样
        */
        vhostfdSize = net->driver.virtio.queues;

        if (VIR_ALLOC_N(vhostfd, vhostfdSize) < 0 || VIR_ALLOC_N(vhostfdName, vhostfdSize))
            goto cleanup;

        memset(vhostfd, -1, vhostfdSize * sizeof(vhostfd[0]));

        if (qemuInterfaceOpenVhostNet(def, net, qemuCaps, vhostfd, &vhostfdSize) < 0)
            goto cleanup;
    }
}
```

#### qemuInterfaceOpenVhostNet

```c
int qemuInterfaceOpenVhostNet(virDomainDefPtr def, virDomainNetDefPtr net, virQEMUCapsPtr qemuCaps,
                          int *vhostfd, size_t *vhostfdSize)
{
    size_t i;
    const char *vhostnet_path = net->backend.vhost;

    if (!vhostnet_path)
        vhostnet_path = "/dev/vhost-net";

    for (i = 0; i < *vhostfdSize; i++) {
        vhostfd[i] = open(vhostnet_path, O_RDWR);
    }
    virDomainAuditNetDevice(def, net, vhostnet_path, *vhostfdSize);
    return 0;
}
```

## 构造qemu参数

```c
if (qemuDomainSupportsNetdev(def, qemuCaps, net)) {
	if (!(host = qemuBuildHostNetStr(net, driver, ',', vlan, tapfdName, tapfdSize,
                                         vhostfdName, vhostfdSize)))
            goto cleanup;
        virCommandAddArgList(cmd, "-netdev", host, NULL);
    }
}

if (qemuDomainSupportsNicdev(def, net)) {
	if (!(nic = qemuBuildNicDevStr(def, net, vlan, bootindex, vhostfdSize, qemuCaps)))
            goto cleanup;
     virCommandAddArgList(cmd, "-device", nic, NULL);
} 
```

构造之后的参数如下：

```c
-netdev tap,fds=80:81:82:83:84:85:86:87,id=hostnet2,vhost=on,vhostfds=88:89:90:91:92:93:94:95 

-device virtio-net-pci,event_idx=on,mq=on,vectors=18,rx_queue_size=1024,tx_queue_size=1024,
netdev=hostnet2,id=net2,mac=20:90:6f:28:84:f2,bus=pci.0,addr=0x7 
```

