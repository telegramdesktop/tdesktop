/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

class Painter;

namespace style {
struct MessageBar;
} // namespace style

namespace Ui {

struct MessageBarContent {
	int id = 0;
	QString title;
	TextWithEntities text;
	QImage preview;
};

class MessageBar final {
public:
	MessageBar(not_null<QWidget*> parent, const style::MessageBar &st);

	void set(MessageBarContent &&content);
	void set(rpl::producer<MessageBarContent> content);

	[[nodiscard]] not_null<Ui::RpWidget*> widget() {
		return &_widget;
	}

	void finishAnimating();

private:
	enum class BodyAnimation : char {
		Full,
		Text,
		None,
	};
	struct Animation {
		Ui::Animations::Simple bodyMoved;
		Ui::Animations::Simple imageShown;
		QPixmap bodyOrTextFrom;
		QPixmap bodyOrTextTo;
		QPixmap imageFrom;
		QPixmap imageTo;
		BodyAnimation bodyAnimation = BodyAnimation::None;
		RectPart movingTo = RectPart::None;
	};
	void setup();
	void paint(Painter &p);
	void tweenTo(MessageBarContent &&content);
	void updateFromContent(MessageBarContent &&content);
	[[nodiscard]] QPixmap prepareImage(const QImage &preview);

	[[nodiscard]] QRect imageRect() const;
	[[nodiscard]] QRect bodyRect(bool withImage) const;
	[[nodiscard]] QRect bodyRect() const;
	[[nodiscard]] QRect textRect() const;

	auto makeGrabGuard();
	[[nodiscard]] QPixmap grabBodyOrTextPart(BodyAnimation type);
	[[nodiscard]] QPixmap grabImagePart();
	[[nodiscard]] QPixmap grabBodyPart();
	[[nodiscard]] QPixmap grabTextPart();

	[[nodiscard]] static BodyAnimation DetectBodyAnimationType(
		Animation *currentAnimation,
		const MessageBarContent &currentContent,
		const MessageBarContent &nextContent);

	const style::MessageBar &_st;
	Ui::RpWidget _widget;
	MessageBarContent _content;
	rpl::lifetime _contentLifetime;
	Ui::Text::String _title, _text;
	QPixmap _image;
	std::unique_ptr<Animation> _animation;

};

} // namespace Ui
