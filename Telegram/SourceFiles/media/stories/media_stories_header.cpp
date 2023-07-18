/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/stories/media_stories_header.h"

#include "base/unixtime.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_user.h"
#include "media/stories/media_stories_controller.h"
#include "lang/lang_keys.h"
#include "ui/controls/userpic_button.h"
#include "ui/layers/box_content.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/labels.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "styles/style_media_view.h"

namespace Media::Stories {
namespace {

constexpr auto kNameOpacity = 1.;
constexpr auto kDateOpacity = 0.8;

struct Timestamp {
	QString text;
	TimeId changes = 0;
};

[[nodiscard]] Timestamp ComposeTimestamp(TimeId when, TimeId now) {
	const auto minutes = (now - when) / 60;
	if (!minutes) {
		return { tr::lng_mediaview_just_now(tr::now), 61 - (now - when) };
	} else if (minutes < 60) {
		return {
			tr::lng_mediaview_minutes_ago(tr::now, lt_count, minutes),
			61 - ((now - when) % 60),
		};
	}
	const auto hours = (now - when) / 3600;
	if (hours < 12) {
		return {
			tr::lng_mediaview_hours_ago(tr::now, lt_count, hours),
			3601 - ((now - when) % 3600),
		};
	}
	const auto whenFull = base::unixtime::parse(when);
	const auto nowFull = base::unixtime::parse(now);
	const auto locale = QLocale();
	auto tomorrow = nowFull;
	tomorrow.setDate(nowFull.date().addDays(1));
	tomorrow.setTime(QTime(0, 0, 1));
	const auto seconds = int(nowFull.secsTo(tomorrow));
	if (whenFull.date() == nowFull.date()) {
		const auto whenTime = locale.toString(
			whenFull.time(),
			QLocale::ShortFormat);
		return {
			tr::lng_mediaview_today(tr::now, lt_time, whenTime),
			seconds,
		};
	} else if (whenFull.date().addDays(1) == nowFull.date()) {
		const auto whenTime = locale.toString(
			whenFull.time(),
			QLocale::ShortFormat);
		return {
			tr::lng_mediaview_yesterday(tr::now, lt_time, whenTime),
			seconds,
		};
	}
	return { Ui::FormatDateTime(whenFull) };
}

[[nodiscard]] TextWithEntities ComposeName(HeaderData data) {
	auto result = Ui::Text::Bold(data.user->shortName());
	if (data.fullCount) {
		result.append(QString::fromUtf8(" \xE2\x80\xA2 %1/%2"
		).arg(data.fullIndex + 1
		).arg(data.fullCount));
	}
	return result;
}

[[nodiscard]] Timestamp ComposeDetails(HeaderData data, TimeId now) {
	auto result = ComposeTimestamp(data.date, now);
	if (data.edited) {
		result.text.append(
			QString::fromUtf8(" \xE2\x80\xA2 ") + tr::lng_edited(tr::now));
	}
	return result;
}

} // namespace

Header::Header(not_null<Controller*> controller)
: _controller(controller)
, _dateUpdateTimer([=] { updateDateText(); }) {
}

Header::~Header() {
}

void Header::show(HeaderData data) {
	if (_data == data) {
		return;
	}
	const auto nameDataChanged = !_data
		|| (_data->user != data.user)
		|| (_data->fullCount != data.fullCount)
		|| (data.fullCount && _data->fullIndex != data.fullIndex);
	_data = data;
	if (nameDataChanged) {
		_date = nullptr;
		const auto parent = _controller->wrap();
		auto widget = std::make_unique<Ui::AbstractButton>(parent);
		const auto raw = widget.get();
		raw->setClickedCallback([=] {
			_controller->uiShow()->show(PrepareShortInfoBox(_data->user));
		});
		const auto userpic = Ui::CreateChild<Ui::UserpicButton>(
			raw,
			data.user,
			st::storiesHeaderPhoto);
		userpic->setAttribute(Qt::WA_TransparentForMouseEvents);
		userpic->show();
		userpic->move(
			st::storiesHeaderMargin.left(),
			st::storiesHeaderMargin.top());
		const auto name = Ui::CreateChild<Ui::FlatLabel>(
			raw,
			rpl::single(ComposeName(data)),
			st::storiesHeaderName);
		name->setAttribute(Qt::WA_TransparentForMouseEvents);
		name->setOpacity(kNameOpacity);
		name->move(st::storiesHeaderNamePosition);
		raw->show();
		_widget = std::move(widget);

		_controller->layoutValue(
		) | rpl::start_with_next([=](const Layout &layout) {
			raw->setGeometry(layout.header);
		}, raw->lifetime());
	}
	auto timestamp = ComposeDetails(data, base::unixtime::now());
	_date = std::make_unique<Ui::FlatLabel>(
		_widget.get(),
		std::move(timestamp.text),
		st::storiesHeaderDate);
	_date->setAttribute(Qt::WA_TransparentForMouseEvents);
	_date->setOpacity(kDateOpacity);
	_date->show();
	_date->move(st::storiesHeaderDatePosition);

	if (timestamp.changes > 0) {
		_dateUpdateTimer.callOnce(timestamp.changes * crl::time(1000));
	}
}

void Header::raise() {
	if (_widget) {
		_widget->raise();
	}
}

void Header::updateDateText() {
	if (!_date || !_data || !_data->date) {
		return;
	}
	auto timestamp = ComposeDetails(*_data, base::unixtime::now());
	_date->setText(timestamp.text);
	if (timestamp.changes > 0) {
		_dateUpdateTimer.callOnce(timestamp.changes * crl::time(1000));
	}
}

} // namespace Media::Stories
