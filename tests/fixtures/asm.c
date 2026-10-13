// edga-options: --gcc --gnu_version=140200 --c17
// Inline assembly: what an asm statement writes and reads, and the symbols asm labels give.
int add(int a, int b) {
  int out;
  __asm__ volatile("add %0, %1, %2" : "=r"(out) : "r"(a), "r"(b) : "memory");
  return out;
}

int bump(int v) {
  __asm__("" : "+r"(v));
  return v;
}

int named(int x) {
  int y;
  __asm__("mov %[dst], %[src]" : [dst] "=r"(y) : [src] "r"(x));
  return y;
}

int tested(int x) {
  __asm__ goto("" : : "r"(x) : : fail);
  return 0;
fail:
  return 1;
}

void barrier(void) { __asm__ __volatile__("" ::: "memory"); }

extern int renamed(int) __asm__("real_symbol");
int counter __asm__("counter_symbol") = 0;

int call(void) { return renamed(1) + counter; }
