/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "editor/scene/scene_item_base.h"
#include "media/media_video_encode.h"

namespace Editor {

class ItemAnimated : public ItemBase {
public:
	using ItemBase::ItemBase;

	[[nodiscard]] virtual bool animated() const = 0;
	[[nodiscard]] virtual QByteArray content() const = 0;
	[[nodiscard]] virtual crl::time loopDuration() const = 0;
	virtual void releasePlayers() = 0;

	[[nodiscard]] Media::Encode::AnimatedEntity animatedEntity(
		const QTransform &sceneToCanvas) const;

protected:
	[[nodiscard]] virtual Media::Encode::AnimatedEntity::Kind entityKind()
		const = 0;

	void paintFrame(
		QPainter *p,
		const QImage &frame,
		bool live,
		bool mirror);

private:
	struct {
		QImage image;
		qint64 key = 0;
		QSize size;
		bool flipped = false;
	} _preview;

};

} // namespace Editor
