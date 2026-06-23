#pragma once
// Lightweight UBoot exception definitions, split out of UBoot.h so consumers
// (and the classification test) can use them without pulling <libuboot.h>.
#include <string>
#include <exception>

namespace UBoot
{
    ///////////////////////////////////////////////////////////////////////////
    /// UBoot' exception definitions
    ///////////////////////////////////////////////////////////////////////////

    /**
     * Base class for exceptions with UBootEnvironment.
     */
    class UBootError : public std::exception
    {
        protected:
            std::string error_string;
        
        public:
            [[nodiscard]] const char * what() const noexcept override 
            {
                return this->error_string.c_str();
            }
    };

    class UBootEnvAccess : public UBootError
    {
        public:
            /**
             *  Report error that occurs during the access of the UBoot-Environment.
             *  The variable is not stated in the UBoot-Environment.
             *  @param var_name Variable name which the error occurs
             */
            explicit UBootEnvAccess(const std::string &var_name)
            {
                this->error_string = std::string("Error while access U-Boot Env; variable: \"")
                                    + var_name + std::string("\"");

            }
    };

    class UBootEnvWrite : public UBootError
    {
        public:
            /**
             * Report error that occurs during the access of the UBoot-Environment.
             * Key-value parameters could not be written to the UBoot-Environment.
             * @param var_name Variable name which throws the error.
             * @param var_content The value of the variable.
             */
            UBootEnvWrite(const std::string &var_name, const std::string &var_content)
            {
                this-> error_string = std::string("Error while writing in U-Boot Env; variable: \"") + var_name;
                this->error_string += std::string("\"; content:\"") + var_content + std::string("\"");
            }
    };

    class UBootEnv : public UBootError
    {
        public:
            /**
             * Report error that occurs when a general problem of accessing
             * the UBoot-Environment happens.
             * @param error_string Contains the reason for the access problem.
             */
            explicit UBootEnv(const std::string &error_string)
            {
                this->error_string = std::string("Error during access U-Boot Env: ") + error_string;
            }
    };

    class UBootEnvVarNotAllowedContent : public UBootError
    {
        public:
            /**
             * Variable missmatched with allowed states inside UBoot variable.
             * @param var Variable that throw error.
             * @param content Actual content of uboot environment.
             * @param allowed List of allowed states as string.
             */
            UBootEnvVarNotAllowedContent(const std::string &var, const std::string &content, const std::string &allowed)
            {
                this->error_string = std::string("Variable: \"") + var + std::string("\" ") ;
                this->error_string += std::string("does not allowed content: \"") + allowed + std::string("\" instead:");
                this->error_string += content + std::string("\"");
            }
    };

    class UBootEnvVarCanNotConvertedIntoReturnType : public UBootError
    {
        public:
            /**
             * UBoot-Enviornment variable can not be converted into given return value type.
             * @param content Variable content that occurs the error.
             */
            explicit UBootEnvVarCanNotConvertedIntoReturnType(const std::string &var, const std::string &content)
            {
                this->error_string = std::string("Variable: \"") + var + std::string("\" ") + std::string("can not converted into return type: ") + content;
            }
    };
}
