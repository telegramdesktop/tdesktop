/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "editor/scene/scene_item_animated.h"
#include "editor/video/video_clip.h"

namespace Editor {

class ItemVideo final : public ItemAnimated {
public:
	enum { Type = ItemBase::Type + 4 };

	ItemVideo(std::shared_ptr<VideoClipSource> source, ItemBase::Data data);
	~ItemVideo();

	void paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *option,
		QWidget *widget) override;
	[[nodiscard]] bool animated() const override;
	[[nodiscard]] bool hasContent() const override;
	[[nodiscard]] QByteArray content() const override;
	[[nodiscard]] crl::time loopDuration() const override;
	[[nodiscard]] bool roundVideo() const override;
	[[nodiscard]] VideoTrim trim() const override;
	void releasePlayers() override;
	void setStatus(Status status) override;
	void save(SaveState state) override;
	void restore(SaveState state) override;
	int type() const override;
	[[nodiscard]] VideoClip *videoClip() override;

	void keepInitialState();
	[[nodiscard]] bool atInitialState() const;

	void setBackground(bool background);
	[[nodiscard]] bool isBackground() const override;
	void setBackgroundLocked(bool locked);
	[[nodiscard]] bool isBackgroundLocked() const override;

protected:
	[[nodiscard]] Media::Encode::AnimatedEntity::Kind entityKind()
		const override;
	[[nodiscard]] QString entityPath() const override;
	void fillContextMenu(not_null<Ui::PopupMenu*> menu) override;
	void performFlip() override;
	std::shared_ptr<ItemBase> duplicate(ItemBase::Data data) const override;

private:
	const std::unique_ptr<VideoClip> _clip;
	const QSize _frameSize;
	QImage _image;
	std::optional<Placement> _initialPlacement;
	VideoClip::State _initialState;
	bool _hasInitial = false;
	VideoClip::State _saved;
	VideoClip::State _kept;
	bool _isBackground = false;
	bool _isLocked = true;

};

} // namespace Editor
