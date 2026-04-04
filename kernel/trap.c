#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fcntl.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(p->killed)
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    intr_on();

    syscall();
  } else if(r_scause() == 13 || r_scause() == 15){
    // page fault
    uint64 va = r_stval();
    if(mmap_fault_handler(p, va) < 0){
      p->killed = 1;
    }
  } else if((which_dev = devintr()) != 0){
    // ok
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  if(p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to trampoline.S
  w_stvec(TRAMPOLINE + (uservec - trampoline));

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 fn = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

// 寻找va所在的vma
struct vma*
find_vma(struct proc *p, uint64 va) {
  for (int i = 0; i < NVMA; i++) {
    if (p->vma[i].used &&
        va >= p->vma[i].addr &&
        va < p->vma[i].addr + p->vma[i].length) {
      return &p->vma[i];
    }
  }
  return 0;
}

// 处理mmap引起的缺页异常
int
mmap_fault_handler(struct proc *p, uint64 va) {
  
  struct vma *v = find_vma(p, va);
  uint64 a;
  if (v == 0) return -1; // 没有找到对应的vma

  // 把 fault 地址对齐到页边界
  a = PGROUNDDOWN(va);
  if(a < v->addr || a >= v->addr + v->length) return -1; // 不在vma范围内
  
  pte_t *pte = walk(p->pagetable, a, 0);
    if(pte && (*pte & PTE_V))
      return -1;
  
      // 分配一页物理页
  char *mem = kalloc();
  if(mem == 0) return -1;
  memset(mem, 0, PGSIZE);

  // 计算这页对应文件中的偏移
  uint64 offset = v->offset + (a - v->addr);
  
  ilock(v->f->ip);
  // 把文件从 offset 开始的这一页内容，读到刚分配的物理页 mem 里
  readi(v->f->ip, 0, (uint64)mem, offset, PGSIZE);
  
  iunlock(v->f->ip);
  
  int perm = PTE_U;
  if (v->prot & PROT_READ)  perm |= PTE_R;
  if (v->prot & PROT_WRITE) perm |= PTE_W;

  
  // 把虚拟地址 a 映射到物理地址 mem，权限是 perm
  if (mappages(p->pagetable, a, PGSIZE, (uint64)mem, perm) != 0) {
    kfree(mem);
    return -1;
  }
  return 0;

}

int
do_munmap(struct proc *p, uint64 addr, uint64 length)
{
  struct vma *v = find_vma(p, addr);
  if(v == 0)
    return -1;

  if(addr < v->addr || addr >= v->addr + v->length)
    return -1;

  uint64 start = PGROUNDDOWN(addr);
  uint64 end = PGROUNDUP(addr + length);

  for(uint64 a = start; a < end; a += PGSIZE){
    pte_t *pte = walk(p->pagetable, a, 0);
    if(pte == 0 || (*pte & PTE_V) == 0)
      continue;

    if(v->flags & MAP_SHARED){
      uint64 mem = PTE2PA(*pte);
      uint64 offset = v->offset + (a - v->addr);

      begin_op();
      ilock(v->f->ip);
      writei(v->f->ip, 0, mem, offset, PGSIZE);
      iunlock(v->f->ip);
      end_op();
    }

    uvmunmap(p->pagetable, a, 1, 1);
  }

  if(addr == v->addr && length >= v->length){
    fileclose(v->f);
    v->used = 0;
  } else if(addr == v->addr){
    v->addr += length;
    v->length -= length;
    v->offset += length;
  } else if(addr + length == v->addr + v->length){
    v->length -= length;
  } else {
    return -1;
  }

  return 0;
}