// SPDX-License-Identifier: GPL-3.0-or-later
// JF8Call — JS8Call-compatible application
// Copyright (C) 2026 Ordo Artificum LLC

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include "jf8call_version.h"
#include <QApplication>
#include "mainwindow.h"
#ifdef HAVE_NCURSES
#include "tuiwindow.h"
#endif

int main(int argc, char *argv[])
{
    bool headless = false;
    bool tui      = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--headless") == 0 ||
            std::strcmp(argv[i], "--no-gui")   == 0) {
            headless = true;
        } else if (std::strcmp(argv[i], "--text") == 0) {
#ifdef HAVE_NCURSES
            tui = true;
#else
            headless = true;
#endif
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
                "  --text                 Run with terminal UI (ncurses)\n"
                "  --version, -v          Print version and exit\n"
                "  --help, -h             Print this help and exit\n"
                "\n"
                "WebSocket API listens on ws://localhost:2102 by default.\n"
                "Configure port and bind address in Preferences or settings.json.\n");
            return 0;
        }
    }

    // Auto-detect headless: no DISPLAY and no WAYLAND_DISPLAY → go headless
    // so the app doesn't crash when launched over SSH without X forwarding.
    if (!headless && !tui) {
        const bool hasDisplay = qEnvironmentVariableIsSet("DISPLAY") ||
                                qEnvironmentVariableIsSet("WAYLAND_DISPLAY");
        if (!hasDisplay) {
            headless = true;
            fprintf(stderr, "jf8call %s — no display detected, running headless\n",
                    JF8CALL_VERSION_STR);
        }
    }

    // --headless / --text: use Qt offscreen platform so no display connection is needed.
    // The offscreen plugin ships with qt6-base (libqoffscreen.so) and renders
    // to memory — all QObject / QTimer / audio / WS logic works normally.
    if (headless || tui) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        if (headless)
            fprintf(stderr, "jf8call %s — headless mode, WebSocket API on configured port\n",
                    JF8CALL_VERSION_STR);
    }

    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("Ordo Artificum"));
    app.setOrganizationDomain(QStringLiteral("ordo-artificum.com"));
    app.setApplicationName(QStringLiteral("JF8Call"));
    app.setApplicationVersion(QStringLiteral(JF8CALL_VERSION_STR));

    // In headless/TUI mode QApplication::quitOnLastWindowClosed must be false
    // so the app doesn't exit when no window is shown.
    if (headless || tui)
        app.setQuitOnLastWindowClosed(false);

    MainWindow w;
    if (!headless && !tui)
        w.show();

#ifdef HAVE_NCURSES
    TuiWindow *tui_win = nullptr;
    if (tui) {
        tui_win = new TuiWindow(&w);
        tui_win->start();
    }
#endif

    return app.exec();
}
