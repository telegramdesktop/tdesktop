/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "editor/editor_trim_timeline.h"
#include "ui/effects/animations.h"

namespace Editor {

struct VideoTimelineDescriptor {
	QString path;
	QByteArray content;
	QSize dimensions;
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
	void paintStrip(QPainter &p, const QRect &strip) override;
	void paintOverlay(QPainter &p) override;
	void headGrabChanged(bool grabbed) override;

	void reloadFrames();

	const QString _path;
	const QByteArray _content;
	const QSize _dimensions;

	std::vector<QImage> _frames;
	QSize _framesBox;
	int _frameWidth = 0;
	std::shared_ptr<std::atomic<bool>> _framesCancel;

	Ui::Animations::Simple _dotActive;

};

} // namespace Editor
