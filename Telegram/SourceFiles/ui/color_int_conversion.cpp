/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
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

} // namespace Ui
