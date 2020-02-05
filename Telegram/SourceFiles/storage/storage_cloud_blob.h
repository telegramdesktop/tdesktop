/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Storage {

struct Blob {
	int id = 0;
	int postId = 0;
	int size = 0;
	QString name;
};

bool UnpackBlob(
	const QString &path,
	const QString &folder,
	Fn<bool(const QString &)> checkNameCallback);

} // namespace Storage
