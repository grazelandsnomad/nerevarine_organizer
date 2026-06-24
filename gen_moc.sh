#!/bin/sh
mkdir -p moc
/usr/lib/qt6/moc -I include include/mainwindow.h -o moc/moc_mainwindow.cpp
/usr/lib/qt6/moc -I include include/separatordialog.h -o moc/moc_separatordialog.cpp
/usr/lib/qt6/moc -I include include/modlistdelegate.h -o moc/moc_modlistdelegate.cpp
/usr/lib/qt6/moc -I include include/fomodwizard.h   -o moc/moc_fomodwizard.cpp

# Copy translations next to both Linux build outputs so the app can find them at runtime
mkdir -p bin/Debug_Linux/translations
mkdir -p bin/Release_Linux/translations
cp -r translations/. bin/Debug_Linux/translations/
cp -r translations/. bin/Release_Linux/translations/

# Copy prefs file to Linux build outputs (only if destination is older or missing)
cp -n nerevarine_prefs.ini bin/Debug_Linux/nerevarine_prefs.ini   2>/dev/null || true
cp -n nerevarine_prefs.ini bin/Release_Linux/nerevarine_prefs.ini 2>/dev/null || true
