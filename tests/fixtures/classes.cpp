// edga-options: --c++17 --g++
// A class hierarchy with a virtual call, constructors and destructors, and an operator overload.
namespace geometry {

struct Vec2 {
  int x, y;
  Vec2 operator+(const Vec2 &other) const { return {x + other.x, y + other.y}; }
};

class Shape {
 public:
  virtual ~Shape() {}
  virtual int area() const = 0;
};

class Square : public Shape {
 public:
  explicit Square(int side) : side_(side) {}
  ~Square() override {}
  int area() const override { return side_ * side_; }

 private:
  int side_;
};

int measure(const Shape &s) { return s.area(); }

int combined() {
  Square sq(3);
  Vec2 a{1, 2};
  Vec2 b = a + a;
  return measure(sq) + b.x;
}

}  // namespace geometry
