// libFuzzer target for fs::open_update_container (the v2.0 F&S header +
// length-prefixed JSON member-table descriptor — see
// UpdateContainerReader.h).
//
// open_update_container() throws fs::GenericException (a std::exception)
// on every malformed-input path by design — that's the expected, handled
// outcome for adversarial input, not a fuzzer finding. Only an unhandled
// exception, an ASan/UBSan report, or a native crash should ever surface.
#include "handle_update/UpdateContainerReader.h"
#include "handle_update/fs_exceptions.h"

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    const std::string bytes(reinterpret_cast<const char *>(data), size);
    std::istringstream source(bytes, std::ios::binary);

    try {
        (void)fs::open_update_container(source);
    } catch (const std::exception &) {
        // Expected: header/descriptor rejection. Any input that reaches
        // here without an ASan/UBSan report or a crash is a pass.
    }
    return 0;
}
