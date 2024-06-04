/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_actions.h"

#include "api/api_blocked_peers.h"
#include "api/api_chat_participants.h"
#include "apiwrap.h"
#include "base/options.h"
#include "base/timer_rpl.h"
#include "base/unixtime.h"
#include "boxes/peers/add_bot_to_chat_box.h"
#include "boxes/peers/edit_contact_box.h"
#include "boxes/report_messages_box.h"
#include "boxes/share_box.h"
#include "boxes/translate_box.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "data/business/data_business_common.h"
#include "data/business/data_business_info.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_folder.h"
#include "data/data_forum_topic.h"
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/notify/data_notify_settings.h"
#include "dialogs/ui/dialogs_layout.h"
#include "dialogs/ui/dialogs_message_view.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "history/view/history_view_context_menu.h" // HistoryView::ShowReportPeerBox
#include "history/view/history_view_item_preview.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "info/profile/info_profile_icon.h"
#include "info/profile/info_profile_phone_menu.h"
#include "info/profile/info_profile_text.h"
#include "info/profile/info_profile_values.h"
#include "info/profile/info_profile_widget.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "menu/menu_mute.h"
#include "support/support_helper.h"
#include "ui/boxes/report_box.h"
#include "ui/controls/userpic_button.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h" // Ui::Text::ToUpper
#include "ui/text/text_variant.h"
#include "ui/toast/toast.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h" // Window::Controller::show.
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

namespace Info {
namespace Profile {
namespace {

constexpr auto kDay = Data::WorkingInterval::kDay;

base::options::toggle ShowPeerIdBelowAbout({
	.id = kOptionShowPeerIdBelowAbout,
	.name = "Show Peer IDs in Profile",
	.description = "Show peer IDs from API below their Bio / Description."
		" Add contact IDs to exported data.",
});

[[nodiscard]] rpl::producer<TextWithEntities> UsernamesSubtext(
		not_null<PeerData*> peer,
		rpl::producer<QString> fallback) {
	return rpl::combine(
		UsernamesValue(peer),
		std::move(fallback)
	) | rpl::map([](std::vector<TextWithEntities> usernames, QString text) {
		if (usernames.size() < 2) {
			return TextWithEntities{ .text = text };
		} else {
			auto result = TextWithEntities();
			result.append(tr::lng_info_usernames_label(tr::now));
			result.append(' ');
			auto &&subrange = ranges::make_subrange(
				begin(usernames) + 1,
				end(usernames));
			for (auto &username : std::move(subrange)) {
				const auto isLast = (usernames.back() == username);
				result.append(Ui::Text::Link(
					'@' + base::take(username.text),
					username.entities.front().data()));
				if (!isLast) {
					result.append(u", "_q);
				}
			}
			return result;
		}
	});
}

[[nodiscard]] Fn<void(QString)> UsernamesLinkCallback(
		not_null<PeerData*> peer,
		not_null<Window::SessionController*> controller,
		const QString &addToLink) {
	const auto weak = base::make_weak(controller);
	return [=](QString link) {
		if (link.startsWith(u"internal:"_q)) {
			Core::App().openInternalUrl(link,
				QVariant::fromValue(ClickHandlerContext{
					.sessionWindow = weak,
				}));
			return;
		} else if (!link.startsWith(u"https://"_q)) {
			link = peer->session().createInternalLinkFull(peer->username())
				+ addToLink;
		}
		if (!link.isEmpty()) {
			if (const auto strong = weak.get()) {
				FastShareLink(strong, link);
			}
		}
	};
}

[[nodiscard]] object_ptr<Ui::RpWidget> CreateSkipWidget(
		not_null<Ui::RpWidget*> parent) {
	return Ui::CreateSkipWidget(parent, st::infoProfileSkip);
}

[[nodiscard]] object_ptr<Ui::SlideWrap<>> CreateSlideSkipWidget(
		not_null<Ui::RpWidget*> parent) {
	auto result = Ui::CreateSlideSkipWidget(
		parent,
		st::infoProfileSkip);
	result->setDuration(st::infoSlideDuration);
	return result;
}

[[nodiscard]] rpl::producer<TextWithEntities> AboutWithIdValue(
		not_null<PeerData*> peer) {

	return AboutValue(
		peer
	) | rpl::map([=](TextWithEntities &&value) {
		if (!ShowPeerIdBelowAbout.value()) {
			return std::move(value);
		}
		using namespace Ui::Text;
		if (!value.empty()) {
			value.append("\n\n");
		}
		value.append(Italic(u"id: "_q));
		const auto raw = peer->id.value & PeerId::kChatTypeMask;
		value.append(Link(
			Italic(Lang::FormatCountDecimal(raw)),
			"internal:copy:" + QString::number(raw)));
		return std::move(value);
	});
}

[[nodiscard]] bool AreNonTrivialHours(const Data::WorkingHours &hours) {
	if (!hours) {
		return false;
	}
	const auto &intervals = hours.intervals.list;
	for (auto i = 0; i != 7; ++i) {
		const auto day = Data::WorkingInterval{ i * kDay, (i + 1) * kDay };
		for (const auto &interval : intervals) {
			const auto intersection = interval.intersected(day);
			if (intersection && intersection != day) {
				return true;
			}
		}
	}
	return false;
}

[[nodiscard]] TimeId OpensIn(
		const Data::WorkingIntervals &intervals,
		TimeId now) {
	using namespace Data;

	while (now < 0) {
		now += WorkingInterval::kWeek;
	}
	while (now > WorkingInterval::kWeek) {
		now -= WorkingInterval::kWeek;
	}
	auto closest = WorkingInterval::kWeek;
	for (const auto &interval : intervals.list) {
		if (interval.start <= now && interval.end > now) {
			return TimeId(0);
		} else if (interval.start > now && interval.start - now < closest) {
			closest = interval.start - now;
		} else if (interval.start < now) {
			const auto next = interval.start + WorkingInterval::kWeek - now;
			if (next < closest) {
				closest = next;
			}
		}
	}
	return closest;
}

[[nodiscard]] rpl::producer<QString> OpensInText(
		rpl::producer<TimeId> in,
		rpl::producer<bool> hoursExpanded,
		rpl::producer<QString> fallback) {
	return rpl::combine(
		std::move(in),
		std::move(hoursExpanded),
		std::move(fallback)
	) | rpl::map([](TimeId in, bool hoursExpanded, QString fallback) {
		return (!in || hoursExpanded)
			? std::move(fallback)
			: (in >= 86400)
			? tr::lng_info_hours_opens_in_days(tr::now, lt_count, in / 86400)
			: (in >= 3600)
			? tr::lng_info_hours_opens_in_hours(tr::now, lt_count, in / 3600)
			: tr::lng_info_hours_opens_in_minutes(
				tr::now,
				lt_count,
				std::max(in / 60, 1));
	});
}

[[nodiscard]] QString FormatDayTime(TimeId time) {
	const auto wrap = [](TimeId value) {
		const auto hours = value / 3600;
		const auto minutes = (value % 3600) / 60;
		return QString::number(hours).rightJustified(2, u'0')
			+ ':'
			+ QString::number(minutes).rightJustified(2, u'0');
	};
	return (time > kDay)
		? tr::lng_info_hours_next_day(tr::now, lt_time, wrap(time - kDay))
		: wrap(time == kDay ? 0 : time);
}

[[nodiscard]] QString JoinIntervals(const Data::WorkingIntervals &data) {
	auto result = QStringList();
	result.reserve(data.list.size());
	for (const auto &interval : data.list) {
		const auto start = FormatDayTime(interval.start);
		const auto end = FormatDayTime(interval.end);
		result.push_back(start + u" - "_q + end);
	}
	return result.join('\n');
}

[[nodiscard]] QString FormatDayHours(
		const Data::WorkingHours &hours,
		const Data::WorkingIntervals &mine,
		bool my,
		int day) {
	using namespace Data;

	const auto local = ExtractDayIntervals(hours.intervals, day);
	if (IsFullOpen(local)) {
		return tr::lng_info_hours_open_full(tr::now);
	}
	const auto use = my ? ExtractDayIntervals(mine, day) : local;
	if (!use) {
		return tr::lng_info_hours_closed(tr::now);
	}
	return JoinIntervals(use);
}

[[nodiscard]] Data::WorkingIntervals ShiftedIntervals(
		Data::WorkingIntervals intervals,
		int delta) {
	auto &list = intervals.list;
	if (!delta || list.empty()) {
		return { std::move(list) };
	}
	for (auto &interval : list) {
		interval.start += delta;
		interval.end += delta;
	}
	while (list.front().start < 0) {
		constexpr auto kWeek = Data::WorkingInterval::kWeek;
		const auto first = list.front();
		if (first.end > 0) {
			list.push_back({ first.start + kWeek, kWeek });
			list.front().start = 0;
		} else {
			list.push_back(first.shifted(kWeek));
			list.erase(list.begin());
		}
	}
	return intervals.normalized();
}

[[nodiscard]] object_ptr<Ui::SlideWrap<>> CreateWorkingHours(
		not_null<QWidget*> parent,
		not_null<UserData*> user) {
	using namespace Data;

	auto result = object_ptr<Ui::SlideWrap<Ui::RoundButton>>(
		parent,
		object_ptr<Ui::RoundButton>(
			parent,
			rpl::single(QString()),
			st::infoHoursOuter),
		st::infoProfileLabeledPadding - st::infoHoursOuterMargin);
	const auto button = result->entity();
	const auto inner = Ui::CreateChild<Ui::VerticalLayout>(button);
	button->widthValue() | rpl::start_with_next([=](int width) {
		const auto margin = st::infoHoursOuterMargin;
		inner->resizeToWidth(width - margin.left() - margin.right());
		inner->move(margin.left(), margin.top());
	}, inner->lifetime());
	inner->heightValue() | rpl::start_with_next([=](int height) {
		const auto margin = st::infoHoursOuterMargin;
		height += margin.top() + margin.bottom();
		button->resize(button->width(), height);
	}, inner->lifetime());

	const auto info = &user->owner().businessInfo();

	struct State {
		rpl::variable<WorkingHours> hours;
		rpl::variable<TimeId> time;
		rpl::variable<int> day;
		rpl::variable<int> timezoneDelta;

		rpl::variable<WorkingIntervals> mine;
		rpl::variable<WorkingIntervals> mineByDays;
		rpl::variable<TimeId> opensIn;
		rpl::variable<bool> opened;
		rpl::variable<bool> expanded;
		rpl::variable<bool> nonTrivial;
		rpl::variable<bool> myTimezone;

		rpl::event_stream<> recounts;
	};
	const auto state = inner->lifetime().make_state<State>();

	auto recounts = state->recounts.events_starting_with_copy(rpl::empty);
	const auto recount = [=] {
		state->recounts.fire({});
	};

	state->hours = user->session().changes().peerFlagsValue(
		user,
		PeerUpdate::Flag::BusinessDetails
	) | rpl::map([=] {
		return user->businessDetails().hours;
	});
	state->nonTrivial = state->hours.value() | rpl::map(AreNonTrivialHours);

	const auto seconds = QTime::currentTime().msecsSinceStartOfDay() / 1000;
	const auto inMinute = seconds % 60;
	const auto firstTick = inMinute ? (61 - inMinute) : 1;
	state->time = rpl::single(rpl::empty) | rpl::then(
		base::timer_once(firstTick * crl::time(1000))
	) | rpl::then(
		base::timer_each(60 * crl::time(1000))
	) | rpl::map([] {
		const auto local = QDateTime::currentDateTime();
		const auto day = local.date().dayOfWeek() - 1;
		const auto seconds = local.time().msecsSinceStartOfDay() / 1000;
		return day * kDay + seconds;
	});

	state->day = state->time.value() | rpl::map([](TimeId time) {
		return time / kDay;
	});
	state->timezoneDelta = rpl::combine(
		state->hours.value(),
		info->timezonesValue()
	) | rpl::filter([](
			const WorkingHours &hours,
			const Timezones &timezones) {
		return ranges::contains(
			timezones.list,
			hours.timezoneId,
			&Timezone::id);
	}) | rpl::map([](WorkingHours &&hours, const Timezones &timezones) {
		const auto &list = timezones.list;
		const auto closest = FindClosestTimezoneId(list);
		const auto i = ranges::find(list, closest, &Timezone::id);
		const auto j = ranges::find(list, hours.timezoneId, &Timezone::id);
		Assert(i != end(list));
		Assert(j != end(list));
		return i->utcOffset - j->utcOffset;
	});

	state->mine = rpl::combine(
		state->hours.value(),
		state->timezoneDelta.value()
	) | rpl::map([](WorkingHours &&hours, int delta) {
		return ShiftedIntervals(hours.intervals, delta);
	});

	state->opensIn = rpl::combine(
		state->mine.value(),
		state->time.value()
	) | rpl::map([](const WorkingIntervals &mine, TimeId time) {
		return OpensIn(mine, time);
	});
	state->opened = state->opensIn.value() | rpl::map(rpl::mappers::_1 == 0);

	state->mineByDays = rpl::combine(
		state->hours.value(),
		state->timezoneDelta.value()
	) | rpl::map([](WorkingHours &&hours, int delta) {
		auto full = std::array<bool, 7>();
		auto withoutFullDays = hours.intervals;
		for (auto i = 0; i != 7; ++i) {
			if (IsFullOpen(ExtractDayIntervals(hours.intervals, i))) {
				full[i] = true;
				withoutFullDays = ReplaceDayIntervals(
					withoutFullDays,
					i,
					Data::WorkingIntervals());
			}
		}
		auto result = ShiftedIntervals(withoutFullDays, delta);
		for (auto i = 0; i != 7; ++i) {
			if (full[i]) {
				result = ReplaceDayIntervals(
					result,
					i,
					Data::WorkingIntervals{ { { 0, kDay } } });
			}
		}
		return result;
	});

	const auto dayHoursText = [=](int day) {
		return rpl::combine(
			state->hours.value(),
			state->mineByDays.value(),
			state->myTimezone.value()
		) | rpl::map([=](
				const WorkingHours &hours,
				const WorkingIntervals &mine,
				bool my) {
			return FormatDayHours(hours, mine, my, day);
		});
	};
	const auto dayHoursTextValue = [=](rpl::producer<int> day) {
		return std::move(day)
			| rpl::map(dayHoursText)
			| rpl::flatten_latest();
	};

	const auto openedWrap = inner->add(object_ptr<Ui::RpWidget>(inner));
	const auto opened = Ui::CreateChild<Ui::FlatLabel>(
		openedWrap,
		rpl::conditional(
			state->opened.value(),
			tr::lng_info_work_open(),
			tr::lng_info_work_closed()
		) | rpl::after_next(recount),
		st::infoHoursState);
	opened->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto timing = Ui::CreateChild<Ui::FlatLabel>(
		openedWrap,
		OpensInText(
			state->opensIn.value(),
			state->expanded.value(),
			dayHoursTextValue(state->day.value())
		) | rpl::after_next(recount),
		st::infoHoursValue);
	timing->setAttribute(Qt::WA_TransparentForMouseEvents);
	state->opened.value() | rpl::start_with_next([=](bool value) {
		opened->setTextColorOverride(value
			? st::boxTextFgGood->c
			: st::boxTextFgError->c);
	}, opened->lifetime());

	rpl::combine(
		openedWrap->widthValue(),
		opened->heightValue(),
		timing->sizeValue()
	) | rpl::start_with_next([=](int width, int h1, QSize size) {
		opened->moveToLeft(0, 0, width);
		timing->moveToRight(0, 0, width);

		const auto margins = opened->getMargins();
		const auto added = margins.top() + margins.bottom();
		openedWrap->resize(width, std::max(h1, size.height()) - added);
	}, openedWrap->lifetime());

	const auto labelWrap = inner->add(object_ptr<Ui::RpWidget>(inner));
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		labelWrap,
		tr::lng_info_hours_label(),
		st::infoLabel);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto link = Ui::CreateChild<Ui::LinkButton>(
		labelWrap,
		QString());
	rpl::combine(
		state->nonTrivial.value(),
		state->hours.value(),
		state->mine.value(),
		state->myTimezone.value()
	) | rpl::map([=](
			bool complex,
			const WorkingHours &hours,
			const WorkingIntervals &mine,
			bool my) {
		return (!complex || hours.intervals == mine)
			? rpl::single(QString())
			: my
			? tr::lng_info_hours_my_time()
			: tr::lng_info_hours_local_time();
	}) | rpl::flatten_latest(
	) | rpl::start_with_next([=](const QString &text) {
		link->setText(text);
	}, link->lifetime());
	link->setClickedCallback([=] {
		state->myTimezone = !state->myTimezone.current();
		state->expanded = true;
	});

	rpl::combine(
		labelWrap->widthValue(),
		label->heightValue(),
		link->sizeValue()
	) | rpl::start_with_next([=](int width, int h1, QSize size) {
		label->moveToLeft(0, 0, width);
		link->moveToRight(0, 0, width);

		const auto margins = label->getMargins();
		const auto added = margins.top() + margins.bottom();
		labelWrap->resize(width, std::max(h1, size.height()) - added);
	}, labelWrap->lifetime());

	const auto other = inner->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			inner,
			object_ptr<Ui::VerticalLayout>(inner)));
	other->toggleOn(state->expanded.value(), anim::type::normal);
	other->finishAnimating();
	const auto days = other->entity();

	for (auto i = 1; i != 7; ++i) {
		const auto dayWrap = days->add(
			object_ptr<Ui::RpWidget>(other),
			QMargins(0, st::infoHoursDaySkip, 0, 0));
		auto label = state->day.value() | rpl::map([=](int day) {
			switch ((day + i) % 7) {
			case 0: return tr::lng_hours_monday();
			case 1: return tr::lng_hours_tuesday();
			case 2: return tr::lng_hours_wednesday();
			case 3: return tr::lng_hours_thursday();
			case 4: return tr::lng_hours_friday();
			case 5: return tr::lng_hours_saturday();
			case 6: return tr::lng_hours_sunday();
			}
			Unexpected("Index in working hours.");
		}) | rpl::flatten_latest();
		const auto dayLabel = Ui::CreateChild<Ui::FlatLabel>(
			dayWrap,
			std::move(label),
			st::infoHoursDayLabel);
		dayLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
		const auto dayHours = Ui::CreateChild<Ui::FlatLabel>(
			dayWrap,
			dayHoursTextValue(state->day.value()
				| rpl::map((rpl::mappers::_1 + i) % 7)),
			st::infoHoursValue);
		dayHours->setAttribute(Qt::WA_TransparentForMouseEvents);
		rpl::combine(
			dayWrap->widthValue(),
			dayLabel->heightValue(),
			dayHours->sizeValue()
		) | rpl::start_with_next([=](int width, int h1, QSize size) {
			dayLabel->moveToLeft(0, 0, width);
			dayHours->moveToRight(0, 0, width);

			const auto margins = dayLabel->getMargins();
			const auto added = margins.top() + margins.bottom();
			dayWrap->resize(width, std::max(h1, size.height()) - added);
		}, dayWrap->lifetime());
	}

	button->setClickedCallback([=] {
		state->expanded = !state->expanded.current();
	});

	result->toggleOn(state->hours.value(
	) | rpl::map([](const WorkingHours &data) {
		return bool(data);
	}));

	return result;
}

[[nodiscard]] object_ptr<Ui::SlideWrap<>> CreateBirthday(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<UserData*> user) {
	using namespace Data;

	auto result = object_ptr<Ui::SlideWrap<Ui::RoundButton>>(
		parent,
		object_ptr<Ui::RoundButton>(
			parent,
			rpl::single(QString()),
			st::infoHoursOuter),
		st::infoProfileLabeledPadding - st::infoHoursOuterMargin);
	result->setDuration(st::infoSlideDuration);
	const auto button = result->entity();

	auto outer = Ui::CreateChild<Ui::SlideWrap<Ui::VerticalLayout>>(
		button,
		object_ptr<Ui::VerticalLayout>(button),
		st::infoHoursOuterMargin);
	const auto layout = outer->entity();
	layout->setAttribute(Qt::WA_TransparentForMouseEvents);

	auto birthday = BirthdayValue(
		user
	) | rpl::start_spawning(result->lifetime());

	auto label = BirthdayLabelText(rpl::duplicate(birthday));
	auto text = BirthdayValueText(
		rpl::duplicate(birthday)
	) | Ui::Text::ToWithEntities();

	const auto giftIcon = Ui::CreateChild<Ui::RpWidget>(layout);
	giftIcon->resize(st::birthdayTodayIcon.size());
	layout->sizeValue() | rpl::start_with_next([=](QSize size) {
		giftIcon->moveToRight(
			0,
			(size.height() - giftIcon->height()) / 2,
			size.width());
	}, giftIcon->lifetime());
	giftIcon->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(giftIcon);
		st::birthdayTodayIcon.paint(p, 0, 0, giftIcon->width());
	}, giftIcon->lifetime());

	rpl::duplicate(
		birthday
	) | rpl::map([](Data::Birthday value) {
		return Data::IsBirthdayTodayValue(value);
	}) | rpl::flatten_latest(
	) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](bool today) {
		const auto disable = !today && user->session().premiumCanBuy();
		button->setDisabled(disable);
		button->setAttribute(Qt::WA_TransparentForMouseEvents, disable);
		button->clearState();
		giftIcon->setVisible(!disable);
	}, result->lifetime());

	auto nonEmptyText = std::move(
		text
	) | rpl::before_next([slide = result.data()](
			const TextWithEntities &value) {
		if (value.text.isEmpty()) {
			slide->hide(anim::type::normal);
		}
	}) | rpl::filter([](const TextWithEntities &value) {
		return !value.text.isEmpty();
	}) | rpl::after_next([slide = result.data()](
			const TextWithEntities &value) {
		slide->show(anim::type::normal);
	});
	layout->add(object_ptr<Ui::FlatLabel>(
		layout,
		std::move(nonEmptyText),
		st::birthdayLabeled));
	layout->add(Ui::CreateSkipWidget(layout, st::infoLabelSkip));
	layout->add(object_ptr<Ui::FlatLabel>(
		layout,
		std::move(
			label
		) | rpl::after_next([=] {
			layout->resizeToWidth(layout->widthNoMargins());
		}),
		st::birthdayLabel));
	result->finishAnimating();

	Ui::ResizeFitChild(button, outer);

	button->setClickedCallback([=] {
		if (!button->isDisabled()) {
			controller->showGiftPremiumsBox(user, u"birthday"_q);
		}
	});

	return result;
}

template <typename Text, typename ToggleOn, typename Callback>
auto AddActionButton(
		not_null<Ui::VerticalLayout*> parent,
		Text &&text,
		ToggleOn &&toggleOn,
		Callback &&callback,
		const style::icon *icon,
		const style::SettingsButton &st
			= st::infoSharedMediaButton) {
	auto result = parent->add(object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
		parent,
		object_ptr<Ui::SettingsButton>(
			parent,
			std::move(text),
			st))
	);
	result->setDuration(
		st::infoSlideDuration
	)->toggleOn(
		std::move(toggleOn)
	)->entity()->addClickHandler(std::move(callback));
	result->finishAnimating();
	if (icon) {
		object_ptr<Profile::FloatingIcon>(
			result,
			*icon,
			st::infoSharedMediaButtonIconPosition);
	}
	return result;
};

template <typename Text, typename ToggleOn, typename Callback>
[[nodiscard]] auto AddMainButton(
		not_null<Ui::VerticalLayout*> parent,
		Text &&text,
		ToggleOn &&toggleOn,
		Callback &&callback,
		Ui::MultiSlideTracker &tracker,
		const style::SettingsButton &st = st::infoMainButton) {
	tracker.track(AddActionButton(
		parent,
		std::move(text) | Ui::Text::ToUpper(),
		std::move(toggleOn),
		std::move(callback),
		nullptr,
		st));
}

class DetailsFiller {
public:
	DetailsFiller(
		not_null<Controller*> controller,
		not_null<Ui::RpWidget*> parent,
		not_null<PeerData*> peer,
		Origin origin);
	DetailsFiller(
		not_null<Controller*> controller,
		not_null<Ui::RpWidget*> parent,
		not_null<Data::ForumTopic*> topic);

	object_ptr<Ui::RpWidget> fill();

private:
	object_ptr<Ui::RpWidget> setupPersonalChannel(not_null<UserData*> user);
	object_ptr<Ui::RpWidget> setupInfo();
	object_ptr<Ui::RpWidget> setupMuteToggle();
	void setupMainButtons();
	Ui::MultiSlideTracker fillTopicButtons();
	Ui::MultiSlideTracker fillUserButtons(
		not_null<UserData*> user);
	Ui::MultiSlideTracker fillChannelButtons(
		not_null<ChannelData*> channel);

	void addReportReaction(Ui::MultiSlideTracker &tracker);
	void addReportReaction(
		GroupReactionOrigin data,
		bool ban,
		Ui::MultiSlideTracker &tracker);

	template <
		typename Widget,
		typename = std::enable_if_t<
		std::is_base_of_v<Ui::RpWidget, Widget>>>
	Widget *add(
			object_ptr<Widget> &&child,
			const style::margins &margin = style::margins()) {
		return _wrap->add(
			std::move(child),
			margin);
	}

	not_null<Controller*> _controller;
	not_null<Ui::RpWidget*> _parent;
	not_null<PeerData*> _peer;
	Data::ForumTopic *_topic = nullptr;
	Origin _origin;
	object_ptr<Ui::VerticalLayout> _wrap;

};

class ActionsFiller {
public:
	ActionsFiller(
		not_null<Controller*> controller,
		not_null<Ui::RpWidget*> parent,
		not_null<PeerData*> peer);

	object_ptr<Ui::RpWidget> fill();

private:
	void addInviteToGroupAction(not_null<UserData*> user);
	void addShareContactAction(not_null<UserData*> user);
	void addEditContactAction(not_null<UserData*> user);
	void addDeleteContactAction(not_null<UserData*> user);
	void addBotCommandActions(not_null<UserData*> user);
	void addReportAction();
	void addBlockAction(not_null<UserData*> user);
	void addLeaveChannelAction(not_null<ChannelData*> channel);
	void addJoinChannelAction(not_null<ChannelData*> channel);
	void fillUserActions(not_null<UserData*> user);
	void fillChannelActions(not_null<ChannelData*> channel);

	not_null<Controller*> _controller;
	not_null<Ui::RpWidget*> _parent;
	not_null<PeerData*> _peer;
	object_ptr<Ui::VerticalLayout> _wrap = { nullptr };

};

void ReportReactionBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> participant,
		GroupReactionOrigin data,
		bool ban,
		Fn<void()> sent) {
	box->setTitle(tr::lng_report_reaction_title());
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		tr::lng_report_reaction_about(),
		st::boxLabel));
	const auto check = ban
		? box->addRow(
			object_ptr<Ui::Checkbox>(
				box,
				tr::lng_report_and_ban_button(tr::now),
				true),
			st::boxRowPadding + QMargins{ 0, st::boxLittleSkip, 0, 0 })
		: nullptr;
	box->addButton(tr::lng_report_button(), [=] {
		const auto chat = data.group->asChat();
		const auto channel = data.group->asMegagroup();
		if (check && check->checked()) {
			if (chat) {
				chat->session().api().chatParticipants().kick(
					chat,
					participant);
			} else if (channel) {
				channel->session().api().chatParticipants().kick(
					channel,
					participant,
					ChatRestrictionsInfo());
			}
		}
		data.group->session().api().request(MTPmessages_ReportReaction(
			data.group->input,
			MTP_int(data.messageId.bare),
			participant->input
		)).done(crl::guard(controller, [=] {
			controller->showToast(tr::lng_report_thanks(tr::now));
		})).send();
		sent();
		box->closeBox();
	}, st::attentionBoxButton);
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

DetailsFiller::DetailsFiller(
	not_null<Controller*> controller,
	not_null<Ui::RpWidget*> parent,
	not_null<PeerData*> peer,
	Origin origin)
: _controller(controller)
, _parent(parent)
, _peer(peer)
, _origin(origin)
, _wrap(_parent) {
}

DetailsFiller::DetailsFiller(
	not_null<Controller*> controller,
	not_null<Ui::RpWidget*> parent,
	not_null<Data::ForumTopic*> topic)
: _controller(controller)
, _parent(parent)
, _peer(topic->peer())
, _topic(topic)
, _wrap(_parent) {
}

template <typename T>
bool SetClickContext(
		const ClickHandlerPtr &handler,
		const ClickContext &context) {
	if (const auto casted = std::dynamic_pointer_cast<T>(handler)) {
		casted->T::onClick(context);
		return true;
	}
	return false;
}

object_ptr<Ui::RpWidget> DetailsFiller::setupInfo() {
	auto result = object_ptr<Ui::VerticalLayout>(_wrap);
	auto tracker = Ui::MultiSlideTracker();

	// Fill context for a mention / hashtag / bot command link.
	const auto infoClickFilter = [=,
		peer = _peer.get(),
		window = _controller->parentController()](
			const ClickHandlerPtr &handler,
			Qt::MouseButton button) {
		const auto context = ClickContext{
			button,
			QVariant::fromValue(ClickHandlerContext{
				.sessionWindow = base::make_weak(window),
				.peer = peer,
			})
		};
		if (SetClickContext<BotCommandClickHandler>(handler, context)) {
			return false;
		} else if (SetClickContext<MentionClickHandler>(handler, context)) {
			return false;
		} else if (SetClickContext<HashtagClickHandler>(handler, context)) {
			return false;
		} else if (SetClickContext<CashtagClickHandler>(handler, context)) {
			return false;
		} else if (SetClickContext<UrlClickHandler>(handler, context)) {
			return false;
		}
		return true;
	};

	const auto addTranslateToMenu = [&,
			peer = _peer.get(),
			controller = _controller->parentController()](
			not_null<Ui::FlatLabel*> label,
			rpl::producer<TextWithEntities> &&text) {
		struct State {
			rpl::variable<TextWithEntities> labelText;
		};
		const auto state = label->lifetime().make_state<State>();
		state->labelText = std::move(text);
		label->setContextMenuHook([=](
				Ui::FlatLabel::ContextMenuRequest request) {
			label->fillContextMenu(request);
			if (Ui::SkipTranslate(state->labelText.current())) {
				return;
			}
			auto item = (request.selection.empty()
				? tr::lng_context_translate
				: tr::lng_context_translate_selected)(tr::now);
			request.menu->addAction(std::move(item), [=] {
				controller->window().show(Box(
					Ui::TranslateBox,
					peer,
					MsgId(),
					request.selection.empty()
						? state->labelText.current()
						: Ui::Text::Mid(
							state->labelText.current(),
							request.selection.from,
							request.selection.to - request.selection.from),
					false));
			});
		});
	};

	const auto addInfoLineGeneric = [&](
			v::text::data &&label,
			rpl::producer<TextWithEntities> &&text,
			const style::FlatLabel &textSt = st::infoLabeled,
			const style::margins &padding = st::infoProfileLabeledPadding) {
		auto line = CreateTextWithLabel(
			result,
			v::text::take_marked(std::move(label)),
			std::move(text),
			st::infoLabel,
			textSt,
			padding);
		tracker.track(result->add(std::move(line.wrap)));

		line.text->setClickHandlerFilter(infoClickFilter);
		return line;
	};
	const auto addInfoLine = [&](
			v::text::data &&label,
			rpl::producer<TextWithEntities> &&text,
			const style::FlatLabel &textSt = st::infoLabeled,
			const style::margins &padding = st::infoProfileLabeledPadding) {
		return addInfoLineGeneric(
			std::move(label),
			std::move(text),
			textSt,
			padding);
	};
	const auto addInfoOneLine = [&](
			v::text::data &&label,
			rpl::producer<TextWithEntities> &&text,
			const QString &contextCopyText,
			const style::margins &padding = st::infoProfileLabeledPadding) {
		auto result = addInfoLine(
			std::move(label),
			std::move(text),
			st::infoLabeledOneLine,
			padding);
		result.text->setDoubleClickSelectsParagraph(true);
		result.text->setContextCopyText(contextCopyText);
		return result;
	};
	if (const auto user = _peer->asUser()) {
		const auto controller = _controller->parentController();
		if (user->session().supportMode()) {
			addInfoLineGeneric(
				user->session().supportHelper().infoLabelValue(user),
				user->session().supportHelper().infoTextValue(user));
		}

		{
			const auto phoneLabel = addInfoOneLine(
				tr::lng_info_mobile_label(),
				PhoneOrHiddenValue(user),
				tr::lng_profile_copy_phone(tr::now)).text;
			const auto hook = [=](Ui::FlatLabel::ContextMenuRequest request) {
				phoneLabel->fillContextMenu(request);
				AddPhoneMenu(request.menu, user);
			};
			phoneLabel->setContextMenuHook(hook);
		}
		auto label = user->isBot()
			? tr::lng_info_about_label()
			: tr::lng_info_bio_label();
		addTranslateToMenu(
			addInfoLine(std::move(label), AboutWithIdValue(user)).text,
			AboutWithIdValue(user));

		const auto usernameLine = addInfoOneLine(
			UsernamesSubtext(_peer, tr::lng_info_username_label()),
			UsernameValue(user, true) | rpl::map([=](TextWithEntities u) {
				return u.text.isEmpty()
					? TextWithEntities()
					: Ui::Text::Link(u, UsernameUrl(user, u.text.mid(1)));
			}),
			QString(),
			st::infoProfileLabeledUsernamePadding);
		const auto callback = UsernamesLinkCallback(
			_peer,
			controller,
			QString());
		const auto hook = [=](Ui::FlatLabel::ContextMenuRequest request) {
			if (!request.link) {
				return;
			}
			const auto text = request.link->copyToClipboardContextItemText();
			if (text.isEmpty()) {
				return;
			}
			const auto link = request.link->copyToClipboardText();
			request.menu->addAction(
				text,
				[=] { QGuiApplication::clipboard()->setText(link); });
			const auto last = link.lastIndexOf('/');
			if (last < 0) {
				return;
			}
			const auto mention = '@' + link.mid(last + 1);
			if (mention.size() < 2) {
				return;
			}
			request.menu->addAction(
				tr::lng_context_copy_mention(tr::now),
				[=] { QGuiApplication::clipboard()->setText(mention); });
		};
		usernameLine.text->overrideLinkClickHandler(callback);
		usernameLine.subtext->overrideLinkClickHandler(callback);
		usernameLine.text->setContextMenuHook(hook);
		usernameLine.subtext->setContextMenuHook(hook);
		const auto usernameLabel = usernameLine.text;
		if (user->isBot()) {
			const auto copyUsername = Ui::CreateChild<Ui::IconButton>(
				usernameLabel->parentWidget(),
				st::infoProfileLabeledButtonCopy);
			result->sizeValue(
			) | rpl::start_with_next([=] {
				const auto s = usernameLabel->parentWidget()->size();
				copyUsername->moveToRight(
					0,
					(s.height() - copyUsername->height()) / 2);
			}, copyUsername->lifetime());
			copyUsername->setClickedCallback([=] {
				const auto link = user->session().createInternalLinkFull(
					user->username());
				if (!link.isEmpty()) {
					QGuiApplication::clipboard()->setText(link);
					controller->showToast(tr::lng_username_copied(tr::now));
				}
				return false;
			});
		} else {
			tracker.track(result->add(
				CreateBirthday(result, controller, user)));
			tracker.track(result->add(CreateWorkingHours(result, user)));

			auto locationText = user->session().changes().peerFlagsValue(
				user,
				Data::PeerUpdate::Flag::BusinessDetails
			) | rpl::map([=] {
				const auto &details = user->businessDetails();
				if (!details.location) {
					return TextWithEntities();
				} else if (!details.location.point) {
					return TextWithEntities{ details.location.address };
				}
				return Ui::Text::Link(
					TextUtilities::SingleLine(details.location.address),
					LocationClickHandler::Url(*details.location.point));
			});
			addInfoOneLine(
				tr::lng_info_location_label(),
				std::move(locationText),
				QString()
			).text->setLinksTrusted();
		}

		AddMainButton(
			result,
			tr::lng_info_add_as_contact(),
			CanAddContactValue(user),
			[=] { controller->window().show(Box(EditContactBox, controller, user)); },
			tracker);
	} else {
		const auto topicRootId = _topic ? _topic->rootId() : 0;
		const auto addToLink = topicRootId
			? ('/' + QString::number(topicRootId.bare))
			: QString();
		auto linkText = LinkValue(
			_peer,
			true
		) | rpl::map([=](const LinkWithUrl &link) {
			const auto text = link.text;
			return text.isEmpty()
				? TextWithEntities()
				: Ui::Text::Link(
					(text.startsWith(u"https://"_q)
						? text.mid(u"https://"_q.size())
						: text) + addToLink,
					(addToLink.isEmpty() ? link.url : (text + addToLink)));
		});
		auto linkLine = addInfoOneLine(
			(topicRootId
				? tr::lng_info_link_label(Ui::Text::WithEntities)
				: UsernamesSubtext(_peer, tr::lng_info_link_label())),
			std::move(linkText),
			QString());
		const auto controller = _controller->parentController();
		const auto linkCallback = UsernamesLinkCallback(
			_peer,
			controller,
			addToLink);
		linkLine.text->overrideLinkClickHandler(linkCallback);
		linkLine.subtext->overrideLinkClickHandler(linkCallback);

		if (const auto channel = _topic ? nullptr : _peer->asChannel()) {
			auto locationText = LocationValue(
				channel
			) | rpl::map([](const ChannelLocation *location) {
				return location
					? Ui::Text::Link(
						TextUtilities::SingleLine(location->address),
						LocationClickHandler::Url(location->point))
					: TextWithEntities();
			});
			addInfoOneLine(
				tr::lng_info_location_label(),
				std::move(locationText),
				QString()
			).text->setLinksTrusted();
		}

		const auto about = addInfoLine(tr::lng_info_about_label(), _topic
			? rpl::single(TextWithEntities())
			: AboutWithIdValue(_peer));
		if (!_topic) {
			addTranslateToMenu(about.text, AboutWithIdValue(_peer));
		}
	}
	if (!_peer->isSelf()) {
		// No notifications toggle for Self => no separator.
		result->add(object_ptr<Ui::SlideWrap<>>(
			result,
			object_ptr<Ui::PlainShadow>(result),
			st::infoProfileSeparatorPadding)
		)->setDuration(
			st::infoSlideDuration
		)->toggleOn(
			std::move(tracker).atLeastOneShownValue()
		);
	}
	object_ptr<FloatingIcon>(
		result,
		st::infoIconInformation,
		st::infoInformationIconPosition);

	return result;
}

object_ptr<Ui::RpWidget> DetailsFiller::setupPersonalChannel(
		not_null<UserData*> user) {
	auto result = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		_wrap,
		object_ptr<Ui::VerticalLayout>(_wrap));
	const auto container = result->entity();
	const auto window = _controller->parentController();

	result->toggleOn(PersonalChannelValue(
		user
	) | rpl::map(rpl::mappers::_1 != nullptr));
	result->finishAnimating();

	auto channelToggleValue = PersonalChannelValue(
		user
	) | rpl::map([=] { return !!user->personalChannelId(); });
	auto channel = PersonalChannelValue(
		user
	) | rpl::start_spawning(result->lifetime());

	const auto channelLabelFactory = [=](rpl::producer<ChannelData*> c) {
		return rpl::combine(
			tr::lng_info_personal_channel_label(Ui::Text::WithEntities),
			std::move(c)
		) | rpl::map([](TextWithEntities &&text, ChannelData *channel) {
			const auto count = channel ? channel->membersCount() : 0;
			if (count > 1) {
				text.append(
					QString::fromUtf8(" \xE2\x80\xA2 ")
				).append(tr::lng_chat_status_subscribers(
					tr::now,
					lt_count_decimal,
					count));
			}
			return text;
		});
	};

	{
		const auto onlyChannelWrap = container->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				container,
				object_ptr<Ui::VerticalLayout>(container)));
		onlyChannelWrap->toggleOn(rpl::duplicate(channelToggleValue)
			| rpl::map(!rpl::mappers::_1));
		onlyChannelWrap->finishAnimating();

		Ui::AddDivider(onlyChannelWrap->entity());

		auto text = rpl::duplicate(
			channel
		) | rpl::map([=](ChannelData *channel) {
			return channel ? NameValue(channel) : rpl::single(QString());
		}) | rpl::flatten_latest() | rpl::map([](const QString &name) {
			return name.isEmpty() ? TextWithEntities() : Ui::Text::Link(name);
		});
		auto line = CreateTextWithLabel(
			result,
			channelLabelFactory(rpl::duplicate(channel)),
			std::move(text),
			st::infoLabel,
			st::infoLabeled,
			st::infoProfileLabeledPadding);
		onlyChannelWrap->entity()->add(std::move(line.wrap));

		line.text->setClickHandlerFilter([=](
				const ClickHandlerPtr &handler,
				Qt::MouseButton button) {
			if (const auto channelId = user->personalChannelId()) {
				window->showPeerInfo(peerFromChannel(channelId));
			}
			return false;
		});

		object_ptr<FloatingIcon>(
			onlyChannelWrap,
			st::infoIconMediaChannel,
			st::infoPersonalChannelIconPosition);
	}

	{
		const auto messageChannelWrap = container->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				container,
				object_ptr<Ui::VerticalLayout>(container)));
		messageChannelWrap->toggleOn(rpl::duplicate(channelToggleValue));
		messageChannelWrap->finishAnimating();

		const auto clear = [=] {
			while (messageChannelWrap->entity()->count()) {
				delete messageChannelWrap->entity()->widgetAt(0);
			}
		};

		const auto rebuild = [=](
				not_null<HistoryItem*> item,
				anim::type animated) {
			const auto &stUserpic = st::infoPersonalChannelUserpic;
			const auto &stLabeled = st::infoProfileLabeledPadding;

			messageChannelWrap->toggle(false, anim::type::instant);
			clear();
			Ui::AddDivider(messageChannelWrap->entity());
			Ui::AddSkip(messageChannelWrap->entity());

			const auto inner = messageChannelWrap->entity()->add(
				object_ptr<Ui::VerticalLayout>(messageChannelWrap->entity()));

			const auto line = inner->add(
				object_ptr<Ui::FixedHeightWidget>(
					inner,
					stUserpic.photoSize + rect::m::sum::v(stLabeled)));
			const auto userpic = Ui::CreateChild<Ui::UserpicButton>(
				line,
				item->history()->peer,
				st::infoPersonalChannelUserpic);

			userpic->moveToLeft(
				-st::infoPersonalChannelUserpicSkip
					+ (stLabeled.left() - stUserpic.photoSize) / 2,
				stLabeled.top());
			userpic->setAttribute(Qt::WA_TransparentForMouseEvents);

			const auto date = Ui::CreateChild<Ui::FlatLabel>(
				line,
				Ui::FormatDialogsDate(ItemDateTime(item)),
				st::infoPersonalChannelDateLabel);

			const auto name = Ui::CreateChild<Ui::FlatLabel>(
				line,
				NameValue(item->history()->peer),
				st::infoPersonalChannelNameLabel);

			const auto preview = Ui::CreateChild<Ui::RpWidget>(line);
			auto &lifetime = preview->lifetime();
			using namespace Dialogs::Ui;
			const auto previewView = lifetime.make_state<MessageView>();
			const auto previewUpdate = [=] { preview->update(); };
			preview->resize(0, st::infoLabeled.style.font->height);
			if (!previewView->dependsOn(item)) {
				previewView->prepare(item, nullptr, previewUpdate, {});
			}
			preview->paintRequest(
			) | rpl::start_with_next([=, fullId = item->fullId()](
					const QRect &rect) {
				auto p = Painter(preview);
				const auto item = user->session().data().message(fullId);
				if (!item) {
					p.setPen(st::infoPersonalChannelDateLabel.textFg);
					p.setBrush(Qt::NoBrush);
					p.setFont(st::infoPersonalChannelDateLabel.style.font);
					p.drawText(
						preview->rect(),
						tr::lng_deleted_message(tr::now),
						style::al_left);
					return;
				}
				if (previewView->prepared(item, nullptr)) {
					previewView->paint(p, preview->rect(), {
						.st = &st::defaultDialogRow,
						.currentBg = st::boxBg->b,
					});
				} else if (!previewView->dependsOn(item)) {
					p.setPen(st::infoPersonalChannelDateLabel.textFg);
					p.setBrush(Qt::NoBrush);
					p.setFont(st::infoPersonalChannelDateLabel.style.font);
					p.drawText(
						preview->rect(),
						tr::lng_contacts_loading(tr::now),
						style::al_left);
					previewView->prepare(item, nullptr, previewUpdate, {});
					preview->update();
				}
			}, preview->lifetime());

			line->sizeValue(
			) | rpl::start_with_next([=](const QSize &size) {
				const auto left = stLabeled.left();
				const auto right = st::infoPersonalChannelDateSkip;
				const auto top = stLabeled.top();
				date->moveToRight(right, top);

				name->resizeToWidth(size.width()
					- left
					- date->width()
					- st::defaultVerticalListSkip
					- right);
				name->moveToLeft(left, top);

				preview->resize(
					size.width() - left - right,
					st::infoLabeled.style.font->height);
				preview->moveToLeft(
					left,
					size.height() - stLabeled.bottom() - preview->height());
			}, preview->lifetime());

			{
				inner->add(
					object_ptr<Ui::FlatLabel>(
						inner,
						channelLabelFactory(
							rpl::single(item->history()->peer->asChannel())),
						st::infoLabel),
					QMargins(
						st::infoProfileLabeledPadding.left(),
						0,
						st::infoProfileLabeledPadding.right(),
						st::infoProfileLabeledPadding.bottom()));
			}
			{
				const auto button = Ui::CreateChild<Ui::RippleButton>(
					messageChannelWrap->entity(),
					st::defaultRippleAnimation);
				button->paintRequest(
				) | rpl::start_with_next([=](const QRect &rect) {
					auto p = QPainter(button);
					button->paintRipple(p, 0, 0);
				}, button->lifetime());
				inner->geometryValue(
				) | rpl::start_with_next([=](const QRect &rect) {
					button->setGeometry(rect);
				}, button->lifetime());
				button->setClickedCallback([=, msg = item->fullId().msg] {
					window->showPeerHistory(
						item->history()->peer,
						Window::SectionShow::Way::Forward,
						msg);
				});
				button->lower();
			}
			inner->setAttribute(Qt::WA_TransparentForMouseEvents);
			Ui::AddSkip(messageChannelWrap->entity());

			Ui::ToggleChildrenVisibility(messageChannelWrap->entity(), true);
			Ui::ToggleChildrenVisibility(line, true);
			messageChannelWrap->toggle(true, animated);
		};

		rpl::duplicate(
			channel
		) | rpl::start_with_next([=](ChannelData *channel) {
			clear();
			if (!channel) {
				return;
			}
			const auto id = FullMsgId(
				channel->id,
				user->personalChannelMessageId());
			if (const auto item = user->session().data().message(id)) {
				return rebuild(item, anim::type::instant);
			}
			user->session().api().requestMessageData(
				channel,
				user->personalChannelMessageId(),
				crl::guard(container, [=] {
					if (const auto i = user->session().data().message(id)) {
						rebuild(i, anim::type::normal);
					}
				}));
		}, messageChannelWrap->lifetime());
	}

	return result;
}

object_ptr<Ui::RpWidget> DetailsFiller::setupMuteToggle() {
	const auto peer = _peer;
	const auto topicRootId = _topic ? _topic->rootId() : MsgId();
	const auto makeThread = [=] {
		return topicRootId
			? static_cast<Data::Thread*>(peer->forumTopicFor(topicRootId))
			: peer->owner().history(peer).get();
	};
	auto result = object_ptr<Ui::SettingsButton>(
		_wrap,
		tr::lng_profile_enable_notifications(),
		st::infoNotificationsButton);
	result->toggleOn(_topic
		? NotificationsEnabledValue(_topic)
		: NotificationsEnabledValue(peer), true);
	result->setAcceptBoth();
	const auto notifySettings = &peer->owner().notifySettings();
	MuteMenu::SetupMuteMenu(
		result.data(),
		result->clicks(
		) | rpl::filter([=](Qt::MouseButton button) {
			if (button == Qt::RightButton) {
				return true;
			}
			const auto topic = topicRootId
				? peer->forumTopicFor(topicRootId)
				: nullptr;
			Assert(!topicRootId || topic != nullptr);
			const auto is = topic
				? notifySettings->isMuted(topic)
				: notifySettings->isMuted(peer);
			if (is) {
				if (topic) {
					notifySettings->update(topic, { .unmute = true });
				} else {
					notifySettings->update(peer, { .unmute = true });
				}
				return false;
			} else {
				return true;
			}
		}) | rpl::to_empty,
		makeThread,
		_controller->uiShow());
	object_ptr<FloatingIcon>(
		result,
		st::infoIconNotifications,
		st::infoNotificationsIconPosition);
	return result;
}

void DetailsFiller::setupMainButtons() {
	auto wrapButtons = [=](auto &&callback) {
		auto topSkip = _wrap->add(CreateSlideSkipWidget(_wrap));
		auto tracker = callback();
		topSkip->toggleOn(std::move(tracker).atLeastOneShownValue());
	};
	if (_topic) {
		wrapButtons([=] {
			return fillTopicButtons();
		});
	} else if (const auto user = _peer->asUser()) {
		wrapButtons([=] {
			return fillUserButtons(user);
		});
	} else if (const auto channel = _peer->asChannel()) {
		if (!channel->isMegagroup()) {
			wrapButtons([=] {
				return fillChannelButtons(channel);
			});
		}
	}
}

void DetailsFiller::addReportReaction(Ui::MultiSlideTracker &tracker) {
	v::match(_origin.data, [&](GroupReactionOrigin data) {
		const auto user = _peer->asUser();
		if (_peer->isSelf()) {
			return;
#if 0 // Only public groups allow reaction reports for now.
		} else if (const auto chat = data.group->asChat()) {
			const auto ban = chat->canBanMembers()
				&& (!user || !chat->admins.contains(_peer))
				&& (!user || chat->creator != user->id);
			addReportReaction(data, ban, tracker);
#endif
		} else if (const auto channel = data.group->asMegagroup()) {
			if (channel->isPublic()) {
				const auto ban = channel->canBanMembers()
					&& (!user || !channel->mgInfo->admins.contains(user->id))
					&& (!user || channel->mgInfo->creator != user);
				addReportReaction(data, ban, tracker);
			}
		}
	}, [](const auto &) {});
}

void DetailsFiller::addReportReaction(
		GroupReactionOrigin data,
		bool ban,
		Ui::MultiSlideTracker &tracker) {
	const auto peer = _peer;
	if (!peer) {
		return;
	}
	const auto controller = _controller->parentController();
	const auto forceHidden = std::make_shared<rpl::variable<bool>>(false);
	const auto user = peer->asUser();
	auto shown = user
		? rpl::combine(
			Info::Profile::IsContactValue(user),
			forceHidden->value(),
			!rpl::mappers::_1 && !rpl::mappers::_2
		) | rpl::type_erased()
		: (forceHidden->value() | rpl::map(!rpl::mappers::_1));
	const auto sent = [=] {
		*forceHidden = true;
	};
	AddMainButton(
		_wrap,
		(ban
			? tr::lng_report_and_ban()
			: tr::lng_report_reaction()),
		std::move(shown),
		[=] { controller->show(
			Box(ReportReactionBox, controller, peer, data, ban, sent)); },
		tracker,
		st::infoMainButtonAttention);
}

Ui::MultiSlideTracker DetailsFiller::fillTopicButtons() {
	using namespace rpl::mappers;

	Ui::MultiSlideTracker tracker;
	const auto window = _controller->parentController();

	const auto forum = _topic->forum();
	auto showTopicsVisible = rpl::combine(
		window->adaptive().oneColumnValue(),
		window->shownForum().value(),
		_1 || (_2 != forum));
	AddMainButton(
		_wrap,
		tr::lng_forum_show_topics_list(),
		std::move(showTopicsVisible),
		[=] { window->showForum(forum); },
		tracker);
	return tracker;
}

Ui::MultiSlideTracker DetailsFiller::fillUserButtons(
		not_null<UserData*> user) {
	using namespace rpl::mappers;

	Ui::MultiSlideTracker tracker;
	auto window = _controller->parentController();

	auto addSendMessageButton = [&] {
		auto activePeerValue = window->activeChatValue(
		) | rpl::map([](Dialogs::Key key) {
			return key.peer();
		});
		auto sendMessageVisible = rpl::combine(
			_controller->wrapValue(),
			std::move(activePeerValue),
			(_1 != Wrap::Side) || (_2 != user));
		auto sendMessage = [window, user] {
			window->showPeerHistory(
				user,
				Window::SectionShow::Way::Forward);
		};
		AddMainButton(
			_wrap,
			tr::lng_profile_send_message(),
			std::move(sendMessageVisible),
			std::move(sendMessage),
			tracker);
	};

	if (user->isSelf()) {
		auto separator = _wrap->add(object_ptr<Ui::SlideWrap<>>(
			_wrap,
			object_ptr<Ui::PlainShadow>(_wrap),
			st::infoProfileSeparatorPadding)
		)->setDuration(
			st::infoSlideDuration
		);

		addSendMessageButton();

		separator->toggleOn(
			std::move(tracker).atLeastOneShownValue()
		);
	} else {
		addSendMessageButton();
	}

	addReportReaction(tracker);

	return tracker;
}

Ui::MultiSlideTracker DetailsFiller::fillChannelButtons(
		not_null<ChannelData*> channel) {
	using namespace rpl::mappers;

	Ui::MultiSlideTracker tracker;
	auto window = _controller->parentController();
	auto activePeerValue = window->activeChatValue(
	) | rpl::map([](Dialogs::Key key) {
		return key.peer();
	});
	auto viewChannelVisible = rpl::combine(
		_controller->wrapValue(),
		std::move(activePeerValue),
		(_1 != Wrap::Side) || (_2 != channel));
	auto viewChannel = [=] {
		window->showPeerHistory(
			channel,
			Window::SectionShow::Way::Forward);
	};
	AddMainButton(
		_wrap,
		tr::lng_profile_view_channel(),
		std::move(viewChannelVisible),
		std::move(viewChannel),
		tracker);

	return tracker;
}

object_ptr<Ui::RpWidget> DetailsFiller::fill() {
	Expects(!_topic || !_topic->creating());

	if (const auto user = _peer->asUser()) {
		add(setupPersonalChannel(user));
	}
	add(object_ptr<Ui::BoxContentDivider>(_wrap));
	add(CreateSkipWidget(_wrap));
	add(setupInfo());
	if (!_peer->isSelf()) {
		add(setupMuteToggle());
	}
	setupMainButtons();
	add(CreateSkipWidget(_wrap));

	return std::move(_wrap);
}

ActionsFiller::ActionsFiller(
	not_null<Controller*> controller,
	not_null<Ui::RpWidget*> parent,
	not_null<PeerData*> peer)
: _controller(controller)
, _parent(parent)
, _peer(peer) {
}

void ActionsFiller::addInviteToGroupAction(
		not_null<UserData*> user) {
	const auto notEmpty = [](const QString &value) {
		return !value.isEmpty();
	};
	const auto controller = _controller->parentController();
	AddActionButton(
		_wrap,
		InviteToChatButton(user) | rpl::filter(notEmpty),
		InviteToChatButton(user) | rpl::map(notEmpty),
		[=] { AddBotToGroupBoxController::Start(controller, user); },
		&st::infoIconAddMember);
	const auto about = _wrap->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			_wrap.data(),
			object_ptr<Ui::VerticalLayout>(_wrap.data())));
	about->toggleOn(InviteToChatAbout(user) | rpl::map(notEmpty));
	Ui::AddSkip(about->entity());
	Ui::AddDividerText(
		about->entity(),
		InviteToChatAbout(user) | rpl::filter(notEmpty));
	Ui::AddSkip(about->entity());
	about->finishAnimating();
}

void ActionsFiller::addShareContactAction(not_null<UserData*> user) {
	const auto controller = _controller->parentController();
	AddActionButton(
		_wrap,
		tr::lng_info_share_contact(),
		CanShareContactValue(user),
		[=] { Window::PeerMenuShareContactBox(controller, user); },
		&st::infoIconShare);
}

void ActionsFiller::addEditContactAction(not_null<UserData*> user) {
	const auto controller = _controller->parentController();
	AddActionButton(
		_wrap,
		tr::lng_info_edit_contact(),
		IsContactValue(user),
		[=] { controller->window().show(Box(EditContactBox, controller, user)); },
		&st::infoIconEdit);
}

void ActionsFiller::addDeleteContactAction(not_null<UserData*> user) {
	const auto controller = _controller->parentController();
	AddActionButton(
		_wrap,
		tr::lng_info_delete_contact(),
		IsContactValue(user),
		[=] { Window::PeerMenuDeleteContact(controller, user); },
		&st::infoIconDelete);
}

void ActionsFiller::addBotCommandActions(not_null<UserData*> user) {
	auto findBotCommand = [user](const QString &command) {
		if (!user->isBot()) {
			return QString();
		}
		for (const auto &data : user->botInfo->commands) {
			const auto isSame = !data.command.compare(
				command,
				Qt::CaseInsensitive);
			if (isSame) {
				return data.command;
			}
		}
		return QString();
	};
	auto hasBotCommandValue = [=](const QString &command) {
		return user->session().changes().peerFlagsValue(
			user,
			Data::PeerUpdate::Flag::BotCommands
		) | rpl::map([=] {
			return !findBotCommand(command).isEmpty();
		});
	};
	auto sendBotCommand = [=, window = _controller->parentController()](
			const QString &command) {
		const auto original = findBotCommand(command);
		if (original.isEmpty()) {
			return;
		}
		BotCommandClickHandler('/' + original).onClick(ClickContext{
			Qt::LeftButton,
			QVariant::fromValue(ClickHandlerContext{
				.sessionWindow = base::make_weak(window),
				.peer = user,
			})
		});
	};
	auto addBotCommand = [=](
			rpl::producer<QString> text,
			const QString &command,
			const style::icon *icon = nullptr) {
		AddActionButton(
			_wrap,
			std::move(text),
			hasBotCommandValue(command),
			[=] { sendBotCommand(command); },
			icon);
	};
	addBotCommand(
		tr::lng_profile_bot_help(),
		u"help"_q,
		&st::infoIconInformation);
	addBotCommand(tr::lng_profile_bot_settings(), u"settings"_q);
	addBotCommand(tr::lng_profile_bot_privacy(), u"privacy"_q);
}

void ActionsFiller::addReportAction() {
	const auto peer = _peer;
	const auto controller = _controller->parentController();
	const auto report = [=] {
		ShowReportPeerBox(controller, peer);
	};
	AddActionButton(
		_wrap,
		tr::lng_profile_report(),
		rpl::single(true),
		report,
		&st::infoIconReport,
		st::infoBlockButton);
}

void ActionsFiller::addBlockAction(not_null<UserData*> user) {
	const auto controller = _controller->parentController();
	const auto window = &controller->window();

	auto text = user->session().changes().peerFlagsValue(
		user,
		Data::PeerUpdate::Flag::IsBlocked
	) | rpl::map([=] {
		switch (user->blockStatus()) {
		case UserData::BlockStatus::Blocked:
			return ((user->isBot() && !user->isSupport())
				? tr::lng_profile_restart_bot
				: tr::lng_profile_unblock_user)();
		case UserData::BlockStatus::NotBlocked:
		default:
			return ((user->isBot() && !user->isSupport())
				? tr::lng_profile_block_bot
				: tr::lng_profile_block_user)();
		}
	}) | rpl::flatten_latest(
	) | rpl::start_spawning(_wrap->lifetime());

	auto toggleOn = rpl::duplicate(
		text
	) | rpl::map([](const QString &text) {
		return !text.isEmpty();
	});
	auto callback = [=] {
		if (user->isBlocked()) {
			const auto show = controller->uiShow();
			Window::PeerMenuUnblockUserWithBotRestart(show, user);
			if (user->isBot()) {
				controller->showPeerHistory(user);
			}
		} else if (user->isBot()) {
			user->session().api().blockedPeers().block(user);
		} else {
			window->show(Box(
				Window::PeerMenuBlockUserBox,
				window,
				user,
				v::null,
				v::null));
		}
	};
	AddActionButton(
		_wrap,
		rpl::duplicate(text),
		std::move(toggleOn),
		std::move(callback),
		&st::infoIconBlock,
		st::infoBlockButton);
}

void ActionsFiller::addLeaveChannelAction(not_null<ChannelData*> channel) {
	Expects(_controller->parentController());

	AddActionButton(
		_wrap,
		tr::lng_profile_leave_channel(),
		AmInChannelValue(channel),
		Window::DeleteAndLeaveHandler(
			_controller->parentController(),
			channel),
		&st::infoIconLeave);
}

void ActionsFiller::addJoinChannelAction(
		not_null<ChannelData*> channel) {
	using namespace rpl::mappers;
	auto joinVisible = AmInChannelValue(channel)
		| rpl::map(!_1)
		| rpl::start_spawning(_wrap->lifetime());
	AddActionButton(
		_wrap,
		tr::lng_profile_join_channel(),
		rpl::duplicate(joinVisible),
		[=] { channel->session().api().joinChannel(channel); },
		&st::infoIconAddMember);
	_wrap->add(object_ptr<Ui::SlideWrap<Ui::FixedHeightWidget>>(
		_wrap,
		CreateSkipWidget(
			_wrap,
			st::infoBlockButtonSkip))
	)->setDuration(
		st::infoSlideDuration
	)->toggleOn(
		rpl::duplicate(joinVisible)
	);
}

void ActionsFiller::fillUserActions(not_null<UserData*> user) {
	if (user->isBot()) {
		addInviteToGroupAction(user);
	}
	addShareContactAction(user);
	if (!user->isSelf()) {
		addEditContactAction(user);
		addDeleteContactAction(user);
	}
	if (!user->isSelf() && !user->isSupport()) {
		if (user->isBot()) {
			addBotCommandActions(user);
		}
		_wrap->add(CreateSkipWidget(
			_wrap,
			st::infoBlockButtonSkip));
		if (user->isBot()) {
			addReportAction();
		}
		addBlockAction(user);
	}
}

void ActionsFiller::fillChannelActions(
		not_null<ChannelData*> channel) {
	using namespace rpl::mappers;

	addJoinChannelAction(channel);
	addLeaveChannelAction(channel);
	if (!channel->amCreator()) {
		addReportAction();
	}
}

object_ptr<Ui::RpWidget> ActionsFiller::fill() {
	auto wrapResult = [=](auto &&callback) {
		_wrap = object_ptr<Ui::VerticalLayout>(_parent);
		_wrap->add(CreateSkipWidget(_wrap));
		callback();
		_wrap->add(CreateSkipWidget(_wrap));
		return std::move(_wrap);
	};
	if (auto user = _peer->asUser()) {
		return wrapResult([=] {
			fillUserActions(user);
		});
	} else if (auto channel = _peer->asChannel()) {
		if (channel->isMegagroup()) {
			return { nullptr };
		}
		return wrapResult([=] {
			fillChannelActions(channel);
		});
	}
	return { nullptr };
}

} // namespace

const char kOptionShowPeerIdBelowAbout[] = "show-peer-id-below-about";

object_ptr<Ui::RpWidget> SetupDetails(
		not_null<Controller*> controller,
		not_null<Ui::RpWidget*> parent,
		not_null<PeerData*> peer,
		Origin origin) {
	DetailsFiller filler(controller, parent, peer, origin);
	return filler.fill();
}

object_ptr<Ui::RpWidget> SetupDetails(
		not_null<Controller*> controller,
		not_null<Ui::RpWidget*> parent,
		not_null<Data::ForumTopic*> topic) {
	DetailsFiller filler(controller, parent, topic);
	return filler.fill();
}

object_ptr<Ui::RpWidget> SetupActions(
		not_null<Controller*> controller,
		not_null<Ui::RpWidget*> parent,
		not_null<PeerData*> peer) {
	ActionsFiller filler(controller, parent, peer);
	return filler.fill();
}

void SetupAddChannelMember(
		not_null<Window::SessionNavigation*> navigation,
		not_null<Ui::RpWidget*> parent,
		not_null<ChannelData*> channel) {
	auto add = Ui::CreateChild<Ui::IconButton>(
		parent.get(),
		st::infoMembersAddMember);
	add->showOn(CanAddMemberValue(channel));
	add->addClickHandler([=] {
		Window::PeerMenuAddChannelMembers(navigation, channel);
	});
	parent->widthValue(
	) | rpl::start_with_next([add](int newWidth) {
		auto availableWidth = newWidth
			- st::infoMembersButtonPosition.x();
		add->moveToLeft(
			availableWidth - add->width(),
			st::infoMembersButtonPosition.y(),
			newWidth);
	}, add->lifetime());
}

object_ptr<Ui::RpWidget> SetupChannelMembers(
		not_null<Controller*> controller,
		not_null<Ui::RpWidget*> parent,
		not_null<PeerData*> peer) {
	using namespace rpl::mappers;

	auto channel = peer->asChannel();
	if (!channel || channel->isMegagroup()) {
		return { nullptr };
	}

	auto membersShown = rpl::combine(
		MembersCountValue(channel),
		Data::PeerFlagValue(
			channel,
			ChannelDataFlag::CanViewParticipants),
			(_1 > 0) && _2);
	auto membersText = tr::lng_chat_status_subscribers(
		lt_count_decimal,
		MembersCountValue(channel) | tr::to_count());
	auto membersCallback = [=] {
		controller->showSection(std::make_shared<Info::Memento>(
			channel,
			Section::Type::Members));
	};

	auto result = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		parent,
		object_ptr<Ui::VerticalLayout>(parent));
	result->setDuration(
		st::infoSlideDuration
	)->toggleOn(
		std::move(membersShown)
	);

	auto members = result->entity();
	members->add(object_ptr<Ui::BoxContentDivider>(members));
	members->add(CreateSkipWidget(members));
	auto button = AddActionButton(
		members,
		std::move(membersText),
		rpl::single(true),
		std::move(membersCallback),
		nullptr)->entity();

	SetupAddChannelMember(controller, button, channel);

	object_ptr<FloatingIcon>(
		members,
		st::infoIconMembers,
		st::infoChannelMembersIconPosition);
	members->add(CreateSkipWidget(members));

	return result;
}

} // namespace Profile
} // namespace Info
