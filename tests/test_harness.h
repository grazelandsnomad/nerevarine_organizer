#pragma once

// Shared test harness: the check() every test_*.cpp used to copy-paste, plus the
// pass/fail counters it drives. One superset signature subsumes every historical
// call shape:
//   check(name, ok)                  - bare assertion
//   check(name, ok, detail)          - failure hint shown in parens
//   check(name, ok, got, want)       - failure shows a want/got diff block
// Each test executable is a single TU that includes this, so the inline counters
// are one instance per binary; main() still reads s_passed/s_failed and returns
// s_failed == 0 ? 0 : 1.

#include <QString>

#include <iostream>

inline int s_passed = 0;
inline int s_failed = 0;

inline void check(const char *name, bool ok,
                  const QString &detail = {}, const QString &want = {})
{
    if (ok) {
        std::cout << "  \033[32m\xE2\x9C\x93\033[0m " << name << "\n";
        ++s_passed;
    } else {
        std::cout << "  \033[31m\xE2\x9C\x97\033[0m " << name;
        if (want.isEmpty()) {
            if (!detail.isEmpty())
                std::cout << " (" << detail.toStdString() << ")";
            std::cout << "\n";
        } else {
            std::cout << "\n";
            std::cout << "    --- want ---\n" << want.toStdString() << "\n";
            std::cout << "    ---  got ---\n" << detail.toStdString() << "\n";
        }
        ++s_failed;
    }
}
