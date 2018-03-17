/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "ui/rp_widget.h"
#include "info/info_controller.h"

namespace Window {
class Controller;
} // namespace Window

namespace Ui {
class ScrollArea;
class Shadow;
} // namespace Ui

namespace Media {
namespace Player {

class CoverWidget;

class Panel : public Ui::RpWidget, private Info::AbstractController {
public:
	enum class Layout {
		Full,
		OnlyPlaylist,
	};
	Panel(
		QWidget *parent,
		not_null<Window::Controller*> controller,
		Layout layout);

	bool overlaps(const QRect &globalRect);

	void hideIgnoringEnterEvents();

	void showFromOther();
	void hideFromOther();

	using ButtonCallback = base::lambda<void()>;
	void setPinCallback(ButtonCallback &&callback);
	void setCloseCallback(ButtonCallback &&callback);

	int bestPositionFor(int left) const;

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	// Info::AbstractController implementation.
	Info::Key key() const override;
	PeerData *migrated() const override;
	Info::Section section() const override;

	void startShow();
	void startHide();
	void startHideChecked();
	bool preventAutoHide() const;
	void listHeightUpdated(int newHeight);
	int emptyInnerHeight() const;
	bool contentTooSmall() const;
	void windowActiveChanged();

	void ensureCreated();
	void performDestroy();

	void updateControlsGeometry();
	void refreshList();
	void updateSize();
	void appearanceCallback();
	void hideFinished();
	int contentLeft() const;
	int contentTop() const;
	int contentRight() const;
	int contentBottom() const;
	int scrollMarginBottom() const;
	int contentWidth() const {
		return width() - contentLeft() - contentRight();
	}
	int contentHeight() const {
		return height() - contentTop() - contentBottom();;
	}

	void startAnimation();
	void scrollPlaylistToCurrentTrack();
	not_null<Info::AbstractController*> infoController() {
		return static_cast<Info::AbstractController*>(this);
	}

	Layout _layout;
	bool _hiding = false;

	QPixmap _cache;
	Animation _a_appearance;

	bool _ignoringEnterEvents = false;

	base::Timer _showTimer;
	base::Timer _hideTimer;

	ButtonCallback _pinCallback, _closeCallback;
	object_ptr<CoverWidget> _cover = { nullptr };
	object_ptr<Ui::ScrollArea> _scroll;
	object_ptr<Ui::Shadow> _scrollShadow = { nullptr };

	rpl::lifetime _refreshListLifetime;
	PeerData *_listPeer = nullptr;
	PeerData *_listMigratedPeer = nullptr;

};

} // namespace Clip
} // namespace Media
