// edga-options: --c11 --gcc
// A diag_suppress pragma with no argument and one with a trailing comma: the front end warns,
// and the code after each is still parsed (before EDG GH #203 the rest of the file was lost).
#pragma diagnostic push
#pragma diag_suppress
int first(void) {
  int a = 1;
  return a;
}
#pragma diagnostic pop
#pragma diag_suppress 177,
static int unused_helper(void) { return 0; }
int second(int *p) { return p ? *p + first() : 0; }
