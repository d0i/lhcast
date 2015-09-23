/*
This software is part of lhcast
Copyright 2015 Yusuke DOI <doi@wide.ad.jp>
*/
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "cauchy_256.h"
#include "lhcast.h"

void lh_recovery_free(LHRecoveryContext *ctx_p){
  if (ctx_p->block_data_p == NULL){free(ctx_p->block_data_p);}
  if (ctx_p->block_info_p == NULL){free(ctx_p->block_info_p);}
  if (ctx_p->aux_p && ctx_p->aux_free_p){
    (ctx_p->aux_free_p)(ctx_p->aux_p);
  }
  free(ctx_p);
  return;
}

LHRecoveryContext *lh_recovery_init(int k, int m, int blocksize, void (*processData_p)(int row, u_int8_t *data, int data_bytes, void *ctx_p)){
  LHRecoveryContext *ctx_p = (LHRecoveryContext*)malloc(sizeof(LHRecoveryContext));
  if (ctx_p == NULL){
    return NULL;
  }
  memset(ctx_p, 0, sizeof(LHRecoveryContext));
  if ((ctx_p->block_data_p = (u_int8_t*)malloc(blocksize*k)) == NULL){goto error;}
  memset(ctx_p->block_data_p, 0, blocksize*k);
  if ((ctx_p->block_info_p = (Block*)malloc(sizeof(Block)*k)) == NULL){goto error;}
  memset(ctx_p->block_info_p, 0, sizeof(Block)*k);
  
  ctx_p->k = k;
  ctx_p->m = m;
  ctx_p->blocksize = blocksize;
  ctx_p->processData_p = processData_p;
  
  return ctx_p;

 error:
  lh_recovery_free(ctx_p);
  return NULL;
}

// Call this function with each block received, either original or recovery
// Returns true on complete
// general idea is from sample of longhair/README.md
bool lh_onData(LHRecoveryContext *ctx_p, u_int8_t row, u_int8_t *new_data) {
  int insertion_point;

  // If it is original data,
  if (row < ctx_p->k) {
    // Process the original data immediately - Do not wait for it all to arrive!
    (ctx_p->processData_p)(row, new_data, ctx_p->blocksize, (void*)ctx_p);

    // Copy to the end of the original block data
    insertion_point = ctx_p->original_count++;
  } else {
    // Copy to the front of the recovery block data
    insertion_point = ctx_p->k - ++(ctx_p->recovery_count);
  }

  // Copy data into place
  u_int8_t *dest = (ctx_p->block_data_p) + insertion_point * ctx_p->blocksize;
  memcpy(dest, new_data, ctx_p->blocksize);

  // NOTE: It may be possible to avoid copying depending on if
  // you can hang onto the provided data buffer.

  // Fill in the block array entry
  Block *block = (ctx_p->block_info_p) + insertion_point;
  block->data = dest;
  block->row = row;

  // If recovery is not possible yet,
  if (ctx_p->original_count + ctx_p->recovery_count < ctx_p->k) {
    return false;
  }

  fprintf(stderr, "starting recovery\n");

  // Attempt decoding
  if (cauchy_256_decode(ctx_p->k, ctx_p->m, ctx_p->block_info_p, ctx_p->blocksize)) {
    // Decoding should never fail - indicates input is invalid
    assert(ctx_p->k + ctx_p->m <= 256);
    assert(ctx_p->block_info_p != 0);
    assert(ctx_p->blocksize % 8 == 0);
    return false;
  }

  // For each recovered block,
  block = (ctx_p->block_info_p) + (ctx_p->k) - (ctx_p->recovery_count);
  for (int ii = 0; ii < ctx_p->recovery_count; ++ii, ++block) {
    // Process the recovered data
    (ctx_p->processData_p)(block->row, block->data, ctx_p->blocksize, (void*)ctx_p);
  }

  return true;
}
