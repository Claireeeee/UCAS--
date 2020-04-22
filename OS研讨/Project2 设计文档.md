# Project2 A Simple Kernel设计文档（Part II）

中国科学院大学   2017K8009929013

## 1. 中断流程：

#### 触发：

1. 时钟中断触发：当cp0寄存器count=compare，硬件触发中断
2. 系统调用触发：

```c
//sys_sleep在测试程序中调用
void sys_sleep(uint32_t time)
{
    invoke_syscall(SYSCALL_SLEEP, time, IGNORE, IGNORE);
}

//invoke调用syscall：
LEAF(invoke_syscall)
//传参
    move    v0, a0
    move    a0, a1
    move    a1, a2
    move    a2, a3
//中断
    syscall
    nop
    jr      ra
    nop
END(invoke_syscall)
```

#### 触发中断后，跳转到exception_handler_entry：检查exccode位

```c
NESTED(exception_handler_entry, 0, sp)   
exception_handler_begin:
    CLI  //关中断
    SAVE_CONTEXT(USER)  //保存上下文
    //检查中断类型（从cause），跳转到对应的处理代码
    mfc0    k0, CP0_CAUSE
    nop
    andi    k0, CAUSE_EXCCODE   //cause中提取出EXCCODE，判断中断类型号+00
    la      k1, exception_handler
    add     k0, k0, k1
    lw      k0, 0(k0)
    j       k0
exception_handler_end:
END(exception_handler_entry)
```

检查IPL位：

非系统调用处理：从exception_handler_entry通过exception_handler[0]跳到handle-int

```assembly
NESTED(handle_int, 0, sp)
    mfc0    k0, CP0_CAUSE
    nop
    andi    k0, k0, CAUSE_IPL   //cause中提取IPL，检查中断类型
    li      k1, 0x8000            //时钟中断
    beq     k1, k0, time_interrupt
    nop
    jal   clear_IPL  //非时钟则不处理
    nop
    j       exception_exit
    nop
time_interrupt:
    la      k0, time_elapsed
    lw      k1, 0(k0)
    addi    k1, k1, 0x1
    sw      k1, 0(k0)           //time_elapsed++

//清空count
    li      k0, TIMER_INTERVAL
    mtc0    zero, CP0_COUNT
    nop
    mtc0    k0, CP0_COMPARE
    nop
    jal     clear_IPL
    nop
    jal     do_scheduler  //切换进程
    nop
    j       exception_exit
clear_IPL:
  mfc0  k0, CP0_CAUSE
  nop
  andi  k1, k0, CAUSE_IPL
  xor   k0, k0, k1
  mtc0  k0, CP0_CAUSE
  nop
  jr  ra
  nop
END(handle_int)
```

系统调用处理：从exception_handler_entry通过exception_handler[0]跳到handle-syscall

```c
NESTED(handle_syscall, 0, sp)
  //传参（因为syscall和普通函数传参寄存器使用不一样，所以需要来回倒腾
    move    a3, a2
    move    a2, a1
    move    a1, a0
    move    a0, v0
    jal     system_call_helper 
    nop
    j       exception_exit
END(handle_syscall)

//system_call_helper内容如下
void system_call_helper(int fn, int arg1, int arg2, int arg3)
{
    // syscall[fn](arg1, arg2, arg3)
    if(fn < 0 || fn >= NUM_SYSCALLS)
        fn = 0;
    //调用对应的函数处理，返回值放入regs[2]
    current_running->user_context.regs[2] = syscall_handler[fn](arg1, arg2, arg3);
    //syscall处理完毕，进入下一条指令
    current_running->user_context.cp0_epc += 4;
}
```



## 2. 睡眠实现

```c
//规定时间睡完时（到sleepto）
void do_sleep(uint32_t sleep_time)
{
    current_running->status = TASK_BLOCKED;
    current_running->sleepto = time_elapsed + 10*sleep_time;
    queue_push(&sleep_queue, current_running);
    do_scheduler();
}
//每次调度前检查睡眠队列
static void check_sleeping()
{
    pcb_t *sh = sleep_queue.head;

    while(sh != NULL){
        if(sh->sleepto <= time_elapsed){
            sh->status = TASK_READY;
            queue_push(&ready_queue,sh);
            //ready_queue_push(sh);
            sh = queue_remove(&sleep_queue, sh);
        }else{
            sh = sh->next;
        }
    }
}
```



## 3. 优先级调度

一开始的设计思路：

* 新增优先级标志项，2、1、0，优先级依次降低
* 不同优先级放入不同队列
* 调度时按优先级顺序检查
* 初始化为2，调度后降1，降为0时再升为2

```c
//进程切出时修改优先级，放入不同队列
if((current_running->status ==TASK_RUNNING))  {   	
        current_running->status = TASK_READY;
        if (current_running->priority>0){
            current_running->priority -= 1;    //优先级降，or重新放回最高
        }
        else current_running->priority =2;
        queue_push(&(ready_queue[current_running->priority]),current_running);  //2，1，0
    }
//优先级队列从高到低查询
int i=2;
for (; ; )
{
    while((i>=0)&&queue_is_empty(&(ready_queue[i]))) {   //2，1，0
        i--
    };
    if(i<0){  
      //如果全空（全去睡了），开中断，时间++，有的时间到了就会醒
        start_int();  
        check_sleeping();
    }
    else break;
}
//找到不空的队列，退出循环，dequeue后切换
    close_int();
    current_running = queue_dequeue(&(ready_queue[i]));
```

2. 后来review的时候经助教提醒，这样实现的其实还是一个FIFO队列，可以通过给每个进程分配不同大小的时间片实现优先级。找这个思路稍微修改了初始化过程和中断流程，主要修改内容如下：

```c
		//初始化每个进程的时候，添加了该进程的时间片大小（模3分配
		p->time_slice=TIMER_INTERVAL*(1+process_id%3);  //1,2,3 time slice

    //每次时钟中断调度后，根据这个时间片，重置compare寄存器
    mtc0    zero, CP0_COUNT
    lw  k0,current_running
    lw  k1,312(k0)          //这个位置存放的刚好是该进程的时间片大小
    mtc0    k1, CP0_COMPARE
```