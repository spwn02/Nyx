import std;

import Nyx.Core;
import Nyx.Test;

using namespace Nyx;
using namespace Nyx::Test;

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
namespace Tests::fixtures {

namespace FixtureSubjects {

struct Transient final {
  u32 instance{};
};

struct Shared final {
  u32 instance{};
};

struct PerTest final {
  usize instance{};
};

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
inline usize transientCreations{};
inline usize sharedCreations{};
inline usize perTestCreations{};
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

auto resetFixtureCounters() -> void {
  transientCreations = 0;
  sharedCreations = 0;
  perTestCreations = 0;
}

[[= fixture]] auto transient() -> Transient {
  const Option<Ref<const Context>> context = currentContext();
  ++transientCreations;
  return Transient{
      .instance = context ? static_cast<u32>(context->get().testCase + 1) : 0,
  };
}

[[ = fixture, = once ]] auto shared() -> Shared {
  ++sharedCreations;
  return Shared{
      .instance = 1,
  };
}

[[= fixture]] auto perTest() -> PerTest {
  return PerTest{
      .instance = ++perTestCreations,
  };
}

[[
  = test,
  = description<"injects a context and both fixture lifetimes">(),
  = Case{11},
  = Case{29},
  = arg<"ctx">(context),
  = arg<"input">(fromCase)
]] auto receivesContext(const Context &ctx, u32 input, Transient transientValue, const Shared &sharedValue)
    -> void {
  const Option<Ref<const Context>> active = currentContext();
  const u32 expectedInput = ctx.testCase == 0 ? 11 : 29;

  require(active);
  require(ctx.name == "receivesContext"_exp);
  require(ctx.description == "injects a context and both fixture lifetimes"_exp);
  require(ctx.testCase < 2_exp);
  require(eq(input, expectedInput));
  require(eq(transientValue.instance, static_cast<u32>(ctx.testCase + 1)));
  require(sharedValue.instance == 1_exp);
  check(active->get().name == ctx.name);
  check(active->get().description == ctx.description);
  check(active->get().testCase == ctx.testCase);
}

[[= test]] auto reusesNormalFixtureWithinOneTest(PerTest first, const PerTest &second) -> void {
  require(eq(first.instance, second.instance));
}

[[ = test,
  = Case{11},
  = Case{29},
  = arg<"ctx">(context),
  = arg<"input">(fromCase) ]] auto receivesAsyncContext(Context ctx,
    u32 input,
    const Transient &transientValue,           // NOLINT
    const Shared &sharedValue) -> Task<void> { // NOLINT
  co_await yield();

  const Option<Ref<const Context>> active = currentContext();
  const u32 expectedInput = ctx.testCase == 0 ? 11 : 29;

  require(active);
  require(ctx.name == "receivesAsyncContext"_exp);
  require(eq(input, expectedInput));
  require(eq(transientValue.instance, static_cast<u32>(ctx.testCase + 1)));
  require(sharedValue.instance == 1_exp);
  check(active->get().testCase == ctx.testCase);
}

} // namespace FixtureSubjects

[[= test]] auto directRunInjectsContext() -> void {
  const Option<Ref<const Context>> outer = currentContext();
  const auto location = std::source_location::current();
  const TestExecution execution = run(
      TestDescriptor{
          .identifier = "directContext",
          .location = location,
      },
      [location](const Context &context) -> void {
        const Option<Ref<const Context>> active = currentContext();

        require(context.name == "directContext"_exp);
        require(context.description.empty());
        require(context.testCase == 0_exp);
        require(context.location.line() == location.line());
        require(active);
        check(std::addressof(active->get()) == std::addressof(context));
      });

  require(outer);
  require(execution.passed());
  require(execution.descriptor.name == "directContext"_exp);
  require(execution.state.assertions == 6_exp);
  require(currentContext());
  check(currentContext()->get().name == outer->get().name);
}

[[= test]] auto reflectedInjectionUsesFixtureScopes() -> void {
  FixtureSubjects::resetFixtureCounters();

  const Vec<TestExecution> firstRun = runAll<^^FixtureSubjects>();
  const Vec<TestExecution> secondRun = runAll<^^FixtureSubjects>();
  const auto passed = [](const TestExecution &execution) -> bool { return execution.passed(); };

  require(firstRun.size() == 5_exp);
  require(secondRun.size() == 5_exp);
  require(std::ranges::all_of(firstRun, passed));
  require(std::ranges::all_of(secondRun, passed));
  require(FixtureSubjects::transientCreations == 8_exp);
  require(FixtureSubjects::sharedCreations == 2_exp);
  require(FixtureSubjects::perTestCreations == 2_exp);
  check(firstRun.front().descriptor.name == "receivesContext"_exp);
  check(firstRun.front().descriptor.description == "injects a context and both fixture lifetimes"_exp);
  check(firstRun.front().descriptor.testCase == 0_exp);
  check(firstRun[1].descriptor.testCase == 1_exp);
  check(firstRun[2].descriptor.name == "reusesNormalFixtureWithinOneTest"_exp);
  check(firstRun[3].descriptor.name == "receivesAsyncContext"_exp);
}

} // namespace Tests::fixtures
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

consteval {
  discover<^^Tests::fixtures>();
}
