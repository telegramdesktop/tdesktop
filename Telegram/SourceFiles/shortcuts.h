/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Shortcuts {

void start();
const QStringList &errors();

bool launch(int shortcutId);
bool launch(const QString &command);

// Media shortcuts are not enabled by default, because other
// applications also use them. They are enabled only when
// the in-app player is active and disabled back after.
void enableMediaShortcuts();
void disableMediaShortcuts();

void finish();

} // namespace Shortcuts
