/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {

void WriteLogEntry(const QString &message);

} // namespace Ui

#define UI_LOG(message) (::Ui::WriteLogEntry(QString message))
