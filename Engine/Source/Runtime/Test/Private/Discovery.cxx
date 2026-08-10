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

[[nodiscard]] auto globMatches(StringView pattern, StringView value) -> bool {
  usize patternIndex{};
  usize valueIndex{};
  Option<usize> starIndex{};
  Option<usize> restartIndex{};

  while (valueIndex < value.size()) {
    if (patternIndex < pattern.size() and
        (pattern[patternIndex] == '?' or pattern[patternIndex] == value[valueIndex])) {
      ++patternIndex;
      ++valueIndex;
    } else if (patternIndex < pattern.size() and pattern[patternIndex] == '*') {
      starIndex = patternIndex++;
      restartIndex = valueIndex;
    } else if (starIndex) {
      patternIndex = *starIndex + 1;
      valueIndex = ++*restartIndex;
    } else {
      return false;
    }
  }

  while (patternIndex < pattern.size() and pattern[patternIndex] == '*')
    ++patternIndex;

  return patternIndex == pattern.size();
}

[[nodiscard]] auto matchesAny(const Vec<String> &patterns, StringView value) -> bool {
  return std::ranges::any_of(
      patterns, [value](const String &pattern) -> bool { return globMatches(pattern, value); });
}

[[nodiscard]] auto hasTag(const TestDescriptor &descriptor, StringView tag) -> bool {
  return std::ranges::contains(descriptor.metadata.tags, tag);
}

// auto validateUniqueIdentifiers(const Vec<TestDescriptor> &descriptors) -> void {
//   const auto duplicate =
//       std::ranges::adjacent_find(descriptors, std::ranges::equal_to{}, &TestDescriptor::identifier);
//   if (duplicate == descriptors.end())
//     return;
//
//   throw std::logic_error{
//       std::format("Nyx::Test discovered duplicate test identifier: {}", duplicate->identifier)};
// }

} // namespace

namespace detail {

auto filterDescriptors(Vec<TestDescriptor> descriptors, const TestSelection &selection)
    -> Vec<TestDescriptor> {
  std::erase_if(descriptors,
      [&selection](const TestDescriptor &descriptor) -> bool { return not selection.matches(descriptor); });
  return descriptors;
}

auto filterPlannedCases(detail::RunSession &session, const TestSelection &selection) -> void {
  std::erase_if(session.plannedCases(), [&selection](const detail::PlannedCase &plannedCases) -> bool {
    return not selection.matches(plannedCases.descriptor);
  });
}

[[nodiscard]] auto describeSuites(const Vec<SuiteEntry> &suites) -> Vec<TestDescriptor> {
  Vec<TestDescriptor> descriptors{};
  std::ranges::for_each(suites, [&descriptors](const SuiteEntry &suite) -> void {
    descriptors.append_range(suite.describe() | std::views::as_rvalue);
  });
  std::ranges::sort(descriptors, {}, &TestDescriptor::identifier);
  // validateUniqueIdentifiers(descriptors);
  return descriptors;
}

auto appendRegisteredSuite(const SuiteEntry &suite) -> void {
  catalog().append(suite);
}

} // namespace detail

auto TestSelection::matches(const TestDescriptor &descriptor) const -> bool {
  if (not include.empty() and not matchesAny(include, descriptor.identifier))
    return false;

  if (matchesAny(exclude, descriptor.identifier))
    return false;

  if (group and (not descriptor.metadata.group or *descriptor.metadata.group != *group))
    return false;

  if (not std::ranges::all_of(
          tagsAll, [&descriptor](const String &tag) -> bool { return hasTag(descriptor, tag); }))
    return false;

  return tagsAny.empty() or std::ranges::any_of(tagsAny,
                                [&descriptor](const String &tag) -> bool { return hasTag(descriptor, tag); });
}

auto discover() -> Vec<TestDescriptor> {
  return detail::describeSuites(registeredSuites());
}

auto list(TestSelection selection) -> Vec<TestDescriptor> { // NOLINT
  return detail::filterDescriptors(detail::describeSuites(registeredSuites()), selection);
}

auto runAll(RunOptions options) -> Vec<TestExecution> {
  return runAll({}, options);
}

auto runAll(TestSelection selection, RunOptions options) -> Vec<TestExecution> { // NOLINT
  const Vec<SuiteEntry> suites = registeredSuites();
  detail::RunSession session{};

  std::ranges::for_each(suites, [&session](const SuiteEntry &suite) -> void { suite.plan(session); });
  detail::filterPlannedCases(session, selection);

  Vec<TestExecution> executions = detail::executePlannedCases(session, options);
  std::ranges::stable_sort(executions, {}, [](const TestExecution &execution) -> const String & {
    return execution.descriptor.identifier;
  });
  return executions;
}

} // namespace Nyx::Test
