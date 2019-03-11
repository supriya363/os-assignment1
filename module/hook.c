/*

Supriya Suresh - 18111077

Discussed with Deba
- How to deal with mmap pages being appended in front of an already existing VMA

Discussed with Arunkumar Vediappan
- Approaches to take care of extra pages in VMA that don't belong to user process
- Discussed reasons for getting infinite page faults at various points, while attempting Q1
- About how to switch CR3 and the complications that come along with it

Discussed with Upasana Singh
- About how to analyze VM Area
- About how to switch CR3 and the problems associated with it

*/



#include "mem_tracker.h"
#include "interface.h"
#include <linux/slab.h>
#include <asm/uaccess.h>


static int command;
static unsigned long tlb_misses, readwss, writewss, unused;


int poisonpages(int);
void findTlbToppers(struct read_command *);
int allocateperpage(void);
void printTlbMissCount(void);
void printVMA(void);   								//stores VM Areas before mmap call
void findReadToppers(struct read_command *);
void findWriteToppers(struct read_command *);
void checkReadWriteCount(unsigned long, unsigned long);

static unsigned long gptr;
static unsigned long cmd;
int problem; 									//Used to take care of situation where kmalloc return NULL
unsigned long no_of_pages;
static pte_t *gpte;
int exitFlag=0; 								//Denotes if user program exited via exit call or terminated normally
unsigned long *start; 								//used for VM Area analysis
unsigned long *end;  								//used for VM Area analysis
unsigned long *tlbmisses; 							//used to count tlb miss per page
int no_of_VMAs;  
unsigned long endpage; 								//first byte after last page of mmap

struct accounting
{
	int urw; 			//unused = 0, readonly = 1, write = 2
	unsigned long count; 		//denotes read or write count
};

struct accounting *pages; 

static pte_t* get_pte(unsigned long address, unsigned long *addr_vma)
{
        pgd_t *pgd;
        p4d_t *p4d;
        pud_t *pud;
        pmd_t *pmd;
        pte_t *ptep;
        struct mm_struct *mm = current->mm;
        struct vm_area_struct *vma = find_vma(mm, address);
        if(!vma){
		
             printk(KERN_INFO "No vma yet\n");
                 goto nul_ret;
        }
        *addr_vma = (unsigned long) vma;
        pgd = pgd_offset(mm, address);
	if (pgd_none(*pgd) || unlikely(pgd_bad(*pgd)))
                goto nul_ret;
        p4d = p4d_offset(pgd, address);
        if (p4d_none(*p4d))
                goto nul_ret;
        if (unlikely(p4d_bad(*p4d)))
                goto nul_ret;
        pud = pud_offset(p4d, address);
        if (pud_none(*pud))
                goto nul_ret;
        if (unlikely(pud_bad(*pud)))
                goto nul_ret;

        pmd = pmd_offset(pud, address);
        if (pmd_none(*pmd))
                goto nul_ret;
        if (unlikely(pmd_trans_huge(*pmd))){
                printk(KERN_INFO "I am huge\n");
                goto nul_ret;
        }
        ptep = pte_offset_map(pmd, address);
        if(!ptep){
                printk(KERN_INFO "pte_p is null\n\n");
                goto nul_ret;
        }
        return ptep;

        nul_ret:
               return NULL;

}

static ssize_t memtrack_command_show(struct kobject *kobj,
                                  struct kobj_attribute *attr, char *buf)
{
        return sprintf(buf, "%d\n", command);
}


static ssize_t memtrack_command_set(struct kobject *kobj,
                                   struct kobj_attribute *attr,
                                   const char *buf, size_t count)
{
        if(count==2&&(buf[0]=='0'||buf[0]=='1'||buf[0]=='2'))  			//checks if given command is valid
        command = buf[0]-'0';
        else
        {
                command = 0;				       			//if given command is invalid, default value 0 is set to command
        }
        return count;       
}

static struct kobj_attribute memtrack_command_attribute = __ATTR(command,0644,memtrack_command_show, memtrack_command_set);

static ssize_t memtrack_tlb_misses_show(struct kobject *kobj,
                                  struct kobj_attribute *attr, char *buf)
{
        return sprintf(buf, "%lu\n", tlb_misses);
}
static struct kobj_attribute memtrack_tlb_misses_attribute = __ATTR(tlb_misses, 0444,memtrack_tlb_misses_show, NULL);

static ssize_t memtrack_readwss_show(struct kobject *kobj,
                                  struct kobj_attribute *attr, char *buf)
{
        return sprintf(buf, "%lu\n", readwss);
}
static struct kobj_attribute memtrack_readwss_attribute = __ATTR(readwss, 0444,memtrack_readwss_show, NULL);

static ssize_t memtrack_writewss_show(struct kobject *kobj,
                                  struct kobj_attribute *attr, char *buf)
{
        return sprintf(buf, "%lu\n", writewss);
}
static struct kobj_attribute memtrack_writewss_attribute = __ATTR(writewss, 0444,memtrack_writewss_show, NULL);


static ssize_t memtrack_unused_show(struct kobject *kobj,
                                  struct kobj_attribute *attr, char *buf)
{
        return sprintf(buf, "%lu\n", unused);
}
static struct kobj_attribute memtrack_unused_attribute = __ATTR(unused, 0444,memtrack_unused_show, NULL);
static struct attribute *memtrack_attrs[] = {
        &memtrack_command_attribute.attr,
        &memtrack_tlb_misses_attribute.attr,
        &memtrack_readwss_attribute.attr,
        &memtrack_writewss_attribute.attr,
        &memtrack_unused_attribute.attr,
        NULL,
};
struct attribute_group memtrack_attr_group = {
        .attrs = memtrack_attrs,
        .name = "memtrack",
};



static int fault_hook(struct mm_struct *mm, struct pt_regs *regs, unsigned long error_code, unsigned long address)
{
   /*TODO Fault handler*/
	unsigned long vma;
	gpte = get_pte(address, &vma);
	if(gpte && address< endpage && address >= gptr) {
               	volatile unsigned long x,cr3;
		gpte->pte &= ~(0x1UL << 50);
		if(command != 1) 					//No PTI
		{ 						
			stac();
			x = *((unsigned long *) address);
			clac();
		}
		else 							//PTI
		{
			__asm__ __volatile__("mov %%cr3, %0;" 		//move cr3 register value to variable cr3

				:"=r" (cr3)
				:
				:"memory");

			cr3|= (0x1UL << 11);				//Make ASID bit as 1

				
			//move the changed value to cr3 register
			 __asm__ __volatile__("mov %0, %%cr3;"
                                :
				:"r" (cr3)
                                );

			//Access the address
			stac();
                	x = *((unsigned long *) address);
                	clac();
			cr3&= ~(0x1UL << 11); 				//Revert the change made to ASID
			__asm__ __volatile__("mov %0, %%cr3;"  		//Put the value back to cr3 register
                                :
				:"r" (cr3)
                                );
		}
		if(problem == 0)
			gpte->pte |= 0x1UL<<50;
		tlb_misses+=1;						//increment TLB Miss Count
		tlbmisses[(address>>12)-(gptr>>12)]+=1;			//Increment TLB Miss Count of the page
		checkReadWriteCount(address,error_code);		//Make changes in readcount and writecount of pages accordingly
		return 0;
	}
	return -1;
}

void checkReadWriteCount(unsigned long address, unsigned long error_code)
{
	
	unsigned long pageIndex;
	pageIndex = (address>>12) - (gptr>>12);

	
	if(error_code == 0xd && (pages[pageIndex].urw == 0 || pages[pageIndex].urw == 1)) 		//read access
	{
		if(pages[pageIndex].urw == 0)  								//if page was previously unused
		{	readwss++; 
			unused--;
		}

		pages[pageIndex].urw = 1;   								//set to readonly page
		pages[pageIndex].count+=1;  								//increment readcount
	}
	else if(error_code == 0xf) 									//write access
	{
		if(pages[pageIndex].urw == 1)  								//if previously readonly page
		{	
			readwss--; writewss++;
			pages[pageIndex].urw = 2;  							//set to write-page
			pages[pageIndex].count=1; 							//reset the count as writecount now instead of readcount
		}
		else 
		{
			if(pages[pageIndex].urw == 0) { writewss++; unused--; }
			pages[pageIndex].urw = 2; 							//set to write-page
			pages[pageIndex].count++; 							//increment writecount
		}

		
	}
	
}

//Helper Function - Not Used Anymore
void printReadWriteCount(void)
{
        int i;
        for( i=0; i<no_of_pages; i++)
                printk(KERN_INFO "PageIndex [%d] Page [%lx] urw [%d] count[%ld]\n",i,gptr+(i<<12),pages[i].urw,pages[i].count);
}

//Helper Function - Not Used Anymore
void printTlbMissCount(void)
{
	int i;
	for( i=0; i<no_of_pages; i++)
		printk(KERN_INFO "PageIndex [%d] Page [%lx] Tlbmisscount [%ld]\n",i,gptr+(i<<12),tlbmisses[i]);
}

ssize_t handle_read(char *buff, size_t length)
{
   /*TODO Read handler*/
	
	int i;
	unsigned long extraPages; 								//extra pages appended to mmap's VMA
 	struct mm_struct *mm;
	struct vm_area_struct *vm;
	struct read_command *rc;
	if(problem ==1) 									//used in situations such as when allocation via kmalloc is unsuccessful
	{ 
		int success;
		success = poisonpages(0);
		return -1;
	}
	stac();
	cmd = *((unsigned long *)buff);	
	rc = (struct read_command*)buff;
	clac();
	if(cmd == FAULT_START) 
	{
		mm= current->mm;
		vm = find_vma(mm, gptr);
		endpage = vm->vm_end; 								//endpage is the byte just after the last page of mmap
		
		if(gptr == vm->vm_start) 							//to check if there are extra pages at the end of mmap's pages
		{
			for(i=0; i< no_of_VMAs; i++) 						//find previous VMA that this mmap would have been appended to
			{
				if(gptr< end[i])
					break;
			}
			
			if(i != no_of_VMAs) //if such VMA exists
			{	
				//if gptr falls in the range of the VMA found, then there may be extra pages at the end of VMA after mmap's pages
				//modify endpage to have the value of the actual byte just after the last page that the user process mmapped
				if(gptr < start[i] && end[i] == vm->vm_end) 
				{
					extraPages = end[i] - start[i];
					endpage = vm->vm_end - extraPages;
				}
			}
		}

		no_of_pages =(endpage>>12) - (gptr>>12);
		if(!allocateperpage()) problem = 1; 					//allocates necessary data structures and initializes them, checks if successful
		if(problem == 1 ) 
		{ 
			int success;
			success = poisonpages(0);
			return -1; 
		}
		if (poisonpages(1)) 
		return 0;
		return -1;
		
	}
	else if(cmd == TLBMISS_TOPPERS)
	{
		int success;
		if(command !=2) 
		{
			readwss=0; writewss=0; unused=0;
			if(problem == 1)
                        	return -1;
			success = poisonpages(0);
        		findTlbToppers(rc);						//find Tlb Toppers and add them to struct read_command
		}
		else									// command = 2 (TLBMISS_TOPPERS not permitted)
		{	
			tlb_misses = 0;
			exitFlag = 1;							//set exitFlag denoting that user program will terminate via exit call
			success = poisonpages(0);					//unpoison all pages
			return -1;
		}
		
	}
	else if(cmd == READ_TOPPERS)
	{	
		int success;
		if(command == 2) 
		{
			if(problem == 1)
                        	return -1;
			tlb_misses = 0;
                	success = poisonpages(0);
                	findReadToppers(rc);						//find read Toppers and add them to struct read_command
		}
		else									// command = 0 or 1 (READ_TOPPERS not permitted)
		{
			readwss=0; writewss=0; unused=0;
			exitFlag = 1;
			success = poisonpages(0);
			return -1;
		}

	}
	else if(cmd == WRITE_TOPPERS)							
	{	
		int success;
		if(command == 2) 
		{
			if(problem == 1)
                        	return -1;
                	tlb_misses = 0;
                	success = poisonpages(0);
                	findWriteToppers(rc);						
		}
		else
                {
                        readwss=0; writewss=0; unused=0;
			exitFlag = 1;
			success = poisonpages(0);
			return -1;
                }
	}
	else
		return -1;

   return 0;
}


int allocateperpage(void) //allocates memory to store tlbmisscount per page
{
	int i;
	tlbmisses = kmalloc(no_of_pages * sizeof(unsigned long), GFP_KERNEL);
	if (!tlbmisses)
	{	
		problem = 1;
	        return 0;
	}
	for ( i=0; i<no_of_pages; i++)
		tlbmisses[i] = 0;
	
	pages = kmalloc(no_of_pages * sizeof(struct accounting), GFP_KERNEL);
	if(!pages) 
	{
                problem = 1;
		return 0;

	}
	for(i=0; i<no_of_pages;i++)
	{
		pages[i].urw = 0;
		pages[i].count = 0;
	}
	unused = no_of_pages;
	return 1;
}

int poisonpages(int flag) //flag = 1 to poison, flag = 0 to unpoison
{
	unsigned  long vma, page;
	int counter;
	page = gptr;
	counter = 0;
	while (page < endpage)
	{	
		gpte = get_pte(page, &vma);
		if(flag == 1)
			gpte->pte|= 0x1UL << 50; 					//poison page
		else
			gpte->pte &= ~(0x1UL << 50); 					//unpoison  page
		page = page + (1 <<12);
		counter ++;
	}
	return counter;  								//Number of pages poisoned
}

void findReadToppers(struct read_command *rc)
{
        int toppers[5];  			//stores index of the toppers, say, page 0, page 1, etc
        int i;

        for(i=0;i<5;i++)
                toppers[i] = -1;

        for( i=0; i< no_of_pages && pages[i].urw ==1; i++)
        {
                if (toppers[0] == -1 || pages[i].count>=pages[toppers[0]].count)
                {
                        int j;
                        for(j=4; j>0; j--)
                                toppers[j] = toppers[j-1];
                        toppers[0] = i;
                }
                else if(toppers[1] == -1 || pages[i].count>=pages[toppers[1]].count)
                {
                        int j;
                        for(j=4; j>1; j--)
                                toppers[j] = toppers[j-1];
                        toppers[1] = i;

                }
                else if(toppers[2] == -1 || pages[i].count>=pages[toppers[2]].count)
                {
                        int j;
                        for(j=4; j>2; j--)
                                toppers[j] = toppers[j-1];
                        toppers[2] = i;

                }
                else if(toppers[3] == -1 || pages[i].count>=pages[toppers[3]].count)
                {
                        int j;
                        for(j=4; j>3; j--)
                                toppers[j] = toppers[j-1];
                        toppers[3] = i;
                }
                else if(toppers[4] == -1 || pages[i].count>=pages[toppers[4]].count)
                {
                        toppers[4] = i;
                }
        }
        
        //fill struct read_command
	stac();
        if(no_of_pages>=5) rc->valid_entries=5;
        else rc->valid_entries=no_of_pages;
        for(i=0;i<rc->valid_entries;i++)
        {
                rc->toppers[i].vaddr = gptr + (toppers[i]<<12);
                rc->toppers[i].count = pages[toppers[i]].count;
        }
        clac();
}

void findWriteToppers(struct read_command *rc)
{
        int toppers[5];
        int i;

        for(i=0;i<5;i++)
                toppers[i] = -1;

        for( i=0; i< no_of_pages && pages[i].urw == 2; i++)
        {
                if(toppers[0] == -1 || pages[i].count>=pages[toppers[0]].count)
                {
                        int j;
                        for(j=4; j>0; j--)
                                toppers[j] = toppers[j-1];
                        toppers[0] = i;
                }
                else if(toppers[1] == -1 || pages[i].count>=pages[toppers[1]].count)
                {
                        int j;
                        for(j=4; j>1; j--)
                                toppers[j] = toppers[j-1];
                        toppers[1] = i;

                }
                else if(toppers[2] == -1 || pages[i].count>=pages[toppers[2]].count)
                {
                        int j;
                        for(j=4; j>2; j--)
                                toppers[j] = toppers[j-1];
                        toppers[2] = i;

                }
                else if(toppers[3] == -1 || pages[i].count>=pages[toppers[3]].count)
                {
                        int j;
                        for(j=4; j>3; j--)
                                toppers[j] = toppers[j-1];
                        toppers[3] = i;
                }
                else if(toppers[4] == -1 || pages[i].count>=pages[toppers[4]].count)
                {
                        toppers[4] = i;
                }
        }
	
	stac();
        if(no_of_pages>=5) rc->valid_entries=5;
        else rc->valid_entries=no_of_pages;
        for(i=0;i<rc->valid_entries;i++)
        {


                rc->toppers[i].vaddr = gptr + (toppers[i]<<12);
                rc->toppers[i].count = pages[toppers[i]].count;
        }
        clac();
}


void findTlbToppers(struct read_command *rc)
{
	int toppers[5];
	int i;
	
	for(i=0;i<5;i++)
		toppers[i] = -1;

	for( i=0; i< no_of_pages; i++)
	{
		if(toppers[0] == -1 || tlbmisses[i]>=tlbmisses[toppers[0]])
		{	
			int j;
			for(j=4; j>0; j--)
				toppers[j] = toppers[j-1];
			toppers[0] = i;
		}
		else if(toppers[1] == -1 || tlbmisses[i]>=tlbmisses[toppers[1]])
		{
			int j;
                        for(j=4; j>1; j--)
                                toppers[j] = toppers[j-1];
                        toppers[1] = i;

		}
		else if(toppers[2] == -1 || tlbmisses[i]>=tlbmisses[toppers[2]])	
		{
			int j;
                        for(j=4; j>2; j--)
                                toppers[j] = toppers[j-1];
                        toppers[2] = i;

		}
		else if(toppers[3] == -1 || tlbmisses[i]>=tlbmisses[toppers[3]])
		{
			int j;
                        for(j=4; j>3; j--)
                                toppers[j] = toppers[j-1];
                        toppers[3] = i;
		}
		else if(toppers[4] == -1 || tlbmisses[i]>= tlbmisses[toppers[4]])
		{
			toppers[4] = i;
		}
	}

	stac();
	if(no_of_pages>=5) rc->valid_entries=5;
	else rc->valid_entries=no_of_pages;
	for(i=0;i<rc->valid_entries;i++)
	{		
		

		rc->toppers[i].vaddr = gptr + (toppers[i]<<12);
		rc->toppers[i].count = tlbmisses[toppers[i]];
	}
	clac();

}

ssize_t handle_write(const char *buff, size_t lenth)
{
   /*TODO Write handler*/
	stac();
   	gptr = *((unsigned long *)buff);
    	clac();
    	printk(KERN_INFO "In write %lx\n", gptr);
 	return 8;

}

void printVMA(void)
{

	int i;
	struct mm_struct *mm;
	struct vm_area_struct *vm;
	mm = current->mm;
	vm = mm->mmap;
	no_of_VMAs = mm->map_count; 			
	start = kmalloc(no_of_VMAs*sizeof(unsigned long), GFP_KERNEL);
	end = kmalloc(no_of_VMAs*sizeof(unsigned long), GFP_KERNEL);
	if(!start || !end) 
	{
		problem = 1;
		return;
	}
	for( i=0; i<no_of_VMAs; i++) //stores start and end of all VMAs
	{
		start[i] = vm->vm_start;
		end[i] = vm->vm_end;
		vm = vm->vm_next;
	}
}

int handle_open(void)
{
   /*TODO open handler*/
   	page_fault_pid = current->pid;
      	rsvd_fault_hook = &fault_hook;
	tlb_misses = 0;
	readwss = 0;
	writewss = 0;
	unused = 0;
	problem = 0;
	printVMA();  //finds start and end of all VMAs before mmap 
   	return 0;
}

int handle_close(void)
{
   /*TODO open handler*/
	if(!exitFlag) 				//if program didn't terminate by an exit() call 
	{
		int success;
		success = poisonpages(0); 	//unpoison all pages
	}
	else exitFlag=0;
	page_fault_pid = -1;
	rsvd_fault_hook = 0;

	//Deallocating
	if(start) kfree(start);
	if(end) kfree(end);
	if(tlbmisses) kfree(tlbmisses);
	if(pages) kfree(pages);
   return 0;
}
