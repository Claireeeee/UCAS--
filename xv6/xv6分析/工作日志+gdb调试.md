## 工作日志



#### 9.1～9.3（周末+周一）

#### 基本熟悉了gdb的使用：已经可以在gdb中打开kernel，设置断点，控制qemu中xv6的运行

#### 9.4  周二

#### 重新分析静态代码：

启动部分（BIOS后～init运行，sh开启）

init进程的加载到待运行过程

普通进程的fork和exec

从int到iret调用exec的全过程

syscall系统调用的整体逻辑（包括读取参数，系统调用函数数组）

#### 9.5  周三

#### 在gdb中设断点，follow从启动到sh开启的整个执行过程：

主要取了以下几个比较重要的断点位置：

allocproc

mpmain

scheduler

swtch

forkret

trapret

进入trapret（要调用initcode.s的那个）后，stepi单步往下走，此处看到了中断调用exec（要转到init.c）的过程：vector——alltraps（一通压栈，call trap—syscall—exec—返回，trapret）

进入init后，又加了fork和exec断点，看到了init调用它们俩运行sh的过程

最后sh就启动了，等待命令

#### 重新看页表部分讲义，分析构建页表的代码（内核部分和用户部分）

#### 重新看x86寄存器和cpu工作过程

#### 9.6  周四

#### 重新看fork和exec调用过程中涉及的中断，内核态切换，tf维护，和返回用户态的过程

#### 总结了一下xv6的内存管理

物理内存空间结构，虚拟内存空间结构，内存调度

留了几个问题，比如kernbase之上的空间，除了内核文件和高地址的设备直接映射，中间还有其他用处吗？

#### 开始看中断部分

#### 然而这种概念性的知识一看就刹不住……中断，锁，调度，文件系统全看了一遍。明显感觉比刷第一遍的时候，消化了好多东西

明天一部分一部分的分析细节吧，想把中断那部分先跳过去（有涉及硬件的部分……留到最后看吧），先看文件系统，然后锁，调度（调度我应该有一个初步了解了，过几天再系统整理一下），最后再看设备中断和磁盘驱动那部分

#### 9.7  周五

#### 详细看文件系统

从底层块缓冲开始，看到一半看不下去……回去补了磁盘驱动

块缓冲层看完

#### 9.8  周六

日志层

ionde层

#### 9.9  周日

目录层，路径解析，文件描述符层

开始分析系统调用层

看完了link，unlink，create，open

#### 9.10  周一

#### 继续分析系统调用层

与文件系统相关的syscall全部分析完毕，其中关于锁的部分没有深究

到sys_pipe的时候，回去先看了pipe部分。pipe部分分析完毕。

#### 9.11  周二

截止到昨天把文件系统看完了。我看了一下文件系统章节后面的练习题，发现我这第一遍看的还是很浅，只是考虑了函数的主要功能，panic，bad和锁的那些分支很多都略过去了。

#### 复习了前几天看的磁盘驱动和文件系统的整体过程

但是我还是不想看细节，我会再回来二刷的，可能顺便还会调试，到时候会分析各种panic

现在想去看锁（整个文件系统到处都能看到锁，我每次都略过去，很想很想系统研究一下）

#### 锁

函数看完，去调度里看例子吧

#### 调度

大概看了一下，明天继续

#### 9.12  周三

#### 重新总结了操作系统，内核，中断，进程目前的进度

#### 然后发现发现调度这部分有很多疑问，比如要挂起时内核态的转换过程。

就去查资料，后来摸索着总结了中断处理，了解了call和调用门等切换特权级和堆栈的机制。一直到9.13周四下午。

#### 9.13  周四

#### 又去看调度的代码，继续研究锁

感觉放到具体的代码里分析，对锁的认知又深入了一些，锁这个概念变得更清晰

#### 9.14 周五

#### 分析proc.c中ptable锁的使用

#### 分析console的输出输入（留了两个函数还没看）

#### 9.15  周六

#### 分析文件系统中锁的使用机制

icache，bcache，inodelock

#### 9.17 周一

#### 接着console

分析input和crt缓冲区，两个缓冲区的read和write函数，梳理设备文件的处理过程

分析console，总结还存在的疑问

#### 9.18  周二

#### console笔记收尾，sh初识

#### 正式研究sh

sh的处理机制，指令语法识别和生成连接，runcmd的解析和执行，完毕。

#### 9.19  周三

#### sh的实际运行

#### grep

#### 9.20  周四

grep结束  vector

#### 9.21

系统时钟，tick。没看细节

#### 9.22

intel编程手册

二刷讲义

#### 9.23  24

无

#### 9.25

继续二刷讲义，只有前两节，后面就转到verilog了

## 9.26

verilog

## 9.27-10.1

零碎着二刷讲义

## 10.1

决定开始写五子棋

















在 `swtch` 中设断点。用 gdb 的 `stepi` 单步调试返回到 `forkret` 的代码，然后使用 gdb 的 `finish` 继续执行到 `trapret`，然后再用 `stepi` 直到你进入虚拟地址0处的 `initicode`。

`KERNBASE` 会限制一个进程能使用的内存量，在一台有着 4GB 内存的机器上，这可能会让人感到不悦。那么提高 `KERNBASE`的值是否能让进程使用更多的内存呢？



## 耶鲁课程  gdb介绍  把握逻辑关系



Remote debugging is a very important technique for kernel development in general: 

the basic idea is that the main debugger (GDB in this case) runs separately from the program being debugged (the xv6 kernel atop QEMU) - 

they could be on completely separate machines, in fact.

 The debugger and the target environment communicate over “some simple communication medium”, such as “a network socket or a serial cable”, and a small *remote debugging stub* handles the "immediate supervision" of the program being debugged in the target environment. 

This way, the main debugger can be a large, full-featured program running in a convenient environment for the developer atop a stable existing operating system, even if the kernel to be debugged is running directly on the bare hardware of some other physical machine and may not be capable of running a full-featured debugger itself

In this case, a small remote debugging stub is typically embedded（嵌入） into the kernel being debugged; 

the remote debugging stub implements a simple command language that the main debugger uses to inspect and modify the target program's memory, set breakpoints, start and stop execution, etc. 

Compared with the size of the main debugger, the remote debugging stub is typically miniscule, since it doesn't need to understand any details of the program being debugged such as high-level language source files, line numbers, or C types, variables, and expressions: it merely executes very low-level operations on behalf of the much smarter main debugger.

we will primarily use GDB with QEMU's remote debugging stub in this course.





To run xv6 under QEMU and enable remote debugging, type:

```
$ make qemu-gdb
```

Then QEMU initialized the virtual machine but stopped it before executing the first instruction, and is now waiting for an instance of GDB to connect to its remote debugging stub and supervise the virtual machine's execution.

To start the debugger and connect it to QEMU's waiting remote debugging stub, open a new, separate terminal window, change to the same xv6 directory, and type:

```
$ gdb kernel
```

GDB load the kernel's ELF program image so that it can extract the debugging information it will need, such as the addresses of C functions and other symbols in the kernel, and the correspondence between line numbers in xv6's C source code and the memory locations in the kernel image at which the corresponding compiled assembly language code resides. 



When remote debugging, *always* make sure that the program image you give to GDB is exactly the same as the program image running on the debugging target: if they get out of sync for any reason (e.g., because you changed and recompiled the kernel and restarted QEMU without also restarting GDB with the new image), then symbol addresses, line numbers, and other information GDB gives you will not make any sense. 



The GDB command 'target remote' connects to a remote debugging stub, given the waiting stub's TCP host name and port number. 

* In our case, the xv6 directory contains a small GDB script residing in the file .gdbinit, which gets run by GDB automatically when it starts from this directory. This script automatically tries to connect to the remote debugging stub on the same machine (localhost) using the appropriate port number: hence the "+ target remote localhost:25501" line output by GDB. 
* If something goes wrong with the xv6 Makefile's port number selection (e.g., it accidentally picks a port number already in use by some other process on the machine), or if you wish to run GDB on a different machine from QEMU (try it!), you can comment out the 'target remote' command in .gdbinit and enter the appropriate command manually once GDB starts.

Once GDB has connected successfully to QEMU's remote debugging stub, it retrieves and displays information about where the remote program has stopped:

As mentioned earlier, QEMU's remote debugging stub stops the virtual machine before it executes the first instruction: i.e., at the very first instruction a real x86 PC would start executing after a power on or reset, even before any BIOS code has started executing. 

We'll leave further exploration of the boot process for later; for now just type in the GDB window:

```
(gdb) b exec
```

These commands set a breakpoint at the entrypoint to the `exec` function in the xv6 kernel

continue the virtual machine's execution until it hits that breakpoint. You should now see QEMU's BIOS go through its startup process, after which GDB will stop again with output like this:

```
The target architecture is assumed to be i386
0x100800 :	push   %ebp

Breakpoint 1, exec (path=0x20b01c "/init", argv=0x20cf14) at exec.c:11
11	{
(gdb) 
```

At this point, the machine is running in 32-bit mode, the xv6 kernel has initialized itself, and it is just about to load and execute its first user-mode process, the /init program. You will learn more about exec and the init program later; for now, just continue execution:

```
(gdb) c
Continuing.
0x100800 :	push   %ebp

Breakpoint 1, exec (path=0x2056c8 "sh", argv=0x207f14) at exec.c:11
11	{
(gdb) 
```

The second time the exec function gets called is when the /init program launches the first interactive shell, sh

Now if you **c**ontinue again, you should see GDB appear to "hang": this is because xv6 is waiting for a command (you should see a '$' prompt in the virtual machine's display), and it won't hit the `exec` function again until you enter a command and the shell tries to run it. Do so by typing something like:

```
$ cat README
```

You should now see in the GDB window:

```
0x100800 :	push   %ebp

Breakpoint 1, exec (path=0x1f40e0 "cat", argv=0x201f14) at exec.c:11
11	{
(gdb) 
```

GDB has now trapped the exec system call the shell invoked to execute the requested command.

Now let's inspect the state of the kernel a bit at the point of this `exec` command.

**Turn in:** the output of the following GDB 'print' or 'p' commands, with which we can inspect the arguments that the `exec` function was called with:

```
(gdb) p argv[0]
(gdb) p argv[1]
(gdb) p argv[2]
```

**Turn in:** the output of the GDB 'backtrace' or 'bt' command at this point, which traces and lists the chain of function calls that led to the current function: i.e., the function that made this call to exec, the function that called that function, etc.

Now go "up" one call frame so we can inspect the context from which `exec` was called:

```
(gdb) up
#1  0x00103e96 in sys_exec () at sysfile.c:367
367	  return exec(path, argv);
(gdb)
```

**Turn in:** the output of the GDB 'list' or 'l' command at this point, showing the source code around sys_exec's call to exec.



```
VBoxManage clonehd "source.vmdk" "cloned.vdi" --format vdi
```

## linux  xv6+gdb

#### linux安装

1. sudo apt-get install git
2. sudo apt-get install gdb
3. sudo apt-get install qemu
4. gcc，g++

有自己带的包，但不是最新的，也可以下载安装包，运行里面的文件就行



#### gdb调试（已涉及）

gdb报错 segment fault

source /home/claire/xv6/.gdbinit



（make qemu-gdb）

gdb kernel

file xxx

info break

p a





```
$ make qemu-gdb
*** Now run 'gdb'.
qemu -parallel mon:stdio -smp 2 -hdb fs.img xv6.img -s -S -p 25501
QEMU 0.10.6 monitor - type 'help' for more information
(qemu)
```

```
$ gdb kernel
GNU gdb (GDB) 6.8
Copyright (C) 2009 Free Software Foundation, Inc.
...
Reading symbols from /Users/ford/cs422/xv6/kernel...done.
+ target remote localhost:25501
The target architecture is assumed to be i8086
[f000:fff0] 0xffff0:	ljmp   $0xf000,$0xe05b
0x0000fff0 in ?? ()
(gdb) 
```

even though we're not going to run the kernel locally under GDB, we still need to have GDB load the kernel's ELF program image so that it can extract the debugging information it will need, such as the addresses of C functions and other symbols in the kernel, and the correspondence between line numbers in xv6's C source code and the memory locations in the kernel image at which the corresponding compiled assembly language code resides. That is what GDB is doing when it reports "Reading symbols from ...".

When remote debugging, *always* make sure that the program image you give to GDB is exactly the same as the program image running on the debugging target

 the xv6 directory contains a small GDB script residing in the file .gdbinit, which gets run by GDB automatically when it starts from this directory.

If something goes wrong with the xv6 Makefile's port number selection (e.g., it accidentally picks a port number already in use by some other process on the machine), or if you wish to run GDB on a different machine from QEMU (try it!), you can comment out the 'target remote' command in .gdbinit and enter the appropriate command manually once GDB starts.

Once GDB has connected successfully to QEMU's remote debugging stub, it retrieves and displays information about where the remote program has stopped:

```
The target architecture is assumed to be i8086
[f000:fff0] 0xffff0:    ljmp   $0xf000,$0xe05b
0x0000fff0 in ?? ()
```

然后gdb就可以控制qemu里面的运行了

As mentioned earlier, QEMU's remote debugging stub stops the virtual machine before it executes the first instruction: i.e., at the very first instruction a real x86 PC would start executing after a power on or reset, even before any BIOS code has started executing. 



#### gdb调试（其他命令）



gcc gdb-sample.c -o gdb-sample -g

在上面的命令行中，使用 -o 参数指定了编译生成的可执行文件名为 gdb-sample，使用参数 -g 表示将源代码信息编译到可执行文件中。如果不使用参数 -g，会给后面的GDB调试造成不便。当然，如果我们没有程序的源代码，自然也无从使用 -g 参数，调试/跟踪时也只能是汇编代码级别的调试/跟踪



程序的调试过程主要有：单步执行，跳入函数，跳出函数，设置断点，设置观察点，查看变量。 

程序的运行结果和预期结果不一致，或者程序出现运行时错误。 

调试的基本思想是： 
分析现象 -> 假设错误原因 -> 产生新的现象去验证假设





gcc –g gdb_example.c –o gdb_example

gdb gdb_example

list  (gdb) list 15  (gdb) list main 

`list`，显示当前行后面的源程序  `list -` ，显示当前行前面的源程序

r

b

怎么从一个开始de的文件里退出呢？

next  step

c

p a

watch  c（只有用next跟着，c声明之后才能用）

`examine命令`（缩写为x）来查看内存地址中的值：(gdb) x/s 0x100000f2e 

将第一个字符改为大写：

```
(gdb) p *(char *)0x100000f2e='H'  
$4 = 72 'H'  
```

set

* 修改寄存器：

  ```
  (gdb) set $v0 = 0x004000000  
  (gdb) set $epc = 0xbfc00000   
  ```

* 修改内存：

  ```
  (gdb) set {unsigned int}0x8048a51=0x0  
  ```



