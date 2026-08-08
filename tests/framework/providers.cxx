import std;

import Nyx.Core;
import Nyx.Test;

using namespace Nyx;
using namespace Nyx::Test;

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
namespace Tests::providers {

namespace ProviderSubjects {

[[ = test, = arg<"input">(values(None, " ", "    ")) ]] auto optionalValues(Option<const char *> input)
    -> void {
  if (input)
    require(StringView{*input}.find(' ') == 0_exp);
  else
    require(not input);
}

[[ = test,
  = Case{2},
  = Case{5},
  = arg<"base">(fromCase),
  = arg<"offset">(values(10, 20)) ]] auto caseAndValues(u32 base, u32 offset) -> void {
  require(base > 0_exp);
  require(offset == 10_exp or offset == 20_exp);
}

[[ = test, = arg<"ctx">(context), = arg<"value">(values(3, 7)) ]] auto receivesProviderContext(
    const Context &ctx,
    u32 value) -> void {
  require(ctx.testCase < 2_exp);
  require(value == 3_exp or value == 7_exp);
}

[[ = test, = arg<"path">(files(__FILE__)) ]] auto receivesFile(const Path &path) -> void {
  require(path == Path{__FILE__});
}

[[ = test, = arg<"path">(files("__nyx_missing_provider__/**/*.md")) ]] auto receivesNoFiles(const Path &path)
    -> void {
  require(path.empty());
}

} // namespace ProviderSubject

auto temporaryDirectory() -> Path {
  const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() / std::format("nyx-test-provider-{}", tick);
}

auto writeFile(const Path &path) -> void {
  std::ofstream output{path};
  require(output);
  output << "Nyx.Test provider fixture\n";
  require(output);
}

[[nodiscard]] auto executionNamed(const Vec<TestExecution> &executions, StringView identifier)
    -> Option<Ref<const TestExecution>> {
  const auto execution = std::ranges::find_if(
      executions, [identifier](const TestExecution &candidate) constexpr noexcept -> bool {
        return candidate.descriptor.identifier == identifier;
      });
  if (execution == executions.end())
    return None;

  return std::cref(*execution);
}

[[= test]] auto expandsValuesAndFileProviders() -> void {
  constexpr usize expectedDescriptors{11};
  constexpr usize expectedPassed{10};
  constexpr usize expectedFailed{1};
  Vec<TestDescriptor> descriptors = describe<^^ProviderSubjects>();
  Vec<TestExecution> executions = runAll<^^ProviderSubjects>();
  const TestSummary summary = Reporter::summarize(executions);

  const Option<Ref<const TestExecution>> noFiles =
      executionNamed(executions, "receivesNoFiles(path=<no values>)");
  const Option<Ref<const TestExecution>> firstContext =
      executionNamed(executions, "receivesProviderContext(value=3)");
  const Option<Ref<const TestExecution>> secondContext =
      executionNamed(executions, "receivesProviderContext(value=7)");

  require(eq(descriptors.size(), expectedDescriptors));
  require(eq(executions.size(), expectedDescriptors));
  require(eq(summary.passedCount, expectedPassed));
  require(eq(summary.failedCount, expectedFailed));
  require(noFiles);
  require(firstContext);
  require(secondContext);
  check(descriptors.front().identifier == "optionalValues(input=None)"_exp);
  check(descriptors[1].identifier == "optionalValues(input=\" \")"_exp);
  check(descriptors[2].identifier == "optionalValues(input=\"    \")"_exp);
  check(descriptors[3].identifier == "caseAndValues(2, offset=10)"_exp);
  check(descriptors[6].identifier == "caseAndValues(5, offset=20)"_exp);
  check(noFiles->get().failed());
  check(noFiles->get().state.errors == 1_exp);
  check(noFiles->get().state.diagnostics.front().header.code == DiagnosticCode::ProviderProducedNoValues);
  check(firstContext->get().descriptor.testCase == 0_exp);
  check(secondContext->get().descriptor.testCase == 1_exp);
}

[[= test]] auto matchesAndFiltersFiles() -> void {
  const Path root = temporaryDirectory();
  const auto cleanup = std::scope_exit([&root] -> void {
    std::error_code error;
    std::filesystem::remove_all(root, error);
  });
  std::error_code error;
  std::filesystem::create_directories(root / ".hidden", error);
  require(not error);
  writeFile(root / "included.md");
  writeFile(root / "excluded.md");
  writeFile(root / ".hidden" / "hidden.md");
  writeFile(root / "ignored.txt");

  const String pattern = (root / "**/*.md").generic_string();
  const FileQuery visible{
      .pattern = pattern,
      .excludes = Vec<String>{"excluded"},
  };
  const FileQuery all{
      .pattern = pattern,
      .includeDotFiles = true,
  };
  const FileQuery hiddenLiteral{
      .pattern = (root / ".hidden" / "hidden.md").generic_string(),
  };
  const FileQuery visibleHiddenLiteral{
      .pattern = hiddenLiteral.pattern,
      .includeDotFiles = true,
  };
  const Vec<Path> visibleFiles = findFiles(visible);
  const Vec<Path> allFiles = findFiles(all);
  const Vec<Path> hiddenFiles = findFiles(hiddenLiteral);
  const Vec<Path> visibleHiddenFiles = findFiles(visibleHiddenLiteral);

  require(matchesGlob("src/readme.md", "src/**/*.md"));
  require(matchesGlob("src/docs/readme.md", "src/**/*.md"));
  require(not matchesGlob("src/docs/readme.txt", "src/**/*.md"));
  require(visibleFiles.size() == 1_exp);
  require(allFiles.size() == 3_exp);
  require(hiddenFiles.empty());
  require(visibleHiddenFiles.size() == 1_exp);
  check(visibleFiles.front().filename().generic_string() == "included.md"_exp);
  check(std::ranges::any_of(
      allFiles, [](const Path &path) -> bool { return path.filename().generic_string() == "hidden.md"; }));
}

} // namespace Tests::providers
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

consteval {
  discover<^^Tests>();
}
