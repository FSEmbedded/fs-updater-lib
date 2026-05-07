#include "UpdateStoreV2.h"

#include "BoundedReader.h"
#include "Descriptor.h"
#include "Sha256Hasher.h"
#include "UpdateStreamSink.h"
#include "fs_exceptions.h"

#include <cerrno>
#include <cstddef>
#include <ios>
#include <string>

namespace fs {

namespace {

constexpr std::streamsize kStreamChunk = 8192;

} // namespace

void extract_member(std::istream& source, const Member& member, UpdateStreamSink& sink)
{
    BoundedReader reader(source,
                         static_cast<std::streamoff>(member.offset),
                         static_cast<std::streamsize>(member.size));
    Sha256Hasher hasher;
    char buf[kStreamChunk];

    try {
        while (reader.remaining() > 0) {
            const std::streamsize got = reader.read(buf, kStreamChunk);
            if (got <= 0) {
                throw GenericException(
                    "v2.0 member '" + member.name +
                        "': source truncated before declared size " +
                        std::to_string(member.size),
                    EIO);
            }
            const auto n = static_cast<std::size_t>(got);
            hasher.update(buf, n);
            sink.write(buf, n);
        }
        const std::string actual = hasher.hex_digest();
        if (actual != member.sha256) {
            throw GenericException(
                "v2.0 member '" + member.name +
                    "': sha256 mismatch (expected " + member.sha256 +
                    ", got " + actual + ")",
                EILSEQ);
        }
        sink.commit();
    } catch (...) {
        sink.abort();
        throw;
    }
}

} // namespace fs
