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

#include <rpl/variable.h>
#include "ui/rp_widget.h"

namespace Window {
class Controller;
} // namespace Window

namespace Ui {
class VerticalLayout;
template <typename Widget>
class SlideWrap;
struct ScrollToRequest;
class MultiSlideTracker;
} // namespace Ui

namespace Info {

enum class Wrap;
class Controller;

namespace Profile {

class Memento;
class Members;
class Cover;

class InnerWidget final : public Ui::RpWidget {
public:
	InnerWidget(
		QWidget *parent,
		not_null<Controller*> controller);

	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

	void setIsStackBottom(bool isStackBottom) {
		_isStackBottom = isStackBottom;
	}
	rpl::producer<Ui::ScrollToRequest> scrollToRequests() const;
	rpl::producer<int> desiredHeightValue() const override;

protected:
	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

private:
	object_ptr<RpWidget> setupContent(not_null<RpWidget*> parent);
	object_ptr<RpWidget> setupSharedMedia(not_null<RpWidget*> parent);

	int countDesiredHeight() const;
	void updateDesiredHeight() {
		_desiredHeight.fire(countDesiredHeight());
	}

	bool canHideDetailsEver() const;
	rpl::producer<bool> canHideDetails() const;

	rpl::variable<bool> _isStackBottom = true;

	const not_null<Controller*> _controller;
	const not_null<PeerData*> _peer;
	PeerData * const _migrated = nullptr;

	Members *_members = nullptr;
	Cover *_cover = nullptr;
	Ui::SlideWrap<RpWidget> *_infoWrap = nullptr;
	Ui::SlideWrap<RpWidget> *_sharedMediaWrap = nullptr;
	object_ptr<RpWidget> _content;

	bool _inResize = false;
	rpl::event_stream<Ui::ScrollToRequest> _scrollToRequests;
	rpl::event_stream<int> _desiredHeight;

};

} // namespace Profile
} // namespace Info
