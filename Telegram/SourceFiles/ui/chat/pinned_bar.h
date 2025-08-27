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
template <typename Widget>
class FadeWrapScaled;
class MessageBar;
class IconButton;
class PlainShadow;
class RpWidget;

class PinnedBar final {
public:
	PinnedBar(
		not_null<QWidget*> parent,
		Fn<bool()> customEmojiPaused,
		rpl::producer<> customEmojiPausedChanges);
	~PinnedBar();

	void show();
	void hide();
	void raise();
	void customEmojiRepaint();
	void finishAnimating();

	void setShadowGeometryPostprocess(Fn<QRect(QRect)> postprocess);

	void setContent(rpl::producer<Ui::MessageBarContent> content);
	void setRightButton(object_ptr<Ui::RpWidget> button);

	void move(int x, int y);
	void resizeToWidth(int width);
	[[nodiscard]] int height() const;
	[[nodiscard]] rpl::producer<int> heightValue() const;
	[[nodiscard]] rpl::producer<> barClicks() const;
	[[nodiscard]] rpl::producer<> contextMenuRequested() const;

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _wrap.lifetime();
	}

private:
	using RightButton = object_ptr<Ui::FadeWrapScaled<Ui::RpWidget>>;
	void createControls();
	void updateShadowGeometry(QRect wrapGeometry);
	void updateControlsGeometry(QRect wrapGeometry);

	Ui::SlideWrap<> _wrap;
	std::unique_ptr<Ui::MessageBar> _bar;

	struct {
		RightButton button = { nullptr };
		rpl::lifetime previousButtonLifetime;
	} _right;

	std::unique_ptr<Ui::PlainShadow> _shadow;
	Fn<bool()> _customEmojiPaused;
	rpl::event_stream<> _barClicks;
	rpl::event_stream<> _contextMenuRequested;
	Fn<QRect(QRect)> _shadowGeometryPostprocess;
	bool _shouldBeShown = false;
	bool _forceHidden = false;

	rpl::lifetime _contentLifetime;

};

} // namespace Ui
