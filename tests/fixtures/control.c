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
