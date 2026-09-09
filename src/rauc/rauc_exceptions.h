#pragma once
// Lightweight rauc:: exception definitions, kept out of rauc_dbus_client.h so
// consumers (and the classification test) can use them without pulling
// subprocess / libubootenv / libsystemd. The rauc_handler this once also came
// from is gone.
#include <string>
#include <exception>

constexpr char RAUC_DOMAIN[] = "RAUC";

namespace rauc
{
    ///////////////////////////////////////////////////////////////////////////
    /// rauc' exception definitions
    ///////////////////////////////////////////////////////////////////////////

    /**
     * Base class of exception class from helper module.
     * All exceptions of RAUC are derived from this base class.
     * RAUC-Errors are saved in variable "error_report".
     */
    class RaucBaseException : public std::exception
    {
        protected:
            std::string error_msg;
            std::string error_report;

            /* The report is the only text that says WHICH failure this was --
             * every consumer in lib, service and CLI reads what() and nothing
             * else, so a report kept beside it is a report nobody ever sees.
             * Composing here is what makes the distinct RAUC reasons distinct
             * at the boundary. */
            void compose(const std::string & msg, const std::string & report)
            {
                this->error_report = report;
                this->error_msg    = report.empty() ? msg : msg + ": " + report;
            }

        public:
            [[nodiscard]] const char * what() const noexcept override 
            {
                return this->error_msg.c_str();
            }

            [[nodiscard]] const std::string report() const
            {
                return this->error_report;
            }
    };

    class ParseJson : public RaucBaseException
    {       
        public:
            /**
             * Describes when the reported JSON-String from RAUC could not be parsed as a valid
             * JSON-String.
             * @param error_msg Reason for error during parsing.
             */
            explicit ParseJson(const std::string & error_msg)
            {
                this->error_msg = std::string("Could not parse JSON: \"") + error_msg + std::string("\"");
            }
    };

    class MarkUBootEnv : public RaucBaseException
    {
        public:
            /**
             * Can not activated or deactivate the partition which contains the UBoot-Environment.
             * @param error_string Error string of problem while writing
             * @param mark_active Activate/Deactivate the UBoot-Env. partition.
             */
            MarkUBootEnv(const std::string &error_string, bool mark_active)
            {
                if (mark_active)
                {
                    this->error_msg = std::string("Error during allow writing UBoot Env: ") + error_string;
                }
                else
                {
                    this->error_msg = std::string("Error during set read-only UBoot Env: ") + error_string;
                }
            }
    };

    class RaucInstallBundle : public RaucBaseException
    {
        public:
            /**
             * Can not install the RAUC bundle on the system.
             * @param bundle_path Path to the RAUC artifact.
             * @param error_report Report of RAUC installer to the failed install attempt.
             */
            RaucInstallBundle(const std::string & bundle_path, const std::string & error_report)
            {
                this->compose(std::string("Error during install of image: \"") + bundle_path + std::string("\""),
                              error_report);
            }
    };

    class RaucGetArtifactInformation : public RaucBaseException
    {
        public:
            /**
             * Error during gaining information from a given artifact.
             * @param bundle_path Path to the RAUC artifact.
             * @param error_report Report of RAUC installer to the failed read attempt.
             */
            RaucGetArtifactInformation(const std::string & bundle_path, const std::string & error_report)
            {
                this->error_msg = std::string("Error during gaining information: \"") + bundle_path + std::string("\"");
                this->error_report = error_report;
            }
    };

    class RaucMarkOtherPartition : public RaucBaseException
    {
        public:
            /**
             * Can not mark alternative partition as active, bootable.
             * @param error_report Report of RAUC installer on failure.
             */
            explicit RaucMarkOtherPartition(const std::string & error_report)
            {
                this->error_msg = std::string("Error during marking other image");
                this->error_report = error_report;
            }
    };

    class RaucRollback : public RaucBaseException
    {
        public:
            /**
             * Can not rollback to the old software version.
             * @param error_report Report during rollback from RAUC.
             */
            explicit RaucRollback(const std::string & error_report)
            {
                this->error_msg = std::string("Error during rollback");
                this->error_report = error_report;
            }
    };

    class RaucGetStatus : public RaucBaseException
    {
        public:
            /**
             * Error during attempt get status with RAUC.
             * @param error_report Problem to get status with RAUC.
             */
            explicit RaucGetStatus(const std::string & error_report)
            {
                this->error_msg = std::string("Error during getting status");
                this->error_report = error_report;
            }
    };

// -------------------------------------------------------------------------
// Additional exception classes for D-Bus-specific failures
// -------------------------------------------------------------------------

class RaucMarkGood : public RaucBaseException {
public:
    explicit RaucMarkGood(const std::string& error_report)
    {
        this->error_msg    = "Error during Mark(good, booted)";
        this->error_report = error_report;
    }
};

class RaucServiceUnavailable : public RaucBaseException {
public:
    explicit RaucServiceUnavailable(const std::string& error_report)
    {
        /* Two different situations throw this -- the service was never there,
         * and it vanished mid-install -- and they call for different operator
         * actions. Only the report tells them apart. */
        this->compose("RAUC D-Bus service unavailable (de.pengutronix.rauc)", error_report);
    }
};
}
