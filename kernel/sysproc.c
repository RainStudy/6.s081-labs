#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;


  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}


#ifdef LAB_PGTBL
int
sys_pgaccess(void)
{
  // lab pgtbl: your code here.
  uint64 addr;
  int len;
  uint64 mask_address;

  argaddr(0, &addr);
  argint(1, &len);
  argaddr(2, &mask_address);

  // set an upper limit to unsigned int mask
  if (len > 32) {
    return -1;
  }

  struct proc *p = myproc();
  uint32 mask = 0;

  for (int i = 0; i < len; i++) {
    pte_t *pte;
    pte = walk(p->pagetable, addr + PGSIZE * i, 0);
    // 是否有 PTE_A 的标记
    if (pte != 0 && ((*pte) & PTE_A)) {
      // 写入mask
      mask |= (1 << i);
      // 清除标记
      *pte &= ~PTE_A;
    }
  }

  // 将结果写回用户态
  if (copyout(p->pagetable, mask_address, (char*) &mask, sizeof(mask)) < 0) {
    return -1;
  }
  return 0;
}
#endif

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
