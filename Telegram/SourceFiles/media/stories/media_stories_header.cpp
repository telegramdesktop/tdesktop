/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/stories/media_stories_header.h"

#include "base/unixtime.h"
#include "data/data_user.h"
#include "media/stories/media_stories_controller.h"
#include "ui/controls/userpic_button.h"
#include "ui/text/format_values.h"
#include "ui/widgets/labels.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "styles/style_media_view.h"

namespace Media::Stories {
namespace {

constexpr auto kNameOpacity = 1.;
constexpr auto kDateOpacity = 0.6;

} // namespace

Header::Header(not_null<Controller*> controller)
: _controller(controller) {
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
		const auto parent = _controller->wrap();
		auto widget = std::make_unique<Ui::RpWidget>(parent);
		const auto raw = widget.get();
		raw->setAttribute(Qt::WA_TransparentForMouseEvents);
		const auto userpic = Ui::CreateChild<Ui::UserpicButton>(
			raw,
			data.user,
			st::storiesHeaderPhoto);
		userpic->show();
		userpic->move(
			st::storiesHeaderMargin.left(),
			st::storiesHeaderMargin.top());
		const auto name = Ui::CreateChild<Ui::FlatLabel>(
			raw,
			data.user->firstName,
			st::storiesHeaderName);
		name->setOpacity(kNameOpacity);
		name->move(st::storiesHeaderNamePosition);
		raw->show();
		_widget = std::move(widget);

		_controller->layoutValue(
		) | rpl::start_with_next([=](const Layout &layout) {
			raw->setGeometry(layout.header);
		}, raw->lifetime());
	}
	_date = std::make_unique<Ui::FlatLabel>(
		_widget.get(),
		Ui::FormatDateTime(base::unixtime::parse(data.date)),
		st::storiesHeaderDate);
	_date->setOpacity(kDateOpacity);
	_date->show();
	_date->move(st::storiesHeaderDatePosition);
}

} // namespace Media::Stories
