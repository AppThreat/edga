// edga-options: --c++17 --g++
// A range-based for, new and delete in every form, a member defined outside its class, and an
// object whose destructor runs where its block ends.
typedef unsigned long size_type;
void *operator new(size_type size, void *where) noexcept;

struct Buffer {
  char *bytes;
  explicit Buffer(int n) : bytes(new char[n]) {}
  ~Buffer() { delete[] bytes; }
  int first() const;
};

int Buffer::first() const { return bytes[0]; }

int sum(const int (&values)[4]) {
  int total = 0;
  for (int v : values) total += v;
  return total;
}

int lifetimes(int n) {
  int *one = new int(n);
  int *many = new int[n];
  char storage[sizeof(int)];
  int *placed = new (storage) int(7);
  Buffer *heap = new Buffer(n);
  int result = *one + many[0] + *placed + heap->first();
  {
    Buffer scoped(n);
    result += scoped.first();
  }
  delete heap;
  delete[] many;
  delete one;
  return result;
}

// A structured binding: the object it initialises and the bindings that name its parts; and the
// name the front end predefines in every function.
struct Span {
  char *data;
  size_type size;
};
Span make_span(char *p, size_type n);
void note(const char *where);

size_type split(char *p, size_type n) {
  note(__func__);
  auto [data, size] = make_span(p, n);
  Span spans[2] = {{p, n}, {p, 0}};
  size_type total = 0;
  for (auto [d, s] : spans) total += s;
  return data[0] + size + total;
}
