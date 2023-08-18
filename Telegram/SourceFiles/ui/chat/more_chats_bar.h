/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/wrap/slide_wrap.h"
#include "ui/effects/animations.h"
#include "ui/text/text.h"
#include "base/object_ptr.h"
#include "base/timer.h"

class Painter;

namespace Ui {

class PlainShadow;
class IconButton;

struct MoreChatsBarContent {
	int count = 0;
};

class MoreChatsBar final {
public:
	MoreChatsBar(
		not_null<QWidget*> parent,
		rpl::producer<MoreChatsBarContent> content);
	~MoreChatsBar();

	[[nodiscard]] not_null<RpWidget*> wrap() {
		return &_wrap;
	}

	void show();
	void hide();
	void raise();
	void finishAnimating();

	void setShadowGeometryPostprocess(Fn<QRect(QRect)> postprocess);

	void move(int x, int y);
	void resizeToWidth(int width);
	[[nodiscard]] int height() const;
	[[nodiscard]] rpl::producer<int> heightValue() const;
	[[nodiscard]] rpl::producer<> barClicks() const;
	[[nodiscard]] rpl::producer<> closeClicks() const;

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _wrap.lifetime();
	}

private:
	void updateShadowGeometry(QRect wrapGeometry);
	void updateControlsGeometry(QRect wrapGeometry);
	void setupInner();
	void paint(Painter &p);

	SlideWrap<> _wrap;
	not_null<RpWidget*> _inner;
	std::unique_ptr<PlainShadow> _shadow;
	object_ptr<IconButton> _close;
	rpl::event_stream<> _barClicks;
	Fn<QRect(QRect)> _shadowGeometryPostprocess;
	bool _shouldBeShown = false;
	bool _forceHidden = false;

	MoreChatsBarContent _content;
	Ui::Text::String _text;
	Ui::Text::String _status;

};

} // namespace Ui
