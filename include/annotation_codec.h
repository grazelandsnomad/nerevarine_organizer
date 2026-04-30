#ifndef ANNOTATION_CODEC_H
#define ANNOTATION_CODEC_H

#include <QString>

// Escape `\`, `\t`, `\n` so a multi-line annotation can be safely embedded in a
// single tab-delimited line of the modlist format. Round-trip with decodeAnnot.
inline QString encodeAnnot(const QString &s)
{
    QString out; out.reserve(s.size());
    for (QChar c : s) {
        if      (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\t') out += "\\t";
        else                out += c;
    }
    return out;
}

inline QString decodeAnnot(const QString &s)
{
    QString out; out.reserve(s.size());
    for (int i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            QChar n = s[i + 1];
            if (n == 'n')  { out += '\n'; ++i; continue; }
            if (n == 't')  { out += '\t'; ++i; continue; }
            if (n == '\\') { out += '\\'; ++i; continue; }
        }
        out += s[i];
    }
    return out;
}

#endif // ANNOTATION_CODEC_H
