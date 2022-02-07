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
struct ReactionAnimationArgs;
} // namespace HistoryView

namespace HistoryView::Reactions {

class Animation final {
public:
	Animation(
		not_null<::Data::Reactions*> owner,
		ReactionAnimationArgs &&args,
		Fn<void()> repaint,
		int size);
	~Animation();

	void setRepaintCallback(Fn<void()> repaint);
	QRect paintGetArea(QPainter &p, QPoint origin, QRect target) const;

	[[nodiscard]] bool flying() const;
	[[nodiscard]] float64 flyingProgress() const;
	[[nodiscard]] bool finished() const;

private:
	void flyCallback();
	void startAnimations();
	void callback();
	int computeParabolicTop(int from, int to, float64 progress) const;

	const not_null<::Data::Reactions*> _owner;
	Fn<void()> _repaint;
	std::shared_ptr<Lottie::Icon> _flyIcon;
	std::unique_ptr<Lottie::Icon> _center;
	std::unique_ptr<Lottie::Icon> _effect;
	Ui::Animations::Simple _fly;
	QRect _flyFrom;
	bool _valid = false;

	mutable std::optional<int> _cachedKey;
	mutable float64 _cachedA = 0.;
	mutable float64 _cachedB = 0.;

};

} // namespace HistoryView::Reactions
