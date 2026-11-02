# Lunaris

Lunaris is a standalone C++23 wrapper around Luau. It is designed to be usable
without Axiom; engine integrations such as VFS and scheduling belong in the
consuming application.

The current implementation starts with RAII state ownership and custom
allocator support. See [the design document](docs/Lunaris_Design.md) for the
planned API and implementation sequence.

## Dependencies

Luau and GoogleTest are vendored in `thirdparty/`; no submodule initialization
is needed. GoogleTest is only used by the test target and is not a Lunaris
runtime dependency.

## Build and test

Requires CMake 3.25+, a C++23 compiler, and Ninja.

```sh
cmake --preset default
cmake --build --preset default
ctest --preset default
```

## License

Lunaris is licensed under the MIT License; see [LICENSE](LICENSE). The vendored
dependencies retain their own license files: Luau is MIT-licensed and
GoogleTest is BSD-3-Clause-licensed.
