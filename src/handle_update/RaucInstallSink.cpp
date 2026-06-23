#include "RaucInstallSink.h"

#include "fs_exceptions.h"
#include "util/posix_utils.h"

#include <cerrno>
#include <cstdlib>
#include <string>

namespace fs {

RaucInstallSink::RaucInstallSink(std::string scratch_path)
    : RaucInstallSink(std::move(scratch_path), &default_rauc_invocation)
{
}

RaucInstallSink::RaucInstallSink(std::string scratch_path, RaucInvoker invoker)
    : scratch_path_(std::move(scratch_path)),
      invoker_(std::move(invoker)),
      file_sink_(scratch_path_)
{
}

void RaucInstallSink::write(const char* data, std::size_t n)
{
    file_sink_.write(data, n);
}

void RaucInstallSink::commit()
{
    // Atomically place the bundle bytes at scratch_path_.
    file_sink_.commit();

    // Hand the path to RAUC.
    const int rc = invoker_(scratch_path_);
    if (rc != 0) {
        // Keep the scratch file for forensics; let the caller diagnose.
        throw GenericException(
            "rauc install '" + scratch_path_ +
                "' failed with rc=" + std::to_string(rc),
            EIO);
    }

    // Success: clean up the staged bundle. Best-effort — install
    // succeeded, so a leftover file is not a fatal condition.
    (void)util::remove_file(scratch_path_);
}

void RaucInstallSink::abort()
{
    file_sink_.abort();
}

int RaucInstallSink::default_rauc_invocation(const std::string& bundle)
{
    // Mirrors src/rauc/rauc_handler.cpp's subprocess invocation pattern
    // without coupling to its internal state. fs-updater-service / the
    // CLI dispatcher can swap in a D-Bus invoker via the test ctor when
    // BUILD_DBUS_SUPPORT=ON.
    const std::string command = "rauc install " + bundle;
    return std::system(command.c_str());
}

} // namespace fs
