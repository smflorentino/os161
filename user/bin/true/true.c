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
#include <err.h>

/*
 * true - succeed.
 */
#define SIZE 1024 * 114 * 2

static void sort()
{
    static int tmp[SIZE];
    for(int i = 0;i< SIZE;i++)
    {
        tmp[i] = i;
        // printf("VA:%p\n",&tmp[i]);
    }
    for(int i = 0;i<SIZE;i++)
    {
        int index = 1024 - i < 0 ? 2 : 1024-i;
        // printf("i:%d Index: %d\n",i,index);
        // printf("VA: %p\n",&tmp[i]);
        tmp[index] = i;
    }
}

int
main(int argc, char* argv[])
{
    sort();
    (void)argc;
    (void)argv;
	exit(0);
}


// static int A[SIZE];

// static
// void
// initarray(void)
// {
//     int i;

    
//      * Initialize the array, with pseudo-random but deterministic contents.
     
//     srandom(533);

//     for (i = 0; i < SIZE; i++) {        
//         A[i] = random();
//     }
// }
