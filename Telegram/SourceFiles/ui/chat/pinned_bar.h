/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once
#include "ui/wrap/slide_wrap.h"

namespace Ui {

struct MessageBarContent;
class MessageBar;
class IconButton;
class PlainShadow;

class PinnedBar final {
public:
	PinnedBar(
		not_null<QWidget*> parent,
		rpl::producer<Ui::MessageBarContent> content,
		bool withClose = false);
	~PinnedBar();

	void show();
	void hide();
	void raise();

	void move(int x, int y);
	void resizeToWidth(int width);
	[[nodiscard]] int height() const;
	[[nodiscard]] rpl::producer<int> heightValue() const;
	[[nodiscard]] rpl::producer<> closeClicks() const;
	[[nodiscard]] rpl::producer<> barClicks() const;

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _wrap.lifetime();
	}

private:
	void createControls();

	Ui::SlideWrap<> _wrap;
	std::unique_ptr<Ui::MessageBar> _bar;
	std::unique_ptr<Ui::IconButton> _close;
	std::unique_ptr<Ui::PlainShadow> _shadow;
	rpl::event_stream<> _barClicks;
	bool _shouldBeShown = false;
	bool _forceHidden = false;

};

} // namespace Ui
