#pragma once

// ImportFlow - the modlist imports and the export: an MO2 modlist.txt, an
// MO2 instance's profile, a Wabbajack list, an openmw.cfg, a Nerevarine list
// (picked or dropped), and the lossless export. Owns the dialogs of that
// concern; the parsing is import_formats (pure, tested); the window is
// reached through WindowShell only, so a test drives the do* entry points
// with a fake shell and a model.

#include <QJsonObject>
#include <QObject>
#include <QString>

class WindowShell;

class ImportFlow : public QObject {
    Q_OBJECT
public:
    explicit ImportFlow(WindowShell &shell, QObject *parent = nullptr);

public slots:
    void exportModList();
    void onImportModList();
    void onImportMO2ModList();
    void onImportMO2Profile();
    void onImportWabbajack();
    // Seed the modlist from an existing openmw.cfg, for users coming from plain
    // openmw-launcher who'd otherwise re-add every mod by hand. Reads data=/
    // content=/groundcover=, skips the vanilla Data Files folder, one managed
    // row per remaining data= path, load order from content=/groundcover= in
    // file order.
    void onImportFromOpenMWConfig();

public:
    // A dropped .txt or .wabbajack, routed by what it holds.
    void handleDroppedImportFile(const QString &path);
    // The bodies behind the pickers, for drops and tests. Each asks before
    // replacing the list (through the shell) and persists what it imported.
    void doImportMO2ModList(const QString &path);
    void doImportOpenMWConfig(const QString &cfgPath);
    void doImportWabbajack(const QString &path);
    void doImportNerevarineModList(const QString &path);

private:
    void finishWabbajackImport(const QJsonObject &root);

    WindowShell &m_shell;
};
