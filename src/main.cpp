// SPDX-License-Identifier: GPL-3.0-or-later
// JF8Call — JS8Call-compatible application
// Copyright (C) 2026 Ordo Artificum LLC

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include "jf8call_version.h"
#include <QApplication>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    bool headless = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--headless") == 0 ||
            std::strcmp(argv[i], "--no-gui")   == 0) {
            headless = true;
        } else if (std::strcmp(argv[i], "--text") == 0) {
            // TUI not yet implemented — fall through to headless for now
            headless = true;
        } else if (std::strcmp(argv[i], "--version") == 0 ||
                   std::strcmp(argv[i], "-v")         == 0) {
            fprintf(stdout, "jf8call %s\n", JF8CALL_VERSION_STR);
            return 0;
        } else if (std::strcmp(argv[i], "--help") == 0 ||
                   std::strcmp(argv[i], "-h")     == 0) {
            fprintf(stdout,
                "Usage: jf8call [OPTIONS]\n"
                "\n"
                "Options:\n"
                "  --headless, --no-gui   Run without GUI (WebSocket API + audio only)\n"
                "  --version, -v          Print version and exit\n"
                "  --help, -h             Print this help and exit\n"
                "\n"
                "WebSocket API listens on ws://localhost:2102 by default.\n"
                "Configure port and bind address in Preferences or settings.json.\n");
            return 0;
        }
    }

    // --headless: use Qt offscreen platform so no display connection is needed.
    // The offscreen plugin ships with qt6-base (libqoffscreen.so) and renders
    // to memory — all QObject / QTimer / audio / WS logic works normally.
    if (headless) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        fprintf(stderr, "jf8call %s — headless mode, WebSocket API on configured port\n",
                JF8CALL_VERSION_STR);
    }

    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("Ordo Artificum"));
    app.setOrganizationDomain(QStringLiteral("ordo-artificum.com"));
    app.setApplicationName(QStringLiteral("JF8Call"));
    app.setApplicationVersion(QStringLiteral(JF8CALL_VERSION_STR));

    // In headless mode QApplication::quitOnLastWindowClosed must be false
    // so the app doesn't exit when no window is shown.
    if (headless)
        app.setQuitOnLastWindowClosed(false);

    MainWindow w;
    if (!headless)
        w.show();

    return app.exec();
}
