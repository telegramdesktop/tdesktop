/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "editor/scene/scene_item_base.h"
#include "media/clip/media_clip_reader.h"
#include "media/media_video_encode.h"

namespace Data {
class DocumentMedia;
} // namespace Data
namespace Lottie {
class SinglePlayer;
} // namespace Lottie
class DocumentData;

namespace Editor {

class ItemSticker : public ItemBase {
public:
	enum { Type = ItemBase::Type + 1 };

	ItemSticker(
		not_null<DocumentData*> document,
		ItemBase::Data data);
	void paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *option,
		QWidget *widget) override;
	[[nodiscard]] not_null<DocumentData*> sticker() const;
	[[nodiscard]] bool animated() const;
	[[nodiscard]] Media::Encode::AnimatedEntity animatedEntity(
		const QTransform &sceneToCanvas) const;
	[[nodiscard]] QByteArray content() const;
	[[nodiscard]] crl::time loopDuration() const;
	void releasePlayers();
	void setStatus(Status status) override;
	int type() const override;

protected:
	void performFlip() override;
	std::shared_ptr<ItemBase> duplicate(ItemBase::Data data) const override;

private:
	const not_null<DocumentData*> _document;
	const std::shared_ptr<::Data::DocumentMedia> _mediaView;

	void updatePixmap(QImage &&image);
	void clipCallback(::Media::Clip::Notification notification);
	bool createPlayer();
	[[nodiscard]] QImage currentFrame();

	struct {
		std::unique_ptr<Lottie::SinglePlayer> player;
		rpl::lifetime lifetime;
	} _lottie;
	::Media::Clip::ReaderPointer _webm;
	QImage _image;
	struct {
		QImage image;
		qint64 key = 0;
		QSize size;
		bool flipped = false;
	} _preview;

	crl::time _loopDuration = 0;
	bool _releasedAnimation = false;
	bool _pendingRecreate = false;

	rpl::lifetime _loadingLifetime;

};

} // namespace Editor
