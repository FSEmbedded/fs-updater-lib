#pragma once

namespace fs
{
    /* The revision this library was built from.
     *
     * Resolved at runtime on purpose. The value is also a macro in the
     * generated config header, but a consumer that read it there would report
     * the build it was *compiled* against, while a shared library can be
     * replaced independently -- and a version report that can be wrong is worse
     * than none. The project version cannot serve here either: it marks a
     * generation, not a build.
     *
     * Never null. Where no revision was supplied at build time the string is
     * "unknown", so a caller can print it unconditionally.
     */
    const char *library_source_id();
}
