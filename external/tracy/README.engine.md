# Tracy client sources

- Upstream: https://github.com/wolfpld/tracy
- Pinned release: **v0.14.1** — https://github.com/wolfpld/tracy/releases/tag/v0.14.1
- Source archive: https://github.com/wolfpld/tracy/archive/refs/tags/v0.14.1.zip
- Archive SHA-256: `908f3a2917fa86a247abfcf85dcf04bad1db6986a4d40f94b70512f3e9e98d5b`
- Contents: upstream `public/` directory and `LICENSE`, copied without changes.

The root CMakeLists.txt compiles only `public/TracyClient.cpp`, as documented
by upstream. Do not add the included client/common `.cpp` files as separate
translation units. `TracyHeaders` provides the include path for builds with
disabled instrumentation; `Tracy::TracyClient` also supplies matching
`TRACY_ENABLE` and `TRACY_ON_DEMAND` definitions to the engine in Release and
RelWithDebInfo when `ENGINE_ENABLE_TRACY=ON`.

The profiler UI/capture tools are separate executables. Use version 0.14.1
to match this client's protocol. They are not required to build the engine.
