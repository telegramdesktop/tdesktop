/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "editor/scene/scene_item_animated.h"
#include "media/clip/media_clip_reader.h"

namespace Editor {

class ItemVideo final : public ItemAnimated {
public:
	enum { Type = ItemBase::Type + 4 };

	struct Source {
		QString path;
		QByteArray content;
		QImage thumbnail;
		crl::time duration = 0;
	};

	ItemVideo(std::shared_ptr<Source> source, ItemBase::Data data);
	void paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *option,
		QWidget *widget) override;
	[[nodiscard]] bool animated() const override;
	[[nodiscard]] bool hasContent() const override;
	[[nodiscard]] QByteArray content() const override;
	[[nodiscard]] crl::time loopDuration() const override;
	void releasePlayers() override;
	void setStatus(Status status) override;
	int type() const override;

protected:
	[[nodiscard]] Media::Encode::AnimatedEntity::Kind entityKind()
		const override;
	void performFlip() override;
	std::shared_ptr<ItemBase> duplicate(ItemBase::Data data) const override;

private:
	void createPlayer();
	void clipCallback(::Media::Clip::Notification notification);
	[[nodiscard]] QImage currentFrame();

	const std::shared_ptr<Source> _source;
	const QSize _frameSize;
	::Media::Clip::ReaderPointer _reader;
	QImage _image;
	bool _releasedAnimation = false;
	bool _pendingRecreate = false;

};

} // namespace Editor
