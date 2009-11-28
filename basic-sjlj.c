#include <assert.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

/* 64kB stack */
#define FIBER_STACK 1024*64

jmp_buf child, parent;

/* The child thread will execute this function. */
void threadFunction()
{
	printf( "Child fiber yielding to parent\n" );
	if ( setjmp( child ) )
	{
		printf( "Child thread exiting\n" );
		longjmp( parent, 1 );
	}

	longjmp( parent, 1 );
}

void signalHandler( int signum )
{
    assert( signum == SIGUSR1 );
	if ( setjmp( child ) )
	{
		threadFunction();
	}
	
	return;
}

int main()
{
	stack_t stack;
	struct sigaction sa;
	
	/* Create the new stack */
	stack.ss_flags = 0;
	stack.ss_size = FIBER_STACK;
	stack.ss_sp = malloc( FIBER_STACK );
	if ( stack.ss_sp == 0 )
	{
		perror( "malloc: Could not allocate stack." );
		exit( 1 );
	}
	sigaltstack( &stack, 0 );
	
	/* Set up the custom signal handler */
	sa.sa_handler = &signalHandler;
	sa.sa_flags = SA_ONSTACK;
	sigemptyset( &sa.sa_mask );
	sigaction( SIGUSR1, &sa, 0 );
	
	/* Send the signal to call the function on the new stack */
	printf( "Creating child fiber\n" );
	raise( SIGUSR1 );

	/* Execute the child context */
	printf( "Switching to child fiber\n" );
	if ( setjmp( parent ) )
	{
		printf( "Switching to child fiber again\n" );
		if ( setjmp( parent ) == 0 ) longjmp( child, 1 );
	}
	else longjmp( child, 1 );
	
	/* Free the stack */
	free( stack.ss_sp );
	printf( "Child fiber returned and stack freed\n" );
	return 0;
}
