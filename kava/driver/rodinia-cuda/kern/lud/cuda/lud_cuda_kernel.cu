#include <cuda.h>
#include <stdio.h>

#include "lud.h"

#define NUM_ITR 100

__global__ void 
lud_diagonal(float *m, int matrix_dim, int offset)
{
for (int itr = 0; itr < NUM_ITR; itr++) {

  int i,j;
  __shared__ float shadow[LUD_BLOCK_SIZE][LUD_BLOCK_SIZE];

  int array_offset = offset*matrix_dim+offset;
  for(i=0; i < LUD_BLOCK_SIZE; i++){
    shadow[i][threadIdx.x]=m[array_offset+threadIdx.x];
    array_offset += matrix_dim;
  }
  __syncthreads();
  for(i=0; i < LUD_BLOCK_SIZE-1; i++) {

    if (threadIdx.x>i){
      for(j=0; j < i; j++)
        shadow[threadIdx.x][i] -= shadow[threadIdx.x][j]*shadow[j][i];
      shadow[threadIdx.x][i] /= shadow[i][i];
    }

    __syncthreads();
    if (threadIdx.x>i){

      for(j=0; j < i+1; j++)
        shadow[i+1][threadIdx.x] -= shadow[i+1][j]*shadow[j][threadIdx.x];
    }
    __syncthreads();
  }

  /* 
     The first row is not modified, it
     is no need to write it back to the
     global memory

   */
  array_offset = (offset+1)*matrix_dim+offset;
  for(i=1; i < LUD_BLOCK_SIZE; i++){
    m[array_offset+threadIdx.x]=shadow[i][threadIdx.x];
    array_offset += matrix_dim;
  }

} // for itr
}

__global__ void
lud_perimeter(float *m, int matrix_dim, int offset)
{
for (int itr = 0; itr < NUM_ITR; itr++) {

  __shared__ float dia[LUD_BLOCK_SIZE][LUD_BLOCK_SIZE];
  __shared__ float peri_row[LUD_BLOCK_SIZE][LUD_BLOCK_SIZE];
  __shared__ float peri_col[LUD_BLOCK_SIZE][LUD_BLOCK_SIZE];

  int i,j, array_offset;
  int idx;

  if (threadIdx.x < LUD_BLOCK_SIZE) {
    idx = threadIdx.x;
    
    array_offset = offset*matrix_dim+offset;
    for (i=0; i < LUD_BLOCK_SIZE/2; i++){
      dia[i][idx]=m[array_offset+idx];
      array_offset += matrix_dim;
    }
    
    array_offset = offset*matrix_dim+offset;
    for (i=0; i < LUD_BLOCK_SIZE; i++) {
      peri_row[i][idx]=m[array_offset+(blockIdx.x+1)*LUD_BLOCK_SIZE+idx];
      array_offset += matrix_dim;
    }

  } else {
    idx = threadIdx.x-LUD_BLOCK_SIZE;
    
    array_offset = (offset+LUD_BLOCK_SIZE/2)*matrix_dim+offset;
    for (i=LUD_BLOCK_SIZE/2; i < LUD_BLOCK_SIZE; i++){
      dia[i][idx]=m[array_offset+idx];
      array_offset += matrix_dim;
    }
    
    array_offset = (offset+(blockIdx.x+1)*LUD_BLOCK_SIZE)*matrix_dim+offset;
    for (i=0; i < LUD_BLOCK_SIZE; i++) {
      peri_col[i][idx] = m[array_offset+idx];
      array_offset += matrix_dim;
    }
  
  }
  __syncthreads();

/* this version works ok on hardware, but not gpgpusim
 **************************************************************
  if (threadIdx.x < LUD_BLOCK_SIZE) { //peri-row
    idx=threadIdx.x;
    for(i=1; i < LUD_BLOCK_SIZE; i++){
      for (j=0; j < i; j++)
        peri_row[i][idx]-=dia[i][j]*peri_row[j][idx];
    }

    
    array_offset = (offset+1)*matrix_dim+offset;
    for(i=1; i < LUD_BLOCK_SIZE; i++){
      m[array_offset+(blockIdx.x+1)*LUD_BLOCK_SIZE+idx] = peri_row[i][idx];
      array_offset += matrix_dim;
    }
  } else { //peri-col
    idx=threadIdx.x - LUD_BLOCK_SIZE;
    for(i=0; i < LUD_BLOCK_SIZE; i++){
      for(j=0; j < i; j++)
        peri_col[idx][i]-=peri_col[idx][j]*dia[j][i];
      peri_col[idx][i] /= dia[i][i];
    }

    __syncthreads();
    
    array_offset = (offset+(blockIdx.x+1)*LUD_BLOCK_SIZE)*matrix_dim+offset;
    for(i=0; i < LUD_BLOCK_SIZE; i++){
      m[array_offset+idx] =  peri_col[i][idx];
      array_offset += matrix_dim;
    }
  }
***************************************************************/
  if (threadIdx.x < LUD_BLOCK_SIZE) { //peri-row
    idx=threadIdx.x;
    for(i=1; i < LUD_BLOCK_SIZE; i++){
      for (j=0; j < i; j++)
        peri_row[i][idx]-=dia[i][j]*peri_row[j][idx];
    }
  } else { //peri-col
    idx=threadIdx.x - LUD_BLOCK_SIZE;
    for(i=0; i < LUD_BLOCK_SIZE; i++){
      for(j=0; j < i; j++)
        peri_col[idx][i]-=peri_col[idx][j]*dia[j][i];
      peri_col[idx][i] /= dia[i][i];
    }
  }

  __syncthreads();
    
  if (threadIdx.x < LUD_BLOCK_SIZE) { //peri-row
    idx=threadIdx.x;
    array_offset = (offset+1)*matrix_dim+offset;
    for(i=1; i < LUD_BLOCK_SIZE; i++){
      m[array_offset+(blockIdx.x+1)*LUD_BLOCK_SIZE+idx] = peri_row[i][idx];
      array_offset += matrix_dim;
    }
  } else { //peri-col
    idx=threadIdx.x - LUD_BLOCK_SIZE;
    array_offset = (offset+(blockIdx.x+1)*LUD_BLOCK_SIZE)*matrix_dim+offset;
    for(i=0; i < LUD_BLOCK_SIZE; i++){
      m[array_offset+idx] =  peri_col[i][idx];
      array_offset += matrix_dim;
    }
  }

} // for itr
}

__global__ void
lud_internal(float *m, int matrix_dim, int offset)
{
for (int itr = 0; itr < NUM_ITR; itr++) {

  __shared__ float peri_row[LUD_BLOCK_SIZE][LUD_BLOCK_SIZE];
  __shared__ float peri_col[LUD_BLOCK_SIZE][LUD_BLOCK_SIZE];

  int i;
  float sum;

  int global_row_id = offset + (blockIdx.y+1)*LUD_BLOCK_SIZE;
  int global_col_id = offset + (blockIdx.x+1)*LUD_BLOCK_SIZE;

  peri_row[threadIdx.y][threadIdx.x] = m[(offset+threadIdx.y)*matrix_dim+global_col_id+threadIdx.x];
  peri_col[threadIdx.y][threadIdx.x] = m[(global_row_id+threadIdx.y)*matrix_dim+offset+threadIdx.x];

  __syncthreads();

  sum = 0;
  for (i=0; i < LUD_BLOCK_SIZE; i++)
    sum += peri_col[threadIdx.y][i] * peri_row[i][threadIdx.x];
  m[(global_row_id+threadIdx.y)*matrix_dim+global_col_id+threadIdx.x] -= sum;

} // for itr
}
