// edga-options: --c++17 --g++
// A function template instantiated with two types, a lambda that captures by reference and by
// value, and exceptions.
template <typename T>
T clamp_add(T a, T b, T limit) {
  T sum = a + b;
  return sum > limit ? limit : sum;
}

struct Failure {
  int code;
};

int use(int n) {
  short s = clamp_add<short>(n, 7, 100);
  long l = clamp_add(10L, 20L, 25L);
  int scale = 3;
  auto scaled = [&scale, n](int v) { return v * scale + n; };
  try {
    if (n < 0) throw Failure{n};
    return scaled(s) + static_cast<int>(l);
  } catch (const Failure &f) {
    return f.code;
  }
}
