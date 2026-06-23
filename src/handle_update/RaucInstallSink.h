#pragma once

#include "UpdateStreamSink.h"

#include <functional>
#include <string>
#include <utility>

namespace fs {

/**
 * Sink that streams the v2.0 firmware-bundle bytes to a configured
 * persistent-storage scratch path, then on `commit()` invokes
 * `rauc install <scratch>`. On RAUC success the scratch file is
 * removed; on RAUC failure it is kept for forensics and a
 * fs::GenericException is thrown.
 *
 * Composes a `FileSink` for the atomic-write semantics. The destructor
 * of the embedded `FileSink` cleans up if `commit()` was never reached.
 *
 * Test seam: a `RaucInvoker` callable can be supplied to redirect the
 * RAUC call. The single-argument constructor binds the real
 * `rauc install` subprocess invocation.
 */
class RaucInstallSink : public UpdateStreamSink
{
public:
    /// Returns 0 on RAUC success, non-zero on RAUC failure.
    using RaucInvoker = std::function<int(const std::string&)>;

    /// Production constructor: real `rauc install` invocation via subprocess.
    explicit RaucInstallSink(std::string scratch_path);

    /// Test constructor: caller supplies the invoker.
    RaucInstallSink(std::string scratch_path, RaucInvoker invoker);

    ~RaucInstallSink() override = default;

    void write(const char* data, std::size_t n) override;
    void commit() override;
    void abort() override;

private:
    static int default_rauc_invocation(const std::string& bundle);

    std::string scratch_path_;
    RaucInvoker invoker_;
    FileSink file_sink_;
};

} // namespace fs
