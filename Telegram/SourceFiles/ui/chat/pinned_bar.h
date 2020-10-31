/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/wrap/slide_wrap.h"
#include "base/object_ptr.h"

namespace Ui {

struct MessageBarContent;
class MessageBar;
class IconButton;
class PlainShadow;
class RpWidget;

class PinnedBar final {
public:
	PinnedBar(
		not_null<QWidget*> parent,
		rpl::producer<Ui::MessageBarContent> content);
	~PinnedBar();

	void show();
	void hide();
	void raise();
	void finishAnimating();

	void setShadowGeometryPostprocess(Fn<QRect(QRect)> postprocess);

	void setRightButton(object_ptr<Ui::RpWidget> button);

	void move(int x, int y);
	void resizeToWidth(int width);
	[[nodiscard]] int height() const;
	[[nodiscard]] rpl::producer<int> heightValue() const;
	[[nodiscard]] rpl::producer<> barClicks() const;

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _wrap.lifetime();
	}

private:
	void createControls();
	void updateShadowGeometry(QRect wrapGeometry);
	void updateControlsGeometry(QRect wrapGeometry);

	Ui::SlideWrap<> _wrap;
	std::unique_ptr<Ui::MessageBar> _bar;
	object_ptr<Ui::RpWidget> _rightButton = { nullptr };
	std::unique_ptr<Ui::PlainShadow> _shadow;
	rpl::event_stream<> _barClicks;
	Fn<QRect(QRect)> _shadowGeometryPostprocess;
	bool _shouldBeShown = false;
	bool _forceHidden = false;

};

} // namespace Ui
