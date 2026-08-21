export module Nyx.RHI:ResourcePool;

import std;
import Miracle;

import :Handle;

using namespace Miracle;

export namespace Nyx::RHI::detail {

template <HandleTag Tag, class Resource>
class ResourcePool final {
public:
  ResourcePool() = default;
  ~ResourcePool() noexcept = default;

  ResourcePool(const ResourcePool &) = delete (
      "Resource pools own GPU resource records and cannot be copied");
  auto operator=(const ResourcePool &)
      -> ResourcePool & = delete ("Resource pools own GPU resource records and cannot be copied");

  ResourcePool(ResourcePool &&) noexcept = default;
  auto operator=(ResourcePool &&) noexcept -> ResourcePool & = default;

  template <class... Args>
  auto emplace(Args &&...args) -> Result<Handle<Tag>> {
    const u32 index = acquireIndex();
    const u32 generation = slots_[index].generation;
    const Handle<Tag> handle{index, generation};

    auto iterator = resources_.emplace(Resource{handle, std::forward<Args>(args)...});

    slots_[index].resource = iterator;
    return handle;
  }

  auto get(Handle<Tag> handle) -> Result<Ref<Resource>> {
    if (not contains(handle))
      return bail({"Attempted to access an invalid resource handle."});

    return Ref<Resource>{**slots_[handle.index()].resource};
  }

  auto get(Handle<Tag> handle) const -> Result<Ref<const Resource>> {
    if (not contains(handle))
      return bail({"Attempted to access an invalid resource handle."});

    return Ref<const Resource>{**slots_[handle.index()].resource};
  }

  auto erase(Handle<Tag> handle) -> Result<void> {
    if (not contains(handle))
      return bail({"Attempted to destroy an invalid resource handle."});

    Slot &slot = slots_[handle.index()];
    resources_.erase(*slot.resource);
    slot.resource.reset();
    slot.generation = nextGeneration(slot.generation);
    freeIndices_.push_back(handle.index());
    return {};
  }

  [[nodiscard]]
  constexpr auto contains(Handle<Tag> handle) const noexcept -> bool {
    return handle.valid() and handle.index() < slots_.size() and
           slots_[handle.index()].generation == handle.generation() and
           slots_[handle.index()].resource.has_value();
  }

private:
  using Iterator = typename Hive<Resource>::iterator;

  struct Slot {
    HandleGeneration generation{1};
    Option<Iterator> resource;
  };

  auto acquireIndex() -> HandleIndex {
    if (not freeIndices_.empty()) {
      const HandleIndex index = freeIndices_.back();
      freeIndices_.pop_back();
      return index;
    }

    slots_.emplace_back();
    return static_cast<HandleIndex>(slots_.size() - 1);
  }

  static constexpr auto nextGeneration(HandleGeneration generation) noexcept -> HandleGeneration {
    if (generation == std::numeric_limits<HandleGeneration>::max())
      return 1;

    return generation + 1;
  }

  Hive<Resource> resources_;
  Vec<Slot> slots_;
  Vec<HandleIndex> freeIndices_;
};

} // namespace Nyx::RHI::detail
