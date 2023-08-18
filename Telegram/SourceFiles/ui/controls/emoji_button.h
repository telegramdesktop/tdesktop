/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/buttons.h"

namespace style {
struct EmojiButton;
} // namespace style

namespace Ui {

class InfiniteRadialAnimation;

class EmojiButton final : public RippleButton {
public:
	EmojiButton(QWidget *parent, const style::EmojiButton &st);

	void setLoading(bool loading);
	void setColorOverrides(
		const style::icon *iconOverride,
		const style::color *colorOverride,
		const style::color *rippleOverride);

protected:
	void paintEvent(QPaintEvent *e) override;
	void onStateChanged(State was, StateChangeSource source) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	void loadingAnimationCallback();

	const style::EmojiButton &_st;

	std::unique_ptr<Ui::InfiniteRadialAnimation> _loading;

	const style::icon *_iconOverride = nullptr;
	const style::color *_colorOverride = nullptr;
	const style::color *_rippleOverride = nullptr;

};

} // namespace Ui
