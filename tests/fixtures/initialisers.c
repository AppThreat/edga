// edga-options: --c11 --gcc
// Designated initialisers, a flexible array member and a variable-length array.
typedef unsigned long size_t;

struct point {
  int x, y;
};

struct message {
  size_t length;
  char body[];
};

struct point origin(void) {
  struct point p = {.y = 2, .x = 1};
  int grid[4] = {[2] = 7};
  return p.x + grid[2] > 0 ? p : (struct point){.x = 0};
}

int sum(int n) {
  int values[n];
  int total = 0;
  for (int i = 0; i < n; i++) {
    values[i] = i;
    total += values[i];
  }
  return total + (int)sizeof(struct message);
}
