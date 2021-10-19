/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "ui/rp_widget.h"
#include "ui/effects/animations.h"
#include "info/info_controller.h"

namespace Window {
class SessionController;
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
	Panel(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	bool overlaps(const QRect &globalRect);

	void hideIgnoringEnterEvents();

	void showFromOther();
	void hideFromOther();

	int bestPositionFor(int left) const;

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
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

	bool _hiding = false;

	QPixmap _cache;
	Ui::Animations::Simple _a_appearance;

	bool _ignoringEnterEvents = false;

	base::Timer _showTimer;
	base::Timer _hideTimer;

	object_ptr<Ui::ScrollArea> _scroll;

	rpl::lifetime _refreshListLifetime;
	PeerData *_listPeer = nullptr;
	PeerData *_listMigratedPeer = nullptr;

};

} // namespace Player
} // namespace Media
