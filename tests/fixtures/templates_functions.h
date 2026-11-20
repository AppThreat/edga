// Function and class templates a unit uses: the shape a standard algorithm and a small trait
// table have, declared here so the fixture does not depend on the host's C++ library.
#ifndef TEMPLATES_FUNCTIONS_H
#define TEMPLATES_FUNCTIONS_H

namespace algo {

template <class InputIt, class Predicate>
InputIt find_if(InputIt first, InputIt last, Predicate pred) {
  for (; first != last; ++first)
    if (pred(*first)) return first;
  return last;
}

}  // namespace algo

namespace traits {

template <typename T>
struct is_small {
  static const bool value = sizeof(T) <= 8;
};

template <typename T>
T pick(const T &a, const T &b) {
  return a < b ? a : b;
}

}  // namespace traits

#endif
