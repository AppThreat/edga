// edga-options: --c11 --gcc
// Pointer arithmetic next to integer arithmetic, subscripts written both ways, and implicit
// conversions that narrow or change sign.
struct buffer {
  char data[16];
  int used;
};

long distance(struct buffer *b, char *end) { return end - b->data; }

char *advance(struct buffer *b, int n) {
  char *p = b->data + n;
  p += 2;
  p--;
  return p;
}

int pick(int *values, int i) { return i[values] + values[i + 1]; }

unsigned char narrow(int n) {
  unsigned char c = n;
  unsigned int u = -1;
  return c + (u > 0);
}
