# Project3 设计文档

**Interactive OS and Process Management**

中国科学院大学	2017K8009929013

 

## 1. Shell

#### 输入命令解析：

1. 初始化终端输出区(下半块屏幕 
2.  循环读取指令 

- 检查是否有输⼊
- 若无，跳过解析部分，直接进入下一循环；若有，读取到commad buffer，开中断，进入解析部分
- 输入解析：识别输入（ps，exec，clear，kill），调⽤相关函数处理

#### 命令函数实现

1. clear：直接调用screen_clear

2. spawn：用参数指定的task结构创建一个新进程，包括

   - 找到空闲的pcb
   - pcb初始化：ra，sp，栈顶，cp0status，cp0epc，pid，status，name
   - 加入ready队列
   - 关键代码：

   ```c
       while(pid<16 && pcb[pid].status!=TASK_EXITED) pid++;  //找空位
       if(pid==16) {
           printkf("Spawn error: no space in ptable.\n");
       }
       else{
         //pcb init
           p = &pcb[pid];
           memset(p,0,sizeof(pcb_t));      
           p->kernel_context.regs[31] = (uint32_t )exception_exit;     
           p->kernel_context.regs[29] = STACK_BASE + pid * STACK_SIZE;
           p->kernel_stack_top = STACK_BASE + pid* STACK_SIZE;
           p->kernel_context.cp0_epc = task->entry_point;
           p->kernel_context.cp0_status=0x30008003;
   
           p->user_context.regs[31] = (uint32_t )exception_exit;
           p->user_context.regs[29] = STACK_BASE + (pid)* STACK_SIZE;
           p->user_stack_top =  STACK_BASE + (pid)* STACK_SIZE;
           p->user_context.cp0_epc = task->entry_point;
           p->user_context.cp0_status=0x30008003;
   
           p->pid = pid;
           p->type = task->type;
           p->status = TASK_READY;
           p->name = task->name;
           p->priority=2; 
   	      p->wait=-1;
           p->time_slice=TIMER_INTERVAL*(1+(pid%3)); 
           queue_push(&(ready_queue[2]), p);
       }
   ```

3. exit

   - 检查是否有进程正在wait当前进程，若有，唤醒wait中的进程
   - 释放当前进程持有的锁
   - 进程status修改为TASK_EXITED
   - 调用调度函数
   - 关键代码：

   ```c
   //唤醒正在等待当前进程的进程
       if(wait_cnt>0){   
           pcb_t *p = (pcb_t *) wait_queue.head;
           int pid = current_running->pid;
           while(p!=NULL && wait_cnt>0){
               if(p->wait == pid) {
                   p->status=TASK_READY;
   								p->wait=-1;
                   pcb_t *p2 = queue_remove(&wait_queue,p); 
                   queue_push(&(ready_queue[p->priority]), p);
                   p = p2;
                   wait_cnt--;}
               else p = p->next;}}
   //释放锁
       int i=0;
       while(i<NUM_MAX_LOCK){
           if(current_running->mutex_lock[i] != 0){
               do_mutex_lock_release(current_running->mutex_lock[i]);
   	    current_running->mutex_lock[i] =0;}
           i++;}
   //修改状态并重新调度
       current_running->status = TASK_EXITED;
       do_scheduler();
   }
   ```

4. kill

   - 若参数指向当前进程，直接调用exit
   - 如果不是当前进程，手动清除进程占有的资源：wait队列唤醒，释放锁
   - 检查进程状态，从其所在的队列（ready、wait、sleep或block）中清除
   - 修改状态
   - 关键代码：

   ```c
   //从进程所在的队列（ready、wait、sleep或block）中将其清除
   switch(pcb[pid].status){
               case TASK_BLOCKED:{
                       int i=0,flag=0;
                       pcb_t *p;
                       while(block_queue[i] != NULL && i<NUM_MAX_LOCK && flag==0)
                       {//block queue
                           p = (pcb_t *) block_queue[i]->head;
                           while(p!=NULL && flag==0){
                               if(p==&pcb[pid]) {
                                   flag=1;
                                   queue_remove(block_queue[i],p);
                                   break;}
                               else p=p->next;}
                           i++;}
                       break;}
               case TASK_SLEEP:
                   {
                       pcb_t *p = (pcb_t *) sleep_queue.head;
                       while(p!=NULL){
                           if(p==&pcb[pid]) {
                               queue_remove(&sleep_queue,p);
                               break;
                           }
                           else p=p->next;
                       }
                       break;
                   }
               case TASK_WAIT:
   								...
               case TASK_READY:
   								...
               default :
   ```



代码如下，流程描述在注释中：

```assembly
	# 1) task1 call BIOS print string "It's bootblock!"
	la    $a0,msg
	lw    $t0,printstr
	jal   $t0
	nop

	# 2) task2 call BIOS read kernel in SD card and jump to kernel start
	
	#传参：内存地址，sd卡地址，大小
	lw    $a0,kernel   #kernel要加载至的内存地址
	li    $a1,0x200    #kernel代码在SD卡的位置偏移和大小
	li    $a2,0x200
	#read函数调用
	lw    $t0,read_sd_card  #read函数的内存地址
	jal   $t0
	nop
	#跳转到kernel代码
	lw    $t0,kernel_main   #kernel入口的内存地址
	jal   $t0
```

## 2. 同步原语

三种原语课上讲的很清楚，按定义实现就好，主要代码如下（关键部分见注释）：

1. semaphore

```c
void do_semaphore_up(semaphore_t *s){
    s->value += 1;
    if(s->value<=0)     //检查是否有等待进程，若有则唤醒
    	do_unblock_one(&s->queue);}

void do_semaphore_down(semaphore_t *s){
    s->value -= 1;
    while(s->value < 0){  //资源不够则阻塞
        current_running->status=TASK_BLOCKED;
        do_block(&s->queue);}}
```

2. condition variable

```c
void do_condition_wait(mutex_lock_t *lock, condition_t *condition)
    do_mutex_lock_release(lock);         //wait时需要释放锁，返回后要重新获取
    current_running->status=TASK_BLOCKED;
    do_block(&condition->queue);
    do_mutex_lock_acquire(lock);}

void do_condition_signal(condition_t *condition){
    do_unblock_one(&condition->queue);}

void do_condition_broadcast(condition_t *condition){
    do_unblock_all(&condition->queue);}
```

3. barrier

```c
void do_barrier_wait(barrier_t *barrierad){
    barrierad->value += 1;                 //value++并检查是否到达goal
    if(barrierad->value >= barrierad->goal){
        do_unblock_all(&barrierad->queue);
        barrierad->value = 0;
    }else{
	    do_block(&barrierad->queue);}}
```



 ## 3. Msilbox设计

1. mailbox具体结构如下

   ```c
   typedef struct mailbox{
       int index;         //index
       queue_t sendqueue; //两个阻塞队列
       queue_t resvqueue;
       char name[20];     //name
       char message[MAXNUMCHAR];  //msg
       int head;					 //头尾指针
       int tail;
       int full;					 //空满标志
       int empty;
       status_t status;   //status：OPEN/CLOSED
   } mailbox_t;
   ```

2. 操作函数

   - open：参数为name，先遍历查看对应box是否已存在，若存在则直接返回，否则开一个新的box
   - close：修改状态即可

   ```c
       int i=0,fstclo=-1;
   		//遍历查看是否已存在同名信箱
       for(;i<MAX_NUM_BOX&&strcmp(name,mboxs[i].name)!=0;i++){  
           if (mboxs[i].status==CLOSED&&fstclo<0)  //遍历时顺便记下第一个空闲信箱的位置
           {
               fstclo=i;
           }
       }
       if (i==MAX_NUM_BOX&&fstclo<0)  //如果信箱已满
       {
           printkf("mboxs open error: mboxs is full\n");
           do_mutex_lock_release(&mutex);
       }
       else if (i==MAX_NUM_BOX&&fstclo>=0) //如果同名信箱不存在，新开一个
       {
           i=fstclo;
           //一系列初始化
           memset(&mboxs[i],0,sizeof(mboxs[i]));
           mboxs[i].status = OPEN;
           mboxs[i].head = mboxs[i].tail = 0;
           mboxs[i].empty=1;
           strcpy(mboxs[i].name,name);
           queue_init(&mboxs[i].sendqueue);
           queue_init(&mboxs[i].resvqueue);
           mboxs[i].index=blockqueue_register(&mboxs[i].sendqueue);
           blockqueue_register(&mboxs[i].resvqueue);
           do_mutex_lock_release(&mutex);
           return &mboxs[i];
       }
       else  //如果已存在，直接返回匹配的信箱
       {
           mboxs[i].status = OPEN;
           do_mutex_lock_release(&mutex);
           return &mboxs[i];
       }
   ```

   - send&receive：
     - 信箱为共享变量，操作需要锁保护
     - 按序依次写入/读出字符，写入后检查队列空满状态，不能继续则阻塞

   ```c
   void mbox_send(mailbox_t *mailbox, void *msg, int msg_length){
       do_mutex_lock_acquire(&mutex);
       char *_msg = (char *)msg;
       int i=0;
       while(i<msg_length){   //一个字符一个字符的输入
           while(mailbox->full==1){   //满则阻塞
               do_mutex_lock_release(&mutex);
               do_block(&mailbox->sendqueue);
               do_mutex_lock_acquire(&mutex);
           }
           mailbox->message[mailbox->tail%MAXNUMCHAR] = *(_msg++);//尾部添加
           mailbox->tail +=1;
           mailbox->full = mailbox->tail==(mailbox->head+MAXNUMCHAR); //满的条件
           mailbox->empty = 0;
           do_unblock_one(&mailbox->resvqueue);  //唤醒一个
           i++;
       }
       do_mutex_lock_release(&mutex);
   }
   
   void mbox_recv(mailbox_t *mailbox, void *msg, int msg_length)
   {
       do_mutex_lock_acquire(&mutex);
       char *_msg = (char *)msg;
       int i=0;
       while(i<msg_length){   //一个字符一个字符的输入
           while(mailbox->empty==1){  //空则阻塞
               do_mutex_lock_release(&mutex);
               do_block(&mailbox->resvqueue);
               do_mutex_lock_acquire(&mutex);
           }
           *(_msg++)=mailbox->message[mailbox->head%MAXNUMCHAR]; //头部读取
           mailbox->head +=1;
           mailbox->empty = mailbox->tail==mailbox->head;   //空的条件
           mailbox->full= 0;
           do_unblock_one(&mailbox->sendqueue);    //唤醒一个
           i++;
       }
       do_mutex_lock_release(&mutex);
   }
   ```

   



## 4. 关键函数功能

见上一部分

