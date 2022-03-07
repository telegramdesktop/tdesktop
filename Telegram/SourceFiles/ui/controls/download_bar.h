/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/wrap/slide_wrap.h"
#include "ui/effects/radial_animation.h"
#include "ui/widgets/shadow.h"
#include "ui/text/text.h"

namespace Ui {

class RippleButton;

struct DownloadBarProgress {
	int64 ready = 0;
	int64 total = 0;
};

struct DownloadBarContent {
	TextWithEntities singleName;
	QImage singleThumbnail;
	int count = 0;
	int done = 0;
};

class DownloadBar final {
public:
	DownloadBar(
		not_null<QWidget*> parent,
		rpl::producer<DownloadBarProgress> progress);
	~DownloadBar();

	void show(DownloadBarContent &&content);

	[[nodiscard]] bool isHidden() const;
	[[nodiscard]] int height() const;
	[[nodiscard]] rpl::producer<int> heightValue() const;
	[[nodiscard]] rpl::producer<bool> shownValue() const;
	void setGeometry(int left, int top, int width, int height);

	[[nodiscard]] rpl::producer<> clicks() const;

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void paint(Painter &p, QRect clip);
	void refreshIcon();
	void refreshThumbnail();
	void refreshInfo(const DownloadBarProgress &progress);
	void radialAnimationCallback(crl::time now);
	[[nodiscard]] float64 computeProgress() const;

	SlideWrap<RippleButton> _button;
	PlainShadow _shadow;
	DownloadBarContent _content;
	rpl::variable<DownloadBarProgress> _progress;
	Ui::Animations::Simple _finishedAnimation;
	bool _finished = false;
	QImage _documentIconLarge;
	QImage _documentIcon;
	QImage _documentIconDone;
	qint64 _thumbnailCacheKey = 0;
	QImage _thumbnailLarge;
	QImage _thumbnail;
	QImage _thumbnailDone;
	Text::String _title;
	Text::String _info;
	RadialAnimation _radial;

};

} // namespace Ui
