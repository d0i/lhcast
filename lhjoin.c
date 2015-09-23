#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "cauchy_256.h"
#include "lhcast.h"

struct lhjoin_args {
  char **fn_v; /* argv - opts */
  char **fn_tail; /* argv + argc (end of filenames) */
  char *outf_fn; // if NULL, use stdout.
  FILE *in_stream;
};



// return expected segment size
int filesize_shdr(LHCSegHdr *shdr_p){
  return ((shdr_p->k-1) * shdr_p->blksz) + shdr_p->final_blksz;
}

// after open, the filename is the previous one.
#define lhjoin_current_filename(argp) (*((argp)->fn_v-1))

// do we have more files to come?
int lhjoin_morefiles(struct lhjoin_args *argp){
  if (argp->in_stream != NULL){
    return !feof(argp->in_stream);
  } 
  // else
  return argp->fn_v < argp->fn_tail;
}
// open next file and return the file pointer
FILE *lhjoin_open_next(struct lhjoin_args *argp){
  FILE *fp;
  if (argp->in_stream != NULL){
    return argp->in_stream;
  }
  if (lhjoin_morefiles(argp)){
    if ((fp = fopen(*(argp->fn_v++), "r")) == NULL){
      perror("fopen");
      return NULL;
    }
    return fp;
  }
  fprintf(stderr, "lhjoin_open_next: no more files.\n");
  return NULL;
}
// close if needed. return the replacement.
FILE *lhjoin_fclose(FILE *fp, struct lhjoin_args *argp){
  if (argp->in_stream == NULL){
    fclose(fp);
  }
  return NULL;
  // shall replace original fp value to indicate it's empty (or it
  // will return original fp if fp is continuous stream
}
// output file.
FILE *lhjoin_open_output(struct lhjoin_args *argp){
  FILE *fp;
  if (argp->outf_fn != NULL){
    if ((fp = fopen(argp->outf_fn, "w")) == NULL){
      fprintf(stderr, "cannot open output file %s\n", argp->outf_fn);
      return NULL;
    }
    return fp;
  }
  // else
  return stdout;
}
// output filename
const char *lhjoin_output_filename(struct lhjoin_args *argp){
  return (argp->outf_fn==NULL)?"stdout":(argp->outf_fn);
}

void processData_forRecovery(int row, u_int8_t *data, int data_bytes, void *ctx_tmp) {
  LHRecoveryContext *ctx_p = (LHRecoveryContext*)ctx_tmp;
  D(fprintf(stderr, "row %d data_bytes %d called.\n", row, data_bytes));
  memcpy(((u_int8_t*)ctx_p->aux_p)+(ctx_p->blocksize*row), data, data_bytes);
  return;
}

LHRecoveryContext *prepare_recovery(int k, int m, int blocksize){
  u_int8_t *data_p;
  if ((data_p = (u_int8_t*)malloc(k*blocksize)) == NULL){
    return NULL;
  }
  memset(data_p, 0, k*blocksize);
  LHRecoveryContext *ctx_p = lh_recovery_init(k, m, blocksize, processData_forRecovery);
  ctx_p->aux_p = data_p;
  ctx_p->aux_free_p = free;

  return ctx_p;
}


void help_exit(const char *message){
  if (message != NULL){
    fprintf(stderr, "%s\n", message);
  }
  fprintf(stderr, "lhjoin [-o (outfilename|-)] (filenames.lhb|-)\n");
  exit(1);
}

// FIXME: can only read just one segment
int main(int argc, char **argv){
  struct lhjoin_args arg;

  LHCSegHdr shdr;
  LHCBlkHdr bhdr;
  u_int8_t *buf = NULL; 
  int i, r;
  int found=0, received=0;
  LHRecoveryContext *ctx_p = NULL;
  FILE *f_in = NULL;
  FILE *f_out = NULL;

  int opt;

  memset(&arg, 0, sizeof(arg));
  while ((opt = getopt(argc, argv, "o:")) != -1){
    switch(opt){
    case 'o':
      if (strcmp(optarg, "-") == 0){
        arg.outf_fn = NULL;
      }
      arg.outf_fn = optarg;
      break;
    default:
      help_exit(NULL);
    }
  }
  arg.fn_v = &(argv[optind]);
  arg.fn_tail = &(argv[argc]);
  if ((arg.fn_v == arg.fn_tail || strcmp(arg.fn_v[0], "-") == 0) ){
    fprintf(stderr, "using stdin.\n");
    arg.in_stream = stdin;
  }

  // read the head
  if ((f_in = lhjoin_open_next(&arg)) == NULL){
    fprintf(stderr, "cannot open the precious header file!\n");
    goto error;
  }
  if (fread(&shdr, sizeof(shdr), 1, f_in) < 1){perror("fread"); goto error;}
  f_in = lhjoin_fclose(f_in, &arg);
  assert(shdr.htype == HTYPE_SEGHDR);
  shdr.blksz = ntohs(shdr.blksz);
  shdr.final_blksz = ntohs(shdr.final_blksz);
  
  fprintf(stderr, "hdr(k: %d, m: %d, blksz: %d)\n", shdr.k, shdr.m, shdr.blksz);
  
  if ((ctx_p = prepare_recovery(shdr.k, shdr.m, shdr.blksz)) == NULL){
    fprintf(stderr, "failed to prepare_recovery.\n");
    goto error;
  }

  if ((buf = (u_int8_t*)malloc(shdr.blksz + sizeof(bhdr))) == NULL){perror("malloc"); goto error;}
  while (lhjoin_morefiles(&arg)){
    D(fprintf(stderr, "opening %s\n", arg.fn_v[0]));
    if ((f_in = lhjoin_open_next(&arg)) == NULL){
      fprintf(stderr, "failed to open %s. Ignored.\n", lhjoin_current_filename(&arg));
      continue;
    }
    received ++;
    if (fread(&bhdr, sizeof(struct lhcast_blkhdr), 1, f_in) < 1){perror("fread");exit(1);}
    if (bhdr.htype != HTYPE_BLKHDR){
      if (bhdr.htype == HTYPE_SEGHDR){
        fprintf(stderr, "FIXME! new (or same) segment header found. multi segment read is not yet implemented.\n");
        continue;
      }else{
        fprintf(stderr, "unknown header type %d. Ignored.\n", bhdr.htype);
        continue;
      }
    }
    // FIXME: debugging...
#if 0
    if (memcmp(shdr.segment_uid, bhdr.segment_uid, SEGUID_LEN) != 0){
      fprintf(stderr, "segment uid mismatch on file %s. Ignored.\n", lhjoin_current_filename(&arg));
      continue;
    }
#endif 
    r = fread(buf, 1, shdr.blksz, f_in);
    if (r < shdr.blksz){
      fprintf(stderr, "read %d is shorter than expected (%d) on file %s. Ignored.\n", r, shdr.blksz, lhjoin_current_filename(&arg));
      continue;
    }
    if (lh_onData(ctx_p, bhdr.blkid, buf)){
      fprintf(stderr, "yay! data must be good at block %d!\n", received);
      found=1;
      break;
    }
    f_in = lhjoin_fclose(f_in, &arg);
  }
  free(buf);

  if (found){
    int fsz;
    fsz = filesize_shdr(&shdr);
    fprintf(stderr, "recovering %d bytes...\n", fsz);
    if ((f_out = lhjoin_open_output(&arg)) == NULL){perror("fopen"); goto error;}
    r = fwrite(ctx_p->aux_p, 1, fsz, f_out);
    if (r < fsz){
      fprintf(stderr, "result file may be truncated (expected %d but it was %d)\n", fsz, r);
    }
    fclose(f_out);
    fprintf(stderr, "successfully wrote to %s\n", lhjoin_output_filename(&arg));
  } else {
    fprintf(stderr, "insufficient number of blocks recieved (expected %d but was %d)\n", shdr.k, received);
  }
  lh_recovery_free(ctx_p);
  return 0;

 error:
  if (buf){free(buf);}
  if (f_in){f_in = lhjoin_fclose(f_in, &arg);}
  if (f_out){fclose(f_out);}
  if (ctx_p){lh_recovery_free(ctx_p);}
  return 1;
}
