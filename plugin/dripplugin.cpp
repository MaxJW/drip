/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Hand-written plugin entry point: the generated one cannot register an image
 * provider, and avatars need one.
 */

#include "avatarprovider.h"

#include <QQmlEngine>
#include <QQmlEngineExtensionPlugin>

class DripPlugin : public QQmlEngineExtensionPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QQmlEngineExtensionInterface_iid)

public:
    void initializeEngine(QQmlEngine *engine, const char *uri) override
    {
        Q_UNUSED(uri)
        engine->addImageProvider(QStringLiteral("dripavatar"), new AvatarProvider);
    }
};

#include "dripplugin.moc"
