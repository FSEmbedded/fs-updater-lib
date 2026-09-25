/**
 * Util is for all needed functions that only fulfill supporting aspects.
 */

#pragma once

#include <cstdint>   // for uint8_t
#include <string>
#include <vector>
#include <exception>
#include <stdexcept>
#include <climits>
#include <ios>
#include <filesystem>
#include <system_error>
#include <utility>

namespace util
{
    ///////////////////////////////////////////////////////////////////////////
    /// utillity function collection
    ///////////////////////////////////////////////////////////////////////////

    /**
     * Split the string at given char and return the parts as array with strings.
     * @param input String that that have to be split at given ASCII-element.
     * @param split ASCII-element where to split string.
     * @return Array of strings which are substrings for given splitted input.
     */
    std::vector<std::string> split(const std::string & input, const char split = ' ');

    /**
     * Convert a string to unsigned char.
     * @param input Input string.
     * @return Array of string where every char is converted to unsigned char.
     */
    std::vector<uint8_t> to_array(const std::string & input);

    /**
     * Convert a string to unsigned char.
     * The string must also be a number which is in the range of unsigned char.
     * @param input String element which is a number;
     * @return Unsigned char extracted of input string.
     * @throw std::overflow_error If number in string is larger than unsigned char.
     */
    unsigned char to_uchar(const std::string & input);

    /**
     * Whether a path can be stat()ed.
     * @param path File or directory path.
     * @return 0 when it can, otherwise the errno of the failure.
     */
    int stat_error(const std::string &path);

    /**
     * Tell whether a loop device's backing file names the given image.
     * An absent or unreadable backing file is no match: a missing loop
     * device must never block an install.
     * @param backing_file The sysfs backing_file of the loop device.
     * @param image_name File name of the image to look for.
     * @return True only when the backing file names the image.
     */
    bool loop_backing_file_names(const std::filesystem::path &backing_file, const std::string &image_name);

    /**
     * Describe the error state of a stream.
     * @param stream The stream to inspect.
     * @return Human-readable error description.
     */
    inline std::string describe_stream_error(const std::ios &stream)
    {
        if (stream.bad())
            return "Read/writing error on I/O operation";
        if (stream.fail())
            return "Logical error on I/O operation";
        if (stream.eof())
            return "End-of-File reached on input operation";
        return "Unknown I/O error";
    }

    /**
     * Remove a directory tree when the guard goes out of scope.
     * Removal is best effort and never throws: the destructor may run while
     * an exception is unwinding the stack, where a second throw terminates.
     */
    class ScopedDirectory
    {
    public:
        explicit ScopedDirectory(std::filesystem::path dir) : dir(std::move(dir)) {}

        ScopedDirectory(const ScopedDirectory &) = delete;
        ScopedDirectory &operator=(const ScopedDirectory &) = delete;
        ScopedDirectory(ScopedDirectory &&) = delete;
        ScopedDirectory &operator=(ScopedDirectory &&) = delete;

        ~ScopedDirectory()
        {
            std::error_code ec;
            std::filesystem::remove_all(this->dir, ec);
        }

        const std::filesystem::path &path() const { return this->dir; }

    private:
        std::filesystem::path dir;
    };
}
