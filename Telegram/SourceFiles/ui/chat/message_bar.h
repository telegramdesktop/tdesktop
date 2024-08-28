/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "ui/rect_part.h"
#include "ui/rp_widget.h"

class Painter;

namespace style {
struct MessageBar;
} // namespace style

namespace Ui {

class SpoilerAnimation;

struct MessageBarContent {
	int index = 0;
	int count = 1;
	QString title;
	TextWithEntities text;
	std::any context;
	QImage preview;
	Fn<void()> spoilerRepaint;
	style::margins margins;
};

class MessageBar final {
public:
	MessageBar(
		not_null<QWidget*> parent,
		const style::MessageBar &st,
		Fn<bool()> customEmojiPaused);

	void set(MessageBarContent &&content);
	void set(rpl::producer<MessageBarContent> content);

	[[nodiscard]] not_null<RpWidget*> widget() {
		return &_widget;
	}

	void customEmojiRepaint();
	void finishAnimating();

private:
	enum class BodyAnimation : char {
		Full,
		Text,
		None,
	};
	struct Animation {
		Animations::Simple bodyMoved;
		Animations::Simple imageShown;
		Animations::Simple barScroll;
		Animations::Simple barTop;
		QPixmap bodyOrTextFrom;
		QPixmap bodyOrTextTo;
		QPixmap titleSame;
		QPixmap titleFrom;
		QPixmap titleTo;
		QPixmap imageFrom;
		QPixmap imageTo;
		std::unique_ptr<SpoilerAnimation> spoilerFrom;
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

	void paintImageWithSpoiler(
		QPainter &p,
		QRect rect,
		const QPixmap &image,
		SpoilerAnimation *spoiler,
		crl::time now,
		bool paused) const;

	[[nodiscard]] static BodyAnimation DetectBodyAnimationType(
		Animation *currentAnimation,
		const MessageBarContent &currentContent,
		const MessageBarContent &nextContent);

	const style::MessageBar &_st;
	RpWidget _widget;
	Fn<bool()> _customEmojiPaused;
	MessageBarContent _content;
	rpl::lifetime _contentLifetime;
	Text::String _title, _text;
	QPixmap _image, _topBarGradient, _bottomBarGradient;
	std::unique_ptr<Animation> _animation;
	std::unique_ptr<SpoilerAnimation> _spoiler;
	bool _customEmojiRepaintScheduled = false;

};

} // namespace Ui
