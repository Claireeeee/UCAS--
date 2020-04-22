# Project5 设计文档

**Device Driver**

中国科学院大学	2017K8009929013

#### 1. 描述符表初始化：

- 描述符数据结构如下

```c
typedef struct desc
{
    uint32_t des0;
    uint32_t des1;
    uint32_t des2;
    uint32_t des3;
} desc_t;
```

- 在mac.c定义两个数组，作为描述符表

```c
desc_t Recv_Des[64];
desc_t Send_Des[64];  
```

- 初始化描述符表：以发送描述符表为例

```c
//填充mac结构：描述符表地址，send buffer地址
    mac->td = Send_Des;
    mac->td_phy = mac->td & 0x1fffffff;  //unmap区物理地址就是虚址高位置零
    mac->saddr = buffer;
    mac->saddr_phy = mac->saddr & 0x1fffffff;
    
    int i=0;
    for(;i<PNUM;i++){
        //des0置零
        (mac->td+i)->des0 = 0x00000000;
        //des1
        //29&30置1：一个buffer就是一帧
        //24置1：des3中为下一个描述符的地址
        (mac->td+i)->des1 = (0 | (1<<30) | (1<<29) | (1 << 24) | (1024))
        //des2
        //64个描述符传输的都是同一个buffer中的数据
        (mac->td+i)->des2 =  buffer & 0x1fffffff;
        //des3: 下一个描述符的地址
        (mac->td+i)->des3 = (mac->td+i+1) & 0x1fffffff;  
    }
    //最后一个描述符：25位也要置1 & des3中放首个描述符的地址
    (mac->td+PNUM-1)->des1 = (0 | (1<<30) | (1<<29) | (1 << 25) | (1 << 24) | (1024));
    (mac->td+PNUM-1)->des3 = (uint32_t) mac->td & 0x1fffffff;
```

#### 2. 收发过程

- 收发函数：以do_net_send为例

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

#### 3. 中断实现

- mac中断处理
  - 初始化接收描述符的时候，仅把最后一个描述符的des0的31位拉低，代表接收完这一帧触发中断
  - 则每次中断都是接收完了64个包，直接唤醒接收进程打印即可

```c
//中断处理时加上mac中断的判断
NESTED(handle_int, 0, sp)
    // interrupt handler
    // Leve3 exception Handler.
    mfc0    k0, CP0_CAUSE
    nop
    andi    k0, k0, CAUSE_IPL
    li      k1, 0x8000            //时钟中断
    beq     k1, k0, time_interrupt
    nop
    //外设中断
    li      k1, 0x800
    bne     k1, k0, exit
    nop
    //mac中断：查看INT1_SR的第3位
    li      k0, INT1_SR    
    lw      k1, 0(k0)
    andi    k1,k1,0x00000008
    li      k0,0
    beq     k1,k0, exit
    jal     mac_irq_handle    //跳转到mac中断处理函数
    nop
  
void mac_irq_handle(void)
{
    //唤醒并清中断
    do_unblock_one(&recv_block_queue);   
    clear_interrupt();
    return;
}


//唤醒后，测试程序调用mac_recv_handle和printf_recv_buffer打印描述符和收到的包
void mac_recv_handle(mac_t *test_mac)
{
    int i=0;
    uint32_t *Recv_desc;
    desc_t *recv = NULL;
    int  print_location = 3;
    for (; i <PNUM; ++i)
    {
        Recv_desc = (uint32_t *)(test_mac->rd + i * 16);
        recv = (desc_t *)Recv_desc;

        sys_move_cursor(1, print_location);
        printf("\n%d recv buffer,r_desc( 0x%x) =0x%x:          \n", i, Recv_desc, *(Recv_desc));

        printf_recv_buffer((recv->des2 | 0xa0000000));
        //sys_sleep(1);
        printf("\n");
    }
}

static uint32_t printf_recv_buffer(uint32_t recv_buffer)
{
    int j=0;
    printkf("start:");
    for(;j<6;j++){     //只打印一个目的地址
        printkf("%x ", *((uint32_t*) (recv_buffer+j*sizeof(int)))); 
    }
}
```

