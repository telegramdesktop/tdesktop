/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/effects/animations.h"

class Painter;

namespace style {
struct MessageBar;
} // namespace style

namespace Ui {

struct MessageBarContent {
	int index = 0;
	int count = 1;
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
		Ui::Animations::Simple barScroll;
		Ui::Animations::Simple barTop;
		QPixmap bodyOrTextFrom;
		QPixmap bodyOrTextTo;
		QPixmap titleSame;
		QPixmap titleFrom;
		QPixmap titleTo;
		QPixmap imageFrom;
		QPixmap imageTo;
		BodyAnimation bodyAnimation = BodyAnimation::None;
		RectPart movingTo = RectPart::None;
	};
	struct BarState {
		float64 scroll = 0.;
		float64 size = 0.;
		float64 skip = 0.;
		float64 offset = 0.;
	};
	void setup();
	void paint(Painter &p);
	void paintLeftBar(Painter &p);
	void tweenTo(MessageBarContent &&content);
	void updateFromContent(MessageBarContent &&content);
	[[nodiscard]] QPixmap prepareImage(const QImage &preview);

	[[nodiscard]] QRect imageRect() const;
	[[nodiscard]] QRect titleRangeRect(int from, int till) const;
	[[nodiscard]] QRect bodyRect(bool withImage) const;
	[[nodiscard]] QRect bodyRect() const;
	[[nodiscard]] QRect textRect() const;

	auto makeGrabGuard();
	[[nodiscard]] QPixmap grabBodyOrTextPart(BodyAnimation type);
	[[nodiscard]] QPixmap grabTitleBase(int till);
	[[nodiscard]] QPixmap grabTitlePart(int from);
	[[nodiscard]] QPixmap grabTitleRange(int from, int till);
	[[nodiscard]] QPixmap grabImagePart();
	[[nodiscard]] QPixmap grabBodyPart();
	[[nodiscard]] QPixmap grabTextPart();

	[[nodiscard]] BarState countBarState(int index) const;
	[[nodiscard]] BarState countBarState() const;
	void ensureGradientsCreated(int size);

	[[nodiscard]] static BodyAnimation DetectBodyAnimationType(
		Animation *currentAnimation,
		const MessageBarContent &currentContent,
		const MessageBarContent &nextContent);

	const style::MessageBar &_st;
	Ui::RpWidget _widget;
	MessageBarContent _content;
	rpl::lifetime _contentLifetime;
	Ui::Text::String _title, _text;
	QPixmap _image, _topBarGradient, _bottomBarGradient;
	std::unique_ptr<Animation> _animation;

};

} // namespace Ui
