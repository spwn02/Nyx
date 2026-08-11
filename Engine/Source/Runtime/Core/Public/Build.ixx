export module Nyx.Core:Build;

/// Re-exports the generated build contract through Nyx.Core.
///
/// Core is the common dependency of the runtime modules, so this bridge keeps existing `import Nyx.Core` call
/// sites on the same CMake-generated values as direct `import Nyx.Build` users.
export import Nyx.Build;
