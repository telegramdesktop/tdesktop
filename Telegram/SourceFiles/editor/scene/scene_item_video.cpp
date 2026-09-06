/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/scene/scene_item_video.h"

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
, _image(_source->thumbnail) {
	if (flipped()) {
		performFlip();
	}
	setAspectRatio(_image.isNull()
		? 1.
		: (_image.height() / float64(_image.width())));
	createPlayer();
}

void ItemVideo::createPlayer() {
	if (!hasContent()) {
		return;
	}
	const auto callback = [=](::Media::Clip::Notification value) {
		clipCallback(value);
	};
	_reader = _source->path.isEmpty()
		? ::Media::Clip::MakeReader(_source->content, callback)
		: ::Media::Clip::MakeReader(_source->path, callback);
}

void ItemVideo::clipCallback(::Media::Clip::Notification notification) {
	using namespace ::Media::Clip;
	if (notification == Notification::Reinit) {
		if (_reader && _reader->state() == State::Error) {
			_reader.setBad();
		} else if (_reader && _reader->ready() && !_reader->started()) {
			_reader->start({ .frame = _frameSize });
		}
	}
	update();
}

bool ItemVideo::animated() const {
	return _reader.valid() || _releasedAnimation;
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
	return _source->duration;
}

void ItemVideo::releasePlayers() {
	if (!animated()) {
		return;
	}
	_releasedAnimation = true;
	_pendingRecreate = true;
	_reader.reset();
}

void ItemVideo::setStatus(Status status) {
	if (status != Status::Normal) {
		releasePlayers();
	}
	ItemBase::setStatus(status);
}

int ItemVideo::type() const {
	return Type;
}

Media::Encode::AnimatedEntity::Kind ItemVideo::entityKind() const {
	return Media::Encode::AnimatedEntity::Kind::Webm;
}

QImage ItemVideo::currentFrame() {
	if (_reader && _reader->started()) {
		auto result = _reader->current({ .frame = _frameSize }, crl::now());
		if (!result.isNull()) {
			return result;
		}
	}
	return _image;
}

void ItemVideo::paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *option,
		QWidget *w) {
	if (_pendingRecreate && w) {
		_pendingRecreate = false;
		createPlayer();
	}
	const auto live = _reader && _reader->started();
	paintFrame(p, currentFrame(), live, live && flipped());
	ItemBase::paint(p, option, w);
}

void ItemVideo::performFlip() {
	_image = _image.transformed(QTransform().scale(-1, 1));
	update();
}

std::shared_ptr<ItemBase> ItemVideo::duplicate(ItemBase::Data data) const {
	return std::make_shared<ItemVideo>(_source, std::move(data));
}

} // namespace Editor
