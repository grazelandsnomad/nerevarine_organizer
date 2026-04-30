// tests/test_annotation_codec.cpp
//
// Round-trip tests for the annotation encode/decode helpers used when writing
// a mod's multi-line personal annotation into the single-line tab-delimited
// modlist format. If these break, saved annotations start corrupting mod
// lists (tabs would shift columns, newlines would split entries).
//
// Build with CMake (see tests/CMakeLists.txt) and run directly:
//   ./build/tests/test_annotation_codec

#include "annotation_codec.h"

#include <QString>
#include <iostream>

static int s_passed = 0;
static int s_failed = 0;

static void check(const char *name, bool ok)
{
    if (ok) {
        std::cout << "  \033[32m✓\033[0m " << name << "\n";
        ++s_passed;
    } else {
        std::cout << "  \033[31m✗\033[0m " << name << "\n";
        ++s_failed;
    }
}

static void checkRoundTrip(const char *name, const QString &in)
{
    QString encoded = encodeAnnot(in);
    QString decoded = decodeAnnot(encoded);

    bool noNewlines = !encoded.contains('\n');
    bool noRawTabs  = !encoded.contains('\t');
    bool roundTrip  = (decoded == in);

    check(name, noNewlines && noRawTabs && roundTrip);
}

int main()
{
    std::cout << "=== annotation codec tests ===\n";

    // Empty
    checkRoundTrip("empty string round-trips",               QString());

    // Plain text (no escaping required)
    checkRoundTrip("plain ASCII round-trips",                "works well! checked April 2026");

    // Unicode including emoji - must not be mangled
    checkRoundTrip("unicode / emoji round-trips",            "Néréwarine 💀 ñoño");

    // The three characters that MUST be escaped for the tab-delimited format
    checkRoundTrip("contains literal backslash",             "path\\to\\thing");
    checkRoundTrip("contains tab",                           "col1\tcol2\tcol3");
    checkRoundTrip("contains newline",                       "line 1\nline 2");
    checkRoundTrip("mixed tabs and newlines",                "a\tb\nc\td\ne");

    // Edge cases: backslashes near escaped sequences (decoder must not over-eat)
    checkRoundTrip("literal '\\n' as text (two chars)",      "raw \\n not a newline");
    checkRoundTrip("literal '\\\\n' as text",                "\\\\n stays literal");

    // Trailing backslash must not cause read-past-end in the decoder
    checkRoundTrip("trailing backslash",                     "ends in backslash\\");

    // Large string with everything mixed
    checkRoundTrip("kitchen sink",
        "Tabs:\t\tNewlines:\n\nBackslashes:\\\\End");

    // The encoder's output must be single-line (critical invariant)
    {
        QString enc = encodeAnnot("a\nb\tc\\d");
        check("encoded form has no raw newline/tab",
              !enc.contains('\n') && !enc.contains('\t'));
    }

    // Decoder tolerates malformed input without crashing
    {
        QString dec = decodeAnnot("stray \\");
        check("decoder handles lone trailing backslash gracefully",
              !dec.isEmpty());
    }

    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
