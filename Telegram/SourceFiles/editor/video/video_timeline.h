/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "editor/editor_trim_timeline.h"
#include "ui/effects/animations.h"

namespace Editor {

struct VideoTimelineFrames {
	std::vector<QImage> frames;
	crl::time from = 0;
	crl::time span = 0;
	QSize box;
};

class VideoTimelineFramesCache final {
public:
	[[nodiscard]] const VideoTimelineFrames *find(
		Fn<bool(const VideoTimelineFrames &set)> matches) const;
	void add(VideoTimelineFrames set);
	[[nodiscard]] int size() const;

private:
	std::vector<VideoTimelineFrames> _sets;

};

struct VideoTimelineDescriptor {
	QString path;
	QByteArray content;
	QSize dimensions;
	std::shared_ptr<VideoTimelineFramesCache> cache;
	crl::time duration = 0;

	crl::time maxDuration = 0;
	crl::time minDuration = 0;

	// A zero |till| means the whole allowed window is selected.
	crl::time from = 0;
	crl::time till = 0;
	crl::time cover = 0;

	bool trimOnly = false;
};

class VideoTimeline final : public TrimTimeline {
public:
	VideoTimeline(
		not_null<Ui::RpWidget*> parent,
		VideoTimelineDescriptor descriptor);
	~VideoTimeline();

	[[nodiscard]] QPoint coverDot() const;

private:
	struct Loading {
		VideoTimelineFrames set;
		std::shared_ptr<std::atomic<bool>> cancel;
	};

	void paintStrip(QPainter &p, const QRect &strip) override;
	void paintOverlay(QPainter &p) override;
	void headGrabChanged(bool grabbed) override;
	void visibleRangeChanged() override;

	void reloadFrames();
	void paintFrames(
		QPainter &p,
		const QRect &strip,
		const VideoTimelineFrames &set);

	const QString _path;
	const QByteArray _content;
	const QSize _dimensions;
	const std::shared_ptr<VideoTimelineFramesCache> _cache;

	VideoTimelineFrames _frames;
	std::unique_ptr<Loading> _loading;
	base::Timer _reloadTimer;

	Ui::Animations::Simple _dotActive;

};

} // namespace Editor
