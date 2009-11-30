#include "libfiber.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The Fiber Structure
*  Contains the information about individual fibers.
*/
typedef struct
{
	void** stack; /* The stack pointer */
	void* stack_bottom; /* The original returned from malloc. */
	int active;
} fiber;

/* The fiber "queue" */
static fiber fiberList[ MAX_FIBERS ];

/* The index of the currently executing fiber */
static int currentFiber = -1;
/* A boolean flag indicating if we are in the main process or if we are in a fiber */
static int inFiber = 0;
/* The number of active fibers */
static int numFibers = 0;

/* Stores the "main" fiber. */
static fiber mainFiber;

/* Prototype for the assembly function to switch processes. */
extern int asm_switch(fiber* next, fiber* current, int return_value);
static void create_stack(fiber* fiber, int stack_size, void (*fptr)(void));
extern void* asm_call_fiber_exit;

/* Sets all the fibers to be initially inactive */
void initFibers()
{
	memset(fiberList, 0, sizeof(fiberList));
	mainFiber.stack = NULL;
	mainFiber.stack_bottom = NULL;
}

/* Switches from a fiber to main or from main to a fiber */
void fiberYield()
{
	/* If we are in a fiber, switch to the main process */
	if ( inFiber )
	{
		/* Switch to the main context */
		LF_DEBUG_OUT1( "libfiber debug: Fiber %d yielding the processor...", currentFiber );

		asm_switch( &mainFiber, &fiberList[currentFiber], 0 );
	}
	/* Else, we are in the main process and we need to dispatch a new fiber */
	else
	{
		if ( numFibers == 0 ) return;
	
		/* Saved the state so call the next fiber */
		currentFiber = (currentFiber + 1) % numFibers;
		
		LF_DEBUG_OUT1( "Switching to fiber %d.", currentFiber );
		inFiber = 1;
		asm_switch( &fiberList[ currentFiber ], &mainFiber, 0 );
		inFiber = 0;
		LF_DEBUG_OUT1( "Fiber %d switched to main context.", currentFiber );
		
		if ( fiberList[currentFiber].active == 0 )
		{
			LF_DEBUG_OUT1( "Fiber %d is finished. Cleaning up.\n", currentFiber );
			/* Free the "current" fiber's stack */
			free( fiberList[currentFiber].stack_bottom );
			
			/* Swap the last fiber with the current, now empty, entry */
			-- numFibers;
			if ( currentFiber != numFibers )
			{
				fiberList[ currentFiber ] = fiberList[ numFibers ];
			}
			fiberList[ numFibers ].active = 0;
		}
		
	}
	return;
}

int spawnFiber( void (*func)(void) )
{
	if ( numFibers == MAX_FIBERS ) return LF_MAXFIBERS;

	/* Set the context to a newly allocated stack */
	create_stack( &fiberList[numFibers], FIBER_STACK, func );
	if ( fiberList[numFibers].stack_bottom == 0 )
	{
		LF_DEBUG_OUT( "Error: Could not allocate stack." );
		return LF_MALLOCERROR;
	}
	fiberList[numFibers].active = 1;
	++ numFibers;
	
	return LF_NOERROR;
}

int waitForAllFibers()
{
	int fibersRemaining = 0;
	
	/* If we are in a fiber, wait for all the *other* fibers to quit */
	if ( inFiber ) fibersRemaining = 1;
	
	LF_DEBUG_OUT1( "Waiting until there are only %d threads remaining...", fibersRemaining );
	
	/* Execute the fibers until they quit */
	while ( numFibers > fibersRemaining )
	{
		fiberYield();
	}
	
	return LF_NOERROR;
}

/* Called when a fiber exits. */
void fiber_exit() {
	assert( inFiber );
	assert( 0 <= currentFiber && currentFiber < numFibers );
	fiberList[currentFiber].active = 0;
	asm_switch( &mainFiber, &fiberList[currentFiber], 0 );

	/* asm_switch should never return for an exiting fiber. */
	abort();
}

#ifdef __APPLE__
#define ASM_PREFIX "_"
#else
#define ASM_PREFIX ""
#endif

/* Used to handle the correct stack alignment on Mac OS X, which requires a
16-byte aligned stack. The process returns here from its "main" function,
leaving the stack at 16-byte alignment. The call instruction then places a
return address on the stack, making the stack correctly aligned for the
process_exit function. */
asm(".globl " ASM_PREFIX "asm_call_fiber_exit\n"
ASM_PREFIX "asm_call_fiber_exit:\n"
/*"\t.type asm_call_fiber_exit, @function\n"*/
"\tcall " ASM_PREFIX "fiber_exit\n");

static void create_stack(fiber* fiber, int stack_size, void (*fptr)(void)) {
	int i;
#ifdef __x86_64
	/* x86-64: rbx, rbp, r12, r13, r14, r15 */
	static const int NUM_REGISTERS = 6;
#else
	/* x86: ebx, ebp, edi, esi */
	static const int NUM_REGISTERS = 4;
#endif
	assert(stack_size > 0);
	assert(fptr != NULL);

	/* Create a 16-byte aligned stack which will work on Mac OS X. */
	assert(stack_size % 16 == 0);
	fiber->stack_bottom = malloc(stack_size);
	if (fiber->stack_bottom == 0) return;
	fiber->stack = (void**)((char*) fiber->stack_bottom + stack_size);
#ifdef __APPLE__
	assert((uintptr_t) fiber->stack % 16 == 0);
#endif

	/* 4 bytes below 16-byte alignment: mac os x wants return address here
	so this points to a call instruction. */
	*(--fiber->stack) = (void*) ((uintptr_t) &asm_call_fiber_exit);
	/* 8 bytes below 16-byte alignment: will "return" to start this function */
	*(--fiber->stack) = (void*) ((uintptr_t) fptr);  /* Cast to avoid ISO C warnings. */
	/* push NULL words to initialize the registers loaded by asm_switch */
	for (i = 0; i < NUM_REGISTERS; ++i) {
		*(--fiber->stack) = 0;
	}
}

#ifdef __x86_64
/* arguments in rdi, rsi, rdx */
asm(".globl " ASM_PREFIX "asm_switch\n"
ASM_PREFIX "asm_switch:\n"
#ifndef __APPLE__
"\t.type asm_switch, @function\n"
#endif
/* Move return value into rax */
"\tmovq %rdx, %rax\n"

/* save registers: rbx rbp r12 r13 r14 r15 (rsp into structure) */
"\tpushq %rbx\n"
"\tpushq %rbp\n"
"\tpushq %r12\n"
"\tpushq %r13\n"
"\tpushq %r14\n"
"\tpushq %r15\n"
"\tmovq %rsp, (%rsi)\n"

/* restore registers */
"\tmovq (%rdi), %rsp\n"
"\tpopq %r15\n"
"\tpopq %r14\n"
"\tpopq %r13\n"
"\tpopq %r12\n"
"\tpopq %rbp\n"
"\tpopq %rbx\n"

/* return to the "next" fiber with eax set to return_value */
"\tret\n");
#else
/* static int asm_switch(fiber* next, fiber* current, int return_value); */
asm(".globl " ASM_PREFIX "asm_switch\n"
ASM_PREFIX "asm_switch:\n"
#ifndef __APPLE__
"\t.type asm_switch, @function\n"
#endif
/* Move return value into eax, current pointer into ecx, next pointer into edx */
"\tmov 12(%esp), %eax\n"
"\tmov 8(%esp), %ecx\n"
"\tmov 4(%esp), %edx\n"

/* save registers: ebx ebp esi edi (esp into structure) */
"\tpush %ebx\n"
"\tpush %ebp\n"
"\tpush %esi\n"
"\tpush %edi\n"
"\tmov %esp, (%ecx)\n"

/* restore registers */
"\tmov (%edx), %esp\n"
"\tpop %edi\n"
"\tpop %esi\n"
"\tpop %ebp\n"
"\tpop %ebx\n"

/* return to the "next" fiber with eax set to return_value */
"\tret\n");
#endif
