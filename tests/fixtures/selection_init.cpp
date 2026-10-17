// edga-options: --c++17 --g++
// A selection statement's initialisation statement, and a switch with one: the declaration
// ahead of the condition, exported as the statement's init.
int source(void);
int pick(void) {
  if (int count = source(); count > 0) {
    return count;
  }
  return 0;
}
int choose(void) {
  switch (int code = source(); code) {
    case 0:
      return code;
    default:
      return -1;
  }
}
