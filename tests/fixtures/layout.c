// edga-options: --c11 --gcc
// Object layout: a flexible array member, bit-fields with their widths and offsets, a type's
// alignment, and compound literals at block scope.
typedef unsigned long size_t;
void *malloc(size_t);

struct message {
  size_t len;
  char data[];
};

struct flags {
  unsigned a : 3;
  unsigned b : 5;
  unsigned c : 12;
};

struct __attribute__((aligned(16))) block {
  char bytes[8];
};

int read_pair(int x) {
  int *pair = (int[]){x, 0};
  return pair[1];
}

int nested(int x) {
  int *p;
  {
    p = (int[]){x};
  }
  return *p;
}

struct message *make_message(size_t n) {
  struct message *m = malloc(sizeof(*m) + n);
  return m;
}

unsigned total(struct flags f) { return f.a + f.b + f.c; }

size_t block_size(struct block *b) { return sizeof(*b); }
