/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "base/optional.h"

namespace Storage {
namespace Cache {

using Version = int32;

QString ComputeBasePath(const QString &original);
QString VersionFilePath(const QString &base);
base::optional<Version> ReadVersionValue(const QString &base);
bool WriteVersionValue(const QString &base, Version value);

struct Error {
	enum class Type {
		None,
		IO,
		WrongKey,
		LockFailed,
	};
	Type type = Type::None;
	QString path;

	static Error NoError();
};

inline Error Error::NoError() {
	return Error();
}

} // namespace Cache
} // namespace Storage
