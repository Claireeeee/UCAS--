## P5



1. 看看大家的验收现象

发：tcpdump的接收情况，打印多少行？——目前这个打印情况，应该不怪我的代码

- 怎么解决？——看看大家的现象，不行问问老师

收：

- pkt程序运行有问题，包没有发成功
- 操作系统的包攒到64个，能中断并打印——代码应该没问题



- pkt软件的问题：尝试解决，不行问老师





2. 设计文档



























sudo tcpdump -i enx00e04c360a38 host 224.0.0.251

dd if=~/ahome/start_code_P5/image of=disk conv=notrunc

sudo tcpdump -i tap0 -n –xx 

cd pktRxTx/
​make
sudo ./pktRxTx -m 1 





Requirements for design review (40 points) 

- –  What are the memory addresses of TX/RX descriptors? Please give an example about the contents of filled TX/RX descriptors? Which registers do you use to handle DMA process? 

* –  How many packets can you receive in task 1? 

* –  What is the procedure for receiving data in task 

  2? 





* –  What is the procedure of your MAC interrupt handler? 
* –  What is the procedure for transmitting data? 



#### 中断处理流程：

```c
#define INT1_SR 0xbfd01058       
#define INT1_EN 0xbfd0105c

初始化时：
  cp0_status的IM域：IM7~IM2置1，中断使能
  mac中断使能：INT1_EN = 0x00000100;
  其他寄存器赋值（防止无限进中断
  	INT1_CLR = 0xFFFFFFFF;
    INT1_POL = 0xFFFFFFFF;
    INT1_EDGE = 0;


触发：最后一个包接收完成，触发mac中断
  仅把最后一个接收描述符的des1的31位置零（其他都置1）；

  
中断入口exception_handler：
->检查EXCCODE（0,int）

  ->handle_int: 检查IP   (IP3=1,外设中断)

    ->外设中断: 检查INT1_SR  (第3位为1, mac中断)

      ->中断处理函数mac_irq_handle: 唤醒+清中断(clear interrupt)

```



#### Transmitting流程

  test文件中准备好了一个包（一个1kb的buffer，只填了头）
  用这个buffer的地址初始化所有发送描述符的des2
  在调用do_net_send后就能完成发送

描述符初始化：

```c
  //mac结构初始化：描述符表地址，send buffer地址
    mac->td = tdes;
    mac->td_phy = mac->td & 0x1fffffff;  //unmap区物理地址就是虚址高位置零
    mac->saddr = buffer;
    mac->saddr_phy = mac->saddr & 0x1fffffff;
    
    desc_t *td = (desc_t *) mac->td;
    int i=0;
    for(;i<PNUM;i++){
        (td+i)->des0 = 0x00000000;
        //29&30置1：一个buffer就是一帧
        //24置1：des3中为下一个描述符的地址
        (td+i)->des1 = (0 | (1<<30) | (1<<29) | (1 << 24) | (1024))
        //64个描述符传输的都一个buffer中的数据
        (td+i)->des2 =  buffer & 0x1fffffff;
        (td+i)->des3 = (td+i+1) & 0x1fffffff;  //下一个描述符的地址
    }
    //最后一个描述符：25位也要置1
    //发送不触发中断
    (td+PNUM-1)->des1 = (0 | (1<<30) | (1<<29) | (1 << 25) | (1 << 24) | (1024));
    //des3中放首个描述符的地址
    (td+PNUM-1)->des3 = (uint32_t) td & 0x1fffffff;
```

测试文件调用do_net_send

```c
do_net_send(uint32_t td, uint32_t td_phy)
{
    //发送描述符的首物理地址（DMA寄存器4填入）
    reg_write_32(DMA_BASE_ADDR + 0x10,td_phy);
    //使能 MAC 传输和接收（mac 第 0 寄存器的第 2 位和第 3 位设置为 1），
    reg_write_32(GMAC_BASE_ADDR, reg_read_32(GMAC_BASE_ADDR)|(1<<3));
    //配置 DMA 第 6 寄存器、DMA 第 7 寄存器。
    reg_write_32(DMA_BASE_ADDR + 0x18, reg_read_32(DMA_BASE_ADDR + 0x18) | 0x02202000); 
    reg_write_32(DMA_BASE_ADDR + 0x1c, 0x10001 | (1 << 6));

    //一次send调用发送64（PNUM）个包
    int i=0;
    for(;i<PNUM;i++)
      //在发送和接收前，每个描述符的 OWN 位需置 1（硬件处理完毕后置0）
      //用不用先判断一下上次传输是否完成？
        //while(((td+i*sizeof(desc_t)))->des0)&0x80000000)则等待？
        ((desc_t *)(td + sizeof(desc_t)*i))->des0 = 0x80000000;
    for(i=0;i<PNUM;i++)
      //DMA 寄存器 1 写入PNUM次 1（触发 DMA 处理这PNUM个包
        reg_write_32(DMA_BASE_ADDR + 0x4, 0x1);
}
```



















































```c
do_net_send(uint32_t td, uint32_t td_phy)
{
    //DMA寄存器4填入发送描述符的首物理地址
    reg_write_32(DMA_BASE_ADDR + 0x10,td_phy);
    //mac 第 0 寄存器的第 2 位和第 3 位设置为 1，使能 MAC 传输功能和接收功能
    reg_write_32(GMAC_BASE_ADDR, reg_read_32(GMAC_BASE_ADDR)|(1<<3));
    //配置 DMA 第 6 寄存器、DMA 第 7 寄存器。
    reg_write_32(DMA_BASE_ADDR + 0x18, reg_read_32(DMA_BASE_ADDR + 0x18) | 0x02202000); //0x02202002); // start tx, rx
    reg_write_32(DMA_BASE_ADDR + 0x1c, 0x10001 | (1 << 6));

    //you should add some code to start send packages
    int i=0;
    for(;i<PNUM;i++)
      //在发送和接收前，每个描述符的 OWN 位需置 1（硬件置0，代表传输完成
        ((desc_t *)(td + sizeof(desc_t)*i))->des0 = 0x80000000;
    for(i=0;i<PNUM;i++)
      //DMA 寄存器 1写入1（发送 DMA 控制器将读取
        reg_write_32(DMA_BASE_ADDR + 0x4, 0x1);
}

do_net_recv(uint32_t rd, uint32_t rd_phy, uint32_t daddr)
{
   //DMA寄存器3填入接收描述符的首物理地址
    reg_write_32(DMA_BASE_ADDR + 0xc, rd_phy);
   //mac 第 0 寄存器的第 2 位和第 3 位设置为 1，使能 MAC 传输功能和接收功能
    reg_write_32(GMAC_BASE_ADDR, reg_read_32(GMAC_BASE_ADDR) | GmacRxEnable); 
    //配置 DMA 第 6 寄存器、DMA 第 7 寄存器。
    reg_write_32(DMA_BASE_ADDR + 0x18, reg_read_32(DMA_BASE_ADDR + 0x18) | 0x02200002);
    reg_write_32(DMA_BASE_ADDR + 0x1c, 0x10001 | (1 << 6));
    
    //you should add some code to start recv and check recv packages
    //printkf("\n");
    int i=0;
    for(;i<PNUM;i++){
        //在发送和接收前，每个描述符的 OWN 位需置 1（硬件置0，代表传输完成
        ((desc_t *)(rd + sizeof(desc_t)*i))->des0 =0x80000000;
    }
    for(i=0;i<PNUM;i++)
      //DMA 寄存器 2写入1（接收 DMA 控制器将读取
        reg_write_32(DMA_BASE_ADDR + 0x8, 0x1);
}
```





## P4

exec 1 0x6ffffff0 0x0 0x80 0x6ffffff0 0x0 0x80

exec 1 0x50800060 0x60500000 0x78050000 0x50800060 0x60500000 0x78050000

exec 1 0x60 0x500 0x800 0x60 0x500 0x800

target remote localhost:50010

## review 老师提醒

1. D位：TLB的，页表的，cache的，作用都是标记修改，但是标记的层次不一样，要理清楚

不替换的话，脏位没什么用，直接置1了

- TLB脏位(dirty)：内存和硬盘：内存块更新到硬盘再替换，或直接覆盖。（有过内存写则脏）

  现在的计算机基本都是使用虚拟存储器，简单来说就是假如你要打开一个很大的程序，它不会把所有的文件都加载进内存。当需要用的内容不在内存上时，它再去硬盘上找并加载到内存。故脏位的作用就是，当内存上的某个块需要被新的块替换时，它需要根据脏位判断这个块之前有没有被修改过，如果被修改过，先把这个块更新到硬盘再替换，否则就直接替换。

- 页表项脏位：也是内存和外存：物理内存回收时，内存内容是否更新到磁盘

  这个标志位只对file backed的page有意义，对anonymous的page是没有意义的。当page被写入后，硬件将该位置1，表明该page的内容比外部disk/flash对应部分要新，当系统内存不足，要将该page回收的时候，需首先将其内容flush到外部存储。之后软件将该标志位清0。

- catch的脏位：更新的内存



2. 页表项的查找，空闲物理页框的查找，最好都不要用遍历，比如页表项可以用虚址右移12位的hash函数，寻找空闲的物理页框，最好也实现一个算法——结构链表
3. 页表不用那么大（那么多项），一些就够了，比如如果你只用前几个页，不用分配那么多项——8M（）







代码添加，makefile修改与替换

sche中的test2删除

TLB是硬件，通过在初始化时填TLB实现页表添加？





物理空间划分为页框：

- 页框大小



页表（存放映射：

- 初始化的时候，在内存留好存放页表的空间
- 表项设计与填充
- 页表是需要查找的：索引算法



TLB初始化：

- tlb操作相关的指令，和寄存器使用



任务1:

- 填充页表和TLB
- PMON 和操作系统内核代码，所在的物理地址，为虚拟地址去掉高位的地址：例如 0xa0800000 对应 物理地址 0x800000
- 选择要映射的物理地址时避免与 PMON 代码或操作系统内核代码相冲突。 



















## P4 design review

* –  What is the virtual memory range of the test process mapped by the test process? 

  测试程序映射的虚拟地址的范围

  - 代码段地址是从entrypoint（内核中，0xa0800000之后）开始，内核栈也设置成unmapped空间中的一段
  - 用户栈就用自己mapped的，如果栈只用一个4kb的页的话，栈顶可以设置成0xffc，在页表中填充对应的项，映射到一个物理地址

* –  Show the data structure of your page table. Where do you place the page table? 

  - 每个进程的页表：
    - 一个整型数组；
    - 每个页4kb，如果要映射4G的话，就需要2^20个项，页表大小为4M
  - 页表项：
    - 4byte整型
    - 参考TLB：高6位空闲，物理地址20位（4kb对齐），C3位（cache），D1位（脏位），V1位（有效），G1位（全局）
    - 往低了排，用不完就空着
  - 页表存放位置：
    - 在内核中，像维护ptable一样维护一个页表数组

  ```c
  uint32_t pte[PAGE_TABLE_NUM][PTE_ENTRY_NUMBER];   //页表数组作为内核中全局变量
  
  //页表项填充
  vaddr = frame_alloc_func();
  pte[pid][vpage_number] = ((vaddr&0xfffff000)>>6) | PTE_C | PTE_D | PTE_V;
  
  #define PTE_C 0x10
  #define PTE_D 0x4
  #define PTE_V 0x2
  #define PTE_G 0x1
  ```

  

* –  What are the initialized values for PTEs in tasks 1 and 2 respectively? How many initialized PTEs in both tasks 1 and 2? 

  - 任务1中只测试3个虚拟地址，对应索引为0x100，0x120，0x200的页表项，理论上只需要填充这三个pte就够
  - 任务2还要求用户栈分配在mapped空间内，用户栈虚址对应的页表项也要填

* –  How do you handle TLB invalid exception? Please show the sample code/pseudo code for this process

  - TLB无效：

  ```c
  void TLB_invalid(index){  //异常的TLB项的index
      vaddr =pcb[pid].user_context.cp0_badvaddr;
      page_number = vaddr>>12;
      //在任务3还需检查页表项是否有效
      //    if((pte[pid][vpage_number]&0x2)==0)  {分配物理地址并填充}
    
      //TLB填充
      if(page_number%2){
          //奇数
          tlbwi_func(pte[pid][page_number-1],pte[pid][page_number],index);
      }else{
          //偶数
          tlbwi_func(pte[pid][page_number],pte[pid][page_number+1],index);
      }
      return ;
  }
  
  LEAF(tlbwi_func)
      mtc0    a0, CP0_ENTRYLO0
      mtc0    a1, CP0_ENTRYLO1
      li      t0, PAGE_MASK
      mtc0    t0, CP0_PAGEMASK
      mtc0    a2, CP0_INDEX
      nop
      tlbwi
      jr      ra
  END(tlbwi_func)
  ```

* –  Considering your page fault handler, where do you allocate physical page frames 

  1. 物理页框管理：

  - 物理页框用一个char型数组(也可以用整型，或者结构数组。目前感觉char就可以)管理：页框大小4kb，物理地址从0xf00000到0x2000000，32M，共2^13项
  - 初始化时每项都置零，代表空闲
  - 分配函数代码如下

  ```c
  frame_alloc_func(){
      for(;i<PAGE_FRAME_NUMBER;i++){
          if(frame[i]==0) {
              frame[i]=1;
              return (char *) (0xf00000+0x1000*i);
          }
      }
      printk("frame empty");
      return NULL;
  }
  ```

  - 如果要考虑回收的话，就需要用结构数组管理了，结构中保存页框的状态等信息

  2. 缺页处理：

  ```c
  if((pte[pid][vpage_number]&0x2)==0){  //页表项无效
      vaddr = frame_alloc_func();       //分配页框
    //如果没有空闲页框，报错或实现替换都可以
      pte[pid][vpage_number] = ((vaddr&0xfffff000)>>6) | PTE_C | PTE_D | PTE_V;
  }
  ```



#### 为什么有些虚址可以不经过tlb转换，这个在哪设置的

这是硬件规定好的，高地址就是直接映射，能给虚存系统的物理空间并不是全部























## p3 2

理思路真的很重要

写设计文档可以理思路

——设计文档可以在代码前写





在malibox中，用户代码，不能直接调用scheduler

我们的scheduler中没有加什么锁

如果某中断下，sche中又发生了中断，再次到entry，cli，save user就会覆盖原来的返回地址，中断就不能完整完成

——中断一定要完整，否则回不到原来的代码

：sche中不能开放中断：那所有可运行程序都在睡眠，等着时钟中断加elapse好醒来，如果sche一直循环的话，就是死锁

所以sche找不到能运行的程序，要退出来，退只能退回现在要切走的程序中，退出来后时钟中断恢复，sleep可以完成

这就要求恢复后的要切走的进程，直接恢复运行不会出错：

- int切，没问题
- 





























小飞的代码没问题，一点一点替换后发现

- 要运行小飞的代码需要修改boot.s和common，都在在ahome下面了









新的进程加上来（9，10，11），还是有epc变为29的问题

——检查中断流程







凡是调用系统调用之后又进入scheduler的，都要记得dosche前自己epc+4

- 其实doscheduler有保存kernel上下文，调度后恢复，恢复后jr ra
- 这时候要保证ra是内存态的ra——**时钟中断后跳的不是内核ra，是直接用用户态的退出了**





* –  How do you handle CV, semaphores, and barrier? What to do if timer interrupt occurs? 
* –  Show the structure for mailbox. How do you protect concurrent accessing for mailbox? 



有互斥锁阻塞的情况下，条件变量的意义在哪里

条件变量机制总是在一个进程间共同协作的变量的使用上发挥作用，它把需求方需要反复加锁检查条件是否满足的这种冗余行为，简化为了一次判断不满足就放弃，更改方发出者在每次修改后顺便检查，符合条件则唤醒等待的进程

——优化了互斥锁的冗余收放



条件变量总是和锁配合使用：因为调度进程行为的那个变量是一个共享变量，它的访问和修改需要锁的



target remote localhost:50010

p pcb[0].user_context.cp0_epc



在计算机领域，同步就是指一个进程在执行某个请求的时候，若该请求需要一段时间才能返回信息，那么这个进程会一直等待下去。直到收到返回信息才继续执行下去。异步是指进程不需要一直等待下去，而是继续执行下面的操作，不管其他进程的状态，当有消息返回时，系统会通知进程进行处理，这样可以提高效率。











```

Temporary breakpoint 5, set_cp0_status () at ./arch/mips/kernel/entry.S:273
273	    jr      ra
(gdb) s

Temporary breakpoint 6, enable_interrupt () at ./test/test_shell.c:19
19	}
(gdb) s
test_shell () at ./test/test_shell.c:126
126	        if(gotcom){
(gdb) n
77	        int i=0, gotcom=1;
(gdb) 
79	        disable_interrupt();
(gdb) 
81		ch=read_uart_ch();
(gdb) 
82	        if((ch!='\0') && (ch!= 13))   //判断有无指令，如果没有，跳过解析进入下一轮循环
(gdb) 
85	            if(ch==8   ){//bs（backspace
(gdb) 
92	                buff[i++] = ch;
(gdb) 
93	                screen_write_ch(ch);   //实时显示
(gdb) 
94	                screen_reflush();
(gdb) 
98	            while(((ch=read_uart_ch()) != 13) && i<10)    //13是回车符（enter键直接映射为13的/r（而不是/n
(gdb) 
100	                if(ch=='\0') continue;
(gdb) 
101	                if(ch==8   ){  //backspace
(gdb) 
108	                buff[i++] = ch;
(gdb) 
109	                screen_write_ch(ch);
(gdb) 
110	                screen_reflush();         
(gdb) 
98	            while(((ch=read_uart_ch()) != 13) && i<10)    //13是回车符（enter键直接映射为13的/r（而不是/n
(gdb) 
113	            buff[i] = '\0';
(gdb) 
114	            screen_write_ch('\n');
(gdb) 
115	            screen_reflush();
(gdb) 
123	        enable_interrupt();
(gdb) 

Breakpoint 1, scheduler () at ./kernel/sched/sched.c:41
41	    	current_running->cursor_x = screen_cursor_x;
(gdb) p/x pcb[0]
$18 = {kernel_context = {regs = {0x0, 0x0, 0x30008001, 0x48, 0x30008001, 
      0xa0fffe2c, 0xa0fffe2c, 0x0 <repeats 22 times>, 0xa0ffff98, 0xa0ffff98, 
      0xa08007e0}, cp0_status = 0x30008002, hi = 0x0, lo = 0x0, 
    cp0_badvaddr = 0x0, cp0_cause = 0x0, cp0_epc = 0xa0800910, pc = 0x0}, 
  user_context = {regs = {0x0, 0x0, 0x30008001, 0x48, 0x30008001, 0xa0fffe2c, 
      0xa0fffe2c, 0x0 <repeats 22 times>, 0xa0ffff98, 0xa0ffff98, 0xa0805354}, 
    cp0_status = 0x30008002, hi = 0x0, lo = 0x0, cp0_badvaddr = 0x0, 
    cp0_cause = 0x40008000, cp0_epc = 0xa0800910, pc = 0x0}, 
  time_slice = 0x4c4b40, kernel_stack_top = 0xa2000000, 
  user_stack_top = 0xa1000000, prev = 0x0, next = 0x0, pid = 0x0, type = 0x2, 
  status = 0x2, cursor_x = 0xf, cursor_y = 0x1d, sleepto = 0x0, 
  priority = 0x1, name = 0xa08062c0, mutex_lock = {0x0, 0x0, 0x0, 0x0, 0x0, 
    0x0, 0x0, 0x0, 0x0, 0x0}, waited = 0x0, wait = 0x0}
(gdb) tb *0xa0800910
Temporary breakpoint 7 at 0xa0800910: file ./arch/mips/kernel/entry.S, line 273.
(gdb) n
42	    	current_running->cursor_y = screen_cursor_y;
(gdb) 
44	    check_sleeping();
(gdb) 

Temporary breakpoint 7, set_cp0_status () at ./arch/mips/kernel/entry.S:273
273	    jr      ra
(gdb) 
warning: GDB can't find the start of the function at 0xa0ffff67.

    GDB is unable to find the start of the function at 0xa0ffff67
and thus can't determine the size of that function's stack frame.
This means that GDB may be unable to access that stack frame, or
the frames below it.
    This problem is most likely caused by an invalid program counter or
stack pointer.
    However, if you think GDB should simply search farther back
from 0xa0ffff67 for code which looks like the beginning of a
function, you can increase the range of the search using the `set
heuristic-fence-post' command.
check_sleeping () at ./kernel/sched/sched.c:26
26	    while(sh != NULL){
(gdb) 
36	}
(gdb) 
warning: GDB can't find the start of the function at 0xa0ffff68.
0xa0ffff68 in ?? ()
(gdb) info r
          zero       at       v0       v1       a0       a1       a2       a3
 R0   00000000 00000000 00000000 00000000 a08069a0 a080814c a0fffe2c 00000000 
            t0       t1       t2       t3       t4       t5       t6       t7
 R8   00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 
            s0       s1       s2       s3       s4       s5       s6       s7
 R16  00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 
            t8       t9       k0       k1       gp       sp       s8       ra
 R24  00000000 00000000 30008003 00000001 00000000 a0ffff68 a0ffff20 a0ffff68 
            sr       lo       hi      bad    cause       pc
      30008001 00000000 00000000 00000144 0000000c a0ffff68 
           fsr      fir
      00000000 00739300 
(gdb) p/x pcb[0]
$19 = {kernel_context = {regs = {0x0, 0x0, 0x30008001, 0x48, 0x30008001, 
      0xa0fffe2c, 0xa0fffe2c, 0x0 <repeats 22 times>, 0xa0ffff98, 0xa0ffff98, 
      0xa08007e0}, cp0_status = 0x30008002, hi = 0x0, lo = 0x0, 
    cp0_badvaddr = 0x0, cp0_cause = 0x0, cp0_epc = 0xa0800910, pc = 0x0}, 
  user_context = {regs = {0x0, 0x0, 0x0, 0x0, 0xa08069a0, 0xa080814c, 
      0xa0fffe2c, 0x0 <repeats 22 times>, 0xa0ffff48, 0xa0ffff48, 0xa0802068}, 
    cp0_status = 0x30008002, hi = 0x0, lo = 0x0, cp0_badvaddr = 0x144, 
    cp0_cause = 0xc, cp0_epc = 0xa0800910, pc = 0x0}, time_slice = 0x4c4b40, 
  kernel_stack_top = 0xa2000000, user_stack_top = 0xa1000000, prev = 0x0, 
  next = 0x0, pid = 0x0, type = 0x2, status = 0x2, cursor_x = 0x0, 
  cursor_y = 0x1d, sleepto = 0x0, priority = 0x1, name = 0xa08062c0, 
  mutex_lock = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0}, 
  waited = 0x0, wait = 0x0}
(gdb) p *current_running

```











首先锁获取的过程是一个syscall，不会被时钟中断打断

获取完毕，持有锁的时候发生时钟中断：

- 目前没有屏蔽持有锁的时候的中断
- 也没有中断时释放锁的机制



- xv6是在持有锁的时候不允许发生任何中断，屏蔽信号是累加的



目前实验测试没有前后两个进程按不同顺序申请锁的情况，（不考虑死锁也可以

- 只有信号量值上的一把锁

在因信号量wait的时候，会释放信号量对应的锁，再阻塞后重新调度

- 因为返回都用的epc，而信号量的使用都用syscall，epc保存的就是syscall那条，调度算法加上来后又是直接去找epc，所以未完成流程（用doscheduler切走）的syscall记得自己epc+4





封装为系统调用保证了同步机制的各种操作都是原子的





信号量是一个更宽的锁，它可以有更大的值n，这个n允许n个进程并发使用信号量保护的值

n=1的时候就是一个普通的锁

但是值修改的原子性谁来保护？

——如果信号量的值大于1的话，测试程序那么写不能避免写覆盖错误



测试程序只是把二元信号量用了一下

n大于1时信号量有更结构的功能，如下：

- n对应资源的数量，
  - 每次修改时sem的原子操作
  - P无时自动阻塞，V等时自动唤醒
  - P、V由不同进程执行（信号量就是用来协作的）
- 比如生消模型：队列（是规模为n的资源池），生，消
  - 空、满都是限制，生、消都要被限制——需要两个信号量（空槽满槽），两个进程互改
  - 生P空槽，V满槽，消P满槽，V空槽





barrier就是，调同一系列进程到一个进度点

- 每个进程执行完毕后，调用barrierwait，拉高一个value，检查是否到齐（到达goal），阻塞，或置零并唤醒所有







原语使用时不用考虑时钟中断，都封装成了系统调用（原子操作）

获取之后

























```c
      typedef struct semaphore{
        int index;  //in block queue
        int value;
        queue_t queue;
      } semaphore_t;

      void do_semaphore_init(semaphore_t *s, int val){
        s->value=val;
        queue_init(&s->queue);
        s->index=blockqueue_register(&s->queue);
      }
      void do_semaphore_up(semaphore_t *s){
        s->value += 1;
        if(s->value<=0)
        do_unblock_one(&s->queue);
      }
      void do_semaphore_down(semaphore_t *s){
        s->value -= 1;
        while(s->value < 0){
        do_block(&s->queue);
        do_scheduler();
        }
      }




          typedef struct condition{
            int index;  //in block queue
            queue_t queue;
          } condition_t;

          void do_condition_init(condition_t *condition){
            queue_init(&condition->queue);
            condition->index=blockqueue_register(&condition->queue);
          }
          void do_condition_wait(mutex_lock_t *lock, condition_t *condition) {
              do_mutex_lock_release(lock);   //need to release lock
              do_block(&condition->queue);
              do_scheduler();
              do_mutex_lock_acquire(lock);
          }
          void do_condition_signal(condition_t *condition){
              do_unblock_one(&condition->queue);
          }
          void do_condition_broadcast(condition_t *condition){
              do_unblock_all(&condition->queue);
          }
```

































锁的初始化可以交给进程吗

如果可以的话，就会重复用尽

初始化之后，进程退出的时候，要释放锁列表吗

锁列表需要进程释放吗，还是os清理



关键现在是没人回收初始化好的锁，锁就会占用锁表空间

确实

——最后改成了init的时候检查一下要申请的锁是否已经有了os锁队列（用锁的阻塞队列地址，在我们这几个进程中，这个地址是固定的）







kill中的锁是静态变量，在编译的时候就建好了，对应的阻塞队列也建好了

在初始化的时候，就是把这个队列的地址放到os的全局阻塞队列表中

第一次初始化，放在了12

第二次，因为12不为空，放在了34，但是12中还留着这个队列的地址，所以在遍历os的队列的时候，从12中还是能输出这个锁队列的内容，所以block的进程就会被输出两遍



可以在每次初始化的时候，前面的地址是否和这个锁的地址相同，如果已经有了，就不要再分配了











## P3

——就体系结构的实现来看，cp0用的不是内存，cp0寄存器也是有硬件实现的

为什么栈空间乱用会导致epc错误？

——局部变量可能会用到栈，比如print location这个变量就保存在栈中

——







用户态和内核态的栈目前不区分也没有错误——

- cp0：在内核态和用户态都一样，即使有重写，写的也是一样的值
- 其他数据：进入entry后到保存内核栈之间，现在没有cp0以外的栈操作（以后可能会有——这是个隐患

进程启动前会把要用的栈清空







因为cp0是用的内存，是栈空间——所以cp0被更改有可能是栈空间被修改

——就体系结构的实现来看，cp0用的不是内存，cp0寄存器也是有硬件实现的

- 被自己的内核态修改
  - cp0这几个寄存器，在内核态和用户态都一样，即使有重写，写的也是一样的值
  - 除了cp0，其他的数据呢？进入entry后到保存内核栈之间，现在没有cp0以外的栈操作（以后可能会有
- 被其他进程修改：其他进程是否可以访问到别人的栈——是不是用了同一个栈？

我们的栈没分配好，导致第一个用户进程和shell会用同一个栈，导致cp0数据被更改

进而出现epc跳转错误和cursor保存错误等

是栈的重叠使用









有些在栈里保存的数据没有被保护（在内核中运转的时候被改写）

- 比如cursor



epc在某个时间点变为256:

- 保存和恢复流程应该没问题：之前的时钟中断和系统调用都处理的很好
- 那还有哪里修改了epc呢？——没有哪里修改过了呀
- 还是真的进入过256？——加断点了，可是没测到
- 看一下新添加的函数？有没有可能是有些代码不规范，改了shell的epc？——可能通过访问pcb改

进程切换几下后就会卡

- 换回shell的时候，epc是256，会一直产生缺页异常
- epc不该是256：哪里保存错了？

shell打印总是延迟3行：

- 是os用的命令行是80*30，ubuntu默认24行，调一下就好了









晚上遇到的问题：

- 用了去年的makefile，内存函数地址除了问题
- 该过来之后，shell程序初始化的时候入口没放好，跳不过去，gdb跟踪发现了
- 修改之后，可正确跳到shell，但是识别好像有问题



 target remote localhost:50010







shell

同步原语

mailbox



design review：

* Which commands can be supported or will be supported by your shell? 
* Show example code about spawn, kill, wait, and exit? 
* How do you handle the case when killing a task meanwhile it holds a lock? 



* How do you handle CV, semaphores, and barrier? What to do if timer interrupt occurs? 
* Show the structure for mailbox. How do you protect concurrent accessing for mailbox? 



lock不要16个行不行

锁变成了16个，阻塞队列也变成了16个？不要吧，还用一个吧（要不然

给锁加个编号



在锁结构里，可以找到对应的queue

要不要就看到这，够review得了











Shell结构如下

1. 初始化终端输出区（下半块屏幕
2. 循环读取指令
   - 检查是否有输入
   - 若有，读取到commad buffer
   - 解析buffer

```c
void test_shell()
{
    //分配输出区
    sys_move_cursor(0,SCREEN_HEIGHT/2);
    printf("---------------------HELLO:)--------------------");
    //用户名和输入提示
    int print_location = SCREEN_HEIGHT-1;
    sys_move_cursor(0,print_location);
    printf(">root@UCAS_OS: ");

//要支持的指令：ps，clear，exec x，kill x
    while (1)
    {
        char ch;
        char combuff[20];    //暂存指令
        int gotcom=1;
        disable_interrupt();
      
        int i=1;
				ch=read_uart_ch();
        if((ch!='\0') && (ch!= 13))   //判断有无指令，如果没有，跳过解析进入下一轮循环
        {
            //如果非空
            //处理刚刚读出的字符
            if(ch==8   ){//backspace
                    i--; 
                    screen_write_ch(ch);
                    screen_reflush();
                    continue;
            }
            else{
                combuff[i++] = ch;    //存入缓存结构
                screen_write_ch(ch);   //实时显示
                screen_reflush();
            }

            //读取一行指令（空格记入缓冲区；回车符结束
            while(((ch=read_uart_ch()) != 13) && i<10)  //13是回车符（enter键直接映射为13
            {
                if(ch=='\0') continue;
                if(ch==8   ){  //backspace
                    i--; 
                    screen_write_ch(ch);
                    screen_reflush();
                    continue;
                }

                combuff[i++] = ch;
                screen_write_ch(ch);
                screen_reflush();         
            }
            //一条指令读取结束
            combuff[i] = '\0';
            screen_write_ch('\n');
            screen_reflush();
        }
        else    //没有指令输入
        {
            gotcom = 0;
        }
      
        enable_interrupt();

        //解析指令
        if(gotcom){
          //检查长度
            if(i==20){
                sys_move_cursor(0, print_location);
                printf("Such a long command！！！\n");
            }
          //要支持的指令：ps，clear，exec x，kill x
            if(combuff[0]=='p' && combuff[1]=='s') 
                sys_ps();
            else if(combuff[0]=='c'&& combuff[1]=='l'&&combuff[2]=='e'&&combuff[3]=='a'&&combuff[4]=='r') 
                sys_clear();
            else if(combuff[0]=='e'&&combuff[1]=='x'&&combuff[2]=='e'&&combuff[3]=='c')
            {
                int tasknum=0,j=5;
                while(combuff[j]>='0' && combuff[j]<='9')
                    tasknum = tasknum*10 + combuff[j++]-'0';
                printf("Loading task[%d]\n",tasknum);
                sys_spawn(test_tasks[tasknum]);
            }
            else if(combuff[0]=='k'&&combuff[1]=='i'&&combuff[2]=='l'&&combuff[3]=='l')
            {
                int tasknum=0,j=5;
                while(combuff[j]>='0' && combuff[j]<='9')
                    tasknum = tasknum*10 + combuff[j++]-'0';
                printf("Killing process[%d]\n",tasknum);
                sys_kill(tasknum);
            }
            else
            {
                printf("Undefined Command :( \n");
            }
            sys_move_cursor(0,print_location);
            printf(">root@UCAS_OS: ");
        }
    }
}
```







## 修改：

是板子上的IPL不能用，每次syscall也会清时钟

关于重新设一个do-scheduler2，并且还需要jal，还需要3个参数，0，sp之列的，不知道为什么，但是要按这个格式

试了一下timer里面reflush，但是效果没有放在write后面好











时间片太小，一直在一个进程里不出来，我觉得是错过了

卡在任务123：可能是closeint中导致的错过

卡在8、9、10中的飞机



因为打印完syscall完也没有清时钟，所以可能会错过

：增大时间片试一试：确实有效果

卡在印刷：每次sys系统调用都重置时间片，所以可能一直占着cpu

syscall错过时间片+没有时钟中断就不flush==错过很多打印，屏幕像是卡着



那怎么办：syscall过程中cli可能会错过时间片：

确实会，但是调到合适的大小，错过的几率没那么大，可以接受

合适：板子上2000，5000都可以





 target remote localhost:50010







初始化开中断，最后3位要是011



长的变态的切换，可能是因为count超过了compare

是因为在打印的时候关了中断，而count还在增，所以超了compare却没有产生中断，所以要到溢出后的下一次——这需要时间片大一些才能避免



第一行不显示是因为初始化的时间片太小了，任务1没运行上——一开始的调大就好了



如果把count和compare的修改放在exit，就会卡顿，或者在一个任务停留很久



不仅要修改count，还要compare

只有时钟中断需要修改，其他时候（syscall）不能修改，不然就会卡在一个上，但是出错原理我还没弄明白

最后方案是：照常所有doshed后进exceptionexit，exit中用IP判断，只有timeinter更新count和compare













中断可以

queue的使用是正常的

而且打印乱码：飞机打印，被中间中断，覆盖终端——在打印的始末加中断打开和关闭



看一下中断的保存流程：小飞和学长的设计文档和代码



中断流程和启动流程







## P2 第二部分

1. 例外处理、时钟中断处理、抢占式调度、计数函数
2. 系统调用、sleep 系统调用方法

#### 例外处理：

流程：

- 硬件跳转：异常地址放入 CP0_EPC 寄存器，然后自动跳转到一个固定的例外处理入口： 0x80000180 
- 例外处理程序：
  - 关中断（修改 CP0_STATUS 寄存器），保存现场
  - 用户态到内核态
  - 种类确定
    1. CP0 的 cause 寄存器中 ExcCode 位域【6:2】，0代表中断
    2.  CP0_STATUS 寄存器中的IP7~IP0 的 8 位域【15:8】，10000000代表时钟中断
  - 跳转到代码处理：
  - 恢复现场，开中断，eret指令返回（返回 CP0_EPC 所指向的地址

#### 时钟中断

：基于时间片中断，在例外处理部分进行任务的切换

触发：

- CP0_COUNT、CP0_COMPARE 寄存器
- CP0_COUNT每个时钟周期会自动增加，当和 CP0_COMPARE相等时，触发一个时钟中断

处理：

轮询式抢占式中断

基于优先级的抢占式调度

#### 系统调用

睡眠



### 任务1

- main.c 中 init_exception：异常处理相关的初始化内容
  - 将例外处理代码拷贝到例外处理入口
  - 初始化例外向量表
  - 初始化 CP0_STATUS、CP0_COUNT、CP0_COMPARE 等异常处理相关寄存器。
- entry.S 中 exception_handler_entry：例外处理入口相关内容
  - 关中断、保存现场
  - 跳转到中断处理函数(handle_int)：根据 CP0_CAUSE寄存器的例外触发状态
- entry.S 中 handle_int：
  - 跳转到中断向量处理函数(interrupt_helper 方法)
  - 恢复现场、开中断
- irq.c 中interrupt_helper和irq_timer
  - 中断处理的相关代码：中断向量处理，时钟中断处理
- sched.c 中 scheduler：基于优先级和等待时间
  - 优先级越高，等待时间越长
- 修改test.c 中 sched1_tasks数组中的三个任务(去除 do_scheduler 方法的调用)
  - 要求打印出和任务1一样的正确结果

我们自己实现的例外入口函数(exception_handler_entry)拷贝到该指定例外入口



流程：中断触发——（硬件跳转至此）exception_handler_entry（保存现场，跳转入）——handle_int——（interrupt_helper）——irq_timer（具体代码）



### 任务2

- main.c 中 init_syscall
- //syscall.S 中 invoke_syscall：调用 syscall 指令发起一次系统调用
- //entry.S 中 exception_handler_entry：跳转到handle_syscall
- //entry.S 中 handle_syscall 方法、syscall.c 的 system_call_helper 方法：根据系统调用号选择要跳转的系统调用函数进行跳转
- sched.c 中 do_sleep 方法、check_sleeping 方法，实现系统调用 sleep 方法。
- 完善用户态打印函数库 printf
- 实现 lock 的系统调用
- 运行给定的测试任务(test.c 中 timer_tasks 数组中的三个任务、test.c 中 sched2_tasks 数组中的三个任务)，要求打印出正确结果



























内核用内核上下文或栈，用户用用户上下文或栈

调试的时候多试几个时间片的值



















## P2 Review

```
typedef struct pcb
{
    /* register context */
    regs_context_t kernel_context;
    regs_context_t user_context;//what is user_context
   
    uint32_t kernel_stack_top;
    uint32_t user_stack_top;

    /* previous, next pointer */
    void *prev;
    void *next;

    /* process id */
    pid_t pid;

    /* kernel/user thread/process */
    task_type_t type;

    /* BLOCK | READY | RUNNING */
    task_status_t status;

    /* cursor position */
    int cursor_x;
    int cursor_y;

//锁之类的？
    //sleep
    uint32_t deadline;

    //priority
    uint32_t priority;
} pcb_t;
```









## P2



### bugs：

1. 用gdb找出的那个

		li    $a0,0xa0800200
		lbu   $a2,0x1ff($a0)

2. 仔细梳理了queue进出的过程，上下文寄存器们保存恢复的过程
3. 意识到执行过程没问题，可能是打印覆盖了，调整了一下scheduler（初始current处理，状态转换等），可以打印静态飞机
4. 开始测试locktask，打印乱码，avoid了好久进gdb，看到运行的是locktask2文件里的函数，里面涉及到1的循环运行，没有手动调scheduler，需要的是抢占式——切换为1文件，输出语句交替打印成功
5. 但是没有飞起来的飞机，意识到lock中没有打印飞机的行为，看到sche2测试文件，以为这个可以飞，想换，结果还是乱码，意识到这个对应的还是抢占式，无意间看sche1，发现里面也在手动插scheduler，意识到不是运行完就停止的——调整scheduler，running 中切回来了就重放回readyqueue，不exit，飞机正常飞起来



全是无意间，被blessed的bug发现啊









实验1静态可以正常打印

实验二是乱码

：实验二任务自动加载了locktask2，需要抢占式的那个——删除2后编译报错——1、2都放成1



实验二中间无故调用dosche，我们的机制下它会退出，无法再开始：修改，不要退出，放回ready

测试任务问题：test.c中只放了两个任务，如果从文件1（locktask1）加载，只是两行打印的交互，如果从文件二加载，函数用的不对（锁的函数用的是系统调用的）（而且也只是打印语句）





实测：

cp0是一些特殊的寄存器，表示一些处理器的状态和设置用的

建议做一个空的pcb代表kernel，初始化current_running的时候让它指向这个pcb





gdb：

- 当前文件夹下可执行文件
- gcc生成可执行文件的时候 -g 符号表



Use `si`

display /i $pc























分两个部分

这次布置第一部分



createimage是否可以处理大内核（暂时并不



压缩成一个，加readme





P2 part1：kernel需要支持：

1. 进程切换（上下文），多进程
2. 加锁，释放





kernel支持：

任务调度：任务阻塞与唤醒，线程锁

- 实现进程控制块、进程切换(现场保存、现场恢复)、非抢占式调度
- 任务的各种状态以及转化方式，实现任务的阻塞、唤醒、互斥锁





1. 

- kernel/entry.S：内核中需要汇编实现的部分，涉及任务切换，异常处理等内容，本次实验任务 1,2,3,4 的补全部分 

- locking/lock.c：锁的实现，本次实验任务 2 补全

- sched/sched.c：任务的调度相关，一个任务的调度、挂起、唤醒等逻辑主要在这个文件夹下实现，本次实验任务1,2,3，4 补全 





2. 

- sched.h 中 PCB 结构体设计
- kernel.c 中 init_pcb 的 PCB 初始化方法
- entry.S 中的 SAVE_CONTEXT、RESTORE_CONTEXT 宏定义
  - 将当前运行进程的现场保存在current_running 指向的 PCB 中
  - 将 current_running 指向的 PCB 中的现场进行恢复
- sched.c 中 scheduler 方法，使其可以完成任务的调度切换
- 运行给定的测试任务
  - (test.c 中 sched1_tasks 数组中的三个任务)
  -  printk_task1、printk_task2 任务在屏幕上方交替的打印字符串“*This task is to test scheduler*”
  - drawing_task1 任务在屏幕上画出一个飞机，并不断移动。



3. 互斥锁：忙时加入阻塞队列，锁被释放后唤醒

- sched.c 中的 do_unblock 方法、do_block 方法，要求其完成对线程的挂起和解除挂起操作
- 实现互斥锁的操作(位于 lock.c 中):：初始化(do_mutex_lock_init)、申请(do_mutex_lock_acquire)、释放
  (do_mutex_lock_release)方法
  - schedule重写：一个任务执行 do_block 时因为被阻塞，需要切换到其他的任务，因此涉及到任务的切换，因此需要保存现场，重新调度，恢复现场
- 运行给定的测试任务(test.c 中 lock_tasks 数组中的三个任务)，
  - 打印出给定结果:两个任务轮流抢占锁，抢占成功会在屏幕打印“*Hash acquired lock and running*”，抢占不成功会打印“*Applying for a lock*”表示还在等待。







注意：

- 保存现场的时候需要保存所有(32 个)通用寄存器的值
- 现场返回时请考虑使用什么指令完成，具体可以了解 ra 寄存器以及 jr 指令的作用
- 任务在初始化的时候需要为其分配栈空间，栈的基地址可以
  自行决定，推荐0xa0f00000
- ![Screen Shot 2019-09-21 at 10.46.58 AM](/Users/caowanlu/Desktop/Screen Shot 2019-09-21 at 10.46.58 AM.png)





































































## P1 bonus



加载过内核之后占用bootblock的位置，原来的代码无法执行完：

bootblock中剩余的代码：跳转到kernel的入口

解决方法：在加载之后返回时，返回地址直接设置为内核入口







































## 操作系统实验

最后写55aa





### de过程

/开头的是绝对路径

～是linux的当前用户路径

.是当前路径

..是上层路径

mkdir

cat：把文件内容输入到终端

less：打开到终端并可以翻页，q退出

（两个都是只查看不修改

grep：两个参数，字符串+文件名，在文件中查找这个字符串

| 管道符：位于两个命令间，将前一个命令的输出作为后一个命令的输入

修改数据权限：chmod  (+, -)r, w, x  filename     eg：chmod +x a.sh

复制文件/文件夹  cp filename path  /cp -r path path 

删除文件/文件夹  rm filename/ rm -r filename

移动： mv file path（在当前路径下，mv filename filename-changed， 可实现重命名







createimage完成

剩引导和kernel

引导和kernel完成







#### boot

bootloader：把kernel从sd加载到内存

print函数和read函数：内置在PMON，就像一开始加载bootloader的代码一样，通过这个地址进入PMON中内置好的代码段。这些段都烧在芯片上，属于BIOS内容

BIOS把bootloader从sd卡加载到a0800000，位于第一个扇区

bootloader调用BIOS中内置的read函数，将sd卡第二个扇区上存放的kernel加载到内存，这个位置是a0800200

bootloader跳转至kernel处运行，从此进入kernel



#### image

**ISO文件**是指包含光盘完整信息的映像文件。通常ISO文件被用于在网络上传输整张光盘的完整信息。 要得到ISO文件，需要对整张光盘的所有扇区按顺序复制下来，每个扇区的大小为2048字节。对于只有一个轨道的数据光盘而言，这种复制方式可以将光盘上的所有信息都保存下来。但是对于其它一些光盘，如音乐光盘、混合模式光盘、CD+G光盘、多区段光盘等，由于在扇区的子通道还存在一些必要的信息，**ISO**格式并不能完整地复制这些光盘，这时我们需要将光盘复制为BIN/CUE格式。

通常，对**ISO文件**的处理有以下几种方式：刻录到光盘、加载到虚拟光驱、使用PowerISO查看或展开其中的内容。

任务：

* 写bootloader：调函数打印，加载内核，跳转到内核
* 写kernel：打印，循环
* 写



在计算机领域内，往往指的是，对应的一个文件，该文件包含了相关的软件的二进制文件。

而相关的软件，往往指的是：操作系统。

而操作系统，有的指的是普通的桌面级的，比如Win7，也有的指的是嵌入式领域内的，比如嵌入式Linux

所以，你可能会看到这样的说法：

* 给我个Win7镜像，我拿去刻盘

  此处的镜像Image，指的是就是：

  对于微软开发的Win7这个桌面操作系统来说，往往都是对应的.iso文件

  是可以刻录到光盘（或者启动U盘中模拟出来的光盘CDROM）中。

  然后用词光盘，就可以启动笔记本电脑，然后按照正常的操作步骤去安装Win7这个操作系统了。

  即：

  桌面级操作系统的（往往是ISO的，可刻录用于启动和安装系统的）镜像文件

* 通过其他把Linux的image烧录到板子上去

  此处的image，就是指的是：

  在嵌入式Linux系统开发过程中，已经把Linux源码==编译成为Linux的二进制文件==了。

  ->这个二进制文件，就是对应某嵌入式开发板的真正运行时候所需要去运行的嵌入式Linux系统。

  ->即嵌入式版本的Linux镜像文件。





#### 预处理，编译，汇编，连接——makefile





#### 为什么只读取一个程序头？（segment header）

因为只有一个

要注意区分段(segment)和节(section)的概念，这两个概念在后面会经常提到。
我们写汇编程序时，用.text，.bss，.data这些指示，都指的是section，比如`.text`，告诉汇编器后面的代码放入.text section中。
目标代码文件中的section和section header table中的条目是一一对应的。section的信息用于链接器对代码重定位。



fflush的真正作用就是立即将缓冲区中的内容输出到设备。正因为这样，所以只能在写入文件的时候使用fflush。



#### sd扇区无法修改

解决方法：执行下partprobe 命令

 

```
   partprobe包含在parted的rpm软件包中。partprobe可以修改kernel中分区表，使kernel重新读取分区表。 因此，使用该命令就可以创建分区并且在不重新启动机器的情况下系统能够识别这些分区。
```

查看是否安装该命令：

[root@db1 dev]# rpm -q parted

parted-1.8.1-23.el5

 

我们执行一下该命令：

[root@db1 dev]# partprobe

Warning: Unable to open /dev/hdc read-write (Read-only file system).  /dev/hdc has been opened read-only.

 

然后在格式化，就ok了：



注意：

虚拟机设备里面没选择usb

——（但是后来sd还是一直是0，写不进去）



#### 镜像一直不正确打印

是小飞的地址问题





老师找到原因了，是我的，需要选择一下usb



#### qemulongson

dd if=/dev/zero of=disk bs=512 count=1M

dd if=image of=disk conv=notrunc

chmod +x qemu/bin/qemu-system-mipsel

sh run_pmon.sh

怎么退出qemu



## gdb

gdb-multiarch 

(gdb) set arch mips
(gdb) target remote localhost:50010
(gdb) c



p/x pcb[0].user_context.cp0_epc





b，info b， d

c

si，s，ni，n

l

p，p $eip

info r

display/i $pc  delete display

x（查看内存）

- x/3uh 0x54320表示，从内存地址0x54320读取内容，h表示以双字节为一个单位，3表示三个单位，u表示按十六进制显示（u可以替换为s（字符串）或i（指令））





命令：

b 变量名/星号+地址：设断点

c：continue

si：单步

i r：查看内存和寄存器

q：退出

空：默认重复上一条指令

查看内存

* x/i（i for instruction）：addr：查看当前地址的命令
* x/i：地址：查看此地址的命令
* x/10i：从此开始的10条指令
* x/x：当前地址数据按16进制输出



## mips汇编

 lw reg, var 

这里var变量会解析成一个内存地址，地址里存的就是它在data段的赋值

所以这句话的意思是，把value的值赋给reg

```
jal   printstr
```

所以jal printstr，会跳到值所在的地址，而不是值对应的那个地址



## C：强制类型转换，地址调用函数

((void (*)()) (add ))();

函数指针的声明形式：函数就是一个void指针
void (*pFunction)(), 没有参数的情况下也可写成void (*pFunction)(void)的形式。

那么pFunction函数指针的原型就是
void (*)(void)，即把变量名去掉，

因此，对于一个给定的entry地址，要把它转换成为函数指针，就是
(void (*) (void))addr

对于函数指针的调用，ANSI C认为 pFunction()和*pFunction()都是正确的，所以
((void (*) (void))(add)();
就形成一个函数调用。

