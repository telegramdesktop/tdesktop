/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/file_utilities.h"

namespace Platform {
namespace FileDialog {
namespace XDP {

using Type = ::FileDialog::internal::Type;

bool Use(Type type = Type::ReadFile);
bool Get(
	QPointer<QWidget> parent,
	QStringList &files,
	QByteArray &remoteContent,
	const QString &caption,
	const QString &filter,
	Type type,
	QString startFile);

} // namespace XDP
} // namespace FileDialog
} // namespace Platform
