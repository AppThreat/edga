// edga-options: --c11 --gcc
// Nested function-like macros, an object-like macro and a stringised argument.
#define LIMIT 16
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define CLAMP(v, lo, hi) MIN(MIN(v, hi), lo)
#define NAME_OF(x) #x

int clamp_size(int requested) {
  int limited = CLAMP(requested, 0, LIMIT);
  return limited;
}

const char *field_name(void) { return NAME_OF(requested); }
