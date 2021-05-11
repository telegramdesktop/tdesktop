/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/abstract_button.h"
#include "ui/effects/cross_line.h"
#include "ui/effects/animations.h"

#if defined Q_OS_MAC
#define USE_OPENGL_LARGE_VIDEO
#endif // Q_OS_MAC

namespace style {
struct GroupCallLargeVideo;
} // namespace style

namespace Webrtc {
class VideoTrack;
} // namespace Webrtc

namespace Calls::Group {

class MembersRow;

struct LargeVideoTrack {
	Webrtc::VideoTrack *track = nullptr;
	MembersRow *row = nullptr;

	[[nodiscard]] explicit operator bool() const {
		return track != nullptr;
	}
};

[[nodiscard]] inline bool operator==(
		LargeVideoTrack a,
		LargeVideoTrack b) noexcept {
	return (a.track == b.track) && (a.row == b.row);
}

[[nodiscard]] inline bool operator!=(
		LargeVideoTrack a,
		LargeVideoTrack b) noexcept {
	return !(a == b);
}

class LargeVideo final {
public:
	LargeVideo(
		QWidget *parent,
		const style::GroupCallLargeVideo &st,
		bool visible,
		rpl::producer<LargeVideoTrack> track,
		rpl::producer<bool> pinned);

	void raise();
	void setVisible(bool visible);
	void setGeometry(int x, int y, int width, int height);

	[[nodiscard]] rpl::producer<bool> pinToggled() const;

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _content.lifetime();
	}

private:
#ifdef USE_OPENGL_LARGE_VIDEO
	using ContentParent = Ui::RpWidgetWrap<QOpenGLWidget>;
#else // USE_OPENGL_OVERLAY_WIDGET
	using ContentParent = Ui::RpWidget;
#endif // USE_OPENGL_OVERLAY_WIDGET

	class Content final : public ContentParent {
	public:
		Content(QWidget *parent, Fn<void(QRect)> paint)
		: ContentParent(parent), _paint(std::move(paint)) {
			Expects(_paint != nullptr);
		}

	private:
		void paintEvent(QPaintEvent *e) override {
			_paint(e->rect());
		}

		Fn<void(QRect)> _paint;

	};

	void setup(
		rpl::producer<LargeVideoTrack> track,
		rpl::producer<bool> pinned);
	void setupControls(rpl::producer<bool> pinned);
	void paint(QRect clip);
	void paintControls(Painter &p, QRect clip);
	void updateControlsGeometry();

	Content _content;
	const style::GroupCallLargeVideo &_st;
	LargeVideoTrack _track;
	QImage _shadow;
	Ui::CrossLineAnimation _pin;
	Ui::AbstractButton _pinButton;
	Ui::Animations::Simple _controlsAnimation;
	bool _topControls = false;
	bool _pinned = false;
	bool _controlsShown = true;
	rpl::lifetime _trackLifetime;

};

[[nodiscard]] QImage GenerateShadow(
	int height,
	int topAlpha,
	int bottomAlpha);

} // namespace Calls::Group
