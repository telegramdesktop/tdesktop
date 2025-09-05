/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/text/text_lottie_custom_emoji.h"

#include "lottie/lottie_icon.h"
#include "ui/text/text_utilities.h"

namespace Ui::Text {

LottieCustomEmoji::LottieCustomEmoji(Lottie::IconDescriptor &&descriptor)
: LottieCustomEmoji(std::move(descriptor), nullptr) {
}

LottieCustomEmoji::LottieCustomEmoji(
		Lottie::IconDescriptor &&descriptor,
		Fn<void()> repaint)
: _entityData(!descriptor.name.isEmpty()
	? descriptor.name
	: !descriptor.path.isEmpty()
	? descriptor.path
	: (Unexpected("LottieCustomEmoji: descriptor.name or descriptor.path"),
		QString()))
, _width(descriptor.sizeOverride.width())
, _icon(Lottie::MakeIcon(std::move(descriptor)))
, _repaint(std::move(repaint)) {
	if (!_width && _icon && _icon->valid()) {
		_width = _icon->width();
		startAnimation();
	}
}

int LottieCustomEmoji::width() {
	return _width;
}

QString LottieCustomEmoji::entityData() {
	return _entityData;
}

void LottieCustomEmoji::paint(QPainter &p, const Context &context) {
	if (!_icon || !_icon->valid()) {
		return;
	}

	const auto paused = context.paused
		|| context.internal.forceFirstFrame
		|| context.internal.overrideFirstWithLastFrame;

	if (paused) {
		const auto frame = context.internal.forceLastFrame
			? _icon->framesCount() - 1
			: 0;
		_icon->jumpTo(frame, _repaint);
	} else if (!_icon->animating()) {
		startAnimation();
	}

	_icon->paint(
		p,
		context.position.x(),
		context.position.y(),
		context.textColor);
}

void LottieCustomEmoji::unload() {
	if (_icon) {
		_icon->jumpTo(0, nullptr);
	}
}

bool LottieCustomEmoji::ready() {
	return _icon && _icon->valid();
}

bool LottieCustomEmoji::readyInDefaultState() {
	return _icon && _icon->valid() && _icon->frameIndex() == 0;
}

void LottieCustomEmoji::startAnimation() {
	if (!_icon || !_icon->valid() || _icon->framesCount() <= 1) {
		return;
	}

	_icon->animate(
		_repaint,
		0,
		_icon->framesCount() - 1);
}

QString LottieEmojiData(Lottie::IconDescriptor descriptor) {
	return !descriptor.name.isEmpty()
		? descriptor.name
		: !descriptor.path.isEmpty()
		? descriptor.path
		: QString();
}

TextWithEntities LottieEmoji(Lottie::IconDescriptor descriptor) {
	return SingleCustomEmoji(LottieEmojiData(std::move(descriptor)));
}

MarkedContext LottieEmojiContext(Lottie::IconDescriptor descriptor) {
	auto customEmojiFactory = [descriptor = std::move(descriptor)](
			QStringView data,
			const MarkedContext &context
		) mutable -> std::unique_ptr<CustomEmoji> {
		if (data == LottieEmojiData(descriptor)) {
			return std::make_unique<LottieCustomEmoji>(
				std::move(descriptor),
				context.repaint);
		}
		return nullptr;
	};
	return { .customEmojiFactory = std::move(customEmojiFactory) };
}

} // namespace Ui::Text