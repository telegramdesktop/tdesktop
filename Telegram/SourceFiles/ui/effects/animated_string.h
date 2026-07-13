/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "ui/style/style_core.h"

namespace Ui {

class AnimatedString final {
public:
	struct Options {
		bool splitByWords = false;
		bool preserveIndex = false;
		bool startFromEnd = false;
		bool enforceByLetter = false;
		bool moveDown = true;
		float64 moveAmplitude = 0.3;
		crl::time duration = 320;
		anim::transition transition = anim::easeOutQuint;
	};

	AnimatedString(
		const style::font &font,
		Fn<void()> update);
	AnimatedString(
		const style::font &font,
		Fn<void()> update,
		Options options);

	void setText(const QString &text, bool animated = true);
	[[nodiscard]] const QString &text() const;

	void draw(
		QPainter &p,
		int x,
		int y,
		const QColor &color,
		float64 opacity = 1.) const;

	[[nodiscard]] float64 currentWidth() const;
	[[nodiscard]] float64 width() const;
	[[nodiscard]] int height() const;
	[[nodiscard]] bool animating() const;
	void finishAnimating();

private:
	struct Part {
		QString text;
		float64 offset = 0.;
		float64 width = 0.;
		int opposite = -1;
	};

	void setInstant(const QString &text);
	void realSetText(const QString &text);
	void animationCallback();
	void drawPart(
		QPainter &p,
		float64 x,
		float64 baseline,
		const QString &text,
		float64 opacity) const;

	const style::font &_font;
	Fn<void()> _update;
	Options _options;

	QString _currentText;
	QString _oldText;
	std::vector<Part> _currentParts;
	std::vector<Part> _oldParts;
	float64 _currentWidth = 0.;
	float64 _oldWidth = 0.;

	std::optional<QString> _scheduled;
	bool _started = false;

	Ui::Animations::Simple _animation;

};

} // namespace Ui
