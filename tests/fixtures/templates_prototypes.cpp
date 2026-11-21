// edga-options: --c++17
// Prototypes in a mode that does not defer template bodies: a member function template of a
// class template used through two instances shares one template and gets no prototype; an
// explicit specialisation leaves its primary template's prototype in place; members of a
// nested class and of a member class template of a class template get their prototypes; and a
// call with explicit template arguments on a dependent object keeps the name it was written with.
template <typename T>
struct Outer {
  template <typename U>
  U conv(T t) const { return static_cast<U>(t); }
  struct Inner {
    T x;
    T twice() const { return x + x; }
  };
  template <typename U>
  struct Nested {
    U u;
    U id() const { return u; }
  };
};

template <typename T>
int narrow(T t) { return (int)t; }

template <>
int narrow<char>(char c) { return 7; }

template <typename T>
struct Box {
  T v;
  T get() const { return v; }
};

template <>
int Box<int>::get() const { return 1; }

template <typename T>
struct Calls {
  T p;
  int g() { return p.template get<int>(); }
};

int use() {
  Outer<int> a;
  Outer<double> b;
  Box<int> x{};
  return a.conv<short>(1) + b.conv<short>(2.0) + narrow('c') + x.get();
}
