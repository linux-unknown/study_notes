# virsh

1. 查看运行的虚拟机
   virsh list
   
2. 查看所有的虚拟机（关闭和运行的虚拟机）
   virsh list --all
   
3. 连接虚拟机

   virsh console 虚拟机名称

   当退出虚拟机console之后，会出现登录提示，可以ctrl+]推出

4. 退出虚拟机

   ctrl+]

5. 停止虚拟机

   virsh destroy domain

   强制停止该域，但是保留资源不变（类似于断电）

6. 从XML创建domain

   virsh create xxx.xml

   但是可以指定很多选项，比如是否启动，是否连接控制台

7. 从XMK定义一个domain 
   virsh define xxx.xml
   定义一个domain，但是不启动

8. undefine

   virsh undefine domain

   undefine一个活动的domain

   当domain在run状态，执行该指令后不会生效，当shutdown domain之后会自动生效。在virsh list --all看不到该domain。undefine命令不会删除镜像文件和xml文件。

9. 启动虚拟机并进入该虚拟机
   virsh start domain  --console
   
10. 象qemu monitor发送命令

    可以使用virsh的“qemu-monitor-command”命令来向Monitor发送命令
    不过，需要注意的是，最好加上“**--hmp**”参数（意为“human monitor command”），以便可以直接传入monitor中操作的普通命令，而不需要任何的格式转换。如果缺少“--hmp”，则monitor会期望接收json格式的命令，所以可能会遇到一些错误，如“internal error cannot parse json info kvm: lexical error: invalid char in json text”(参考http://smilejay.com/2012/12/virsh-use-qemu-monitor-command/

    )

    `virsh  qemu-monitor-command domain  --hmp info mtree`

