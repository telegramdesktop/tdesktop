/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/scene/scene_item_video.h"

#include "editor/editor_audio_menu.h"
#include "ui/rect.h"

namespace Editor {
namespace {

constexpr auto kPlaybackFrameSide = 512;

[[nodiscard]] QSize FrameSizeFor(const QImage &thumbnail) {
	const auto size = thumbnail.size();
	if (size.isEmpty()) {
		return Size(kPlaybackFrameSide);
	} else if (size.width() <= kPlaybackFrameSide
		&& size.height() <= kPlaybackFrameSide) {
		return size;
	}
	return size.scaled(Size(kPlaybackFrameSide), Qt::KeepAspectRatio);
}

} // namespace

ItemVideo::ItemVideo(
	std::shared_ptr<VideoClipSource> source,
	ItemBase::Data data)
: ItemAnimated(std::move(data))
, _clip(std::make_unique<VideoClip>(std::move(source), [=] { update(); }))
, _frameSize(FrameSizeFor(_clip->source()->thumbnail))
, _image(_clip->source()->thumbnail) {
	if (flipped()) {
		performFlip();
	}
	setAspectRatio(_image.isNull()
		? 1.
		: (_image.height() / float64(_image.width())));
}

ItemVideo::~ItemVideo() = default;

VideoClip *ItemVideo::videoClip() {
	return _clip.get();
}

VideoTrim ItemVideo::trim() const {
	return _clip->trim();
}

bool ItemVideo::animated() const {
	return _clip->animated();
}

bool ItemVideo::hasContent() const {
	return _clip->hasContent();
}

QByteArray ItemVideo::content() const {
	return _clip->content();
}

crl::time ItemVideo::loopDuration() const {
	return _clip->loopDuration();
}

void ItemVideo::releasePlayers() {
	_clip->stop();
}

void ItemVideo::setStatus(Status status) {
	if (status != Status::Normal) {
		releasePlayers();
	}
	ItemBase::setStatus(status);
}

void ItemVideo::save(SaveState state) {
	ItemBase::save(state);
	((state == SaveState::Keep) ? _kept : _saved) = _clip->state();
}

void ItemVideo::restore(SaveState state) {
	if (!hasState(state)) {
		return;
	}
	ItemBase::restore(state);
	_clip->restore((state == SaveState::Keep) ? _kept : _saved);
}

int ItemVideo::type() const {
	return Type;
}

Media::Encode::AnimatedEntity::Kind ItemVideo::entityKind() const {
	return Media::Encode::AnimatedEntity::Kind::Webm;
}

void ItemVideo::paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *option,
		QWidget *w) {
	if (w) {
		_clip->resume();
	}
	const auto frame = _clip->frame(_frameSize);
	if (frame.isNull()) {
		paintFrame(p, _image, false, false);
	} else {
		paintFrame(p, frame, true, flipped());
	}
	ItemBase::paint(p, option, w);
}

void ItemVideo::fillContextMenu(not_null<Ui::PopupMenu*> menu) {
	if (_clip->hasAudio()) {
		AddVolumeAction(menu, _clip->volume(), [=](float64 volume) {
			_clip->setVolume(volume);
		});
	}
}

void ItemVideo::performFlip() {
	_image = _image.transformed(QTransform().scale(-1, 1));
	update();
}

std::shared_ptr<ItemBase> ItemVideo::duplicate(ItemBase::Data data) const {
	auto result = std::make_shared<ItemVideo>(
		_clip->source(),
		std::move(data));
	result->_clip->restore(_clip->state());
	return result;
}

} // namespace Editor
