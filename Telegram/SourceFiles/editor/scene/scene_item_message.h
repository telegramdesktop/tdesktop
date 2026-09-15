/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "editor/scene/scene_item_animated.h"
#include "editor/video/video_clip.h"

namespace Ui {
class RadialAnimation;
} // namespace Ui

namespace Editor {

class MessageRenderer;
class MessageSource;

struct MessageVideoOptions {
	bool play = false;
	bool sound = false;
};

class ItemMessage final : public ItemAnimated, public base::has_weak_ptr {
public:
	enum { Type = ItemBase::Type + 5 };

	using EditCallback = Fn<void(not_null<ItemMessage*>)>;

	ItemMessage(
		std::shared_ptr<MessageSource> source,
		std::unique_ptr<MessageRenderer> renderer,
		ItemBase::Data data,
		std::optional<bool> dark = std::nullopt,
		MessageVideoOptions video = {});
	~ItemMessage();

	void paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *option,
		QWidget *widget) override;
	int type() const override;
	[[nodiscard]] bool animated() const override;
	[[nodiscard]] bool hasContent() const override;
	[[nodiscard]] QByteArray content() const override;
	[[nodiscard]] crl::time loopDuration() const override;
	[[nodiscard]] VideoTrim trim() const override;
	void releasePlayers() override;
	void setStatus(Status status) override;
	void save(SaveState state) override;
	void restore(SaveState state) override;
	[[nodiscard]] VideoClip *videoClip() override;
	[[nodiscard]] Media::Encode::AnimatedEntity animatedEntity(
		const QTransform &sceneToCanvas) const override;

	[[nodiscard]] const std::shared_ptr<MessageSource> &source() const;
	void setSource(std::shared_ptr<MessageSource> source);
	[[nodiscard]] std::optional<bool> dark() const;
	void setDark(std::optional<bool> dark);
	void setEditCallback(EditCallback callback);

protected:
	[[nodiscard]] Media::Encode::AnimatedEntity::Kind entityKind()
		const override;
	[[nodiscard]] QRectF entityRect() const override;
	[[nodiscard]] QRectF visibleRect() const override;
	[[nodiscard]] bool flippable() const override;
	void fillContextMenu(not_null<Ui::PopupMenu*> menu) override;
	void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;
	std::shared_ptr<ItemBase> duplicate(ItemBase::Data data) const override;

private:
	void attachRenderer();
	void scheduleRefresh();
	void refresh();
	void setImage(QImage image, int ratio);
	void updateSize();
	void ensureRatio(int ratio);
	[[nodiscard]] bool editable() const;
	[[nodiscard]] int neededRatio(not_null<QPainter*> p) const;

	void watchVideo();
	void checkVideo();
	void createClip(std::shared_ptr<VideoClipSource> source);
	void checkCutout();
	void updateRadial(crl::time now);
	void notifyVideoClipChanged();
	[[nodiscard]] QRect holeRect() const;
	[[nodiscard]] QRect framesRect() const;
	[[nodiscard]] Media::Encode::AnimatedEntity::Cutout cutout() const;
	[[nodiscard]] const QImage &composeFrame();
	void paintLoading(QPainter *p) const;

	std::shared_ptr<MessageSource> _source;
	std::unique_ptr<MessageRenderer> _renderer;
	EditCallback _edit;
	const MessageVideoOptions _videoOptions;
	std::unique_ptr<VideoClip> _clip;
	std::unique_ptr<Ui::RadialAnimation> _radial;
	std::optional<VideoClip::State> _saved;
	std::optional<VideoClip::State> _kept;
	QImage _image;
	mutable QImage _mask;
	QImage _composite;
	QSize _size;
	QRect _mediaRect;
	std::optional<bool> _dark;
	int _ratio = 0;
	bool _refreshScheduled = false;
	bool _cutout = false;
	bool _playersReleased = false;

	rpl::lifetime _videoLifetime;

};

} // namespace Editor
