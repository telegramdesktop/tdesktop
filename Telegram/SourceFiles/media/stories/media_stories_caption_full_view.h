/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class FlatLabel;
class ElasticScroll;
template <typename Widget>
class PaddingWrap;
} // namespace Ui

namespace Media::Stories {

class Controller;

class CaptionFullView final {
public:
	explicit CaptionFullView(not_null<Controller*> controller);
	~CaptionFullView();

	void close();
	void repaint();
	[[nodiscard]] bool closing() const;
	[[nodiscard]] bool focused() const;

private:
	void updateGeometry();
	void startAnimation();

	const not_null<Controller*> _controller;
	const std::unique_ptr<Ui::ElasticScroll> _scroll;
	const not_null<Ui::PaddingWrap<Ui::FlatLabel>*> _wrap;
	const not_null<Ui::FlatLabel*> _text;
	Ui::Animations::Simple _animation;
	QRect _outer;
	int _closingTopAdded = 0;
	bool _pulling = false;
	bool _closing = false;
	bool _down = false;

};

} // namespace Media::Stories
