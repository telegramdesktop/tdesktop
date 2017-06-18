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

#include "mtproto/sender.h"

namespace AdminLog {

class SectionMemento;

class InnerWidget final : public TWidget, private MTP::Sender {
public:
	InnerWidget(QWidget *parent, gsl::not_null<ChannelData*> channel);

	gsl::not_null<ChannelData*> channel() const {
		return _channel;
	}

	// Updates the area that is visible inside the scroll container.
	void setVisibleTopBottom(int visibleTop, int visibleBottom) override;

	void resizeToWidth(int newWidth, int minHeight) {
		_minHeight = minHeight;
		return TWidget::resizeToWidth(newWidth);
	}

	void saveState(gsl::not_null<SectionMemento*> memento) const;
	void restoreState(gsl::not_null<const SectionMemento*> memento);
	void setCancelledCallback(base::lambda<void()> callback) {
		_cancelledCallback = std::move(callback);
	}

	// Empty "flags" means all events. Empty "admins" means all admins.
	void applyFilter(MTPDchannelAdminLogEventsFilter::Flags flags, const std::vector<gsl::not_null<UserData*>> &admins);

	~InnerWidget();

protected:
	void paintEvent(QPaintEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

	// Resizes content and counts natural widget height for the desired width.
	int resizeGetHeight(int newWidth) override;

private:
	void checkPreloadMore();
	void preloadMore();
	void updateSize();

	gsl::not_null<ChannelData*> _channel;
	base::lambda<void()> _cancelledCallback;

	int _minHeight = 0;
	int _visibleTop = 0;
	int _visibleBottom = 0;

	int32 _preloadGroupId = 0;
	mtpRequestId _preloadRequestId = 0;
	bool _allLoaded = true;

	MTPDchannelAdminLogEventsFilter::Flags _filterFlags = 0;
	std::vector<gsl::not_null<UserData*>> _filterAdmins;

};

} // namespace AdminLog
