#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/fcntl.h>
#include<signal.h>
#include<sys/ioctl.h>
#include<sys/mman.h>
#include <sys/unistd.h>

#include "module/interface.h"

#define MAX_MEM_PAGES (1 << 15)

int main(int argc, char **argv)
{
	
	struct read_command cmd;
	int fd, ctr;
	char *ptr;
	char buf[100];

	if(argc != 2)
	{
        fprintf(stderr, "Err1: Usage: %s mempages\n", argv[0]);
        exit(-1);        
   	}

   	long mmap_size = atoll(argv[1]);
   	if(mmap_size > MAX_MEM_PAGES || mmap_size <= 0)
   	{
        fprintf(stderr, "Err2: Usage: %s mempages\n", argv[0]);
        exit(-1);        
   	}

   	mmap_size <<= 12;   //Multiplied by page size
   	fd = open("/dev/memtrack", O_RDWR);
   	if(fd < 0)
   	{
       	perror("open");
       	exit(-1);
   	}
   	ptr = mmap(NULL, mmap_size,PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE|MAP_POPULATE, 0,0);
	if(ptr == MAP_FAILED){
        perror("mmap");
        exit(-1);
   	}
   	memset(ptr, 0, mmap_size); //fill with 0
   	printf("Passing pointer %lx\n", (unsigned long)ptr);
   	*((unsigned long *)buf) = (unsigned long)ptr;
   	if(write(fd, buf, 8) < 0)
   	{
	perror("read");
    	exit(-1);
   	}
   	cmd.command = FAULT_START;
	cmd.valid_entries = 0;
     	printf("After setting command\n");
//	*ptr = 100;
	
	if(read(fd, &cmd, sizeof(cmd)) < 0)
	{
	     perror("read");
	     exit(-1);
	}
   
	printf("After read\n");
	sleep(1);
	/*
	for(ctr=0; ctr<10; ++ctr){
	      	volatile char ch;
      		char *aptr = ptr + random() % mmap_size;
		printf("Accessing [%d]: Address [%p] \n",ctr, aptr);
      		if(random() % 2)
            	*aptr = 1;
      		else
           	ch = *aptr;
		sleep(1);
  	}
	*/
		

	for (ctr=0; ctr<atoi(argv[1]);++ctr)
	{
		volatile char ch;
		int num;
		num = random() % atoi(argv[1]);
		char *aptr = ptr+(num*(1<<12));
		long page = (((unsigned long ) aptr)>>12)-(((unsigned long ) ptr)>>12);
		printf("Accessing [%d]: Address [%p] Page[%ld]\n",ctr, aptr,page );
		if(random() % 2)
            	*aptr = 1;
      		else
           	ch = *aptr;
		sleep(1);
	}
	
	cmd.command = TLBMISS_TOPPERS;
	cmd.valid_entries = 0;
  	if(read(fd, &cmd, sizeof(cmd)) < 0){
     		perror("read");
     		exit(-1);
  	}
  	for(ctr=0; ctr < cmd.valid_entries; ++ctr){
     	struct topper_t *topper = &cmd.toppers[ctr];
     	printf("Rank #%ld va=%lx count = %ld\n", ctr+1, topper->vaddr, topper->count);
  	}
/*	
	cmd.command = READ_TOPPERS;
        cmd.valid_entries = 0;
        if(read(fd, &cmd, sizeof(cmd)) < 0){
                perror("read");
                exit(-1);
        }
        for(ctr=0; ctr < cmd.valid_entries; ++ctr){
        struct topper_t *topper = &cmd.toppers[ctr];
        printf("Rank #%ld va=%lx count = %ld\n", ctr+1, topper->vaddr, topper->count);
        }
	cmd.command = WRITE_TOPPERS;
        cmd.valid_entries = 0;
        if(read(fd, &cmd, sizeof(cmd)) < 0){
                perror("read");
                exit(-1);
        }
*/
        for(ctr=0; ctr < cmd.valid_entries; ++ctr){
        struct topper_t *topper = &cmd.toppers[ctr];
        printf("Rank #%ld va=%lx count = %ld\n", ctr+1, topper->vaddr, topper->count);
        }
	
  	close(fd); 
  	munmap((void *)ptr, mmap_size);
	return 0;
}

