/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_react_animation.h"

#include "history/view/history_view_element.h"
#include "lottie/lottie_icon.h"
#include "data/data_message_reactions.h"
#include "data/data_document.h"
#include "data/data_document_media.h"

namespace HistoryView::Reactions {
namespace {

constexpr auto kFlyDuration = crl::time(200);

} // namespace

SendAnimation::SendAnimation(
	not_null<::Data::Reactions*> owner,
	SendReactionAnimationArgs &&args,
	Fn<void()> repaint,
	int size)
: _owner(owner)
, _emoji(args.emoji)
, _repaint(std::move(repaint))
, _flyFrom(args.flyFrom) {
	const auto &list = owner->list(::Data::Reactions::Type::All);
	const auto i = ranges::find(list, _emoji, &::Data::Reaction::emoji);
	if (i == end(list) || !i->centerIcon) {
		return;
	}
	const auto resolve = [&](
			std::unique_ptr<Lottie::Icon> &icon,
			DocumentData *document,
			int size) {
		if (!document) {
			return false;
		}
		const auto media = document->activeMediaView();
		if (!media || !media->loaded()) {
			return false;
		}
		icon = std::make_unique<Lottie::Icon>(Lottie::IconDescriptor{
			.path = document->filepath(true),
			.json = media->bytes(),
			.sizeOverride = QSize(size, size),
		});
		return true;
	};
	_flyIcon = std::move(args.flyIcon);
	if (!resolve(_center, i->centerIcon, size)
		|| !resolve(_effect, i->aroundAnimation, size * 2)) {
		return;
	}
	if (_flyIcon) {
		_fly.start([=] { flyCallback(); }, 0., 1., kFlyDuration);
	} else {
		startAnimations();
	}
	_valid = true;
}

SendAnimation::~SendAnimation() = default;

QRect SendAnimation::paintGetArea(QPainter &p, QPoint origin, QRect target) const {
	if (!_flyIcon) {
		p.drawImage(target, _center->frame());
		const auto wide = QRect(
			target.topLeft() - QPoint(target.width(), target.height()) / 2,
			target.size() * 2);
		p.drawImage(wide, _effect->frame());
		return wide;
	}
	const auto from = _flyFrom.translated(origin);
	const auto lshift = target.width() / 4;
	const auto rshift = target.width() / 2 - lshift;
	const auto margins = QMargins{ lshift, lshift, rshift, rshift };
	target = target.marginsRemoved(margins);
	const auto progress = _fly.value(1.);
	const auto rect = QRect(
		anim::interpolate(from.x(), target.x(), progress),
		anim::interpolate(from.y(), target.y(), progress),
		anim::interpolate(from.width(), target.width(), progress),
		anim::interpolate(from.height(), target.height(), progress));
	const auto wide = rect.marginsAdded(margins);
	auto hq = PainterHighQualityEnabler(p);
	if (progress < 1.) {
		p.setOpacity(1. - progress);
		p.drawImage(rect, _flyIcon->frame());
	}
	if (progress > 0.) {
		p.setOpacity(progress);
		p.drawImage(wide, _center->frame());
	}
	p.setOpacity(1.);
	return wide;
}

void SendAnimation::startAnimations() {
	_center->animate([=] { callback(); }, 0, _center->framesCount() - 1);
	_effect->animate([=] { callback(); }, 0, _effect->framesCount() - 1);
}

void SendAnimation::flyCallback() {
	if (!_fly.animating()) {
		_flyIcon = nullptr;
		startAnimations();
	}
	callback();
}

void SendAnimation::callback() {
	if (_repaint) {
		_repaint();
	}
}

void SendAnimation::setRepaintCallback(Fn<void()> repaint) {
	_repaint = std::move(repaint);
}

bool SendAnimation::flying() const {
	return (_flyIcon != nullptr);
}

QString SendAnimation::playingAroundEmoji() const {
	const auto finished = !_valid
		|| (!_flyIcon && !_center->animating() && !_effect->animating());
	return finished ? QString() : _emoji;
}

} // namespace HistoryView::Reactions
