/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#define KERN_PTR	((void *)0x80000000)	/* addr within kernel */
#define INVAL_PTR	((void *)0x40000000)	/* addr not part of program */
/*
 * true - succeed.
 */
static
void
wait_badstatus(void *ptr, const char *desc)
{
	int rv, pid, x;
	(void)desc;
	pid = fork();
	if (pid<0) {
		// warn("UH-OH: fork failed");
		return;
	}
	if (pid==0) {
		exit(0);
	}

	rv = waitpid(pid, ptr, 0);
	// report_test(rv, errno, EFAULT, desc);
	waitpid(pid, &x, 0);
}

int
main()
{
	// printf("Process ID at start:%d\n", getpid());
	// int result = fork();
	// if(result == 0)
	// {
	// 	printf("Result was 0\n");
	// 	printf("Child ID is: %d\n", getpid());
	// }
	// else
	// {
	// 	int exitcode;
	// 	waitpid(result, &exitcode,0);
	// 	printf("Result was: %d\n", result);
	// 	printf("My process ID is: %d\n", getpid());
	// }
	// int exitcode;
	// wait_badstatus(&exitcode,NULL);
	printf("bad");
	int x = 2;
	wait_badstatus(&x,NULL);
	printf("done");
	// wait_badstatus(KERN_PTR,NULL);
	wait_badstatus(INVAL_PTR,NULL);
	printf("DONE");
	// waitpid(6,INVAL_PTR,0);
	// for(int i=0;i<7;i++)
	// {
	// 	printf("===%d===",i);
	// 	printf("PARENT%d\n",getpid());
	// 	int result = fork();
	// 	int exitcode = 0;

	// 	if(result == 0)
	// 	{
	// 		printf("Child%d\n",getpid());
	// 		exit(550);
	// 	}
	// 	else
	// 	{
	// 		printf("ParentW: %d -> %d %d\n", getpid(),result,exitcode);
	// 		// waitpid(result, &exitcode, 0);
	// 		// printf("PARENTD: %d -> %d %d\n", getpid(),result,exitcode);
	// 	}
	// }
	//Printf with hello world:
	//This calls the write system call!
	// // printf("Hello World");
	// char buffer[] = "Goodbye Cruel World\n";
	// write(1,buffer,20);

	// printf("My process ID is: %d\n", getpid());
	// printf("Hello World");
	// printf("My My process ID is: %d\n", getpid());
	// printf("Should call open now...\n");
	// char to_open[] = "to open\n";
	// //Open file "to open" as read only (O_RDONLY = 0).
	// open(to_open,0);

	// char buffer[] = "Goodbye Cruel World\n";
	// write(1,buffer,20);
	
	// char buffer2[] = "Goodbye Cruel Worl2\n";
	// write(1,buffer2,20);

	// char buffer3[] = "Goodbye Cruel Worl3\n";
	// write(1,buffer3,20);
	
	// char buffer4[] = "Goodbye Cruel Worl4\n";
	// write(1,buffer4,20);

	// printf("My process ID is: %d\n", getpid());
	
	// while(1) {}
	/* Just exit with success. */
	printf("Exiting parent...");
	exit(0);
}
