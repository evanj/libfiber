#include "libfiber.h"

#include <sched.h> /* For clone */
#include <signal.h> /* For SIGCHLD */
#include <stdlib.h>
#include <sys/types.h> /* For pid_t */
#include <sys/wait.h> /* For wait */
#include <unistd.h> /* For getpid */


/* The Fiber Structure
*  Contains the information about individual fibers.
*/
typedef struct
{
	pid_t pid; /* The pid of the child thread as returned by clone */
	void* stack; /* The stack pointer */
} fiber;

/* The fiber "queue" */
static fiber fiberList[ MAX_FIBERS ];
/* The pid of the parent process */
static pid_t parentPid;
/* The number of active fibers */
static int numFibers = 0;

/* Initialize the fibers to null */
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

/* Call the sched_yield system call which moves the current process to the
end of the process queue. */
void fiberYield()
{
	sched_yield();
}

/* Standard C99 does not permit void* to point to functions. This exists to
work around that in a standards compliant fashion. */
struct FiberArguments {
	void (*function)();
};

/* Exists to give the proper function type to clone. */
static int fiberStart( void* arg )
{
	struct FiberArguments* arguments = (struct FiberArguments*) arg;
	void (*function)() = arguments->function;
	free( arguments );
	arguments = NULL;

	LF_DEBUG_OUT1( "Child created and calling function = %p", arg );
	function();
	return 0;
}

int spawnFiber( void (*func)(void) )
{
	struct FiberArguments* arguments = NULL;
	if ( numFibers == MAX_FIBERS ) return LF_MAXFIBERS;

	/* Allocate the stack */
	fiberList[numFibers].stack = malloc( FIBER_STACK );
	if ( fiberList[numFibers].stack == 0 )
	{
		LF_DEBUG_OUT( "Error: Could not allocate stack." );
		return LF_MALLOCERROR;
	}

	/* Create the arguments structure. */
	arguments = (struct FiberArguments*) malloc( sizeof(*arguments) );
	if ( arguments == 0 ) {
		free( fiberList[numFibers].stack );
		LF_DEBUG_OUT( "Error: Could not allocate fiber arguments." );
		return LF_MALLOCERROR;
	}
	arguments->function = func;

	/* Call the clone system call to create the child thread */
	fiberList[numFibers].pid = clone( &fiberStart, (char*) fiberList[numFibers].stack + FIBER_STACK,
		SIGCHLD | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_VM, arguments );
	if ( fiberList[numFibers].pid == -1 )
	{
		free( fiberList[numFibers].stack );
		free( arguments );
		LF_DEBUG_OUT( "Error: clone system call failed." );
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
		
	/* Check to see if we are in a fiber, since we don't get signals in the child threads */
	pid = getpid();
	if ( pid != parentPid ) return LF_INFIBER;			
		
	/* Wait for the fibers to quit, then free the stacks */
	while ( numFibers > fibersRemaining )
	{
		pid = wait( 0 );
		if ( pid == -1 )
		{
			LF_DEBUG_OUT( "Error: wait system call failed." );
			exit( 1 );
		}
		
		/* Find the fiber, free the stack, and swap it with the last one */
		for ( i = 0; i < numFibers; ++ i )
		{
			if ( fiberList[i].pid == pid )
			{
				LF_DEBUG_OUT1( "Child fiber pid = %d exited", pid );
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
			LF_DEBUG_OUT1( "Did not find child pid = %d in the fiber list", pid ); 
		}
	}
	
	return LF_NOERROR;
}

