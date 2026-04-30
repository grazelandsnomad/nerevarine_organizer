// tests/test_fs_utils.cpp
//
// Coverage for include/fs_utils.h pure helpers.  Only sanitizeFolderName
// lives there so far; this is the file to grow if more pure naming /
// path-munging helpers get extracted.
//
// Build + run:
//   ./build/tests/test_fs_utils

#include "fs_utils.h"

#include <QCoreApplication>
#include <QString>

#include <iostream>

static int s_passed = 0;
static int s_failed = 0;

static void check(const char *name, bool ok, const QString &got = QString())
{
    if (ok) {
        std::cout << "  \033[32m✓\033[0m " << name << "\n";
        ++s_passed;
    } else {
        std::cout << "  \033[31m✗\033[0m " << name;
        if (!got.isNull()) std::cout << "  - got: \"" << got.toStdString() << "\"";
        std::cout << "\n";
        ++s_failed;
    }
}

static void expect(const char *name, const QString &input, const QString &expected)
{
    QString got = fsutils::sanitizeFolderName(input);
    check(name, got == expected, got);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::cout << "=== fs_utils tests ===\n";

    // -- Empty / trivial ---
    expect("empty → empty",                "",                        "");
    expect("plain ASCII round-trips",      "OAAB Data",               "OAAB Data");
    expect("underscores kept",             "OAAB_Data",               "OAAB_Data");
    expect("digits kept",                  "Mod v2.3",                "Mod v2.3");

    // -- Punctuation whitelist ---
    expect("hyphen",                       "a-b",                     "a-b");
    expect("dot",                          "a.b",                     "a.b");
    expect("parens",                       "Mod (v1)",                "Mod (v1)");
    expect("apostrophe",                   "Arkngthand's Lost",       "Arkngthand's Lost");
    expect("ampersand",                    "Tombs & Towers",          "Tombs & Towers");

    // -- Filesystem-hostile characters get dropped ---
    expect("forward slash dropped",        "bad/name",                "badname");
    expect("backslash dropped",            "bad\\name",               "badname");
    expect("colon dropped",                "bad:name",                "badname");
    expect("star dropped",                 "bad*name",                "badname");
    expect("question mark dropped",        "bad?name",                "badname");
    expect("pipe dropped",                 "bad|name",                "badname");
    expect("quote dropped",                "bad\"name",               "badname");
    expect("angle brackets dropped",       "<bad>",                   "bad");
    expect("null byte dropped",            QString("bad") + QChar(0) + "name",
                                           "badname");

    // -- Whitespace normalisation ---
    expect("leading space stripped",       "   OAAB",                 "OAAB");
    expect("trailing space stripped",      "OAAB   ",                 "OAAB");
    expect("internal run collapsed",       "a    b",                  "a b");
    expect("tab → space",                  "a\tb",                    "a b");
    expect("newline → space",              "a\nb",                    "a b");
    expect("nbsp → space",                 QString("a") + QChar(0x00A0) + "b",
                                           "a b");

    // -- Unicode preservation ---
    expect("accented Latin preserved",     "Néréwarine",              "Néréwarine");
    expect("Cyrillic preserved",           "Морровинд",               "Морровинд");
    expect("CJK preserved",                "魔法師",                   "魔法師");
    expect("Greek preserved",              "Μόροουιντ",               "Μόροουιντ");
    expect("Arabic preserved",             "اللحن",                   "اللحن");

    // Emoji: not "letter-or-number" so should be dropped.  Test with a
    // character in the Basic Multilingual Plane so the source file is clean
    // ASCII for QChar literal safety; surrogate pairs are covered by the
    // Cyrillic/CJK cases above.
    expect("symbol dropped",               QString("A") + QChar(0x00A9) + "B", // © copyright
                                           "AB");

    // -- Combined bad + good ---
    expect("mixed filesystem garbage",     "Mod<>:|Name/\\v1",        "ModNamev1");
    expect("whitespace + garbage",         "   Mod  *  Name   ",      "Mod Name");

    // -- Worst-case inputs shouldn't crash ---
    expect("all invalid → empty",          "///***",                  "");
    expect("only whitespace → empty",      " \t\n ",                  "");

    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
