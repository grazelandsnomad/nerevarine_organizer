#ifndef NEXUS_NAME_H
#define NEXUS_NAME_H

#include <QString>

// Strip the Nexus-style "-<modId>-<version>...-<timestamp>" suffix from a
// folder name so sibling folders that represent the same mod can be matched.
// e.g. "OAAB_Data" and "OAAB_Data-49042-2-5-1-1764958680" both reduce to
// "OAAB_Data".
QString nexusNameStem(const QString &name);

#endif // NEXUS_NAME_H
