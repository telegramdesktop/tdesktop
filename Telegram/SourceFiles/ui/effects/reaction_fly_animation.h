/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "data/data_message_reaction_id.h"

namespace Ui::Text {
class CustomEmoji;
} // namespace Ui::Text

namespace Data {
class Reactions;
enum class CustomEmojiSizeTag : uchar;
} // namespace Data

namespace Ui {

class AnimatedIcon;

struct ReactionFlyAnimationArgs {
	::Data::ReactionId id;
	QImage flyIcon;
	QRect flyFrom;
	crl::time scaleOutDuration = 0;
	float64 scaleOutTarget = 0.;
	float64 miniCopyMultiplier = 1.;
	bool effectOnly = false;
	bool forceFirstFrame = false;

	[[nodiscard]] ReactionFlyAnimationArgs translated(QPoint point) const;
};

struct ReactionFlyCenter {
	std::unique_ptr<Text::CustomEmoji> custom;
	std::unique_ptr<AnimatedIcon> icon;
	float64 scale = 0.;
	float64 centerSizeMultiplier = 0.;
	int customSize = 0;
	int size = 0;
	bool forceFirstFrame = false;
};

class ReactionFlyAnimation final {
public:
	ReactionFlyAnimation(
		not_null<::Data::Reactions*> owner,
		ReactionFlyAnimationArgs &&args,
		Fn<void()> repaint,
		int size,
		Data::CustomEmojiSizeTag customSizeTag = {});
	~ReactionFlyAnimation();

	void setRepaintCallback(Fn<void()> repaint);
	QRect paintGetArea(
		QPainter &p,
		QPoint origin,
		QRect target,
		const QColor &colored,
		QRect clip,
		crl::time now) const;

	[[nodiscard]] bool flying() const;
	[[nodiscard]] float64 flyingProgress() const;
	[[nodiscard]] bool finished() const;

	[[nodiscard]] ReactionFlyCenter takeCenter();

private:
	struct Parabolic {
		float64 a = 0.;
		float64 b = 0.;
		std::optional<int> key;
	};
	struct MiniCopy {
		mutable Parabolic cached;
		float64 maxScale = 1.;
		float64 duration = 1.;
		int flyUp = 0;
		int finalX = 0;
		int finalY = 0;
	};

	[[nodiscard]] auto flyCallback();
	[[nodiscard]] auto callback();
	void startAnimations();
	int computeParabolicTop(
		Parabolic &cache,
		int from,
		int to,
		int top,
		float64 progress) const;
	void paintCenterFrame(
		QPainter &p,
		QRect target,
		const QColor &colored,
		crl::time now) const;
	void paintMiniCopies(
		QPainter &p,
		QPoint center,
		const QColor &colored,
		crl::time now) const;
	void generateMiniCopies(int size, float64 miniCopyMultiplier);

	const not_null<::Data::Reactions*> _owner;
	Fn<void()> _repaint;
	QImage _flyIcon;
	std::unique_ptr<Text::CustomEmoji> _custom;
	std::unique_ptr<AnimatedIcon> _center;
	std::unique_ptr<AnimatedIcon> _effect;
	Animations::Simple _noEffectScaleAnimation;
	std::vector<MiniCopy> _miniCopies;
	Animations::Simple _fly;
	Animations::Simple _minis;
	QRect _flyFrom;
	float64 _centerSizeMultiplier = 0.;
	int _customSize = 0;
	crl::time _scaleOutDuration = 0;
	float64 _scaleOutTarget = 0.;
	bool _noEffectScaleStarted = false;
	bool _forceFirstFrame = false;
	bool _effectOnly = false;
	bool _valid = false;

	mutable Parabolic _cached;

};

} // namespace Ui
