#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <ctype.h>

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

// sdd --if=... --of=... --bs=... --count=..
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
     }
    };

  int opt, longindex;
  char *input_path = NULL;
  char *output_path = NULL;
  unsigned int io_size = 0;
  unsigned int count = 0;
  while((opt = getopt_long(argc, argv, "i:o:b:c:", longopts, &longindex)) != -1) {
    printf("%d %s\n", longindex, longopts[longindex].name);

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
  posix_memalign((void**)&buf, 512, io_size);
  
  for(unsigned int iter = 0; iter < count; ++iter) {
    read(fdi,  buf, io_size);
    printf("read pos = %lu\n", lseek(fdi, 0, SEEK_CUR));
    
    write(fdo, buf, io_size);
    printf("write pos = %lu\n", lseek(fdo, 0, SEEK_CUR));
  }
}
