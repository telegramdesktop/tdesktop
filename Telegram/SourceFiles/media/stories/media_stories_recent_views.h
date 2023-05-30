/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text.h"

namespace Ui {
class RpWidget;
class GroupCallUserpics;
} // namespace Ui

namespace Media::Stories {

class Controller;

struct RecentViewsData {
	std::vector<not_null<PeerData*>> list;
	int total = 0;
	bool valid = false;

	friend inline auto operator<=>(
		const RecentViewsData &,
		const RecentViewsData &) = default;
	friend inline bool operator==(
		const RecentViewsData &,
		const RecentViewsData &) = default;
};

class RecentViews final {
public:
	explicit RecentViews(not_null<Controller*> controller);
	~RecentViews();

	void show(RecentViewsData data);

private:
	const not_null<Controller*> _controller;

	std::unique_ptr<Ui::RpWidget> _widget;
	std::unique_ptr<Ui::GroupCallUserpics> _userpics;
	Ui::Text::String _text;
	RecentViewsData _data;
	rpl::lifetime _userpicsLifetime;
	int _userpicsWidth = 0;

};

} // namespace Media::Stories