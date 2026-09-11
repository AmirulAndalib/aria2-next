# Test organization

CTest runs one native doctest executable. Test names identify the behavior and
remain stable when a case moves between files. Source registration lives in
`cmake/TestSources.cmake`; generated files and test output belong in the build
directory.

| Location | Coverage |
| --- | --- |
| `rpc/` | Request validation, task controls, status projection, options and protocol extensions |
| `ed2k/` | Link/packet codecs, source policies, requested ranges, task lifecycle and loopback network exchanges |
| `metalink/` | Version-specific documents, malformed input and piece checksums |
| `support/` | Text, filenames, encoding, numbers, paths and network address utilities |
| Root test files | Focused engine, storage, platform and native-library contracts |

Use `TEST_CASE` for independent cases and `TEST_CASE_FIXTURE` when initialization
is genuinely shared. RPC initialization and ED2K loopback helpers are compiled
once in their respective support files. `CurlSessionTest` retains a small friend
adapter because doctest's derived fixture classes do not inherit friendship.
`a2doctest.h` supplies diagnostic formatting, not another test lifecycle.

Keep tests that distinguish protocol boundaries, failure handling, ownership,
integrity and recovery. Remove fixtures or helpers when their last caller is
removed. Related boundary cases may share a longer file; splitting a file must
preserve its cases and feature guards rather than reduce coverage to meet a
line-count target.

Executable-level transfer scenarios remain under
[`tools/transfer_validation`](../tools/transfer_validation/README.md). Public
network interoperability checks are separate from this deterministic suite.
