/*
 * Copyright (c) 2001, 2002, 2009
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
 * Driver code for whale mating problem
 */
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

/*
 * Global locks for Stoplight problem
 */
struct lock *sp_intr_zero = NULL;
struct lock *sp_intr_one = NULL;
struct lock *sp_intr_two = NULL;
struct lock	*sp_intr_three = NULL;

struct lock *male_lock = NULL;
struct lock *female_lock = NULL;
struct lock *match_maker_lock = NULL;
struct lock *thread_count_lock = NULL;

struct cv *thread_count_cv = NULL;

int thread_count = 0;

/*
 * 08 Feb 2012 : GWA : Driver code is in kern/synchprobs/driver.c. We will
 * replace that file. This file is yours to modify as you see fit.
 *
 * You should implement your solution to the whalemating problem below.
 */

// 13 Feb 2012 : GWA : Adding at the suggestion of Isaac Elbaz. These
// functions will allow you to do local initialization. They are called at
// the top of the corresponding driver code.

void whalemating_init() {
	male_lock = lock_create("male");
	female_lock = lock_create("female");
	match_maker_lock = lock_create("match_maker");
	thread_count_lock = lock_create("counter");
	thread_count_cv = cv_create("count_cv");
	return;
}
// 20 Feb 2012 : GWA : Adding at the suggestion of Nikhil Londhe. We don't
// care if your problems leak memory, but if you do, use this to clean up.

void whalemating_cleanup() {
	lock_destroy(male_lock);
	lock_destroy(female_lock);
	lock_destroy(match_maker_lock);
	lock_destroy(thread_count_lock);
	cv_destroy(thread_count_cv);
  return;
}

void
male(void *p, unsigned long which)
{
	struct semaphore * whalematingMenuSemaphore = (struct semaphore *)p;
  	(void)which;
  
  	male_start();
	lock_acquire(male_lock);
	lock_acquire(thread_count_lock);
	thread_count++;
	if (thread_count == 3) {
		cv_broadcast(thread_count_cv, thread_count_lock);
	} else {
		cv_wait(thread_count_cv, thread_count_lock);
	}
	thread_count = 0;
	lock_release(thread_count_lock);
	lock_release(male_lock);		
 	male_end();
	
	// 08 Feb 2012 : GWA : Please do not change this code. This is so that your
	// whalemating driver can return to the menu cleanly.
 	V(whalematingMenuSemaphore);
  	return;
}

void
female(void *p, unsigned long which)
{
	struct semaphore * whalematingMenuSemaphore = (struct semaphore *)p;
  	(void)which;
  
  	female_start();
  	lock_acquire(female_lock);
	lock_acquire(thread_count_lock);
	thread_count++;
	if (thread_count == 3) {
		cv_broadcast(thread_count_cv, thread_count_lock);
	} else {
		cv_wait(thread_count_cv, thread_count_lock);
	}
	thread_count = 0;
	lock_release(thread_count_lock);
	lock_release(female_lock);
  	female_end();
  
	// 08 Feb 2012 : GWA : Please do not change this code. This is so that your
	// whalemating driver can return to the menu cleanly.
	V(whalematingMenuSemaphore);
  	return;
}

void
matchmaker(void *p, unsigned long which)
{
	struct semaphore * whalematingMenuSemaphore = (struct semaphore *)p;
  	(void)which;
  
  	matchmaker_start();
  	lock_acquire(match_maker_lock);
	lock_acquire(thread_count_lock);
	thread_count++;
	if (thread_count == 3) {
		cv_broadcast(thread_count_cv, thread_count_lock);
	} else {
		cv_wait(thread_count_cv, thread_count_lock);
	}
	thread_count = 0;
	lock_release(thread_count_lock);
	lock_release(match_maker_lock);
  	matchmaker_end();
  
	// 08 Feb 2012 : GWA : Please do not change this code. This is so that your
  	// whalemating driver can return to the menu cleanly.
  	V(whalematingMenuSemaphore);
  	return;
}

/*
 * You should implement your solution to the stoplight problem below. The
 * quadrant and direction mappings for reference: (although the problem is,
 * of course, stable under rotation)
 *
 *   | 0 |
 * --     --
 *    0 1
 * 3       1
 *    3 2
 * --     --
 *   | 2 | 
 *
 * As way to think about it, assuming cars drive on the right: a car entering
 * the intersection from direction X will enter intersection quadrant X
 * first.
 *
 * You will probably want to write some helper functions to assist
 * with the mappings. Modular arithmetic can help, e.g. a car passing
 * straight through the intersection entering from direction X will leave to
 * direction (X + 2) % 4 and pass through quadrants X and (X + 3) % 4.
 * Boo-yah.
 *
 * Your solutions below should call the inQuadrant() and leaveIntersection()
 * functions in drivers.c.
 */

// 13 Feb 2012 : GWA : Adding at the suggestion of Isaac Elbaz. These
// functions will allow you to do local initialization. They are called at
// the top of the corresponding driver code.

void stoplight_init() {
  	
	sp_intr_zero = lock_create("zero");
	KASSERT(sp_intr_zero != NULL);	
	
	sp_intr_one = lock_create("one");
	KASSERT(sp_intr_one != NULL);	
	
	sp_intr_two = lock_create("two");
	KASSERT(sp_intr_two != NULL);	
	
	sp_intr_three = lock_create("three");
	KASSERT(sp_intr_three != NULL);	
	return;
}

// 20 Feb 2012 : GWA : Adding at the suggestion of Nikhil Londhe. We don't
// care if your problems leak memory, but if you do, use this to clean up.

void stoplight_cleanup() {
  	lock_destroy(sp_intr_zero);
  	lock_destroy(sp_intr_one);
  	lock_destroy(sp_intr_two);
  	lock_destroy(sp_intr_three);
	return;
}

void
gostraight(void *p, unsigned long direction)
{
	struct semaphore * stoplightMenuSemaphore = (struct semaphore *)p;
 	switch (direction) {
		case 0:
			lock_acquire(sp_intr_zero);
			lock_acquire(sp_intr_three);
			
			inQuadrant(0);
			inQuadrant(3);
			lock_release(sp_intr_zero);
			
			leaveIntersection();
			lock_release(sp_intr_three);
			break;
		case 1:
			lock_acquire(sp_intr_zero);
			lock_acquire(sp_intr_one);
			
			inQuadrant(1);
			inQuadrant(0);
			lock_release(sp_intr_one);
			
			leaveIntersection();
			lock_release(sp_intr_zero);
			break;
		case 2:
			lock_acquire(sp_intr_one);
			lock_acquire(sp_intr_two);
			
			inQuadrant(2);
			inQuadrant(1);
			lock_release(sp_intr_two);
			
			leaveIntersection();
			lock_release(sp_intr_one);
			break;
		case 3:
			lock_acquire(sp_intr_two);
			lock_acquire(sp_intr_three);
			
			inQuadrant(3);
			inQuadrant(2);
			lock_release(sp_intr_three);
			
			leaveIntersection();
			lock_release(sp_intr_two);
			break;
		default:
			KASSERT(false);
	}
  
  	// 08 Feb 2012 : GWA : Please do not change this code. This is so that your
  	// stoplight driver can return to the menu cleanly.
  	V(stoplightMenuSemaphore);
 	return;
}

void
turnleft(void *p, unsigned long direction)
{
	struct semaphore * stoplightMenuSemaphore = (struct semaphore *)p;
	switch (direction) {
		case 0:
			lock_acquire(sp_intr_zero);
			lock_acquire(sp_intr_two);
			lock_acquire(sp_intr_three);
			
			inQuadrant(0);
			inQuadrant(3);
			lock_release(sp_intr_zero);
			inQuadrant(2);
			lock_release(sp_intr_three);
			
			leaveIntersection();
			lock_release(sp_intr_two);
			break;
		case 1:
			lock_acquire(sp_intr_zero);
			lock_acquire(sp_intr_one);
			lock_acquire(sp_intr_three);
			
			inQuadrant(1);
			inQuadrant(0);
			lock_release(sp_intr_one);
			inQuadrant(3);
			lock_release(sp_intr_zero);
			
			leaveIntersection();
			lock_release(sp_intr_three);
			break;
		case 2:
			lock_acquire(sp_intr_zero);
			lock_acquire(sp_intr_one);
			lock_acquire(sp_intr_two);
			
			inQuadrant(2);
			inQuadrant(1);
			lock_release(sp_intr_two);
			
			inQuadrant(0);
			lock_release(sp_intr_one);
			
			leaveIntersection();
			lock_release(sp_intr_zero);
			break;
		case 3:
			lock_acquire(sp_intr_one);
			lock_acquire(sp_intr_two);
			lock_acquire(sp_intr_three);
			
			inQuadrant(3);
			inQuadrant(2);
			lock_release(sp_intr_three);
			
			inQuadrant(1);
			lock_release(sp_intr_two);
			
			leaveIntersection();
			lock_release(sp_intr_one);
			break;
		default:
			KASSERT(false);
 	} 
  	// 08 Feb 2012 : GWA : Please do not change this code. This is so that your
  	// stoplight driver can return to the menu cleanly.
  	V(stoplightMenuSemaphore);
  	return;
}

void
turnright(void *p, unsigned long direction)
{
	struct semaphore * stoplightMenuSemaphore = (struct semaphore *)p;
 	switch (direction) {
		case 0:
			lock_acquire(sp_intr_zero);
			inQuadrant(0);
			leaveIntersection();
			lock_release(sp_intr_zero);
			break;
		case 1:
			lock_acquire(sp_intr_one);
			inQuadrant(1);
			leaveIntersection();
			lock_release(sp_intr_one);
			break;
		case 2:
			lock_acquire(sp_intr_two);
			inQuadrant(2);
			leaveIntersection();
			lock_release(sp_intr_two);
			break;
		case 3:
			lock_acquire(sp_intr_three);
			inQuadrant(3);
			leaveIntersection();
			lock_release(sp_intr_three);
			break;
		default:
			KASSERT(false);
	}

  	// 08 Feb 2012 : GWA : Please do not change this code. This is so that your
  	// stoplight driver can return to the menu cleanly.
  	V(stoplightMenuSemaphore);
  	return;
}
