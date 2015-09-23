/*
This software is part of lhcast
Copyright 2015 Yusuke DOI <doi@wide.ad.jp>
*/
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <assert.h>

#include "cauchy_256.h"
#include "lhcast.h"

#define MIN(x, y) ((x)>(y)?(y):(x))
#define DEFAULT_BLOCKSIZE 1464
#define DEFAULT_REDUNDANCY_PCT 50
#define FNLEN 128

struct lhsplit_args {
  char outf_basename[FNLEN];
  int outf_cnt;
  FILE *outf_stream; // has priority to outf_basename
  int blocksize;
  int redundancy_pct;
  u_int8_t current_seguid[SEGUID_LEN];
};

void init_seguid(u_int8_t *seguid){
  // randomly initialize SEGUID_LEN-3
  for (int i = 0; i < SEGUID_LEN-3; i++){
    seguid[i] = rand()%256;
  }
  for (int i = SEGUID_LEN-3; i < SEGUID_LEN; i++){
    seguid[i] = 0;
  }
}
void incr_seguid(u_int8_t *seguid){
  for (int i = SEGUID_LEN-1; i >= 0; i--){
    seguid[i] ++;
    if (seguid[i] != 0){
      break;
    }
  }
  return;
}

FILE *prepare_nextfile(struct lhsplit_args *arg_p){
  char fnbuf[FNLEN];
  FILE *outf;
  if (arg_p->outf_stream){
    return arg_p->outf_stream;
  }
  snprintf(fnbuf, sizeof(fnbuf)-1, "%s%03d.lhb", arg_p->outf_basename, arg_p->outf_cnt++);
  if ((outf = fopen(fnbuf, "w")) == NULL){
    perror("open");
    return NULL;
  }
  return outf;
}
FILE *lharg_fclose(struct lhsplit_args *arg_p, FILE *fp){
  // if it's not a stream, close
  if (arg_p->outf_stream == NULL){
    fclose(fp);
    fp = NULL;
  }
  return fp;
}
  

int proper_k(int blksum, int redun_pct){
  // ceil(k * (redun_pct/100)) < m && (k+m) < blksum
  // (redun_pct + 100)/100 =:= blksum
  // 100:redun_pct = k:m

  float blk_rt;
  int k;
  int m;
  blk_rt = (100.0/(float)(redun_pct+100)) * blksum;
  m = (int)(blk_rt * float(redun_pct)/100.0)+1;
  k = blksum-m;
  return k;
}

// returns 0 on success, -1 on failure
int lh_encode_segment(u_int8_t *segbuf, int segsz, int k, int m, struct lhsplit_args *arg_p){
  LHCSegHdr shdr;
  LHCBlkHdr bhdr;
  int i, r;
  FILE *outf = NULL;
  u_int8_t **data_ptrs=NULL;
  u_int8_t *recovery_blocks=NULL;
  int rcode=0;

  memset(&shdr, 0, sizeof(shdr));
  shdr.htype = HTYPE_SEGHDR;
  shdr.k = k;
  shdr.m = m;
  shdr.blksz = htons(arg_p->blocksize);
  shdr.final_blksz = htons(segsz%arg_p->blocksize);
  memcpy(shdr.segment_uid, arg_p->current_seguid, SEGUID_LEN);
  incr_seguid(arg_p->current_seguid);

  if ((outf = prepare_nextfile(arg_p)) == NULL){goto error;}
  r = fwrite((void*)&shdr, sizeof(shdr), 1, outf);
  if (r < 1){perror("fwrite"); goto error;}
  outf = lharg_fclose(arg_p, outf);

  // prepare body data
  //  e.g. set the data_ptrs
  if ((data_ptrs = (u_int8_t**)malloc(sizeof(u_int8_t*)*k))==NULL){perror("malloc"); goto error;}
  for (i = 0; i < k; i++){
    data_ptrs[i] = segbuf + i*arg_p->blocksize;
  }
  if ((recovery_blocks = (u_int8_t*)malloc(arg_p->blocksize * m)) == NULL){perror("malloc"); goto error;}
  memset(recovery_blocks, 0, arg_p->blocksize * m);
    
  // encode
  if (cauchy_256_encode(k, m, (const unsigned char **)data_ptrs, (char *)recovery_blocks, arg_p->blocksize)) {
    fprintf(stderr, "encode failed.\n"); // should not fail
    abort();
  }
  
  // write data blocks
  memset(&bhdr, 0, sizeof(bhdr));
  bhdr.htype = HTYPE_BLKHDR;
  memcpy(bhdr.segment_uid, arg_p->current_seguid, SEGUID_LEN);
  for (i = 0; i < k; i++){
    bhdr.blkid = i;
    if ((outf = prepare_nextfile(arg_p)) == NULL){goto error;}
    if (fwrite(&bhdr, sizeof(bhdr), 1, outf) < 1){perror("fwrite");goto error;}
    if (fwrite(data_ptrs[i], arg_p->blocksize, 1, outf) < 1){perror("fwrite");goto error;}
    outf = lharg_fclose(arg_p, outf);
  }

  // write redundancy blocks
  for (i = 0; i < m; i++){
    bhdr.blkid = k+i;
    if ((outf = prepare_nextfile(arg_p)) == NULL){goto error;}
    if (fwrite(&bhdr, sizeof(bhdr), 1, outf) < 1){perror("fwrite"); goto error;}
    if (fwrite((recovery_blocks+(i*arg_p->blocksize)), arg_p->blocksize, 1, outf) < 1){perror("fwrite"); goto error;}
    outf = lharg_fclose(arg_p, outf);
  }
  // done!
 end:  
  if (outf){
    outf = lharg_fclose(arg_p, outf);
  }
  if (data_ptrs){
    free(data_ptrs);
  }
  if (recovery_blocks){
    free(recovery_blocks);
  }
  return rcode;
  
 error:
  fprintf(stderr, "lh_encode_segment: something wrong happen.\n");
  rcode = -1;
  goto end;
}

// returns 0 on success. -1 on failure
int lhsplit_readfile(char *filename, struct lhsplit_args *arg_p){
  FILE *fp;
  int file_length;
  struct stat fsbuf;
  int full_k, full_m, k, m;
  int segsz;
  int seg_i;
  int r;
  u_int8_t *segbuf;

  srand((unsigned int)time(NULL));
  
  fprintf(stderr, "reading %s\n", filename);
  // find file length
  if (stat(filename, &fsbuf) < 0){
    perror("stat");
    return -1;
  }
  file_length = fsbuf.st_size;
  fprintf(stderr, "file length: %d\n", file_length);

  // decide proper k and m (k * redundancy_pct < m && (k+m) < 256) for full segment
  k = full_k = proper_k(256, arg_p->redundancy_pct);
  m = full_m = 256-full_k;
  //fprintf(stderr, "k: %d, m: %d\n", k, m);
  //fprintf(stderr, "block sum: %d\n", k*arg_p->blocksize);
  
  // find segment size
  segsz = full_k*arg_p->blocksize;
  segbuf = (u_int8_t*)malloc(segsz);
  if (segbuf == NULL){
    perror("malloc");
    return -1;
  }

  if ((fp = fopen(filename, "r")) == NULL){
    fprintf(stderr, "cannot open file %s\n", filename);
    return -1;
  }
  // for each segment of file
  for (seg_i = 0; seg_i*segsz < file_length && !feof(fp); seg_i++){
    //   read the segment
    int this_segsz = segsz;
    r = fread(segbuf, 1, segsz, fp);
    if (r < segsz){
      //   if segment length is shorter than full segment size, recalc k and m
      k = (r / arg_p->blocksize);
      if (r % arg_p->blocksize != 0){ k++; }; 
      m = (int)(float(k)*(float(arg_p->redundancy_pct)/100))+1;
      assert(k+m < 256);
      this_segsz = r;
    }
    //   encode and send
    if (lh_encode_segment(segbuf, this_segsz, k, m, arg_p) < 0){
      fprintf(stderr, "encoding failed.\n");
      return -1;
    }
  }

  return 0;
}


void help_exit(const char *message){
  if (message != NULL){
    fprintf(stderr, "%s\n", message);
  }
  fprintf(stderr, "lhsplit [-o (outfn-basename|-)] [-b blocksize] [-r redundancy%%] filenames\n");
  exit(1);
}

int main(int argc, char **argv){

  int opt;
  struct lhsplit_args args;
  memset(&args, 0, sizeof(args));
  args.blocksize = DEFAULT_BLOCKSIZE;
  args.redundancy_pct = DEFAULT_REDUNDANCY_PCT;

  while ((opt = getopt(argc, argv, "o:b:r:")) != -1){
    switch (opt){
    case 'o':
      memcpy(args.outf_basename, optarg, MIN(strlen(optarg), sizeof(args.outf_basename)-1));
      break;
    case 'b':
      args.blocksize = atoi(optarg);
      if (args.blocksize%8 != 0){
        help_exit("blocksize%8 must be zero");
      }
      break;
    case 'r':
      args.redundancy_pct = atoi(optarg);
      if (args.redundancy_pct < 0){
        help_exit("redundancy must be positive number.");
      }
      break;
    case 'h':
    default:
      help_exit(NULL);
    }
  }

  if (strlen(args.outf_basename) == 0){
    help_exit("please specify outfn-basename by -o option.");
  }
  if (strcmp(args.outf_basename, "-") == 0){
    args.outf_stream = stdout;
  }
  init_seguid(args.current_seguid);
  if (optind >= argc){
    help_exit("reading stdin is not yet supported\n");
  }
  for (; optind < argc; optind++){
    if (lhsplit_readfile(argv[optind], &args) < 0){
      fprintf(stderr, "failed to send %s\n", argv[optind]);
    }
  }

  fprintf(stderr, "succeed.\n");
  exit(0);
}

