#include "libfiber.h"

#include <assert.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct
{
	jmp_buf context;
	void (*function)(void);
	int active;
	void* stack;
#ifdef VALGRIND
	int stackId;
#endif
} fiber;

/* The fiber "queue" */
static fiber fiberList[ MAX_FIBERS ];

/* The index of the currently executing fiber */
static int currentFiber = -1;
/* A boolean flag indicating if we are in the main process or if we are in a fiber */
static int inFiber = 0;
/* The number of active fibers */
static int numFibers = 0;

/* The "main" execution context */
jmp_buf	mainContext;

static void usr1handlerCreateStack( int signum )
{
	assert( signum == SIGUSR1 );
	LF_DEBUG_OUT1( "Signal handler for fiber %d", numFibers );
	
	/* Save the current context, and return to terminate the signal handler scope */
	if ( setjmp( fiberList[numFibers].context ) )
	{
		/* We are being called again from the main context. Call the function */
		LF_DEBUG_OUT1( "Starting fiber %d", currentFiber );
		fiberList[currentFiber].function();
		LF_DEBUG_OUT1( "Fiber %d finished, returning to main", currentFiber );
		fiberList[currentFiber].active = 0;
		longjmp( mainContext, 1 );
	}
	
	return;
}

void initFibers()
{
	int i;
	for ( i = 0; i < MAX_FIBERS; ++ i )
	{
		fiberList[i].stack = 0;
		fiberList[i].function = 0;
		fiberList[i].active = 0;
	}
		
	return;
}

int spawnFiber( void (*func)(void) )
{
	struct sigaction handler;
	struct sigaction oldHandler;
	
	stack_t stack;
	stack_t oldStack;
	
	if ( numFibers == MAX_FIBERS ) return LF_MAXFIBERS;
	
	/* Create the new stack */
	stack.ss_flags = 0;
	stack.ss_size = FIBER_STACK;
	stack.ss_sp = malloc( FIBER_STACK );
	if ( stack.ss_sp == 0 )
	{
		LF_DEBUG_OUT( "Error: Could not allocate stack." );
		return LF_MALLOCERROR;
	}
	LF_DEBUG_OUT1( "Stack address from malloc = %p", stack.ss_sp );
#ifdef VALGRIND
	/* Sadly, this *still* doesn't fix all warnings. */
	fiberList[numFibers].stackId =
		VALGRIND_STACK_REGISTER(stack.ss_sp, ((char*) stack.ss_sp + FIBER_STACK));
#endif

	/* Install the new stack for the signal handler */
	if ( sigaltstack( &stack, &oldStack ) )
	{
		LF_DEBUG_OUT( "Error: sigaltstack failed." );
		return LF_SIGNALERROR;
	}
	
	/* Install the signal handler */
	/* Sigaction *must* be used so we can specify SA_ONSTACK */
	handler.sa_handler = &usr1handlerCreateStack;
	handler.sa_flags = SA_ONSTACK;
	sigemptyset( &handler.sa_mask );

	if ( sigaction( SIGUSR1, &handler, &oldHandler ) )
	{
		LF_DEBUG_OUT( "Error: sigaction failed." );
		return LF_SIGNALERROR;
	}
	
	/* Call the handler on the new stack */
	if ( raise( SIGUSR1 ) )
	{
		LF_DEBUG_OUT( "Error: raise failed." );
		return LF_SIGNALERROR;
	}
	
	/* Restore the original stack and handler */
	sigaltstack( &oldStack, 0 );
	sigaction( SIGUSR1, &oldHandler, 0 );
	
	/* We now have an additional fiber, ready to roll */
	fiberList[numFibers].active = 1;
	fiberList[numFibers].function = func;
	fiberList[numFibers].stack = stack.ss_sp;

	++ numFibers;
	return LF_NOERROR;
}

void fiberYield()
{
	/* If we are in a fiber, switch to the main context */
	if ( inFiber )
	{
		/* Store the current state */
		if ( setjmp( fiberList[ currentFiber ].context ) )
		{
			/* Returning via longjmp (resume) */
			LF_DEBUG_OUT1( "Fiber %d resuming...", currentFiber );
		}
		else
		{
			LF_DEBUG_OUT1( "Fiber %d yielding the processor...", currentFiber );
			/* Saved the state: Let's switch back to the main state */
			longjmp( mainContext, 1 );
		}
	}
	/* If we are in main, dispatch the next fiber */
	else
	{
		if ( numFibers == 0 ) return;
	
		/* Save the current state */
		if ( setjmp( mainContext ) )
		{
			/* The fiber yielded the context to us */
			inFiber = 0;
			if ( ! fiberList[currentFiber].active )
			{
				/* If we get here, the fiber returned and is done! */
				LF_DEBUG_OUT1( "Fiber %d returned, cleaning up.", currentFiber );
				
				free( fiberList[currentFiber].stack );
#ifdef VALGRIND
				VALGRIND_STACK_DEREGISTER(fiberList[currentFiber].stackId);
#endif
				
				/* Swap the last fiber with the current, now empty, entry */
				-- numFibers;
				if ( currentFiber != numFibers )
				{
					fiberList[ currentFiber ] = fiberList[ numFibers ];
				}
				
				/* Clean up the entry */
				fiberList[numFibers].stack = 0;
				fiberList[numFibers].function = 0;
				fiberList[numFibers].active = 0;
			}
			else
			{
				LF_DEBUG_OUT1( "Fiber %d yielded execution.", currentFiber );
			}
		}
		else
		{
			/* Saved the state so call the next fiber */
			currentFiber = (currentFiber + 1) % numFibers;
			
			LF_DEBUG_OUT1( "Switching to fiber %d", currentFiber );
			inFiber = 1;
			longjmp( fiberList[ currentFiber ].context, 1 );
		}
	}
	
	return;
}

int waitForAllFibers()
{
	int fibersRemaining = 0;
	
	/* If we are in a fiber, wait for all the *other* fibers to quit */
	if ( inFiber ) fibersRemaining = 1;
	
	LF_DEBUG_OUT1( "Waiting until there are only %d threads remaining...", fibersRemaining );
	
	while ( numFibers > fibersRemaining )
	{
		fiberYield();
	}
	
	return LF_NOERROR;
}
