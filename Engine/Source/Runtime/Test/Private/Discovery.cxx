module Nyx.Test;

import :Discovery;

import std;
import Nyx.Core;

namespace Nyx::Test {

namespace {

[[nodiscard]] auto locationComesBefore(const std::source_location &left, const std::source_location &right)
    -> bool {
  const StringView leftFile{left.file_name()};
  const StringView rightFile{right.file_name()};

  if (leftFile != rightFile)
    return leftFile < rightFile;

  if (left.line() != right.line())
    return left.line() < right.line();

  return left.column() < right.column();
}

[[nodiscard]] auto suiteComesBefore(const SuiteEntry &left, const SuiteEntry &right) -> bool {
  if (left.scope != right.scope)
    return left.scope < right.scope;

  return locationComesBefore(left.location, right.location);
}

class SuiteCatalog final {
public:
  auto append(const SuiteEntry &suite) -> void {
    const std::scoped_lock lock{mutex_};
    suites_.emplace(suite);
  }

  [[nodiscard]] auto snapshot() const -> Vec<SuiteEntry> {
    const std::scoped_lock lock{mutex_};
    Vec<SuiteEntry> result = suites_ | std::ranges::to<Vec<SuiteEntry>>();
    std::ranges::sort(result, suiteComesBefore);
    return result;
  }

private:
  mutable std::mutex mutex_;
  Hive<SuiteEntry> suites_;
};

[[nodiscard]] auto catalog() -> SuiteCatalog & {
  static SuiteCatalog catalog_{};
  return catalog_;
}

[[nodiscard]] auto registeredSuites() -> Vec<SuiteEntry> {
  return catalog().snapshot();
}

auto validateUniqueIdentifiers(const Vec<TestDescriptor> &descriptors) -> void {
  const auto duplicate =
      std::ranges::adjacent_find(descriptors, std::ranges::equal_to{}, &TestDescriptor::identifier);
  if (duplicate == descriptors.end())
    return;

  throw std::logic_error{
      std::format("Nyx::Test discovered duplicate test identifier: {}", duplicate->identifier)};
}

[[nodiscard]] auto describeSuites(const Vec<SuiteEntry> &suites) -> Vec<TestDescriptor> {
  Vec<TestDescriptor> descriptors{};
  std::ranges::for_each(suites, [&descriptors](const SuiteEntry &suite) -> void {
    descriptors.append_range(suite.describe() | std::views::as_rvalue);
  });
  std::ranges::sort(descriptors, {}, &TestDescriptor::identifier);
  validateUniqueIdentifiers(descriptors);
  return descriptors;
}

} // namespace

namespace detail {

auto appendRegisteredSuite(const SuiteEntry &suite) -> void {
  catalog().append(suite);
}

} // namespace detail

auto discover() -> Vec<TestDescriptor> {
  return describeSuites(registeredSuites());
}

auto runAll(RunOptions options) -> Vec<TestExecution> {
  const Vec<SuiteEntry> suites = registeredSuites();
  static_cast<void>(describeSuites(suites));

  Vec<TestExecution> executions{};
  std::ranges::for_each(registeredSuites(), [&executions, &options](const SuiteEntry &suite) -> void {
    executions.append_range(suite.execute(options) | std::views::as_rvalue);
  });
  std::ranges::sort(executions, {}, [](const TestExecution &execution) -> const String & {
    return execution.descriptor.identifier;
  });
  return executions;
}

} // namespace Nyx::Test
