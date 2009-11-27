#include "libfiber.h"

#include <sys/types.h> // For pid_t
#include <sys/wait.h> // For wait
#include <signal.h> // For SIGCHLD
#include <sched.h> // For clone
#include <unistd.h> // For getpid

#include <malloc.h>

/* The Fiber Structure
*  Contains the information about individual fibers.
*/
typedef struct
{
	pid_t pid; // The pid of the child thread as returned by clone
	void* stack; // The stack pointer
} fiber;

// The fiber "queue"
static fiber fiberList[ MAX_FIBERS ];
// The pid of the parent process
static pid_t parentPid;
// The number of active fibers
static int numFibers = 0;

// Initialize the fibers to null
void initFibers()
{
	int i;
	for ( i = 0; i < MAX_FIBERS; ++ i )
	{
		fiberList[i].pid = 0;
		fiberList[i].stack = 0;
	}
	
	parentPid = getpid();
}

// Call the sched_yield system call which moves the current process to the
// end of the process queue
void fiberYield()
{
	sched_yield();
}

// Exists to give the proper function type to clone
static int fiberStart( void* arg )
{
	LF_DEBUG_OUT( "Child created and calling function = 0x%x", (int) arg );
	((void (*)()) arg)();
	return 0;
}

int spawnFiber( void (*func)(void) )
{
	if ( numFibers == MAX_FIBERS ) return LF_MAXFIBERS;

	// Allocate the stack
	fiberList[numFibers].stack = malloc( FIBER_STACK );
	if ( fiberList[numFibers].stack == 0 )
	{
		LF_DEBUG_OUT( "Error: Could not allocate stack.", 0 );
		return LF_MALLOCERROR;
	}
	
	// Call the clone system call to create the child thread
	fiberList[numFibers].pid = clone( &fiberStart, (char*) fiberList[numFibers].stack + FIBER_STACK,
		SIGCHLD | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_VM, (void*) func );
	if ( fiberList[numFibers].pid == -1 )
	{
		LF_DEBUG_OUT( "Error: clone system call failed.", 0 );
		return LF_CLONEERROR;
	}
	
	numFibers ++;
	
	return LF_NOERROR;
}

int waitForAllFibers()
{
	pid_t pid;
	int i;
	int fibersRemaining = 0;
		
	// Check to see if we are in a fiber, since we don't get signals in the child threads
	pid = getpid();
	if ( pid != parentPid ) return LF_INFIBER;			
		
	// Wait for the fibers to quit, then free the stacks
	while ( numFibers > fibersRemaining )
	{
		pid = wait( 0 );
		if ( pid == -1 )
		{
			LF_DEBUG_OUT( "Error: wait system call failed.", 0 );
			exit( 1 );
		}
		
		// Find the fiber, free the stack, and swap it with the last one
		for ( i = 0; i < numFibers; ++ i )
		{
			if ( fiberList[i].pid == pid )
			{
				LF_DEBUG_OUT( "Child fiber pid = %d exited", pid );
				numFibers --;
				
				free( fiberList[i].stack );
				if ( i != numFibers )
				{
					fiberList[i] = fiberList[numFibers];
				}
				
				i = -1;
				break;
			}
		}
		if ( i == numFibers )
		{
			LF_DEBUG_OUT( "Did not find child pid = %d in the fiber list", pid ); 
		}
	}
	
	return LF_NOERROR;
}

