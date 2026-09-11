/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/scene/scene_item_video.h"

#include "editor/editor_audio_menu.h"
#include "editor/video/video_segment_player.h"
#include "ui/image/image_prepare.h"
#include "ui/rect.h"

#include <QtCore/QFile>

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

ItemVideo::ItemVideo(std::shared_ptr<Source> source, ItemBase::Data data)
: ItemAnimated(std::move(data))
, _source(std::move(source))
, _frameSize(FrameSizeFor(_source->thumbnail))
, _player(std::make_unique<SegmentPlayer>(
	_source->path,
	_source->content))
, _image(_source->thumbnail) {
	if (flipped()) {
		performFlip();
	}
	setAspectRatio(_image.isNull()
		? 1.
		: (_image.height() / float64(_image.width())));
	_player->repaints(
	) | rpl::on_next([=] {
		update();
	}, _lifetime);
	_player->start();
}

ItemVideo::~ItemVideo() = default;

const ItemVideo::Source &ItemVideo::source() const {
	return *_source;
}

crl::time ItemVideo::duration() const {
	return _source->duration;
}

not_null<SegmentPlayer*> ItemVideo::player() const {
	return _player.get();
}

ItemAnimated::Trim ItemVideo::trim() const {
	return _trim;
}

void ItemVideo::setTrim(Trim trim) {
	const auto full = duration();
	if (full > 0) {
		trim.from = std::clamp(trim.from, crl::time(0), full);
		trim.till = (trim.till > 0)
			? std::clamp(trim.till, trim.from, full)
			: 0;
	}
	if (_trim == trim) {
		return;
	}
	_trim = trim;
	_player->setSegment(_trim.from, _trim.till);
}

bool ItemVideo::hasAudio() const {
	return _source->hasAudio;
}

float64 ItemVideo::volume() const {
	return _volume;
}

void ItemVideo::setVolume(float64 volume) {
	_volume = std::clamp(volume, 0., 1.);
	_player->setVolume(_volume);
}

void ItemVideo::setSoundEnabled(bool enabled) {
	_player->setSound(enabled && hasAudio());
}

bool ItemVideo::sounding() const {
	return hasAudio() && (_volume > 0.);
}

bool ItemVideo::animated() const {
	return _player->valid() || _releasedAnimation;
}

bool ItemVideo::hasContent() const {
	return !_source->path.isEmpty() || !_source->content.isEmpty();
}

QByteArray ItemVideo::content() const {
	if (_source->path.isEmpty()) {
		return _source->content;
	}
	auto file = QFile(_source->path);
	if (file.size() > Images::kReadBytesLimit
		|| !file.open(QIODevice::ReadOnly)) {
		return QByteArray();
	}
	return file.readAll();
}

crl::time ItemVideo::loopDuration() const {
	const auto full = duration();
	if (full <= 0) {
		return 0;
	}
	const auto till = (_trim.till > _trim.from) ? _trim.till : full;
	return till - _trim.from;
}

void ItemVideo::releasePlayers() {
	if (!animated()) {
		return;
	}
	_releasedAnimation = true;
	_pendingRecreate = true;
	_player->stop();
}

void ItemVideo::setStatus(Status status) {
	if (status != Status::Normal) {
		releasePlayers();
	}
	ItemBase::setStatus(status);
}

void ItemVideo::save(SaveState state) {
	ItemBase::save(state);
	auto &saved = (state == SaveState::Keep) ? _kept : _saved;
	saved = { .trim = _trim, .volume = _volume };
}

void ItemVideo::restore(SaveState state) {
	if (!hasState(state)) {
		return;
	}
	ItemBase::restore(state);
	const auto &saved = (state == SaveState::Keep) ? _kept : _saved;
	setTrim(saved.trim);
	setVolume(saved.volume);
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
	if (_pendingRecreate && w) {
		_pendingRecreate = false;
		_player->start();
	}
	const auto frame = _player->frame(_frameSize);
	if (frame.isNull()) {
		paintFrame(p, _image, false, false);
	} else {
		paintFrame(p, frame, true, flipped());
	}
	ItemBase::paint(p, option, w);
}

void ItemVideo::fillContextMenu(not_null<Ui::PopupMenu*> menu) {
	if (hasAudio()) {
		AddVolumeAction(menu, _volume, [=](float64 volume) {
			setVolume(volume);
		});
	}
}

void ItemVideo::performFlip() {
	_image = _image.transformed(QTransform().scale(-1, 1));
	update();
}

std::shared_ptr<ItemBase> ItemVideo::duplicate(ItemBase::Data data) const {
	auto result = std::make_shared<ItemVideo>(_source, std::move(data));
	result->setTrim(_trim);
	result->setVolume(_volume);
	return result;
}

} // namespace Editor
