// edga-options: --c++17 --g++ --edga-no-prototypes
// An instantiated template and an uninstantiated one, exported with prototypes off: only the
// instance appears, the shape exports before prototypes wrote. The uninstantiated template
// leaves no routine behind.
template <typename T>
T scaled(T x) {
  T sum = T();
  return sum + x + x;
}

template <typename T>
int lone(T p) {
  return p.size() + 1;
}

int use() { return scaled(3); }
