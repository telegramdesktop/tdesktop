/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/wrap/padding_wrap.h"
#include "ui/widgets/checkbox.h"
#include "base/timer.h"

namespace style {
struct InfoToggle;
} // namespace style

namespace Data {
class Feed;
} // namespace Data

namespace Ui {
class FeedUserpicButton;
class FlatLabel;
template <typename Widget>
class SlideWrap;
} // namespace Ui

namespace Info {
class Controller;
} // namespace Info

namespace Info {
namespace FeedProfile {

class Cover : public Ui::FixedHeightWidget {
public:
	Cover(
		QWidget *parent,
		not_null<Controller*> controller);

	~Cover();

private:
	void setupChildGeometry();
	void initViewers();
	void refreshNameText();
	void refreshStatusText();
	void refreshNameGeometry(int newWidth);
	void refreshStatusGeometry(int newWidth);

	not_null<Controller*> _controller;
	not_null<Data::Feed*> _feed;

	object_ptr<Ui::FeedUserpicButton> _userpic;
	object_ptr<Ui::FlatLabel> _name = { nullptr };
	object_ptr<Ui::FlatLabel> _status = { nullptr };
	//object_ptr<CoverDropArea> _dropArea = { nullptr };

};

} // namespace FeedProfile
} // namespace Info
