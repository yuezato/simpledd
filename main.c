#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <ctype.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/mman.h>
#include <stdint.h>

/*
 Warning1: 汎用的な関数ではない
 Warning2: huge_freeで解放すること
 引数: size は512の倍数
 返り値: 512バイトアラインに揃った確保済みアドレスを返す
 */
void* huge_malloc(size_t size, void** orig) {
  if(size % 512) {
    printf("[huge_malloc] `size=%lu` should be a multiple of 512\n", size);
    return NULL;
  }
  
  void* addr = mmap(NULL, size + 512, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_HUGETLB | MAP_ANONYMOUS, 0, 0);

  *orig = addr;
  if(addr == MAP_FAILED) {
    perror("[huge_malloc] ERROR (Check /proc/sys/vm/nr_hugepages)");
    return NULL;
  }

  unsigned int m = ((uintptr_t)addr % 512);
  if(m) { // not aligned
    return addr + (512 - m);
  } else {
    return addr;
  }
}

void huge_free(void *addr, size_t size) {
  munmap(addr, size + 512);
}

// xK -> 1024 * x
// xM -> 1024 * x
unsigned long num_as_str_to_real_num(const char *str) {
  int len = strlen(str);
  char *err = NULL;
 
  char lastc = str[len-1];

  // 末尾がKやらMではない場合はそのまま数字にする
  if(isdigit(lastc)) {
    unsigned long ret = strtoul(str, &err, 10);
    return ret; // TODO: エラー処理
  }
  
  char target[len-1];
  strncpy(target, str, len-1);
  unsigned long v = strtoul(target, &err, 10);
  unsigned int coeff = 1;
  
  switch(str[len - 1]) {
  case 'K':
    coeff = 1024;
    break;
  case 'M':
    coeff = 1024 * 1024;
    break;
  default:
    return 0;
  }

  return v * coeff;
}

/*
 * Usage: sdd --if=... --of=... --bs=... --count=.. --hugepage
 */
int main(int argc, char *argv[]) {
  struct option longopts[] =
    {
     {
      "if", required_argument, NULL, 'i'
     },
     {
      "of", required_argument, NULL, 'o'
     },
     {
      "bs", required_argument, NULL, 'b'
     },
     {
      "count", required_argument, NULL, 'c'
     },
     {
      "hugepage", optional_argument, NULL, 'h'
     }
    };

  int opt, longindex;
  char *input_path = NULL;
  char *output_path = NULL;
  unsigned int io_size = 0;
  unsigned int count = 0;
  bool hugepage = false;
  void* orig_ptr = NULL;
  while((opt = getopt_long(argc, argv, "i:o:b:c:h", longopts, &longindex)) != -1) {
    printf("%c %d %s\n", opt, longindex, longopts[longindex].name);

    switch(opt) {
    case 'i':
      input_path = strdup(optarg);
      break;
    case 'o':
      output_path = strdup(optarg);
      break;
    case 'b':
      io_size = num_as_str_to_real_num(optarg);
      break;
    case 'c':
      count = num_as_str_to_real_num(optarg);
      break;
    case 'h':
      hugepage = true;
      break;
    }
  }

  printf("input_path = %p, output_path = %p\n", input_path, output_path);
  
  if(input_path) {
    printf("input_path = %s\n", input_path);
  } else {
    puts("invalid argument on --if");
    exit(EX_USAGE);
  }

  if(output_path) {
    printf("output_path = %s\n", output_path);
  } else {
    puts("invalid argument on --of");
    exit(EX_USAGE);
  }

  if(io_size) {
    printf("io_size = %u\n", io_size);
  } else {
    puts("invalid argument on --bs");
    exit(EX_USAGE);
  }

  if(count) {
    printf("count = %u\n", count);
  } else {
    puts("invalid argument on --count");
    exit(EX_USAGE);
  }

  int fdi = open(input_path,  O_RDONLY);
  int fdo = open(output_path, O_WRONLY|O_CREAT|O_DIRECT);

  char *buf;
  if(hugepage) {
    buf = huge_malloc(io_size, &orig_ptr);
  } else {
    posix_memalign((void**)&buf, 512, io_size);
  }
  
  for(unsigned int iter = 0; iter < count; ++iter) {
    read(fdi,  buf, io_size);
    printf("read pos = %lu\n", lseek(fdi, 0, SEEK_CUR));
    
    write(fdo, buf, io_size);
    printf("write pos = %lu\n", lseek(fdo, 0, SEEK_CUR));
  }

  if(hugepage && orig_ptr != NULL) {
    huge_free(orig_ptr, io_size);
  }
}
