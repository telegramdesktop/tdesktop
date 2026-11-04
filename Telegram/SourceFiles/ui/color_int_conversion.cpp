/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/color_int_conversion.h"

namespace Ui {

QColor ColorFromSerialized(quint32 serialized) {
	return QColor(
		int((serialized >> 16) & 0xFFU),
		int((serialized >> 8) & 0xFFU),
		int(serialized & 0xFFU));
}

std::optional<QColor> MaybeColorFromSerialized(quint32 serialized) {
	return (serialized == quint32(-1))
		? std::nullopt
		: std::make_optional(ColorFromSerialized(serialized));
}

QColor Color32FromSerialized(quint32 serialized) {
	return QColor(
		int(serialized & 0xFFU),
		int((serialized >> 8) & 0xFFU),
		int((serialized >> 16) & 0xFFU),
		int((serialized >> 24) & 0xFFU));
}

quint32 Color32ToSerialized(QColor color) {
	return (quint32(std::clamp(color.alpha(), 0, 255)) << 24)
		| (quint32(std::clamp(color.blue(), 0, 255)) << 16)
		| (quint32(std::clamp(color.green(), 0, 255)) << 8)
		| quint32(std::clamp(color.red(), 0, 255));
}

} // namespace Ui
