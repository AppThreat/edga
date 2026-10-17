// edga-options: --c11 --gcc
// Array bounds as the types record them: a declared extent, GNU's zero-length trailing array and
// a flexible member. The last two both count 0 elements; the zero bound tells them apart.
struct zero_tail {
  int count;
  char data[0];
};
struct flexible_tail {
  int count;
  char data[];
};
char fixed[4];
extern char unknown[];
int first_bytes(struct zero_tail *z, struct flexible_tail *f) {
  return z->data[0] + f->data[0] + fixed[0] + unknown[0];
}
