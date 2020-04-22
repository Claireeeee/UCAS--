目录：

1. 内存管理（分页）
2. 进程管理
3. 进程调度
4. 中断
5. 文件系统
6. 锁
7. 启动过程



## 内存管理

### 内存分配

xv6通过维护一个freelist实现内存分配：

- freelist：一个空闲页的链表，所有进程共用，需要锁保护（除了kinit1）
- kalloc从freelist中分配的是某页的虚拟地址（物理地址加kernbase），在往页表中放的时候会手动减去kernbase

操作函数kfree，kalloc：

```c
void kfree(char *v)
{
  struct run *r;

//检查v是否在合理范围，是否对齐一页
  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)   
    panic("kfree");

//释放前junk
	memset(v, 1, PGSIZE);

  if(kmem.use_lock)        //kmem锁（kinit1的时候不用）
    acquire(&kmem.lock);
  r = (struct run*)v;       //转换为run结构地址
  r->next = kmem.freelist;  //放回freelist
  kmem.freelist = r; 
  if(kmem.use_lock)
    release(&kmem.lock);
}
```

​	kalloc与kfree类似

初始化函数：

- kinit1：用kfree构建一个“end（内核结束）～4M”的freelist，没有用到锁
- kinit2：将“4M～PHYSTOP”添加到freelist，用锁保护

### 页表

#### 页表结构

二级结构，pde构成的页目录和pte构成的二级页目录，页目录和页大小都是4KB，pde和pte大小都是4Byte，都是1024个，索引值需要10位（启动时用的entrypgdir映了一个4M的页）

#### 分页寻址过程

偏移（虚拟地址）到线性地址：段寄存器里有段选择子，用前13位查表（gdt）得到段选择符，选择符里有物理段基。xv6的gdt中段基都是0，所以偏移就是线性地址，段寄存器只是保存了特权级等相关信息。

线性地址到物理地址：前10位查页目录，中间10位查二级目录，得到物理基址，最后12位为页内偏移（页是分配的最小单位，页内映射是连续的）

相关过程由mmu硬件实现。

#### 页表构建：发生在

1. 最初直接声明的entrypedir

2. kvmalloc（mian.c中，用来替换entrypedir）：
   - setupkvm：构建内核态映射

3. userinit（手动建立第一个进程的页表）：
   - setupkvm：先映射内核态部分
   - inituvm：构建inocode部分页表映射（并加载进来）

4. fork（复制父进程的页表）：
   - copyuvm：内核态映射+复制父进程内存（保证父子进程虚拟内存内容相同，而映射到不同的物理地址）

5. exec（构建子进程自己的页表）：
   - setupkvm：内核态部分
   - allocuvm：构建进程页表

主要函数：

- setupkvm，inituvm，copyuvm，allocuvm
- 流程都是：建立页目录，确定并验证虚拟地址，按虚拟地址构建页表项，分配物理空间（kalloc），填充页表项（在页表中建立va和pa的映射）（mappages）

核心工具函数有两个：walkpgdir和mappages

```c
//在pgdir中找到（或建立）va对应的pte
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)];  //PDX取出va中的页目录索引部分
  if(*pde & PTE_P){       //如果pde可用
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));   //取出基址
  } 
  else {         //建立一个ptble（填充一个pde）
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)
      return 0;
    memset(pgtab, 0, PGSIZE);   //置零
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U; //填入信息
  }
  return &pgtab[PTX(va)];   //用va中pte表索引部分找到pte
}
```

```c
//构建va到pa的一段页表映射
mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  char *a, *last;
  pte_t *pte;

  a = (char*)PGROUNDDOWN((uint)va);  //va对齐取下界
  last = (char*)PGROUNDDOWN(((uint)va) + size - 1);
  for(;;){           //一页一页的映射：把pa放到va对应的pte中
    if((pte = walkpgdir(pgdir, a, 1)) == 0) 
      return -1;
    if(*pte & PTE_P)
      panic("remap");
    *pte = pa | perm | PTE_P; 
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}
```

内核态页表的内容：

- 0～240M（phystop）：加kernbase（2G）。其中从低到高分别为IO空间，内核文件，和内存
- DEVSPACE（4G-16M）～4G：直接映射
- 其他都没映

```c
static struct kmap {
  void *virt;
  uint phys_start;
  uint phys_end;
  int perm;   //使用权限 R W等
} 
kmap[] = {
 { (void*)KERNBASE, 0,             EXTMEM,    PTE_W}, 
 { (void*)KERNLINK, V2P(KERNLINK), V2P(data), 0},    
 { (void*)data,     V2P(data),     PHYSTOP,   PTE_W}, 
 { (void*)DEVSPACE, DEVSPACE,      0,         PTE_W}, 
};
```

#### 页表转换

- lcr3指令，用新页目录地址更新%cr3
- 发生在exec或者进程恢复的时候（scheduler中交换上下文之前）

## 进程管理

维护一个ptable：

- 一个进程结构数组，有64项
- 属于共享数据，加了锁，后面介绍锁的时候有分析。

proc结构

```c
struct proc {
  uint sz;                     // Size (bytes)
  pde_t* pgdir;                // Page table
  char *kstack;                // Bottom of kernel stack 
  enum procstate state;        // Process started
  int pid;                     // Process ID
  struct proc *parent;         // Parent process
  struct trapframe *tf;        // 一个tf
  struct context *context;     // 上下文（调度的时候用）
  void *chan;                  // 睡眠
  int killed;                  // killed
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name
};

```

### 创建进程

#### fork：

1. allocproc：

```c
allocproc(void){
  struct proc *p;         
  char *sp;
  acquire(&ptable.lock);  //锁

//找一个空结构
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;
  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;    //分配pid

  release(&ptable.lock); //ptable使用完毕，释放锁

  // 内核栈，一页（4K）
  if((p->kstack = kalloc()) == 0){  
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;   //内核栈顶

  //内核栈
  //放trapframe
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;
  
  //放tarpret
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  //放上下文（调度的时候用）
  sp -= sizeof *p->context;    
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  
  //上下文的eip放forkret
  p->context->eip = (uint)forkret;   

  return p;
}
```

2. copyuvm，复制父进程的页表，前面已介绍

3. 复制父进程的其他信息

4. ```c
     
     np->sz = curproc->sz;
     np->parent = curproc;
     *np->tf = *curproc->tf;
        
     //使子进程中fork返回0
     np->tf->eax = 0;
        
     //ofile
     for(i = 0; i < NOFILE; i++)
       if(curproc->ofile[i])
         np->ofile[i] = filedup(curproc->ofile[i]);
     np->cwd = idup(curproc->cwd);
     
     //name
     safestrcpy(np->name, curproc->name, sizeof(curproc->name));
        
     //pid是自己的
     pid = np->pid;
     ```

   ```
5. 修改状态

6. c
   acquire(&ptable.lock);
   np->state = RUNNABLE;
   release(&ptable.lock);
   ```

7. return pid



#### exec：

加载文件，填充内存空间，构建页表，修改程序信息

```c
exec(char *path, char **argv)
{
    
    *************1，加载文件，构建页表*****************
        
        
  ...//省略一些变量声明
  begin_op();  //文件系统相关的函数会在文件系统部分介绍

//找到要加载的inode
  if((ip = namei(path)) == 0){
    end_op();   
    cprintf("exec: fail\n");
    return -1;
  }
  ilock(ip);  //锁
  
//加载文件头并检查
  if(readi(ip, (char*)&elf, 0, sizeof(elf)) != sizeof(elf))   
    goto bad;
  if(elf.magic != ELF_MAGIC)
    goto bad;

//开始构建页表：先映射内核态的部分
  if((pgdir = setupkvm()) == 0)  
    goto bad;

//加载文件，构建页表。一个ph一个ph的来
  sz = 0;
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){   
      if(readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
  //检查程序段
    if(ph.type != ELF_PROG_LOAD) //是否可加载？
      continue;
    if(ph.memsz < ph.filesz)     //内存大小和文件大小
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr) //这是？
      goto bad;
  //构建该段的页表映射
    if((sz = allocuvm(pgdir, sz, ph.vaddr + ph.memsz)) == 0)
      goto bad;
    if(ph.vaddr % PGSIZE != 0)//程序段的虚拟地址应该对齐一页？
      goto bad;
  //加载程序段（往页表映射好的地址里）
    if(loaduvm(pgdir, (char*)ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  iunlockput(ip);
  end_op();
  ip = 0;

  

    ****************2，用户栈填充************************
        
        
sz = PGROUNDUP(sz);   //sz记录的应该是当前进程使用的最高虚拟地址
    
//分两个页，一个空着（没有页表映射）隔离，一个作用户栈
  if((sz = allocuvm(pgdir, sz, sz + 2*PGSIZE)) == 0)
    goto bad;
  //clear低地址的页的PTE_U位
  clearpteu(pgdir, (char*)(sz - 2*PGSIZE));
  //高地址的页作为用户栈
  sp = sz; 

//用户栈填充
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
  //参数压栈，并记录每个参数的地址
    sp = (sp - (strlen(argv[argc]) + 1)) & ~3;  
    //（清空后两位，4字节对齐）
    if(copyout(pgdir, sp, argv[argc], strlen(argv[argc]) + 1) < 0)   
      goto bad;
    ustack[3+argc] = sp;//记录参数指针
  }
  ustack[3+argc] = 0;      //最后一位放空指针

  ustack[0] = 0xffffffff;  //系统调用不用返回，这里pc占个位就行）
  ustack[1] = argc;        //参数个数
  ustack[2] = sp - (argc+1)*4;  // argv pointer  参数指针的地址
  
  //ustack压栈
  sp -= (3+argc+1) * 4;
  if(copyout(pgdir, sp, ustack, (3+argc+1)*4) < 0) 
    goto bad;

//进程的栈都是这样的结构（从低到高：pc，参数个数，参数地址指针，参数）


    *************3，修改proc信息，加载新页表*************
        
        
// Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(curproc->name, last, sizeof(curproc->name));

//更新进程信息：页表，sz，tf
  oldpgdir = curproc->pgdir;
  curproc->pgdir = pgdir;
  curproc->sz = sz;
  curproc->tf->eip = elf.entry;  // main
  curproc->tf->esp = sp;
  switchuvm(curproc);     //用当前proc更新cpu：tss中内核栈和页表
  freevm(oldpgdir);       //释放原页表的空间
  return 0;

 bad:
  if(pgdir)
    freevm(pgdir);
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return -1;
}
```

### 第一个进程：

userinit：

1. allocproc
2. 自己构建页表：内核态映射和inicode.S文件的地址映射
3. 自己填tf和其他proc信息

```c
userinit(void)
{
  p = allocproc(); 
  
//页表
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)  
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);  
  
//sz，tf，name
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));          
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER; 
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // initcode.S的entry

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  acquire(&ptable.lock);
  p->state = RUNNABLE;   
  release(&ptable.lock);
}

```

启动：mpmain

```c
mpmain(void)
{
  cprintf("cpu%d: starting %d\n", cpuid(), cpuid());
  idtinit();       //装载idt
  xchg(&(mycpu()->started), 1); //启动cpu？
  scheduler();     //启动调度器
}
```

进入inicode.S：

调度器启动我们建立好的进程，进程通过tarpret弹出tf中的参数返回用户态，进入eip中放好的initcode.S

```c
#手动启动一个exec
.globl start
start:
  pushl $argv         #exec参数压栈
  pushl $init         #路径压栈
  pushl $0            #caller的pc
  movl $SYS_exec, %eax     #系统调用参数放入寄存器
  int $T_SYSCALL           #中断，参数代表中断为系统调用        
```

（中断处理过程放后面介绍）

进入exec，加载init并执行

```c
//init.c
main(void)
{
  int pid, wpid;

//打开一个console文件，作为标准输入
  if(open("console", O_RDWR) < 0){
    mknod("console", 1, 1);  
    open("console", O_RDWR); 
  }
//打开标准输出和标准错误输出
  dup(0); 
  dup(0);  

  for(;;)   //为什么要一直循环？
  {
  //启动shell
    printf(1, "init: starting sh\n");   
    pid = fork();  //为了配合exec开shell
    if(pid < 0){
      printf(1, "init: fork failed\n");
      exit();
    }
    //子进程分支启动shell
    if(pid == 0){   
      exec("sh", argv);  
      //出错时才返回
      printf(1, "init: exec sh failed\n");  
      exit();
    }
    //父进程负责回收僵尸进程，直到shell关闭
    while((wpid=wait()) >= 0 && wpid != pid) 
      printf(1, "zombie!\n");
  }
}
```

## 进程调度

进程切换发生在内核态，由swtch函数切换某进程和scheduler的上下文

### 调度器scheduler函数：

- 遍历进程表找一个runable的进程，用其更新cpu中TSS中内核栈和页表，修改状态为running，swtch到进程的上下文继续执行
- 某个进程释放cpu：修改状态为runable，通过swtch转回scheduler
- scheduler从swach的下一步继续执行：切换回内核态的TSS和页表，释放锁，防止进程表没有runable进程时cpu空转，重新进循环

```c
scheduler(void)
{
  for(;;)
  {
    // 允许中断
    sti();

    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if(p->state != RUNNABLE)
        continue;
      c->proc = p;  
//用p修改cpu的TSS和页表
      switchuvm(p); 
      p->state = RUNNING;
//上下文切换
      swtch(&(c->scheduler), p->context);


//某进程释放cpu后转入scheduler：
      switchkvm();           //切换回内核态TSS和页表
      c->proc = 0;
    }
//循环之间释放锁，防止进程表没有runable进程时cpu空转
    release(&ptable.lock); 
  }
}
```

swach函数：

```c
.globl swtch
swtch:
//old和new上下文指针的地址放入eax和edx
  movl 4(%esp), %eax  
  movl 8(%esp), %edx

//当前进程上下文压栈

  pushl %ebp
  pushl %ebx
  pushl %esi
  pushl %edi

//此时esp刚好是保存好的old上下文的地址，放入eax中指针处
  movl %esp, (%eax)
//取出edx中new上下文指针，放入esp
  movl %edx, %esp

//加载new上下文
  popl %edi
  popl %esi
  popl %ebx
  popl %ebp
  ret   //pop IP
  
  /* 用栈中的数据，修改IP的内容，实现近转移
     CPU执行ret指令时，进行下面的两步操作：(=pop ip)
  		（1）(IP) = ((ss)*16 +(sp)) //栈顶地址ss:[sp]
  		（2）(sp) = (sp)+2    */
  	

```

### xv6进程释放cpu出现在：yield，sleep，exit

#### yield：时钟中断

```c
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;      //放弃一下当前进程的running
  sched();              //检查proc状态，调用swtch转到scheduler
  release(&ptable.lock);
}
```

#### sleep：睡眠

- sleep发生在：写入log区，读写磁盘，访问一些有sleeplock的数据，pipe读写时，后面相关部分有介绍

```c
sleep(void *chan, struct spinlock *lk)                 
{
  if(lk == 0)                   //修改sleep状态的时候要有锁
    panic("sleep without lk");

//睡眠时释放锁
  if(lk != &ptable.lock){
    acquire(&ptable.lock);
    release(lk);              //顺序可不可以反过来？
  }
//修改状态
  p->chan = chan;                
  p->state = SLEEPING;   

//转到scheduler
  sched();                     

//唤醒时     
  p->chan = 0;

//get原来的锁
  if(lk != &ptable.lock){
    release(&ptable.lock);
    acquire(lk);
  }
}
```

#### exit：退出

```c
exit(void)
{
    ...
//init进程不能exit
  if(curproc == initproc)
    panic("init exiting");

//关闭所有打开的文件
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

//清除当前目录
  begin_op();
  iput(curproc->cwd);             
  end_op();
  curproc->cwd = 0;

//子进程交给init
  acquire(&ptable.lock);
  wakeup1(curproc->parent);//唤醒wait的父进程完成剩余的清理
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc)
    {
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

//转到scheduler
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}
```

wait完成exit后的清理

```c
wait(void)
{
  acquire(&ptable.lock);
  for(;;)
  {
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
 //遍历ptable找子进程
      if(p->parent != curproc)
        continue;
      havekids = 1;
 //如果子进程已经退出，开始清理
      if(p->state == ZOMBIE)
      {
        pid = p->pid;
        kfree(p->kstack);
 //释放内核栈和页表空间
        p->kstack = 0;
        freevm(p->pgdir);
 //修改proc其他值，放回ptable
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

 //没有子进程，或父进程已被kill，直接退出
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

 //sleep，等待还没有退出的子进程
    sleep(curproc, &ptable.lock);
  }
}
```

kill和wake都是很简单的遍历ptable表改一下状态，不再详述。

## 中断（和系统调用）

### xv6的IDT

 `main` 中调用`Tvinit` ：设置了 `idt` 表中的 256 个表项，并将idt[i]连接到 vectors[i]上，vectors[i]中有该中断对应的处理代码

### 中断

int n：

- 从 IDT 中获得第 n 个描述符
- 检查 %cs 和描述符中记录的特权级，必要时保存 %esp 和 %ss 并更新
- 将 %eflags ，%cs 压栈， %eip 压栈，清除 %eflags 的一些位。设置 %cs 和 %eip 为描述符中的值

硬件可以通过IRQ产生一个中断信号，经APIC处理后发送给cpu，CPU检测到了中断请求信号后也会进行类似的处理：关闭中断响应，保存请求指令地址，查表（IDT）找到处理程序

### 中断处理程序：

xv6 使用一个 perl 脚本来产生“ IDT 表项指向的”中断处理函数入口点。每一个入口都会压入一个错误码，压入中断号，然后跳转到 `alltraps`

#### alltraps：

- 继续将寄存器压栈填充tf
- 修改%ds和%es
- call trap
- trap返回后trapret，返回到中断前

```c
.globl alltraps
alltraps:                         
# Build trap frame.
  pushl %ds
  pushl %es
  pushl %fs
  pushl %gs
  pushal               //ax，bx，cx和esp，ebp等众多寄存器压栈
  
# Set up data segments.
  movw $(SEG_KDATA<<3), %ax        
  movw %ax, %ds
  movw %ax, %es

# Call trap，此时tf=%esp       
  pushl %esp         //esp压栈（不压可以吗，怕过程中会不小心修改？）
  call trap
  addl $4, %esp      //esp弹出       

.globl trapret    //trap return
trapret:        //弹出alltraps保存的值
  popal
  popl %gs
  popl %fs
  popl %es
  popl %ds
  addl $0x8, %esp  //跳过trapno and errcode
  iret    //弹出int保存的值

```

#### trap：

- 根据错误号转入不同的处理程序（我只看懂了系统调用，时钟，和磁盘部分）

##### 系统调用：int触发，对应中断向量64，trap调用syscall函数处理

syscall函数：

- 由%eax确定系统调用号，通过函数指针数组调用相应的系统调用函数（这里就包括前面介绍的exec，fork等），返回结果放入%eax
- 系统调用的参数是通过几个工具函数 `argint`、`argptr` 和 `argstr`等获取的，函数思路都是通过tf->esp加地址偏移读取，此处不再详述

```c
syscall(void)
{
  int num;
  struct proc *curproc = myproc();

  num = curproc->tf->eax;       //syscall的编号
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    curproc->tf->eax = syscalls[num]();    //系统调用的返回值
  } 
  else 
  {
    cprintf("%d %s: unknown sys call %d\n",
            curproc->pid, curproc->name, num);
    curproc->tf->eax = -1;
  }
}

```

##### 时钟中断：

- 由时钟芯片触发，对应中断向量32
- trap直接处理：递增ticks的值，唤醒，调用yield使进程释放cpu

```c
//trap处理时钟中断
case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);                //tick
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
...
    
if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();
```

##### 磁盘中断：

- 磁盘触发（完成一次buf同步之后），trap调用ideintr处理

ideintr

```c
ideintr(void)             
{
  struct buf *b;

  acquire(&idelock);

//取出磁盘等待队列中的第一个buf（就是刚刚处理完的这个）
  if((b = idequeue) == 0){
    release(&idelock);
    return;
  }
  idequeue = b->qnext;      //更新队列

// 读磁盘到buf（outsl在idestart中）（读操作发生在中断后）
  if(!(b->flags & B_DIRTY) && idewait(1) >= 0)
    insl(0x1f0, b->data, BSIZE/4);  //4字节为单位读取128次

// 修改状态，唤醒这个buf上等待的进程
  b->flags |= B_VALID;
  b->flags &= ~B_DIRTY;
  wakeup(b);

// 再次启动磁盘处理队列中的下一个buf
  if(idequeue != 0)
    idestart(idequeue);

  release(&idelock);

```



## 文件系统

### 整体概述：

进程通过文件描述符访问资源（inode文件，管道，设备）

所有的文件访问（磁盘文件）最终都由outsl和insl两个语句落实。磁盘管理从buf，到inode，file，fd，通过对底层函数的层层包装形成每一层的抽象：越来越抽象的结构，及其操作接口函数。最终实现为FS相关的一个个系统调用

文件系统把磁盘分区：

[![figure6-2](https://gitee.com/senjienly/xv6-chinese/raw/master/pic/f6-2.png)](https://gitee.com/senjienly/xv6-chinese/raw/master/pic/f6-2.png)

boot区存储bootloader

super区保存磁盘信息

```c
struct superblock {
  uint size;         // Size of file system image (blocks)
  uint nblocks;      // Number of data blocks
  uint ninodes;      // Number of inodes.
  uint nlog;         // Number of log blocks
  uint logstart;     // Block number of first log block
  uint inodestart;   // Block number of first inode block
  uint bmapstart;    // Block number of first free map block
};
```

inode区和bitmap区管理inode和block，后面会详细介绍

data区存储实际数据

log区记录写操作，并整体完成转移，用于崩溃恢复

### 逐层介绍细节

#### 文件描述符：

* 一个文件在该进程的ofile表中的索引值
* 操作函数为fdalloc：往ofile数组中添加一个文件，返回索引

维护：file（filedup，fileclose，read，write），ftable（系统打开文件表，fileinit，filealloc），ofile（fdalloc）

#### 文件：

* 一个file结构（只是一个接口，核心是inode）

```c
struct file {
  enum { FD_NONE, FD_PIPE, FD_INODE } type;  //类型
  int ref;      // reference count
  char readable;    //访问模式
  char writable;
  struct pipe *pipe;      //实际数据
  struct inode *ip;
  uint off;           //偏移量
};
```

* 操作函数：filedup，fileclose，用于修改ref；read，write，需要调用下一层的相关函数落实
* 系统维护了一个文件结构表ftable，大小固定，100个，ref为0代表空闲，需要锁保护，操作函数有filealloc，返回一个空闲文件结构地址

#### inode：

* inode是文件的实体，在磁盘上有inode区编号管理，操作函数有ialloc，iput（ref=0时），分别用于分配和放回一个空闲inode
* inode结构，核心是一组block，最大为12+512/4=140个block

```c
//内存inode结构
struct inode {
  uint dev;           // 磁盘设备号
  uint inum;          // Inode编号
  int ref;            // Reference count
  struct sleeplock lock; // 锁
  int valid;          // 是否有效

//磁盘inode只有以下部分
  short type;         //类型：目录，文件，设备
  short major;        // 磁盘设备信息
  short minor;
  short nlink;        //？
  uint size;
  uint addrs[NDIRECT+1];     //包含的blocks
};
```

* 操作函数：iupdate：同步到磁盘；read，write：读写；idup：ref++；也需要下层函数，一个block一个block的落实
* 系统维护一个inode cache，大小为50，操作函数有init，iget，iput，需要锁保护

#### block，buf

* 文件系统把磁盘划分为一个个block（512byte）并编号，通过bitmap区统一管理，相关函数有balloc，bfree，用于分配或放回空闲块，思路都是从起始开始找一个空bit，该bit的索引值即使空闲block的编号
* buf在内存上，用于实现磁盘上一个block到内存的映射，结构如下：

```c
struct buf {
  int flags;     //buf的状态
  uint dev;      //设备号
  uint blockno;  //block号
  struct sleeplock lock;     //锁
  uint refcnt;         //refs
  struct buf *prev; // 在bcache链表中的位置
  struct buf *next;
  struct buf *qnext; // 磁盘请求队列中下一个buf
  uchar data[BSIZE]; //数据在内存中的地址
};
```

* 操作函数：iderw：与磁盘同步；bread，bwrite：读写，检查状态后通过iderw实现（实际只有log区相关的函数用了bwrite，其他调用读写都是用的log_write，后面会介绍）
* 系统维护一个buf cache，大小为30，用链表实现，为了提高使用效率，操作函数有bget，brelse

buf更新相关函数分析：

iderw：放到队列上，等待start

```c
iderw(struct buf *b)
{
  struct buf **pp;

  if(!holdingsleep(&b->lock))      //锁好才能更新
    panic("iderw: buf not locked");
//buf状态检查
  if((b->flags & (B_VALID|B_DIRTY)) == B_VALID)
    panic("iderw: nothing to do");
  if(b->dev != 0 && !havedisk1)
    panic("iderw: ide disk 1 not present");

  acquire(&idelock); 
//放到idequeue上，排队等待
  b->qnext = 0;
  for(pp=&idequeue; *pp; pp=&(*pp)->qnext)
    ;
  *pp = b;
//试一下是不是不用等
  if(idequeue == b)
    idestart(b);

//睡眠循环（每次唤醒队列的时候所有的buf都会唤醒，所以要一个循环来回睡）
  while((b->flags & (B_VALID|B_DIRTY)) != B_VALID){
    sleep(b, &idelock);
  }
  release(&idelock);
}

```

idestart：

```c
static void
idestart(struct buf *b)     
{
//检查buf信息
  if(b == 0)
    panic("idestart");
  if(b->blockno >= FSSIZE)
    panic("incorrect blockno");
//准备磁盘指令，一会放到磁盘寄存器上
  int sector_per_block =  BSIZE/SECTOR_SIZE;
  int sector = b->blockno * sector_per_block;
  int read_cmd = (sector_per_block == 1) ? IDE_CMD_READ :  IDE_CMD_RDMUL;
  int write_cmd = (sector_per_block == 1) ? IDE_CMD_WRITE : IDE_CMD_WRMUL;

  if (sector_per_block > 7) panic("idestart");  

  idewait(0);      //等待磁盘就绪
//往磁盘输入命令参数
  outb(0x3f6, 0);  // generate interrupt
  outb(0x1f2, sector_per_block);  // number of sectors
  outb(0x1f3, sector & 0xff);
  outb(0x1f4, (sector >> 8) & 0xff);
  outb(0x1f5, (sector >> 16) & 0xff);
  outb(0x1f6, 0xe0 | ((b->dev&1)<<4) | ((sector>>24)&0x0f));
  if(b->flags & B_DIRTY){
    outb(0x1f7, write_cmd);
    outsl(0x1f0, b->data, BSIZE/4);  //写操作
  } else {
    outb(0x1f7, read_cmd);  //读操作发生在中断后
  }
}

```

ideintr：一次buf同步的收尾，并启动队列中下一个buf

```c
ideintr(void)             
{
  struct buf *b;

  acquire(&idelock);

//取出磁盘等待队列中的第一个buf（就是刚刚处理完的这个）
  if((b = idequeue) == 0){
    release(&idelock);
    return;
  }
  idequeue = b->qnext;      //更新队列

// 读磁盘到buf（outsl在idestart中）（读操作发生在中断后）
  if(!(b->flags & B_DIRTY) && idewait(1) >= 0)
    insl(0x1f0, b->data, BSIZE/4);  //4字节为单位读取128次

// 修改状态，唤醒这个buf上等待的进程
  b->flags |= B_VALID;
  b->flags &= ~B_DIRTY;
  wakeup(b);

// 再次启动磁盘处理队列中的下一个buf
  if(idequeue != 0)
    idestart(idequeue);

  release(&idelock);
```

#### 日志区

处理FS系统调用的写操作，它把一个对磁盘“写操作描述包”装成一个日志“写在磁盘

log区位于磁盘末端，用于记录每次FS系统调用的写操作，最多三次，每次最多10个block

log结构

```c
struct log {
  struct spinlock lock;   //锁
  int start;              //起始位置（block号）
  int size;               //大小
  int outstanding; // how many FS sys calls are executing.
  int committing;  // 是否在commit
  int dev;         // 设备号
  struct logheader lh;   //要读写的blocks数组，有锁保护
};
```

##### 相关函数：

initlog：初始化，并检查是否需要recover

```c
initlog(int dev)  
{
  if (sizeof(struct logheader) >= BSIZE)   
    panic("initlog: too big logheader");

//superblock有log区的信息
  struct superblock sb;          
  initlock(&log.lock, "log");
  readsb(dev, &sb);                 
  log.start = sb.logstart;         
  log.size = sb.nlog;
  log.dev = dev;
//检查是否需要recover
  recover_from_log(); 
}
```

log_write：加一个block到logheader，修改时用锁保护

commit：转移一个完整的log区

```c
commit()
{
  if (log.lh.n > 0) {
//将要更新的block写入log区的数据块里
    write_log();    
    write_head();   
//从log区数据块转移到实际block处
    install_trans();
//清空log区
    log.lh.n = 0;
    write_head();    // Erase the transaction from the log
  }
}
```

begin_op：每次FS syscall前调用

```c
begin_op(void)
{
  acquire(&log.lock);
  while(1){
//如果log区在commit或空间不足，进程sleep等待
    if(log.committing){          
      sleep(&log, &log.lock);
    } else if(log.lh.n + (log.outstanding+1)*MAXOPBLOCKS > LOGSIZE){
      sleep(&log, &log.lock);
    } 
//outstanding表示正在往log区写的系统调用数
    else {
      log.outstanding += 1;
      release(&log.lock);
      break;
    }
  }
}
```

end_op：每次FS syscall时调用

```c
end_op(void)
{
  int do_commit = 0;
  acquire(&log.lock);
  log.outstanding -= 1;   //代表一个FS syscall往log区写入完毕
  if(log.committing)
    panic("log.committing");

//如果此时FS syscall都写入完毕，准备commit
  if(log.outstanding == 0){    
    do_commit = 1;
    log.committing = 1;
  } 
//否则唤醒在log上睡眠的进程
  else {
    wakeup(&log);
  }
  release(&log.lock);
//检查是否可以commit
  if(do_commit){
    // call commit w/o holding locks, since not allowed
    // to sleep with locks.
    commit();
    acquire(&log.lock);
    log.committing = 0;
    wakeup(&log);
    release(&log.lock);
  }
}
```

#### 路径和文件名

目录是一个类型为T_DIR的inode，内容为一个个的dirent

```c
//目录条目
#define DIRSIZ 14
struct dirent {
  ushort inum;
  char name[DIRSIZ];
};
```

处理inode的函数都能处理目录，在此之外还有几个专用于目录的函数，用于查找，添加条目，解析路径名

dirlookup：遍历目录，查找一个条目

```c
dirlookup(struct inode *dp, char *name, uint *poff)
{
  uint off, inum;
  struct dirent de;

  if(dp->type != T_DIR)
    panic("dirlookup not DIR");
//遍历目录dp
  for(off = 0; off < dp->size; off += sizeof(de))
  {
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))    
      panic("dirlookup read");
    if(de.inum == 0) 
      continue;
//名字对比
    if(namecmp(name, de.name) == 0){     
//记录下de的位置和inum
      if(poff)
        *poff = off;               
      inum = de.inum;
//对应的inode放入icache，返回地址
      return iget(dp->dev, inum); 
    }
  }
  return 0;
}
```

dirlink：添加一个条目

```c
dirlink(struct inode *dp, char *name, uint inum)
{
  int off;
  struct dirent de;
  struct inode *ip;

//已经存在
  if((ip = dirlookup(dp, name, 0)) != 0){ 
    iput(ip);       //（查找时用了iget，此处减去iget加上的ref）
    return -1;
  }
//遍历，找一个空条目
  for(off = 0; off < dp->size; off += sizeof(de)){              
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))   
      panic("dirlink read");
    if(de.inum == 0)   
      break;
//如果dir已经满了，就覆盖最后一个
  }
//放入条目信息
  strncpy(de.name, name, DIRSIZ); 
  de.inum = inum;
//修改后的di写回inode
  if(writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
    panic("dirlink");

  return 0;
}

```

路径名解析函数主要为namex，思路为以／为间隔解读字符串，层层使用dirlookup查找，查找的时候使用锁保护dir不变。此处不再详述。

#### FS syscall

open，read，write，close，dup，mkdir，mknod，link，unlink

这些名字看着都不陌生，都是对相关函数的包装，加了一些检查和锁保护之类的，此处不再详述



## 锁

锁其实是一个很简明的概念，一个锁同一时间只能被一个进程持有，通过在操作前获取锁，可以暂时保护一些共享数据，或实现原子操作

### 锁相关的函数：

init

```c
//初始化一个锁
initlock(struct spinlock *lk, char *name)  
{
  lk->name = name;
  lk->locked = 0;
  lk->cpu = 0;
}
```

acquire

```c
acquire(struct spinlock *lk)
{
//持有锁的时候不能中断（防止死锁）
  pushcli();
  if(holding(lk))
    panic("acquire");

//用xchg原子操作实现acquire
  while(xchg(&lk->locked, 1) != 0)             
    ;
//强制cpu执行完前面的写入以后再执行下面的指令：
  __sync_synchronize();   

//记录cpu信息
  lk->cpu = mycpu();
  getcallerpcs(&lk, lk->pcs); //好想是记录一些函数调用时栈中的pc们
}
```

release

```c
release(struct spinlock *lk)
{
  if(!holding(lk))
    panic("release");

  lk->pcs[0] = 0;
  lk->cpu = 0;
//保证前面指令执行完毕
  __sync_synchronize();

//用原子操作把locked设为0
  asm volatile("movl $0, %0" : "+m" (lk->locked) : );     
//打开中断
  popcli();
}

```

cpu共享数据：ptable，buf，bcache，inode，icache，log，需要保持暂时不变的时候都需要锁保护，

### 一些锁的介绍

#### ptable lock

访问ptable和修改proc状态的时候使用

调度的时候，都是在swtch之前获得，之后释放，scheduler和释放cpu的进程彼此互相为对方释放。

#### buf，bcache

bcache结构编译之后就在kernel的全局变量区，每次修改需要锁

每个buf也有锁，get之后就锁上，relse的时候释放，保证同一时间只有一个cpu可以使用buf

buf在使用的时候也需要保护，而且时间较长，所以用了sleeplock

#### inode相关的锁

inode结构也有锁。cpu操作的都是内存上的inode，inode锁要保护的是对inode结构的修改

icache保护的是其中inode的ref（icache就是用ref参考分配的））也是一个sleeplock

ialloc，iget，idup的时候，有buf和icache里的锁保护，没有用inode锁，

iput（itrunc）的时候，修改link的时候（不能同时修改，会覆盖）使用了inode锁

每次fileread中调用iread之前也会用ilock锁上（保证每次），因为iwrite的时候必须用锁，如果iread不用，则另一个cpu就可能进入write，影响read

## 启动过程

bootasm.S：清空段寄存器，打开A20端口，加载gdt，打开保护模式，更新段寄存器

```c
.code16                       # 编译为16位（刚启动时为实模式）
.globl start
start:
  cli                         # 屏蔽中断

//清空段寄存器
  xorw    %ax,%ax             # Set %ax to zero
  movw    %ax,%ds             # -> Data Segment
  movw    %ax,%es             # -> Extra Segment
  movw    %ax,%ss             # -> Stack Segment

//设置0x64 和 0x60上的键盘控制器，使输出端口的第2位置为高位，来使多于20位的地址正常工作
seta20.1:
  inb     $0x64,%al               # Wait for not busy
  testb   $0x2,%al
  jnz     seta20.1

  movb    $0xd1,%al               # 0xd1 -> port 0x64
  outb    %al,$0x64

seta20.2:
  inb     $0x64,%al               # Wait for not busy
  testb   $0x2,%al
  jnz     seta20.2

  movb    $0xdf,%al               # 0xdf -> port 0x60
  outb    %al,$0x60

//加载gdt 
  lgdt    gdtdesc
  
//打开保护模式（修改%cr0中标志位）
  movl    %cr0, %eax              
  orl     $CR0_PE, %eax   #CR0_PE：0x00000001，取或，最后一位置1
  movl    %eax, %cr0

  //长跳转到start32处
  //修改CS为SEG_KCODE
  //SEG_KCODE=1，移3位来保证高13位上是1，代表段描述符的索引
  //gdt中得到段描述符，其实基址是0，直接映射偏移
  //$start32为段内偏移量
  ljmp    $(SEG_KCODE<<3), $start32

.code32  //编译为32位
start32:
//初始化保护模式下段寄存器（其实gdt中设置的都是零）
  movw    $(SEG_KDATA<<3), %ax    # Our data segment selector
  movw    %ax, %ds                # -> DS: Data Segment
  movw    %ax, %es                # -> ES: Extra Segment
  movw    %ax, %ss                # -> SS: Stack Segment
  movw    $0, %ax                 
  //Zero segments not ready for use
  movw    %ax, %fs                # -> FS
  movw    %ax, %gs                # -> GS

  # $start（0x7c00）作为栈顶，进入bootmain
  movl    $start, %esp
  call    bootmain  

  # If bootmain returns (it shouldn't), trigger a Bochs
  # breakpoint if running under Bochs, then loop.
  movw    $0x8a00, %ax            # 0x8a00 -> port 0x8a00
  movw    %ax, %dx
  outw    %ax, %dx
  movw    $0x8ae0, %ax            # 0x8ae0 -> port 0x8a00
  outw    %ax, %dx
spin:
  jmp     spin

# Bootstrap GDT
.p2align 2                                # force 4 byte alignment
gdt:
  SEG_NULLASM                             # null seg
  #两个段都是基址为0，段长为4G
  SEG_ASM(STA_X|STA_R, 0x0, 0xffffffff)   # code seg  
  SEG_ASM(STA_W, 0x0, 0xffffffff)         # data seg

gdtdesc:
  .word   (gdtdesc - gdt - 1)             # sizeof(gdt) - 1
  .long   gdt                             # address gdt


```

bootmain.c：加载内核文件

```c
//从磁盘中读取内核扇区的内容，并写到规定好的内存地址 paddr 处，然后从entry执行

void bootmain(void)
{
	...      //省略了一些变量声明
	
  elf = (struct elfhdr*)0x10000;  
  //此时都是直接映射，内核文件头放到了物理地址0x00100000处

  readseg((uchar*)elf, 4096, 0);  //加载文件头

  if(elf->magic != ELF_MAGIC)     //检查elf文件
    return;

  //加载内核各程序段，都是直接映射
  ph = (struct proghdr*)((uchar*)elf + elf->phoff);
  eph = ph + elf->phnum;   
  for(; ph < eph; ph++)
  {
    pa = (uchar*)ph->paddr;
    readseg(pa, ph->filesz, ph->off); 
    if(ph->memsz > ph->filesz)
      stosb(pa + ph->filesz, 0, ph->memsz - ph->filesz);
  }

  //从entry（程序段的入口地址）处执行
  entry = (void(*)(void))(elf->entry);
  entry();   //这句语法没看懂诶……
}

```

entry.S：加载初始页表，打开分页，准备内核栈

```c
.p2align 2
.text
.globl multiboot_header
multiboot_header:                   //没看懂，这是多核启动？
  #define magic 0x1badb002
  #define flags 0
  .long magic
  .long flags
  .long (-magic-flags)

.globl _start
_start = V2P_WO(entry)  //虚拟地址还没映射，直接用物理地址

# Entering xv6 on boot processor, with paging off.
.globl entry
entry:

//设置 %cr4 中的CP_PSE位来通知分页硬件允许使用超级页(4M)（要映射内核）
  movl    %cr4, %eax
  orl     $(CR4_PSE), %eax
  movl    %eax, %cr4
  
//entrypgdir（改为物理地址后）放入%cr3
//一个初期页表，把高地址0～4M和物理地址0～4M都映射到0～4M
  movl    $(V2P_WO(entrypgdir)), %eax
  movl    %eax, %cr3
  
//设置%cr0，开启分页机制
  movl    %cr0, %eax
  orl     $(CR0_PG|CR0_WP), %eax
  movl    %eax, %cr0

//准备内核栈
//stack是申请好的一段空间的基址，最后有声明
  movl $(stack + KSTACKSIZE), %esp

//跳转到内核的 C 代码
  mov $main, %eax    
  jmp *%eax
//indirect call，为了保证兼容吧
//“The indirect call is needed because the assembler produces a PC-relative instruction for a direct jump.”

.comm stack, KSTACKSIZE

```

main.c：开内存空间，换页表，一堆初始化，构建第一个进程，打开调度器，启动init

```c
int main(void)
{
  kinit1(end, P2V(4*1024*1024));  //开一个4M的freelist
  kvmalloc();      //换页表

  mpinit();        
  lapicinit();     
  seginit();       // 段基址设为0
  picinit();       // disable pic
  ioapicinit();    
  consoleinit();   // console hardware 待续 初始化了一个锁和结构 
  uartinit();      
  pinit();         // 初始化ptable锁 
  tvinit();        // 中断相关的初始化，设置idt表
  binit();         // 初始化bcache锁，构建buf链表，
  fileinit();      // 初始化文件锁
  ideinit();      
  startothers();   // start other processors
 
  kinit2(P2V(4*1024*1024), P2V(PHYSTOP)); 
    //将4M到PHYSTOP都加到freelist

  userinit();      // 构建第一个进程
  mpmain();        // finish this processor's setup
}

```

