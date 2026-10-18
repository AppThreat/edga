// edga-options: --c++17 --g++
// The lifetimes a full-expression and a lambda give the objects they make: the temporaries a
// statement destroys at its end, one a reference binding keeps alive, and what each capture of a
// lambda holds.
typedef unsigned long size_t;

struct text {
  text(const char *s);
  text(const text &other);
  ~text();
  const char *c_str() const;
  size_t size() const;

 private:
  char *data_;
};

text make(int id);
void use(const char *);

void temporary_ends(int id) {
  const char *p = make(id).c_str();
  use(p);
}

void reference_extends(int id) {
  const text &kept = make(id);
  use(kept.c_str());
}

int captures(int seed) {
  int local = seed;
  char buf[8] = {0};
  auto by_ref = [&local]() { return local; };
  auto by_copy = [local, &buf]() { return local + buf[0]; };
  auto init = [n = seed * 2]() { return n; };
  auto implicit = [=]() { return local; };
  auto folded = [k = 16]() { return k; };
  return by_ref() + by_copy() + init() + implicit() + folded();
}
