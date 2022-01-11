/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"

namespace Lottie {
class Icon;
} // namespace Lottie

namespace Data {
class Reactions;
} // namespace Data

namespace HistoryView {
struct SendReactionAnimationArgs;
} // namespace HistoryView

namespace HistoryView::Reactions {

class SendAnimation final {
public:
	SendAnimation(
		not_null<::Data::Reactions*> owner,
		SendReactionAnimationArgs &&args,
		Fn<void()> repaint,
		int size);
	~SendAnimation();

	void setRepaintCallback(Fn<void()> repaint);
	void paint(QPainter &p, QPoint origin, QRect target) const;

	[[nodiscard]] QString playingAroundEmoji() const;
	[[nodiscard]] bool flying() const;

private:
	void flyCallback();
	void startAnimations();
	void callback();

	const not_null<::Data::Reactions*> _owner;
	const QString _emoji;
	Fn<void()> _repaint;
	std::shared_ptr<Lottie::Icon> _flyIcon;
	std::unique_ptr<Lottie::Icon> _center;
	std::unique_ptr<Lottie::Icon> _effect;
	Ui::Animations::Simple _fly;
	QRect _flyFrom;
	bool _valid = false;

};

} // namespace HistoryView::Reactions
