// edga-options: --c++17 --max_depth_constexpr_call=300
// In the ISO dialect: a template argument deduced from a noexcept-specifier (CWG 2355, EDG GH
// #197), and a constexpr recursion through call arguments that stays within the call depth limit
// (EDG GH #178).
template <bool B>
constexpr bool is_nothrow(void (*)() noexcept(B)) {
  return B;
}
void quiet() noexcept;
void loud();

constexpr int add(int a, int b) { return a + b; }
constexpr int depth(int n) { return n == 0 ? 0 : add(1, depth(n - 1)); }
static_assert(depth(256) == 256, "");

int table[depth(256)];

int pick() { return (is_nothrow(quiet) ? 1 : 0) + (is_nothrow(loud) ? 2 : 0) + sizeof table; }
