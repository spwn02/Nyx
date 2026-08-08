import Nyx.Core;
import Nyx.Test;

using namespace Nyx;
using namespace Nyx::Test;

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
namespace Tests::example {

[[nodiscard]] auto divide(i32 num, i32 den) noexcept -> Result<i32> {
  if (den == 0)
    return bail(Error{"Cannot divide by zero!"});

  return num / den;
}

[[ = test, = group("example"), = tag("math"), = Case{5, 2} ]] auto dividePass(i32 num, i32 den) noexcept
    -> Result<i32> {
  return divide(num, den);
}

[[ = test, = group("example"), =tag("math"), = Case{5, 0}, = shouldPanic("Cannot divide by zero!") ]] auto divideFail(
    i32 num,
    i32 den) noexcept -> Result<i32> {
  return divide(num, den);
}

} // namespace Tests::example
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

consteval {
  discover<^^Tests::example>();
}
