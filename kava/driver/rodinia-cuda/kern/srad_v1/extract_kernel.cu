#include "srad.h"

// statistical kernel

__global__ void extract
(long d_Ne,
 int *d_I) // pointer to input image (DEVICE GLOBAL MEMORY)
{
	// indexes
	int bx = blockIdx.x; // get current horizontal block index
	int tx = threadIdx.x; // get current horizontal thread index
	int ei = (bx*NUMBER_THREADS)+tx; // unique thread id, more threads than actual elements !!!
	// copy input to output & log uncompress
	if (ei<d_Ne) { // do only for the number of elements, omit extra threads
		// exponentiate input IMAGE and copy to output image
		d_I[ei] = exp(d_I[ei]/1000.0/255) * 1000;
	}
}
