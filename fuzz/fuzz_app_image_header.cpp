// libFuzzer target for updater::HeaderParser (the F&S application-image
// header — see app_image_format.h).
//
// parse()/validate_crc() are pure, exception-free, dependency-free
// functions over a fixed-size byte buffer — no setup needed beyond
// wrapping the fuzzer's input.
#include "handle_update/app_image_format.h"

#include <cstddef>
#include <cstdint>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    // parse() itself already rejects any size != HEADER_SIZE, but exploring
    // that path costs nothing and confirms the guard holds for every size.
    const std::vector<uint8_t> header_data(data, data + size);

    const auto header = updater::HeaderParser::parse(header_data);
    if (header.is_valid()) {
        // validate_crc reads the same buffer again under a header that
        // claims a valid squashfs_size/version; the interesting case is
        // whether CRC computation itself ever goes out of bounds.
        (void)updater::HeaderParser::validate_crc(header, header_data);
    }
    return 0;
}
