# Lunaris

A C++23 library for embedding [Luau](https://github.com/luau-lang/luau), with an
API in the spirit of [sol3](https://github.com/ThePhD/sol2). Lunaris reports
errors with exceptions and is meant to gain sandboxing features later.

> **Status: pre-0.1.** The API is not stable. Only the `State` class exists so
> far. See [docs/PLAN.md](docs/PLAN.md) for the plan.

## Requirements

- CMake 3.25 or newer
- A C++23 compiler with `std::expected` (GCC 12+, Clang 16+, MSVC 2022 17.3+).
  These are the targeted minimums and are not yet verified by CI.

Luau and GoogleTest are vendored in `thirdparty/`. Nothing else needs to be
installed.

## Build and test

```
cmake --preset default
cmake --build --preset default
ctest --preset default
```

The preset uses Ninja and a Debug build.

## Use in your project

Add Lunaris as a subdirectory and link the target:

```cmake
add_subdirectory(path/to/Lunaris)
target_link_libraries(your_target PRIVATE Lunaris::Lunaris)
```

Tests and examples are only built when Lunaris is the top-level project.

```cpp
#include <lunaris/state.h>

lunaris::State state; // owns a Luau state and closes it on destruction
```

## Third-party code

- Luau, MIT license, version in `thirdparty/luau/VERSION.md`
- GoogleTest, BSD 3-Clause license, in `thirdparty/googletest`

## License

MIT, see [LICENSE](LICENSE).
