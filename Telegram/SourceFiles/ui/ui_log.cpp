/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/ui_log.h"

#include "ui/ui_integration.h"

namespace Ui {

void WriteLogEntry(const QString &message) {
	Integration::Instance().writeLogEntry(message);
}

} // namespace Ui
