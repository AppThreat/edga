// edga-options: --c++26
// C++26 expansion statements: `template for` over a list becomes one block per element, each
// declaring the loop variable bound to its element, after the init-statement if there is one.
int sink(int);

int over_list() {
  int total = 0;
  template for (constexpr int v : {1, 2, 3}) {
    total += sink(v);
  }
  return total;
}

int with_init() {
  template for (int base = 10; auto v : {1, 2}) {
    sink(base + v);
  }
  return 0;
}

int empty_list() {
  template for (auto v : {}) {
    sink(v);
  }
  return 0;
}

template <typename T> T scaled(T x) {
  T total = 0;
  template for (constexpr int k : {2, 4}) {
    total += x * k;
  }
  return total;
}

int use_scaled() { return scaled(3); }
