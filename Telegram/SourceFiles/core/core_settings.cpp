/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/core_settings.h"

#include "storage/serialize_common.h"

namespace Core {

Settings::Variables::Variables() {
}

QByteArray Settings::serialize() const {
	const auto themesAccentColors = _variables.themesAccentColors.serialize();
	auto size = Serialize::bytearraySize(themesAccentColors);

	auto result = QByteArray();
	result.reserve(size);
	{
		QDataStream stream(&result, QIODevice::WriteOnly);
		stream.setVersion(QDataStream::Qt_5_1);
		stream << themesAccentColors;
	}
	return result;
}

void Settings::constructFromSerialized(const QByteArray &serialized) {
	if (serialized.isEmpty()) {
		return;
	}

	QDataStream stream(serialized);
	stream.setVersion(QDataStream::Qt_5_1);
	QByteArray themesAccentColors;

	stream >> themesAccentColors;
	if (stream.status() != QDataStream::Ok) {
		LOG(("App Error: "
			"Bad data for Core::Settings::constructFromSerialized()"));
		return;
	}
	if (!_variables.themesAccentColors.setFromSerialized(themesAccentColors)) {
		return;
	}
}

} // namespace Core
