# Project4 设计文档

**Virtual Memory**

中国科学院大学		2017K8009929013

#### 1. 物理内存：

- ple_t结构管理页框，页框大小4kb
- 用页框数组page_frame管理内存
- 结构连成链表，分配回收用头尾指针定位，具体内容如下

```c
//表示页框的结构
typedef struct pool_e{
    void* pre;
    void* next;
    int index;
}ple_t;
//头尾指针，用于页框分配和回收
ple_t pool_head;
ple_t pool_tail;

//物理页框数组
ple_t page_frame[PAGE_FRAME_NUMBER];
//初始化函数
void init_page_frame(void){
    memset(page_frame,0,sizeof(page_frame));
    set_page_mask();

  //把数组连成链表，并用头尾指针管理
    pool_head.pre=NULL;
    pool_head.next=&(page_frame[0]);
    int i;
    ple_t *pre=&pool_head;
    ple_t *p=pre->next;
    for (i=0; i <PAGE_FRAME_NUMBER/2-1; i++)
    {
        p->pre=pre;
        p->next=&(page_frame[i+1]);
        p->index=i;
        pre=p;
        p=p->next;
    }
    page_frame[i].index=i;
    page_frame[i].pre=pre;
    page_frame[i].next=&pool_tail;
    pool_tail.pre=&(page_frame[i]);
    pool_tail.next=NULL;
}
```

- 页框大小4kb，物理地址从0xf00000到0x2000000，32M，共2^13项
- 分配回收函数如下

```c
#define PAGE_FRAME_NUMBER 0x2000  //32M/4KB=2^13个页框
void frame_free(uint32_t v){
  //回收：修改尾指针pool_tail，添加到尾部
    ((ple_t*)pool_tail.pre)->next=&(page_frame[v]);
    page_frame[v].pre=pool_tail.pre;
    page_frame[v].next=&pool_tail;
    pool_tail.pre=&(page_frame[v]);
}

char *frame_alloc(void ){
    if (pool_head.next!=&pool_tail)
    {
  //分配：用头指针pool_head处的index作为索引定位空闲页框，并修改头指针
        int idx=((ple_t*)pool_head.next)->index;
        pool_head.next=((ple_t*)pool_head.next)->next;
        ((ple_t*)pool_head.next)->pre=&pool_head;
  //物理内存基址为0xf00000，页框大小为0x1000
        return (char *) (0xf00000+0x1000*idx);
    }
    else {
      printk("memory empty");
      sys_exit();
    }
}
```

#### 2. 虚拟空间：页表

- 全局页表数组pt_table：每个进程有独立的页表，所有的页表作为全局页表数组在内核代码中声明

```c
//每个页表0x800个页表项，一页4kb，虚拟空间范围0x000000~0x800000，即每个进程8M空间
#define PTE_ENTRY_NUMBER 0x800 
pte_t pt_table[NUM_MAX_TASK][PTE_ENTRY_NUMBER];  //pte_t为管理页表项的数据结构
```

- 页表项数据结构：
  - 4byte整型
  - 参考TLB：高6位空闲，物理地址20位（4kb对齐），C3位（cache），D1位（脏位），V1位（有效），G1位（全局）

```c
#define PTE_C 0x10
#define PTE_D 0x4
#define PTE_V 0x2
#define PTE_G 0x1
pt_table[pid][vpage_number] = ((vaddr&0xfffff000)>>6) | PTE_C | PTE_D | PTE_V;
```

#### 3. TLB例外&缺页处理

- TLB例外触发中断，跳至0x80000000处，接着跳到handle_tlb，传递参数（例外发生的index）并调用do_TLB_Refill处理

```assembly
LEAF(tlb_exception_entry)
tlb_exception_begin:
    CLI
    SAVE_CONTEXT(USER)
    lw      k0, current_running
    lw      $29, OFFSET_REG29(k0)
    j       handle_tlb
    nop
tlb_exception_end:
END(tlb_exception_entry)

LEAF(handle_tlb)
    tlbp
    mfc0    a0, CP0_INDEX
    jal     do_TLB_Refill
    nop
    j       exception_exit
END(handle_tlb)

```

- do_TLB_Refill：

```c
void do_TLB_Refill(uint32_t index){
    static int tlb_i=0;
    int pid = do_getpid();
  //若miss:新开一个tlb
  //若invalid:直接修改index对应的tlb
    int _index = (index&0x80000000==0) ? index&0x1f : (tlb_i++)%32;
  //根据触发例外的虚址，右移12位得到页号
    uint32_t vaddr = pcb[pid].user_context.cp0_badvaddr;
    int vpage_number = V_2_PNUM(vaddr);  //右移12bit
  //缺页检查：检查页是否有效，无效则分配并填充
    if((pt_table[pid][vpage_number]&0x2)==0){
        uint32_t addr = (uint32_t ) frame_alloc();//如果没有空闲页框，alloc函数内部会直接退出
        pt_table[pid][vpage_number] = ((addr&0xfffff000)>>6) | PTE_C | PTE_D | PTE_V;
    }
  //填充TLB
    if(vpage_number%2){
        //odd
        tlbwi_func(pt_table[pid][vpage_number-1],pt_table[pid][vpage_number],_index);
    }else{
        //even
        tlbwi_func(pt_table[pid][vpage_number],pt_table[pid][vpage_number+1],_index);
    }
}
```

