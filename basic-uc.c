/* On Mac OS X, _XOPEN_SOURCE must be defined before including ucontext.h.
Otherwise, getcontext/swapcontext causes memory corruption. See:

http://lists.apple.com/archives/darwin-dev/2008/Jan/msg00229.html */
#ifdef __APPLE__
#define _XOPEN_SOURCE
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>

/* 64kB stack */
#define FIBER_STACK 1024*64

ucontext_t child, parent;

/* The child thread will execute this function */
void threadFunction()
{
        printf( "Child fiber yielding to parent\n" );
        swapcontext( &child, &parent );
        printf( "Child thread exiting\n" );
        swapcontext( &child, &parent );
}

int main()
{
        void* child_stack;
        int error;
        /* Get the current execution context */
        getcontext( &child );

        /* Modify the context to a new stack */
        child_stack = malloc( FIBER_STACK );
        child.uc_link = 0;
        child.uc_stack.ss_sp = child_stack;
        child.uc_stack.ss_size = FIBER_STACK;
        child.uc_stack.ss_flags = 0;        
        if ( child.uc_stack.ss_sp == 0 )
        {
                perror( "malloc: Could not allocate stack" );
                exit( 1 );
        }

        /* Create the new context */
        printf( "Creating child fiber\n" );
        makecontext( &child, &threadFunction, 0 );
        
        /* Execute the child context */
        printf( "Switching to child fiber\n" );
        error = swapcontext( &parent, &child );
        assert( error == 0 );
        printf( "Switching to child fiber again\n" );
        error = swapcontext( &parent, &child );
        assert( error == 0 );

        /* Free the stack */
        free( child_stack );

        printf( "Child fiber returned and stack freed\n" );
        
        return 0;
}
