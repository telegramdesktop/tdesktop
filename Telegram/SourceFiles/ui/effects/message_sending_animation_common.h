/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {

struct MessageSendingAnimationFrom {
	enum class Type {
		None,
		Sticker,
		Gif,
		Emoji,
		Text,
	};
	struct FieldText {
		QImage frame;
		QRect globalGeometry;
		crl::time capturedAt = 0;
		bool singleLine = false;
		bool scrolled = false;
		bool rightToLeft = false;
	};
	Type type = Type::None;
	std::optional<MsgId> localId;
	QRect globalStartGeometry;
	QImage frame;
	bool crop = false;
	std::optional<FieldText> fieldText;
};

} // namespace Ui
