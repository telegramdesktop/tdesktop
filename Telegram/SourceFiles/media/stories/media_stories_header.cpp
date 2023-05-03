/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/stories/media_stories_header.h"

#include "base/unixtime.h"
#include "data/data_user.h"
#include "media/stories/media_stories_delegate.h"
#include "ui/controls/userpic_button.h"
#include "ui/text/format_values.h"
#include "ui/widgets/labels.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "styles/style_boxes.h" // defaultUserpicButton.

namespace Media::Stories {

Header::Header(not_null<Delegate*> delegate)
: _delegate(delegate) {
}

Header::~Header() {
}

void Header::show(HeaderData data) {
	if (_data == data) {
		return;
	}
	const auto userChanged = (!_data || _data->user != data.user);
	_data = data;
	if (userChanged) {
		_date = nullptr;
		const auto parent = _delegate->storiesWrap();
		auto widget = std::make_unique<Ui::RpWidget>(parent);
		const auto raw = widget.get();
		parent->sizeValue() | rpl::start_with_next([=](QSize size) {
			raw->setGeometry(50, 50, 600, 100);
		}, raw->lifetime());
		raw->setAttribute(Qt::WA_TransparentForMouseEvents);
		const auto userpic = Ui::CreateChild<Ui::UserpicButton>(
			raw,
			data.user,
			st::defaultUserpicButton);
		userpic->move(0, 0);
		const auto name = Ui::CreateChild<Ui::FlatLabel>(
			raw,
			data.user->firstName,
			st::defaultFlatLabel);
		name->move(100, 0);
		raw->show();
		_widget = std::move(widget);
	}
	_date = std::make_unique<Ui::FlatLabel>(
		_widget.get(),
		Ui::FormatDateTime(base::unixtime::parse(data.date)),
		st::defaultFlatLabel);
	_date->move(100, 50);
	_date->show();
}

} // namespace Media::Stories
