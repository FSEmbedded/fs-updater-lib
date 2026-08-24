# Coding Standard Deviations

Documented deviations from the framework's C++ coding standard. Each entry
records the rule, the deviation, and the rationale.

The entries below record what this component does and why; they are not
a to-do list. Every entry was measured against the tree, not assumed.

| ID | Rule / guideline | Deviation | Rationale |
|----|------------------|-----------|-----------|
| D-1 | Banned feature: exceptions (`throw`/`try`/`catch`), `-fno-exceptions` | The whole error model is exception-based: an exception hierarchy rooted in `fs::BaseFSUpdateException`, raised and caught at roughly 280 sites | The hierarchy is **installed public interface**: consumers outside this repo catch these types by name, and the command-line tool maps them to documented exit codes. Replacing it is an ABI and contract change for every consumer, not a code-style change. No component in the framework actually sets `-fno-exceptions`; the rule is met in practice only by `dynamic-overlay`, which raises none. Replacing it requires moving every consumer in the same change and verifying it separately. |
| D-2 | Banned feature: `std::function` | Used for callback seams — chunk and progress callbacks in the container reader and the application image path, and the injected rollback action | These are per-operation callbacks on I/O-bound paths, not inner-loop calls; the type erasure buys the testability that lets those paths be driven from an in-memory double. A function pointer cannot carry the captured state the call sites need. |
| D-3 | Banned feature: `fork`/`exec`/`system` | One call: `std::system` in the RAUC install sink | The single remaining shell-out; already reported by clang-tidy (`bugprone-command-processor`) and carried as this component's accepted lint ceiling of one error, so it cannot grow unnoticed. |
| D-4 | Naming: constants and `constexpr` are `snake_case`, not `ALL_CAPS` | Logging domain constants are `ALL_CAPS` (`FSUPDATE_DOMAIN`, `BOOTSTATE_DOMAIN`, `RAUC_DOMAIN`) | Consistent across every domain constant in the component. The standard's own naming section states that consistency with the project convention outranks a default, and renaming a subset would make the component less consistent, not more. |

## Rules this component does meet

Recorded because a deviations register that lists only failures invites the
assumption that the rest was never checked. Measured on the tree:
no `dynamic_cast`, no `typeid`, no `std::regex`, no `<iostream>`, no
`std::list`, and no raw `new`/`delete` — the only textual matches are
comments and one commented-out line. `std::filesystem` is not used either:
the POSIX helpers in `src/util/posix_utils.h` exist precisely to replace it,
and say so. Members carry a trailing underscore, and `[[nodiscard]]` is used
on value-returning interfaces.
