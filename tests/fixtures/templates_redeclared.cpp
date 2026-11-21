// edga-options: --c++17
// A template declared before it is defined: the declaration and the definition are two
// declarations of one template, and the instances point at the id of the one canonical entry,
// whichever declaration it came from.
template<typename T> T combine(T a, T b);

template<typename T> struct Pair {
    T first;
    T second;
};

template<typename T> T combine(T a, T b) { return a + b; }

int main()
{
    Pair<int> p{1, 2};
    return combine(p.first, p.second) + combine(3, 4);
}
