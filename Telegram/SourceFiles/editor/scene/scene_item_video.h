/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "editor/scene/scene_item_animated.h"

namespace Editor {

class SegmentPlayer;

class ItemVideo final : public ItemAnimated {
public:
	enum { Type = ItemBase::Type + 4 };

	struct Source {
		QString path;
		QByteArray content;
		QImage thumbnail;
		crl::time duration = 0;
		bool hasAudio = false;
	};

	ItemVideo(std::shared_ptr<Source> source, ItemBase::Data data);
	~ItemVideo();

	void paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *option,
		QWidget *widget) override;
	[[nodiscard]] bool animated() const override;
	[[nodiscard]] bool hasContent() const override;
	[[nodiscard]] QByteArray content() const override;
	[[nodiscard]] crl::time loopDuration() const override;
	[[nodiscard]] Trim trim() const override;
	void releasePlayers() override;
	void setStatus(Status status) override;
	void save(SaveState state) override;
	void restore(SaveState state) override;
	int type() const override;

	[[nodiscard]] const Source &source() const;
	[[nodiscard]] crl::time duration() const;
	[[nodiscard]] not_null<SegmentPlayer*> player() const;
	void setTrim(Trim trim);
	[[nodiscard]] bool hasAudio() const;
	[[nodiscard]] float64 volume() const;
	void setVolume(float64 volume);
	[[nodiscard]] bool sounding() const;

protected:
	[[nodiscard]] Media::Encode::AnimatedEntity::Kind entityKind()
		const override;
	void performFlip() override;
	std::shared_ptr<ItemBase> duplicate(ItemBase::Data data) const override;

private:
	struct Saved {
		Trim trim;
		float64 volume = 1.;
	};

	const std::shared_ptr<Source> _source;
	const QSize _frameSize;
	const std::unique_ptr<SegmentPlayer> _player;
	QImage _image;
	Trim _trim;
	Saved _saved;
	Saved _kept;
	float64 _volume = 1.;
	bool _releasedAnimation = false;
	bool _pendingRecreate = false;

	rpl::lifetime _lifetime;

};

} // namespace Editor
