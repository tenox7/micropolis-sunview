#include "sim.h"

#include <sys/types.h>
#ifndef SUNOS3
#include <stdlib.h>
#endif

#define SIM_RAND_MAX 0xffff

static unsigned QUAD next = 1;

int
sim_rand()
{
	next = next * 1103515245 + 12345;
	return ((next % ((SIM_RAND_MAX + 1) <<8)) >>8);
}

void
sim_srand(seed)
u_int seed;
{
	next = seed;
}
