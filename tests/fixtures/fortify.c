// edga-options: --c11 --gcc
// A checked copy as C libraries write it under _FORTIFY_SOURCE: the destination's object size
// travels with the call.
typedef unsigned long size_t;
#define memcpy(d, s, n) __builtin___memcpy_chk(d, s, n, __builtin_object_size(d, 0))

struct packet {
  char data[16];
  unsigned short len;
};

void fill(struct packet *p, const char *src, size_t n) {
  memcpy(p->data, src, n);
  p->len = (unsigned short)n;
}
