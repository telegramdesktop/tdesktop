/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_stories.h"

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Media::Stories {

class Header;
class Slider;
class ReplyArea;
class Delegate;

class View final {
public:
	explicit View(not_null<Delegate*> delegate);
	~View();

	void show(const Data::StoriesList &list, int index);

private:
	const not_null<Delegate*> _delegate;
	const not_null<Ui::RpWidget*> _wrap;

	std::unique_ptr<Header> _header;
	std::unique_ptr<Slider> _slider;
	std::unique_ptr<ReplyArea> _replyArea;

};

} // namespace Media::Stories
