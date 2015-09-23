#ifndef LHCAST_H
#define LHCAST_H

#include <sys/types.h>
#include "cauchy_256.h"

#define HTYPE_BLKHDR 1
#define HTYPE_SEGHDR 2
#define HTYPE_FILEHDR 3 // not defined
#define HTYPE_STREAMHDR 4 // not defined

#define SEGUID_LEN 30

#ifdef DEBUG
#define D(x) x
#else
#define D(x)
#endif

struct lhcast_seghdr {
  u_int8_t htype; // must be HTYPE_SEGHDR
  u_int8_t padding; // must be zero
  u_int8_t segment_uid[SEGUID_LEN];
  u_int8_t k; // number of blocks for the file
  u_int8_t m; // number of redundancy codes (m <= 256-k)
  u_int16_t blksz; // blocksize (should be less than MTU-blkhdr)
  u_int16_t final_blksz; // truncate
  u_int8_t hash[160/8]; // some hash (not used yet)
};
typedef struct lhcast_seghdr LHCSegHdr;

struct lhcast_blkhdr {
  u_int8_t htype; // must be HTYPE_BLKHDR
  u_int8_t blkid; // if id < k it's data. if k <= id it's recovery block
  u_int8_t segment_uid[SEGUID_LEN];
};
typedef struct lhcast_blkhdr LHCBlkHdr;

typedef struct lh_recovery_context {
  int k;
  int m;
  int blocksize;

  u_int8_t *block_data_p; // should have allocated blocksize * k
  Block *block_info_p; // should have allocated k blocks
  int original_count;
  int recovery_count;
  void (*processData_p)(int row, u_int8_t *data, int data_bytes, void *ctx_p);
  void *aux_p; // any use for apps.
  void (*aux_free_p)(void *aux_p); // if aux_p shall be free'ed it must have the pointer for proper free function.
} LHRecoveryContext;

void lh_recovery_free(LHRecoveryContext *ctx_p);
LHRecoveryContext *lh_recovery_init(int k, int m, int blocksize, void (*processData_p)(int row, u_int8_t *data, int data_bytes, void *ctx_p));
bool lh_onData(LHRecoveryContext *ctx_p, u_int8_t row, u_int8_t *new_data);

#endif
