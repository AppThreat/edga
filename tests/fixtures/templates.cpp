// edga-options: --c++17 --g++
// Templates and their instances: a class template with members used with two argument types, a
// function template used with two types, templates nothing instantiates, a dependent call on a
// parameter of type T, a partial and an explicit specialisation, a variadic template, a lambda
// with an auto parameter passed to an algorithm, and templates a project header defines that
// the unit uses.

#include "templates_functions.h"

template <typename T>
struct Box {
  T value;
  T get() const { return value; }
  void reset(T v) { value = v; }
  static int live;
};

template <typename T>
int Box<T>::live = 0;

template <typename T>
T twice(T x) {
  T sum = T();
  return sum + x + x;
}

// Nothing instantiates this function template or the class template member below.
template <typename T>
int unused(T p) {
  return p.size() + 1;
}

template <typename T>
struct Unused {
  T load() const { return T(); }
};

// A call written in template code on a parameter of type T.
template <typename T>
int read_value(const T &x) {
  return x.get();
}

// A partial specialisation over pointers.
template <typename T>
struct Box<T *> {
  T *value;
  T *get() const { return value; }
};

// An explicit specialisation for bool.
template <>
struct Box<bool> {
  bool value;
  bool get() const { return value; }
};

// An explicit specialisation of a function template.
template <>
double twice<double>(double x) {
  return 2.0 * x;
}

template <typename T, typename... Rest>
int count_pack(T first, Rest... rest) {
  return 1 + sizeof...(rest);
}

template <typename T>
struct Holder {
  T stored;
  T get() const { return stored; }
};

template <typename T, typename U = T>
struct Defaulted {
  U convert(T v) { return static_cast<U>(v); }
};

template <typename T>
T var_scale = T(2);

template <>
int var_scale<char> = 8;

template <typename T>
struct Wrapper {
  T item;
};

template <typename T>
struct Table {
  static const bool small = traits::is_small<T>::value;
};

struct reading {
  int get() const { return 1; }
};

int use(int n) {
  Box<int> boxed{3};
  boxed.live = n;
  Box<long> long_boxed{4L};
  Box<bool> flag{true};
  Box<reading> reader;
  Box<int *> pbox{nullptr};
  int total = twice(n) + twice(3L) + twice(1.5) + static_cast<int>(twice<double>(2.5));
  total += boxed.get() + long_boxed.get() + flag.get() + reader.get().get();
  total += pbox.get() != nullptr ? 1 : 0;
  total += count_pack(1, 2L, 3.0f);
  total += var_scale<int> + var_scale<char>;
  Holder<int> holder{5};
  total += holder.get();
  Defaulted<int> widened;
  total += static_cast<int>(widened.convert(6));
  total += read_value(reading());
  int values[4] = {n, 1, 2, 3};
  int *end = values + 4;
  int *found = algo::find_if(values, end, [](auto v) { return v > 2; });
  auto first = *found;
  total += first;
  total += Wrapper<int>{7}.item;
  total += traits::pick(n, 9);
  return total;
}
