/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
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
	not_null<PeerData*> peer() const override;
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
