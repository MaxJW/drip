/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QString>

namespace drip
{

/** "1.4 MB" */
QString humanSize(qint64 bytes);

/** A path in @p directory for @p fileName, suffixed "(2)", "(3)"... if taken. */
QString uniquePath(const QString &directory, const QString &fileName);

/** Replaces the user's home directory with "~". */
QString abbreviateHome(const QString &path);

}
