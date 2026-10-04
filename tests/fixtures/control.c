// edga-options: --c11 --gcc
// Every kind of statement: loops, switch with fall-through, goto and labels, break, continue.
int classify(int n) {
  int result = 0;
  switch (n) {
    case 0:
      result = 1;
    case 1:
      result += 2;
      break;
    default:
      result = -1;
  }
  while (n > 10) {
    n /= 2;
    if (n == 12) continue;
    if (n == 11) break;
  }
  do {
    n++;
  } while (n < 3);
  if (n < 0) goto fail;
  return result;
fail:
  return -1;
}

/* An enumeration with explicit and implicit values, indexing a table with one entry for each. */
enum mode { MODE_READ = 1, MODE_WRITE, MODE_BOTH = MODE_READ | MODE_WRITE };
static const char *const mode_names[4] = {"none", "read", "write", "both"};

const char *mode_name(enum mode m) { return mode_names[m]; }

/* A variable length array, its number of elements evaluated where it is declared. */
int vla_sum(int n) {
  int values[n];
  int total = 0;
  for (int i = 0; i < n; i++) values[i] = i;
  for (int i = 0; i < n; i++) total += values[i];
  return total;
}
