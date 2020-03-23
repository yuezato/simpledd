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
#include <time.h>
#include <sys/time.h>

__uint128_t current_time_as_milisec() {
  struct timeval tv;
  gettimeofday(&tv, NULL);

  return tv.tv_sec*1000 + tv.tv_usec/1000;
}

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
  
  char *target = strndupa(str, len-1);
  unsigned long v = strtoul(target, &err, 10);
  unsigned int coeff = 1;
  
  switch(lastc) {
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
  fun("123456", ..., 2, '_') -> 12_34_56
  fun("123456", ..., 4, '_') -> 12_3456
 */
void to_readable(const char *src, char *dst, int s, int c) {
  size_t len = strlen(src);
  size_t dst_last = len + ((len-1)/s);
  dst[dst_last] = '\0';
  for(int count = 0, i = len-1, j = dst_last-1; i >= 0; ++count, --i) {
    if(count > 0 && count % s == 0) {
      dst[j] = c;
      --j;
    }
    dst[j] = src[i];
    --j;
  }
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
     },
     {
      "random", optional_argument, NULL, 'r'
     }
    };

  int opt, longindex;
  char *input_path = NULL;
  char *output_path = NULL;
  unsigned int io_size = 0;
  unsigned int count = 0;
  bool hugepage = false;
  unsigned int random_unit = 1;
  bool random_seek = false;
  void* orig_ptr = NULL;
  while((opt = getopt_long(argc, argv, "i:o:b:c:h", longopts, &longindex)) != -1) {
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
    case 'r':
      srandom(42);
      random_seek = true;
      random_unit = num_as_str_to_real_num(optarg);
      break;
    }
  }

  if(!input_path) {
    puts("invalid argument on --if");
    exit(EX_USAGE);
  }

  if(!output_path) {
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

  if(random_seek) {
    if ((random_unit % io_size) == 0) {
      printf("random_unit = %u\n", random_unit);
    } else {
      puts("random_unit % io_size must be 0");
      exit(EX_USAGE);
    }
  }

  int fdi = open(input_path,  O_RDONLY, S_IRUSR);
  int fdo = open(output_path, O_WRONLY|O_CREAT|O_DIRECT, S_IRUSR | S_IWUSR);

  char *buf;
  if(hugepage) {
    buf = huge_malloc(io_size, &orig_ptr);
  } else {
    int ret = posix_memalign((void**)&buf, 512, io_size);
    if(ret) {
      puts("[ERROR] posix_memalign");
      exit(errno);
    }
  }

  ssize_t ret;
  __uint128_t stime = current_time_as_milisec();
  __uint128_t read_total = 0;
  __uint128_t write_total = 0;
  for(unsigned int iter = 0; iter < count; ++iter) {
    // printf("iter = %d\n", iter);

    {
      __uint128_t s = current_time_as_milisec();
      ret = read(fdi,  buf, io_size);
      if(ret != io_size) {
	puts("[ERROR] read");
	goto FREE;
      }
      read_total += (current_time_as_milisec() - s);
    }
    // printf("read pos = %lu\n", lseek(fdi, 0, SEEK_CUR));

    {
      __uint128_t s = current_time_as_milisec();
      ssize_t ret = write(fdo, buf, io_size);
      if(ret != io_size) {
	puts("[ERROR] write");
	goto FREE;
      }
      write_total += (current_time_as_milisec() - s);
    }
    // printf("write pos = %lu\n", lseek(fdo, 0, SEEK_CUR));

    __uint128_t check = iter; check *= io_size; check %= random_unit;
    if(random_seek && check == 0) {
      __uint128_t pos = random();
      pos %= count;
      pos *= io_size;
      int res = lseek(fdo, pos, SEEK_SET);

      // printf("new pos = %u\n", pos);
      if(res < 0) {
	puts("[ERROR] seek");
	goto FREE;
      }
    }
  }

  puts("test");
  
  __uint128_t elapsed = current_time_as_milisec() - stime;

  __uint128_t tmp = count;
  tmp = tmp * io_size * 1000;
  unsigned long long throughput = tmp / elapsed;

  long double et = elapsed; et /= 1000;
  long double rt = read_total; rt /= 1000;
  long double wt = write_total; wt /= 1000;

  char* buf1 = malloc(8192);
  char* buf2 = malloc(8192);
  sprintf(buf1, "%llu", throughput);
  to_readable(buf1, buf2, 3, ',');
  
  printf("elapsed = %Lf(sec), throughput = %s byte/sec\n", et, buf2);
  printf("read secs = %Lf(sec), write secs = %Lf(sec)\n", rt, wt);

  free(buf1); free(buf2);
  
FREE:
  if(hugepage && orig_ptr != NULL) {
    huge_free(orig_ptr, io_size);
  } else {
    free(buf);
  }
}
