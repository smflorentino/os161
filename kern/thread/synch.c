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

/*
 * Synchronization primitives.
 * The specifications of the functions are in synch.h.
 */

#include <types.h>
#include <lib.h>
#include <spinlock.h>
#include <wchan.h>
#include <thread.h>
#include <current.h>
#include <synch.h>

////////////////////////////////////////////////////////////
//
// Semaphore.

struct semaphore *
sem_create(const char *name, int initial_count)
{
        struct semaphore *sem;

        KASSERT(initial_count >= 0);

        sem = kmalloc(sizeof(struct semaphore));
        if (sem == NULL) {
                return NULL;
        }

        sem->sem_name = kstrdup(name);
        if (sem->sem_name == NULL) {
                kfree(sem);
                return NULL;
        }

	sem->sem_wchan = wchan_create(sem->sem_name);
	if (sem->sem_wchan == NULL) {
		kfree(sem->sem_name);
		kfree(sem);
		return NULL;
	}

	spinlock_init(&sem->sem_lock);
        sem->sem_count = initial_count;

        return sem;
}

void
sem_destroy(struct semaphore *sem)
{
        KASSERT(sem != NULL);

	/* wchan_cleanup will assert if anyone's waiting on it */
	spinlock_cleanup(&sem->sem_lock);
	wchan_destroy(sem->sem_wchan);
        kfree(sem->sem_name);
        kfree(sem);
}

//decrement (lock)

void 
P(struct semaphore *sem)
{
        KASSERT(sem != NULL);

        /*
         * May not block in an interrupt handler.
         *
         * For robustness, always check, even if we can actually
         * complete the P without blocking.
         */
        KASSERT(curthread->t_in_interrupt == false);

	spinlock_acquire(&sem->sem_lock);
        while (sem->sem_count == 0) {
		/*
		 * Bridge to the wchan lock, so if someone else comes
		 * along in V right this instant the wakeup can't go
		 * through on the wchan until we've finished going to
		 * sleep. Note that wchan_sleep unlocks the wchan.
		 *
		 * Note that we don't maintain strict FIFO ordering of
		 * threads going through the semaphore; that is, we
		 * might "get" it on the first try even if other
		 * threads are waiting. Apparently according to some
		 * textbooks semaphores must for some reason have
		 * strict ordering. Too bad. :-)
		 *
		 * Exercise: how would you implement strict FIFO
		 * ordering?
		 */
		wchan_lock(sem->sem_wchan);
		spinlock_release(&sem->sem_lock);
                wchan_sleep(sem->sem_wchan);

		spinlock_acquire(&sem->sem_lock);
        }
        KASSERT(sem->sem_count > 0);
        sem->sem_count--;
	spinlock_release(&sem->sem_lock);
}

//increment (unlock)

void
V(struct semaphore *sem)
{
        KASSERT(sem != NULL);

	spinlock_acquire(&sem->sem_lock);

        sem->sem_count++;
        KASSERT(sem->sem_count > 0);
	wchan_wakeone(sem->sem_wchan);

	spinlock_release(&sem->sem_lock);
}

////////////////////////////////////////////////////////////
//
// Lock.

struct lock *
lock_create(const char *name)
{
        struct lock *lock;

        lock = kmalloc(sizeof(struct lock));
        if (lock == NULL) {
                return NULL;
        }

        lock->lk_name = kstrdup(name);
        if (lock->lk_name == NULL) {
                kfree(lock);
                return NULL;
        }

        //Initialize Lock Wait Channel
        lock->lk_wchan = wchan_create(lock->lk_name);
        if(lock->lk_wchan == NULL) {
            kfree(lock->lk_name);
            kfree(lock);
            return NULL;
        }


        //Lock is initially unlocked
        lock->lk_locked = false;

        //Initialize Lock Spinlock
        spinlock_init(&lock->lk_spinlock);
        
        // add stuff here as needed
        
        return lock;
}

//Need not be atomic
void
lock_destroy(struct lock *lock)
{
        KASSERT(lock != NULL);
        KASSERT(lock->lk_locked == false);

        //Clean up the lock.
        spinlock_cleanup(&lock->lk_spinlock);
        //Clean up the wait channel (asserts if pending threads are present)
        wchan_destroy(lock->lk_wchan);
        //Clean up data structures
        kfree(lock->lk_name);
        kfree(lock);
}

//ATOMIC
void
lock_acquire(struct lock *lock)
{
        KASSERT(lock != NULL);
        KASSERT(curthread != NULL);
        // kprintf("Acquired LOck...\n");
        //Ensure this operation is atomic
        spinlock_acquire(&lock->lk_spinlock);
        
        while(lock->lk_locked)
        {
            /*
                Lock the wait channel,
                just in case someone else is trying to
                acquire the lock at the same time
            */
            wchan_lock(lock->lk_wchan);
            //After locking the Channel, release the spinlock and then sleep.
            spinlock_release(&lock->lk_spinlock);
            wchan_sleep(lock->lk_wchan);
            //When we wake up, get the spinlock again so we can properly set lock bit.
            spinlock_acquire(&lock->lk_spinlock);
        }
            //Sanity Check - Make sure we're unlocked.
            KASSERT(lock->lk_locked == false);
            //Lock the lock!
            lock->lk_locked = true;
            lock->lk_owner = curthread;
            //Now, release the spinlock.
            spinlock_release(&lock->lk_spinlock);
        // kprintf("got lock\n");
}

//ATOMIC
void
lock_release(struct lock *lock)
{
        // kprintf("Release lock\n");
        KASSERT(lock != NULL);
        KASSERT(curthread != NULL);

        //Ensure this operation is atomic.
        spinlock_acquire(&lock->lk_spinlock);
        
        //Ensure we are actually locked.
        KASSERT(lock->lk_locked == true);
        //Ensure the owner of the lock is requesting an unlock, and no one else.
        KASSERT(lock->lk_owner == curthread);
        //Unlock the lock.
        lock->lk_locked = false;
        lock->lk_owner = NULL;
        //Notify those waiting for the lock.
        wchan_wakeone(lock->lk_wchan);
        //End of Atomic Operation.
        spinlock_release(&lock->lk_spinlock);
        // kprintf("release lock\n");
}

//ATOMIC
bool
lock_do_i_hold(struct lock *lock)
{
        KASSERT(lock != NULL);
        KASSERT(curthread != NULL);

        //Ensure this operation is atomic
        spinlock_acquire(&lock->lk_spinlock);

        //Check if we are the thread that locked this lock.
        bool result = (curthread == lock->lk_owner);

        //End Atomic Operation
        spinlock_release(&lock->lk_spinlock);

        //Return our result
        return result;
}

////////////////////////////////////////////////////////////
//
// CV


struct cv *
cv_create(const char *name)
{
        struct cv *cv;

        cv = kmalloc(sizeof(struct cv));
        if (cv == NULL) {
                return NULL;
        }

        cv->cv_name = kstrdup(name);
        if (cv->cv_name==NULL) {
                kfree(cv);
                return NULL;
        }

        //Initialize the CV Wait Channel
        cv->cv_wchan = wchan_create(cv->cv_name);
        if(cv->cv_wchan == NULL) {
            kfree(cv->cv_name);
            kfree(cv);
            return NULL;
        }

        //Initialize CV Internal Lock
        cv->cv_intlock = lock_create(cv->cv_name);
        cv->sup_lock = NULL;

        return cv;
}

//Need not be atomic
void
cv_destroy(struct cv *cv)
{
        KASSERT(cv != NULL);
        // KASSERT(cv->sup_lock == NULL); <- we let who ever supplied the lock worry about it.

        //Clean up the CV
        lock_destroy(cv->cv_intlock);
        //Clean up the wait channel (asserts if pending threads are present)
        wchan_destroy(cv->cv_wchan);

        //Clean up data structures
        // lock_destroy(cv->sup_lock); <- cleaned up by the provider of the outside lock
        kfree(cv->cv_name);
        kfree(cv);
}

//ATOMIC
void
cv_wait(struct cv *cv, struct lock *lock)
{
        KASSERT(cv != NULL);
        //Ensure this operation is atomic
        lock_acquire(cv->cv_intlock);

        /*
            Lock the wait channel, so we can
            go to sleep later. wchan_sleep()
            will automatically unlock the wait
            channel.
        */
        wchan_lock(cv->cv_wchan);
        //Ensure we have the lock.
        KASSERT(lock_do_i_hold(lock));
        if(cv->sup_lock == NULL)
        {
            cv->sup_lock = lock;
        }
        //Release supplied lock
        lock_release(lock);
        //Release the internal lock & go to sleep
        lock_release(cv->cv_intlock);
        wchan_sleep(cv->cv_wchan);
        //When we wake up, try to get the lock back.
        lock_acquire(lock);
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
        KASSERT(cv != NULL);
        //Ensure this operation is atomic
        lock_acquire(cv->cv_intlock);
                if(cv->sup_lock == NULL)
        {
            cv->sup_lock = lock;
        }
        KASSERT(cv->sup_lock != NULL);
        //Ensure we have the lock.
        KASSERT(cv->sup_lock == lock);
        KASSERT(lock_do_i_hold(lock));
        //We're sure we have the lock. Now wake up a thread.
        wchan_wakeone(cv->cv_wchan);
        //End Atomic Operation
        lock_release(cv->cv_intlock);
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
        KASSERT(cv != NULL);

        //Ensure this operation is atomic
        lock_acquire(cv->cv_intlock);
                if(cv->sup_lock == NULL)
        {
            cv->sup_lock = lock;
        }
        KASSERT(cv->sup_lock != NULL);
        //Ensure we have the lock.
        KASSERT(cv->sup_lock == lock);
        KASSERT(lock_do_i_hold(lock));
        //We're sure we have the lock. Now wake up all threadsi
        wchan_wakeall(cv->cv_wchan);
        //End Atomic Operation
        lock_release(cv->cv_intlock);
}

////////////////////////////////////////////////////////////
//
// Reader-Writer Lock

struct rwlock *
rwlock_create(const char *name)
{
        struct rwlock *rwlock;

        rwlock = kmalloc(sizeof(struct rwlock));
        if (rwlock == NULL) {
            return NULL;
        }

        rwlock->rwlock_name = kstrdup(name);
        if(rwlock->rwlock_name == NULL)
        {
            kfree(rwlock);
            return NULL;
        }

        //Initialize Reader WChan
        rwlock->rwlock_rch = wchan_create(rwlock->rwlock_name);
            if(rwlock->rwlock_rch == NULL) {
            kfree(rwlock->rwlock_name);
            kfree(rwlock);
            return NULL;
        };

        //Initialize Writer WChan
        rwlock->rwlock_wch = wchan_create(rwlock->rwlock_name);
            if(rwlock->rwlock_wch == NULL) {
            wchan_destroy(rwlock->rwlock_rch);
            kfree(rwlock->rwlock_name);
            kfree(rwlock);
            return NULL;
        };

        //Initialize RWL Internal Lock
        rwlock->rwlock_intlock = lock_create(rwlock->rwlock_name);
        if(rwlock->rwlock_intlock == NULL)
        {
            wchan_destroy(rwlock->rwlock_rch);
            wchan_destroy(rwlock->rwlock_wch);
            lock_destroy(rwlock->rwlock_intlock);
            kfree(rwlock->rwlock_name);
            kfree(rwlock);
            return NULL;   
        }

        rwlock->readers = 0;
        rwlock->writers = 0;

        return rwlock;
}

//Need not be atomic?
void
rwlock_destroy(struct rwlock *rwlock)
{
    KASSERT(rwlock != NULL);

    lock_destroy(rwlock->rwlock_intlock);

    //Clean up internal data
    wchan_destroy(rwlock->rwlock_rch);
    wchan_destroy(rwlock->rwlock_wch);
    kfree(rwlock->rwlock_name);
    kfree(rwlock);
}

void 
rwlock_acquire_read(struct rwlock *rwlock)
{
    KASSERT(rwlock != NULL);
    //Ensure this operation is atomic
    lock_acquire(rwlock->rwlock_intlock);
    //See if there are active or pending writers. If so, wait.
    while(rwlock->rwlock_writer != NULL || rwlock->writers != 0)
    {
        //Lock the wait channel, so we can sleep before someone else wakes up.
        wchan_lock(rwlock->rwlock_rch);
        lock_release(rwlock->rwlock_intlock);
        wchan_sleep(rwlock->rwlock_rch);
        //When we wake up, continue atomic operation
        lock_acquire(rwlock->rwlock_intlock);
    }
    //Begin Read Lock
    rwlock->readers++;

    //End Atomic Operation
    lock_release(rwlock->rwlock_intlock);

}

//ATOMIC(?)
void
rwlock_release_read(struct rwlock *rwlock)
{
    KASSERT(rwlock != NULL);
    //Ensure this operation is atomic
    lock_acquire(rwlock->rwlock_intlock);
    //Decrement the reader count
    rwlock->readers--;
    //If we were the last reader, release a writer if one exists:
    if(rwlock->readers == 0 && rwlock->writers > 0)
    {
        wchan_wakeone(rwlock->rwlock_wch);
    }
    //End Atomic Operation
    lock_release(rwlock->rwlock_intlock);
}

//ATOMIC(?)
void
rwlock_acquire_write(struct rwlock *rwlock)
{
    KASSERT(rwlock != NULL);
    //Ensure this operation is atomic
    lock_acquire(rwlock->rwlock_intlock);
    rwlock->writers++;
    //See if there are any readers or a writer; if so, wait.
    while(rwlock->readers > 0 || rwlock->rwlock_writer != NULL)
    {
        //Lock the wait channel, so we can sleep before someone else wakes up.
        wchan_lock(rwlock->rwlock_wch);
        lock_release(rwlock->rwlock_intlock);
        wchan_sleep(rwlock->rwlock_wch);
        //When we wake up, continue atomic operation
        lock_acquire(rwlock->rwlock_intlock);
    }
    rwlock->writers--;
    rwlock->rwlock_writer=curthread;

    //End Atomic Operation
    lock_release(rwlock->rwlock_intlock);
}

//ATOMIC(?)
void
rwlock_release_write(struct rwlock *rwlock)
{
    KASSERT(rwlock != NULL);
    //Ensure this operation is atomic (shouldn't have to be, but this is in case someone misbehaves)
    lock_acquire(rwlock->rwlock_intlock);
    //Ensure we actually hold this lock
    KASSERT(rwlock->rwlock_writer == curthread);
    //Unlock it
    rwlock->rwlock_writer = NULL;
    //Release the readers:
    wchan_wakeall(rwlock->rwlock_rch);
    wchan_wakeone(rwlock->rwlock_wch);
    //End Atomic Operation
    lock_release(rwlock->rwlock_intlock);
 }
