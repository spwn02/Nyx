import std;

import Nyx.Core;
import Nyx.Test;

using namespace Nyx;
using namespace Nyx::Test;

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
namespace Tests {

constexpr auto fibonacci(u32 input) -> u32 {
  switch (input) {
    case 0: return 0;
    case 1: return 1;
    default: return fibonacci(input - 2) + fibonacci(input - 1);
  }
}

[[ = test,
  = group("math"),
  = tag("fibonacci"),
  = Case{0, 0},
  = Case{1, 1},
  = Case{5, 5} ]] constexpr auto fibonacciCases(u32 input, u32 expected) -> bool {
  return fibonacci(input) == expected;
}

[[ = test, = group("demo") ]] auto voidCase() -> void {
  require(true);
}

auto ignored() -> bool {
  return false;
}

} // namespace Tests
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

consteval {
  discover<^^Tests>();
}
