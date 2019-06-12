/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/text/text_utilities.h"

namespace Ui {
namespace Text {

TextWithEntities Bold(const QString &text) {
	auto result = TextWithEntities{ text };
	result.entities.push_back({ EntityType::Bold, 0, text.size() });
	return result;
}

} // namespace Text
} // namespace Ui
