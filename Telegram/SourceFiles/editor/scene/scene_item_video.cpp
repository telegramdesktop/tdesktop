/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/scene/scene_item_video.h"

#include "editor/editor_audio_menu.h"
#include "lang/lang_keys.h"
#include "ui/rect.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_menu_icons.h"

#include <QtGui/QPainterPath>

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

void ItemVideo::keepInitialState() {
	_initialPlacement = placement();
	_initialState = _clip->state();
	_hasInitial = true;
}

bool ItemVideo::atInitialState() const {
	if (!_hasInitial || (_initialPlacement != placement())) {
		return false;
	}
	const auto current = _clip->state();
	if (std::abs(_initialState.volume - current.volume) > 0.01) {
		return false;
	}
	const auto duration = _clip->duration();
	const auto isUntrimmed = [&](const VideoTrim &trim) {
		return (trim.from == 0)
			&& ((trim.till == 0) || (duration > 0 && trim.till >= duration));
	};
	if (isUntrimmed(_initialState.trim) && isUntrimmed(current.trim)) {
		return true;
	}
	return (_initialState.trim == current.trim);
}

void ItemVideo::setBackground(bool background) {
	_isBackground = background;
}

bool ItemVideo::isBackground() const {
	return _isBackground;
}

void ItemVideo::setBackgroundLocked(bool locked) {
	if (_isLocked != locked) {
		_isLocked = locked;
		update();
	}
}

bool ItemVideo::isBackgroundLocked() const {
	return _isBackground && _isLocked;
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

bool ItemVideo::roundVideo() const {
	return _clip->source()->round;
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

QString ItemVideo::entityPath() const {
	return _clip->source()->path;
}

void ItemVideo::paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *option,
		QWidget *w) {
	if (w) {
		_clip->resume();
	}
	const auto frame = _clip->frame(_frameSize);
	if (roundVideo()) {
		p->save();
		auto clip = QPainterPath();
		clip.addEllipse(contentRect());
		p->setClipPath(clip, Qt::IntersectClip);
	}
	if (frame.isNull()) {
		paintFrame(p, _image, false, false);
	} else {
		paintFrame(p, frame, true, flipped());
	}
	if (roundVideo()) {
		p->restore();
	}
	ItemBase::paint(p, option, w);
}

void ItemVideo::fillContextMenu(not_null<Ui::PopupMenu*> menu) {
	if (_isBackground) {
		const auto locked = _isLocked;
		menu->addAction(
			locked
				? tr::lng_story_video_unlock(tr::now)
				: tr::lng_story_video_lock(tr::now),
			[=] { setBackgroundLocked(!locked); },
			&st::mediaMenuIconPrivacy);
	}
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
