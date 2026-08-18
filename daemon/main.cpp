/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * dripd -- the drip engine.
 *
 * Runs headless under systemd --user. Owns the tailscaled connection, the
 * inbox, and the transfer queue; the Plasma applet is only a view onto it.
 */

#include "dbusservice.h"
#include "inbox.h"
#include "localapi.h"
#include "notifier.h"
#include "settings.h"
#include "tailnet.h"
#include "transfers.h"
#include "util.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QTimer>

#include <cstdio>

namespace
{

QTextStream &out()
{
    static QTextStream stream(stdout);
    return stream;
}

/** Match a device by stable id, DNS label, or display name, case-insensitively. */
Device resolveDevice(const Tailnet *tailnet, const QString &needle)
{
    const QList<Device> devices = tailnet->devices();
    for (const Device &device : devices) {
        if (device.stableId == needle) {
            return device;
        }
    }
    for (const Device &device : devices) {
        if (device.hostName.compare(needle, Qt::CaseInsensitive) == 0
            || device.displayName.compare(needle, Qt::CaseInsensitive) == 0) {
            return device;
        }
    }
    return {};
}

/**
 * --probe: print what the engine can see, then exit. This is the gate the whole
 * project rests on -- if this is right, the daemon is talking to tailscaled
 * correctly and everything above it is just presentation.
 */
int runProbe(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    auto *api = new LocalApi(&app);
    auto *tailnet = new Tailnet(api, &app);

    int exitCode = 0;
    bool statusDone = false;
    bool inboxDone = false;

    const auto finishIfReady = [&] {
        if (statusDone && inboxDone) {
            out().flush();
            QCoreApplication::exit(exitCode);
        }
    };

    out() << "socket: " << api->socketPath() << "\n\n";

    QObject::connect(tailnet, &Tailnet::devicesChanged, &app, [&] {
        if (statusDone) {
            return;
        }
        statusDone = true;

        const Device self = tailnet->self();
        out() << "self:   " << self.displayName << "  (" << self.os << ", " << self.primaryIp << ")\n";
        out() << "owner:  " << self.ownerName << "\n";
        out() << "state:  " << tailnet->backendState() << "\n\n";

        const QList<Device> devices = tailnet->devices();
        out() << "devices (" << devices.size() << "):\n";
        out() << QStringLiteral("  %1 %2 %3 %4 %5\n")
                     .arg(QStringLiteral("NAME"), -20)
                     .arg(QStringLiteral("OS"), -9)
                     .arg(QStringLiteral("OWNER"), -14)
                     .arg(QStringLiteral("ONLINE"), -7)
                     .arg(QStringLiteral("CAN RECEIVE"));
        for (const Device &device : devices) {
            out() << QStringLiteral("  %1 %2 %3 %4 %5\n")
                         .arg(device.displayName, -20)
                         .arg(device.os, -9)
                         .arg(device.ownerName, -14)
                         .arg(device.online ? QStringLiteral("yes") : QStringLiteral("no"), -7)
                         .arg(device.canReceive() ? QStringLiteral("yes") : device.unavailableReason());
        }
        out() << "\n";
        finishIfReady();
    });

    LocalApiReply *inbox = api->get(QStringLiteral("/localapi/v0/files/"));
    QObject::connect(inbox, &LocalApiReply::finished, &app, [&, inbox] {
        const QJsonArray files = QJsonDocument::fromJson(inbox->body()).array();
        out() << "inbox (" << files.size() << " waiting):\n";
        for (const QJsonValue &value : files) {
            const QJsonObject file = value.toObject();
            out() << "  " << file.value(QStringLiteral("Name")).toString() << "  "
                  << drip::humanSize(static_cast<qint64>(file.value(QStringLiteral("Size")).toDouble())) << "\n";
        }
        if (files.isEmpty()) {
            out() << "  (empty)\n";
        }
        inboxDone = true;
        finishIfReady();
    });
    QObject::connect(inbox, &LocalApiReply::errored, &app, [&](const QString &message) {
        out() << "inbox: ERROR " << message << "\n";
        exitCode = 1;
        inboxDone = true;
        finishIfReady();
    });

    tailnet->refresh();

    QTimer::singleShot(10000, &app, [&] {
        out() << "\nERROR: timed out talking to tailscaled.\n";
        out() << "Check that tailscaled is running and that you are the operator:\n";
        out() << "  tailscale set --operator=$USER\n";
        out().flush();
        QCoreApplication::exit(1);
    });

    return app.exec();
}

/** --send <device> <file>... : exercise the send path with no UI in the way. */
int runSend(int argc, char **argv, const QStringList &args)
{
    QCoreApplication app(argc, argv);

    if (args.size() < 2) {
        out() << "usage: dripd --send <device> <file>...\n";
        out().flush();
        return 2;
    }

    const QString target = args.first();
    const QStringList files = args.mid(1);

    auto *api = new LocalApi(&app);
    auto *tailnet = new Tailnet(api, &app);
    auto *transfers = new TransferManager(api, tailnet, &app);

    int exitCode = 0;
    int outstanding = 0;
    bool started = false;

    QObject::connect(transfers, &TransferManager::transferUpdated, &app, [&](const Transfer &transfer) {
        if (transfer.state == TransferState::Active) {
            const double pct = transfer.progress() * 100.0;
            out() << QStringLiteral("\r  %1  %2 / %3  (%4%)")
                         .arg(transfer.fileName)
                         .arg(drip::humanSize(transfer.transferred))
                         .arg(drip::humanSize(transfer.size))
                         .arg(pct, 0, 'f', 1);
            out().flush();
            return;
        }
        if (!transfer.isFinished()) {
            return;
        }

        if (transfer.state == TransferState::Completed) {
            out() << QStringLiteral("\r  %1  %2  sent\n").arg(transfer.fileName, drip::humanSize(transfer.size));
        } else {
            out() << QStringLiteral("\r  %1  FAILED: %2\n").arg(transfer.fileName, transfer.error);
            exitCode = 1;
        }
        out().flush();

        if (--outstanding == 0) {
            QCoreApplication::exit(exitCode);
        }
    });

    QObject::connect(tailnet, &Tailnet::devicesChanged, &app, [&] {
        if (started) {
            return;
        }
        started = true;

        const Device device = resolveDevice(tailnet, target);
        if (device.stableId.isEmpty()) {
            out() << "no such device: " << target << "\n";
            out() << "known devices:\n";
            for (const Device &known : tailnet->devices()) {
                out() << "  " << known.hostName << "  (" << known.displayName << ")\n";
            }
            out().flush();
            QCoreApplication::exit(2);
            return;
        }
        if (!device.canReceive()) {
            out() << device.displayName << " cannot receive files right now: " << device.unavailableReason() << "\n";
            out().flush();
            QCoreApplication::exit(1);
            return;
        }

        out() << "sending to " << device.displayName << " (" << device.stableId << ")\n";
        for (const QString &file : files) {
            if (!QFileInfo::exists(file)) {
                out() << "  " << file << "  SKIPPED: no such file\n";
                exitCode = 1;
                continue;
            }
            if (!transfers->send(device.stableId, file).isEmpty()) {
                ++outstanding;
            }
        }
        if (outstanding == 0) {
            out().flush();
            QCoreApplication::exit(exitCode);
        }
    });

    tailnet->refresh();
    return app.exec();
}

int runDaemon(int argc, char **argv)
{
    // QApplication rather than QGuiApplication: the "Move to..." notification
    // action needs a folder picker, and QFileDialog is a widget.
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("drip"));
    QApplication::setApplicationDisplayName(QStringLiteral("drip"));
    QApplication::setOrganizationName(QStringLiteral("drip"));
    QApplication::setDesktopFileName(QStringLiteral("dev.drip.daemon"));
    QApplication::setQuitOnLastWindowClosed(false);

    auto *settings = new Settings(&app);
    auto *api = new LocalApi(&app);
    auto *tailnet = new Tailnet(api, &app);
    auto *transfers = new TransferManager(api, tailnet, &app);
    auto *inbox = new InboxWatcher(api, tailnet, transfers, &app);
    auto *notifier = new Notifier(&app);

    const auto applySettings = [settings, inbox, transfers] {
        inbox->setDestinationRoot(settings->destinationRoot());
        inbox->setGroupBySender(settings->groupBySender());
        inbox->setAutoAccept(settings->autoAccept());
        transfers->setKeepHistory(settings->keepHistory());
    };
    applySettings();
    QObject::connect(settings, &Settings::changed, inbox, applySettings);

    auto *service = new DBusService(tailnet, transfers, inbox, settings, &app);
    if (!service->registerService()) {
        out() << "another dripd already owns " << DBusService::serviceName() << "; exiting.\n";
        out().flush();
        return 1;
    }

    QObject::connect(inbox, &InboxWatcher::fileReceived, notifier, &Notifier::fileReceived);
    QObject::connect(inbox, &InboxWatcher::receiveFailed, notifier, &Notifier::receiveFailed);

    // The prompt exists in two places at once -- a notification and the panel --
    // because either may be the one the user is looking at. Both drive the same
    // accept/decline on the inbox, and whichever loses the race is a no-op.
    QObject::connect(inbox, &InboxWatcher::arrivalPending, notifier, [notifier](const PendingArrival &arrival) {
        notifier->arrivalPending(arrival.name, arrival.size, arrival.senderName);
    });
    QObject::connect(notifier, &Notifier::arrivalAccepted, inbox, &InboxWatcher::accept);
    QObject::connect(notifier, &Notifier::arrivalDeclined, inbox, &InboxWatcher::decline);
    QObject::connect(inbox, &InboxWatcher::arrivalResolved, notifier, &Notifier::arrivalResolved);

    QObject::connect(transfers, &TransferManager::transferUpdated, notifier, [notifier](const Transfer &transfer) {
        if (transfer.state == TransferState::Failed && transfer.direction == TransferDirection::Outgoing) {
            notifier->sendFailed(transfer.fileName, transfer.deviceName, transfer.error);
        }
    });

    // Keep the ledger honest when the user relocates a received file.
    QObject::connect(notifier, &Notifier::fileMoved, &app, [](const QString &from, const QString &to) {
        out() << "moved " << from << " -> " << to << "\n";
        out().flush();
    });

    QObject::connect(inbox, &InboxWatcher::fileReceived, &app, [](const QString &path, const QString &sender) {
        out() << "received " << path << " from " << sender << "\n";
        out().flush();
    });

    tailnet->start();
    inbox->start();

    return app.exec();
}

}

int main(int argc, char **argv)
{
    QStringList args;
    args.reserve(argc - 1);
    for (int i = 1; i < argc; ++i) {
        args += QString::fromLocal8Bit(argv[i]);
    }

    if (args.contains(QLatin1String("--help")) || args.contains(QLatin1String("-h"))) {
        std::fputs("dripd -- drip engine\n\n"
                   "  --probe                     print tailnet devices and inbox state, then exit\n"
                   "  --send <device> <file>...   send files and report progress, then exit\n"
                   "  --help                      this text\n\n"
                   "With no arguments, runs as the background daemon.\n",
                   stdout);
        return 0;
    }
    if (args.contains(QLatin1String("--probe"))) {
        return runProbe(argc, argv);
    }
    const int sendIndex = args.indexOf(QLatin1String("--send"));
    if (sendIndex >= 0) {
        return runSend(argc, argv, args.mid(sendIndex + 1));
    }
    return runDaemon(argc, argv);
}
