// A source file with Windows line endings (CRLF): the positions must be the same as the
// same file with LF line endings.
#define STEPS 3
static int scale(int v) {
  int doubled = v * STEPS;
  return doubled + 1;
}
int total(const int *values, int n) {
  int sum = 0;
  for (int i = 0; i < n; i++)
    sum += scale(values[i]);
  return sum;
}
