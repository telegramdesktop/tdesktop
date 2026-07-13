/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_actions.h"

#include "api/api_blocked_peers.h"
#include "api/api_chat_participants.h"
#include "api/api_credits.h"
#include "api/api_report.h"
#include "api/api_statistics.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "base/event_filter.h"
#include "base/options.h"
#include "base/qt/qt_key_modifiers.h"
#include "base/timer_rpl.h"
#include "base/unixtime.h"
#include "boxes/peer_list_box.h"
#include "boxes/peers/add_bot_to_chat_box.h"
#include "boxes/peers/edit_contact_box.h"
#include "boxes/peers/edit_participants_box.h"
#include "boxes/peers/edit_peer_info_box.h"
#include "boxes/peers/verify_peers_box.h"
#include "boxes/report_messages_box.h"
#include "boxes/share_box.h"
#include "boxes/star_gift_box.h"
#include "boxes/translate_box.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "core/ui_integration.h"
#include "data/business/data_business_common.h"
#include "data/business/data_business_info.h"
#include "data/components/credits.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_folder.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_peer_values.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/notify/data_notify_settings.h"
#include "data/stickers/data_custom_emoji.h"
#include "dialogs/ui/dialogs_layout.h"
#include "dialogs/ui/dialogs_message_view.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/history_item_helpers.h"
#include "history/view/history_view_item_preview.h"
#include "history/view/reactions/history_view_reactions_list.h"
#include "info/bot/earn/info_bot_earn_widget.h"
#include "info/bot/starref/info_bot_starref_common.h"
#include "info/channel_statistics/earn/earn_format.h"
#include "info/channel_statistics/earn/earn_icons.h"
#include "info/channel_statistics/earn/info_channel_earn_list.h"
#include "info/profile/info_profile_icon.h"
#include "info/profile/info_profile_phone_menu.h"
#include "info/profile/info_profile_text.h"
#include "info/profile/info_profile_values.h"
#include "info/profile/info_profile_widget.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "inline_bots/bot_attach_web_view.h"
#include "iv/iv_instance.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "menu/menu_mute.h"
#include "settings/settings_common.h"
#include "support/support_helper.h"
#include "ui/boxes/peer_qr_box.h"
#include "ui/boxes/report_box_graphics.h"
#include "ui/controls/userpic_button.h"
#include "ui/effects/credits_graphics.h"
#include "ui/effects/toggle_arrow.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/ui_utility.h"
#include "ui/text/format_values.h"
#include "ui/text/text_custom_emoji.h"
#include "ui/text/text_utilities.h"
#include "ui/text/text_variant.h"
#include "ui/toast/toast.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h" // Window::Controller::show.
#include "window/window_peer_menu.h"
#include "window/window_separate_id.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_channel_earn.h" // st::channelEarnCurrencyCommonMargins
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h" // settingsButtonRightSkip.
#include "styles/style_window.h" // mainMenuToggleFourStrokes.

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

namespace Info {
namespace Profile {
namespace {

constexpr auto kDay = Data::WorkingInterval::kDay;
constexpr auto kPeerIdLinkIndex = uint16(1);

class DraggableUrlClickHandler final : public UrlClickHandler {
public:
	DraggableUrlClickHandler(const QString &url, QString drag)
	: UrlClickHandler(url, false)
	, _drag(std::move(drag)) {
	}
	QString dragText() const override {
		return _drag;
	}

private:
	const QString _drag;

};

base::options::toggle ShowPeerIdBelowAbout({
	.id = kOptionShowPeerIdBelowAbout,
	.name = "Show Peer IDs in Profile",
	.description = "Show peer IDs from API below their Bio / Description."
		" Add contact IDs to exported data.",
});

base::options::toggle ShowChannelJoinedBelowAbout({
	.id = kOptionShowChannelJoinedBelowAbout,
	.name = "Show Channel Joined Date in Profile",
	.description = "Show when you join Channel under its Description.",
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
				result.append(tr::link(
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

[[nodiscard]] rpl::producer<TextWithEntities> TopicSubtext(
		not_null<PeerData*> peer) {
	return rpl::conditional(
		UsernamesValue(peer) | rpl::map([](std::vector<TextWithEntities> v) {
			return !v.empty();
		}),
		tr::lng_filters_link_subtitle(tr::marked),
		tr::lng_info_link_topic_label(tr::marked));
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
			TextUtilities::SetClipboardText({ link });
			if (const auto strong = weak.get()) {
				strong->showToast({
					.text = {
						tr::lng_channel_public_link_copied(tr::now),
					},
					.iconLottie = u"toast/voip_invite"_q,
					.iconLottieSize = st::toastLottieIconSize,
				});
			}
		}
	};
}

[[nodiscard]] object_ptr<Ui::RpWidget> CreateSkipWidget(
		not_null<Ui::RpWidget*> parent) {
	return Ui::CreateSkipWidget(parent, st::infoProfileSkip);
}

[[nodiscard]] rpl::producer<TextWithEntities> AboutWithAdvancedValue(
		not_null<PeerData*> peer) {

	return AboutValue(
		peer
	) | rpl::map([=](TextWithEntities &&value) {
		if (ShowPeerIdBelowAbout.value()) {
			using namespace Ui::Text;
			if (!value.empty()) {
				value.append("\n\n");
			}
			value.append(Italic(u"id: "_q));
			const auto raw = peer->id.value & PeerId::kChatTypeMask;
			value.append(Link(
				Italic(Lang::FormatCountDecimal(raw)),
				kPeerIdLinkIndex));
		}
		if (ShowChannelJoinedBelowAbout.value()) {
			if (const auto channel = peer->asChannel()) {
				if (!channel->amCreator() && channel->inviteDate) {
					if (!value.empty()) {
						if (ShowPeerIdBelowAbout.value()) {
							value.append("\n");
						} else {
							value.append("\n\n");
						}
					}
					using namespace Ui::Text;
					value.append((channel->isMegagroup()
						? tr::lng_you_joined_group
						: tr::lng_action_you_joined)(
							tr::now,
							tr::italic));
					value.append(Italic(": "));
					const auto raw = channel->inviteDate;
					value.append(Link(
						Italic(langDateTimeFull(base::unixtime::parse(raw))),
						"internal:~join_date~:show:" + QString::number(raw)));
				}
			}
		}
		return std::move(value);
	});
}

void SetupAboutPeerIdDrag(
		not_null<Ui::FlatLabel*> label,
		not_null<PeerData*> peer) {
	if (!ShowPeerIdBelowAbout.value()) {
		return;
	}
	const auto id = QString::number(peer->id.value & PeerId::kChatTypeMask);
	AboutValue(
		peer
	) | rpl::on_next([=] {
		label->setLink(
			kPeerIdLinkIndex,
			std::make_shared<DraggableUrlClickHandler>(
				u"internal:~peer_id~:copy:"_q + id,
				id));
	}, label->lifetime());
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
	button->setTextTransform(Ui::RoundButtonTextTransform::ToUpper);
	const auto inner = Ui::CreateChild<Ui::VerticalLayout>(button);
	button->widthValue() | rpl::on_next([=](int width) {
		const auto margin = st::infoHoursOuterMargin;
		inner->resizeToWidth(width - margin.left() - margin.right());
		inner->move(margin.left(), margin.top());
	}, inner->lifetime());
	inner->heightValue() | rpl::on_next([=](int height) {
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
	const auto timingArrow = Ui::CreateChild<Ui::RpWidget>(openedWrap);
	timingArrow->resize(Size(timing->st().style.font->height));
	timing->setAttribute(Qt::WA_TransparentForMouseEvents);
	state->opened.value() | rpl::on_next([=](bool value) {
		opened->setTextColorOverride(value
			? st::boxTextFgGood->c
			: st::boxTextFgError->c);
	}, opened->lifetime());

	rpl::combine(
		openedWrap->widthValue(),
		opened->heightValue(),
		timing->sizeValue()
	) | rpl::on_next([=](int width, int h1, QSize size) {
		opened->moveToLeft(0, 0, width);
		timingArrow->moveToRight(0, 0, width);
		timing->moveToRight(timingArrow->width(), 0, width);

		const auto margins = opened->getMargins();
		const auto added = margins.top() + margins.bottom();
		openedWrap->resize(width, std::max(h1, size.height()) - added);
	}, openedWrap->lifetime());

	rpl::combine(
		state->opened.value(),
		state->opensIn.value(),
		state->expanded.value(),
		dayHoursTextValue(state->day.value())
	) | rpl::on_next([=](
			bool opened,
			TimeId opensIn,
			bool expanded,
			const QString &timing) {
		const auto status = (opened
			? tr::lng_info_work_open
			: tr::lng_info_work_closed)(tr::now);
		const auto when = (!opensIn || expanded)
			? timing
			: (opensIn >= 86400)
			? tr::lng_info_hours_opens_in_days(tr::now, lt_count, opensIn / 86400)
			: (opensIn >= 3600)
			? tr::lng_info_hours_opens_in_hours(tr::now, lt_count, opensIn / 3600)
			: tr::lng_info_hours_opens_in_minutes(
				tr::now,
				lt_count,
				std::max(opensIn / 60, 1));
		button->setAccessibleName(
			tr::lng_info_hours_label(tr::now) + ": " + status + ", " + when);
	}, inner->lifetime());

	const auto labelWrap = inner->add(object_ptr<Ui::RpWidget>(inner));
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		labelWrap,
		tr::lng_info_hours_label(),
		st::infoLabel);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	auto linkText = rpl::combine(
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
	}) | rpl::flatten_latest();
	const auto link = Ui::CreateChild<Ui::RoundButton>(
		labelWrap,
		std::move(linkText),
		st::defaultTableSmallButton);
	link->setClickedCallback([=] {
		state->myTimezone = !state->myTimezone.current();
		state->expanded = true;
	});

	rpl::combine(
		labelWrap->widthValue(),
		label->heightValue(),
		link->sizeValue()
	) | rpl::on_next([=](int width, int h1, QSize size) {
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
	constexpr auto kSlideDuration = float64(st::slideWrapDuration);
	other->setDuration(kSlideDuration);
	{
		const auto arrowAnimation
			= other->lifetime().make_state<Ui::Animations::Basic>();
		arrowAnimation->init([=] {
			timingArrow->update();
			if (!other->animating()) {
				arrowAnimation->stop();
			}
		});
		timingArrow->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(timingArrow);
			const auto progress = other->animating()
				? (crl::now() - arrowAnimation->started()) / kSlideDuration
				: 1.;

			const auto path = Ui::ToggleUpDownArrowPath(
				timingArrow->width() / 2,
				timingArrow->height() / 2,
				st::infoHoursArrowSize,
				st::mainMenuToggleFourStrokes,
				other->toggled() ? progress : 1 - progress);

			auto hq = PainterHighQualityEnabler(p);
			p.fillPath(path, timing->st().textFg);
		}, timingArrow->lifetime());
		state->expanded.value() | rpl::on_next([=] {
			arrowAnimation->start();
		}, other->lifetime());
	}

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
		) | rpl::on_next([=](int width, int h1, QSize size) {
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

void DeleteContactNote(
		not_null<UserData*> user,
		Fn<void(const QString &)> showError = nullptr) {
	user->session().api().request(MTPcontacts_UpdateContactNote(
		user->inputUser(),
		MTP_textWithEntities(MTP_string(), MTP_vector<MTPMessageEntity>())
	)).done([=] {
		user->setNote(TextWithEntities());
	}).fail([=](const MTP::Error &error) {
		if (showError) {
			showError(error.description());
		}
	}).send();
}

[[nodiscard]] object_ptr<Ui::SlideWrap<>> CreateNotes(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<UserData*> user) {
	auto allNotesText = user->session().changes().peerFlagsValue(
		user,
		Data::PeerUpdate::Flag::FullInfo
	) | rpl::map([=] {
		return user->note();
	});

	auto notesText = rpl::duplicate(
		allNotesText
	) | rpl::filter([](const TextWithEntities &note) {
		return !note.text.isEmpty();
	});

	auto result = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		parent,
		object_ptr<Ui::VerticalLayout>(parent));
	result->toggleOn(rpl::duplicate(
		allNotesText
	) | rpl::map([](const TextWithEntities &note) {
		return !note.text.isEmpty();
	}));
	result->finishAnimating();

	const auto notesContainer = result->entity();

	const auto context = Ui::Text::MarkedContext{
		.customEmojiFactory = user->owner().customEmojiManager().factory(
			Data::CustomEmojiManager::SizeTag::Normal)
	};

	auto notesLine = CreateTextWithLabel(
		notesContainer,
		tr::lng_info_notes_label(TextWithEntities::Simple),
		rpl::duplicate(notesText),
		st::infoLabel,
		st::infoLabeled,
		st::infoProfileLabeledPadding);

	std::move(
		notesText
	) | rpl::on_next([=, raw = notesLine.text](
			TextWithEntities note) {
		TextUtilities::ParseEntities(note, TextParseLinks);
		raw->setMarkedText(note, context);
	}, notesLine.text->lifetime());

	notesLine.text->setContextMenuHook([=, raw = notesLine.text](
			Ui::FlatLabel::ContextMenuRequest request) {
		raw->fillContextMenu(request);
		const auto addAction = Ui::Menu::CreateAddActionCallback(
			request.menu);
		addAction({
			.text = tr::lng_edit_note(tr::now),
			.handler = [=] {
				controller->window().show(
					Box(EditContactNoteBox, controller, user));
			},
		});
		addAction({
			.text = tr::lng_delete_note(tr::now),
			.handler = [=] {
				DeleteContactNote(user, [=](const QString &error) {
					controller->showToast(error);
				});
			},
			.isAttention = true,
		});
	});

	rpl::merge(
		notesLine.wrap->events(),
		notesLine.subtext->events()
	) | rpl::on_next([=, raw = notesLine.text](not_null<QEvent*> e) {
		if (e->type() == QEvent::ContextMenu) {
			const auto ce = static_cast<QContextMenuEvent*>(e.get());
			QCoreApplication::postEvent(
				raw,
				new QContextMenuEvent(
					ce->reason(),
					ce->pos(),
					ce->globalPos()));
		}
	}, notesLine.wrap->lifetime());

	const auto subtextLabel = Ui::CreateChild<Ui::FlatLabel>(
		notesLine.wrap->entity(),
		tr::lng_info_notes_private(tr::now),
		st::infoLabel);
	subtextLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

	rpl::combine(
		notesLine.wrap->entity()->widthValue(),
		notesLine.subtext->geometryValue()
	) | rpl::on_next([=, skip = st::lineWidth * 5](
			int width,
			const QRect &subtextGeometry) {
		subtextLabel->moveToRight(
			0,
			subtextGeometry.y() + skip,
			width);
	}, subtextLabel->lifetime());

	notesContainer->add(std::move(notesLine.wrap));

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
	button->setTextTransform(Ui::RoundButtonTextTransform::ToUpper);

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
	) | rpl::map(tr::marked);

	const auto giftIcon = Ui::CreateChild<Ui::RpWidget>(layout);
	giftIcon->resize(st::birthdayTodayIcon.size());
	layout->sizeValue() | rpl::on_next([=](QSize size) {
		giftIcon->moveToRight(
			0,
			(size.height() - giftIcon->height()) / 2,
			size.width());
	}, giftIcon->lifetime());
	giftIcon->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(giftIcon);
		st::birthdayTodayIcon.paint(p, 0, 0, giftIcon->width());
	}, giftIcon->lifetime());

	rpl::duplicate(
		birthday
	) | rpl::map([](Data::Birthday value) {
		return Data::IsBirthdayTodayValue(value);
	}) | rpl::flatten_latest(
	) | rpl::distinct_until_changed(
	) | rpl::on_next([=](bool today) {
		const auto disable = !today && user->session().premiumCanBuy();
		button->setDisabled(disable);
		button->setAttribute(Qt::WA_TransparentForMouseEvents, disable);
		button->clearState();
		giftIcon->setVisible(!disable);
	}, result->lifetime());

	BirthdayValueText(
		rpl::duplicate(birthday),
		true
	) | rpl::on_next([=](const QString &accessibleText) {
		button->setAccessibleName(
			tr::lng_info_birthday_label(tr::now) + ": " + accessibleText);
	}, button->lifetime());

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
			Ui::ShowStarGiftBox(controller, user);
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
		const style::SettingsButton &st = st::infoSharedMediaButton) {
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
auto AddMainButton(
		not_null<Ui::VerticalLayout*> parent,
		Text &&text,
		ToggleOn &&toggleOn,
		Callback &&callback,
		Ui::MultiSlideTracker *tracker = nullptr,
		Ui::MultiSlideTracker *buttonTracker = nullptr,
		const style::SettingsButton &st = st::infoMainButton) {
	const auto button = AddActionButton(
		parent,
		std::move(text) | rpl::map(tr::upper),
		std::move(toggleOn),
		std::move(callback),
		nullptr,
		st);
	if (tracker) {
		tracker->track(button);
	}
	if (buttonTracker) {
		buttonTracker->track(button);
	}
	return button->entity();
}

rpl::producer<CreditsAmount> AddCurrencyAction(
		not_null<UserData*> user,
		not_null<Ui::VerticalLayout*> wrap,
		not_null<Controller*> controller) {
	struct State final {
		rpl::variable<CreditsAmount> balance;
		Ui::Text::CustomEmojiHelper helper;
	};
	const auto state = wrap->lifetime().make_state<State>();
	const auto parentController = controller->parentController();
	const auto wrapButton = AddActionButton(
		wrap,
		tr::lng_manage_peer_bot_balance_currency(),
		state->balance.value(
		) | rpl::map(rpl::mappers::_1 > CreditsAmount(0)),
		[=] { parentController->showSection(Info::ChannelEarn::Make(user)); },
		nullptr);
	{
		const auto button = wrapButton->entity();
		const auto icon = Ui::CreateChild<Ui::RpWidget>(button);
		icon->resize(st::infoIconReport.size());
		const auto image = Ui::Earn::MenuIconCurrency(icon->size());
		icon->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(icon);
			p.drawImage(0, 0, image);
		}, icon->lifetime());

		button->sizeValue(
		) | rpl::on_next([=](const QSize &size) {
			icon->move(st::infoEarnCurrencyIconPosition);
		}, icon->lifetime());
	}
	const auto balance = user->session().credits().balanceCurrency(user->id);
	if (balance) {
		state->balance = balance;
	}
	{
		const auto weak = base::make_weak(wrap);
		const auto currencyLoadLifetime
			= std::make_shared<rpl::lifetime>();
		const auto currencyLoad
			= currencyLoadLifetime->make_state<Api::EarnStatistics>(user);
		const auto done = [=](CreditsAmount balance) {
			if ([[maybe_unused]] const auto strong = weak.get()) {
				state->balance = balance;
				currencyLoadLifetime->destroy();
			}
		};
		currencyLoad->request() | rpl::on_error_done(
			[=](const QString &error) {
				done(CreditsAmount(0, CreditsType::Ton));
			},
			[=] { done(currencyLoad->data().currentBalance); },
			*currencyLoadLifetime);
	}
	const auto &st = st::infoSharedMediaButton;
	const auto button = wrapButton->entity();
	const auto name = Ui::CreateChild<Ui::FlatLabel>(button, st.rightLabel);
	const auto icon = state->helper.paletteDependent({ .factory = [=] {
		return Ui::Earn::IconCurrencyColored(
			st.rightLabel.style.font,
			st.rightLabel.textFg->c);
	}, .margin = st::channelEarnCurrencyCommonMargins });
	name->show();
	rpl::combine(
		button->widthValue(),
		tr::lng_manage_peer_bot_balance_currency(),
		state->balance.value()
	) | rpl::on_next([=, &st](
			int width,
			const QString &button,
			CreditsAmount balance) {
		const auto available = width
			- rect::m::sum::h(st.padding)
			- st.style.font->width(button)
			- st::settingsButtonRightSkip;
		name->setMarkedText(
			base::duplicate(icon)
				.append(QChar(' '))
				.append(Info::ChannelEarn::MajorPart(balance))
				.append(Info::ChannelEarn::MinorPart(balance)),
			state->helper.context());
		name->resizeToNaturalWidth(available);
		name->moveToRight(st::settingsButtonRightSkip, st.padding.top());
	}, name->lifetime());
	name->setAttribute(Qt::WA_TransparentForMouseEvents);
	wrapButton->finishAnimating();
	return state->balance.value();
}

rpl::producer<CreditsAmount> AddCreditsAction(
		not_null<UserData*> user,
		not_null<Ui::VerticalLayout*> wrap,
		not_null<Controller*> controller) {
	struct State final {
		rpl::variable<CreditsAmount> balance;
	};
	const auto state = wrap->lifetime().make_state<State>();
	const auto parentController = controller->parentController();
	const auto wrapButton = AddActionButton(
		wrap,
		tr::lng_manage_peer_bot_balance_credits(),
		state->balance.value(
		) | rpl::map(rpl::mappers::_1 > CreditsAmount(0)),
		[=] { parentController->showSection(Info::BotEarn::Make(user)); },
		nullptr);
	{
		const auto button = wrapButton->entity();
		const auto icon = Ui::CreateChild<Ui::RpWidget>(button);
		const auto image = Ui::Earn::MenuIconCredits();
		icon->resize(image.size() / style::DevicePixelRatio());
		icon->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(icon);
			p.drawImage(0, 0, image);
		}, icon->lifetime());

		button->sizeValue(
		) | rpl::on_next([=](const QSize &size) {
			icon->move(st::infoEarnCreditsIconPosition);
		}, icon->lifetime());
	}
	if (const auto balance = user->session().credits().balance(user->id)) {
		state->balance = balance;
	}
	{
		const auto api = wrap->lifetime().make_state<Api::CreditsStatus>(
			user);
		api->request({}, [=](Data::CreditsStatusSlice data) {
			state->balance = data.balance;
		});
	}
	const auto &st = st::infoSharedMediaButton;
	const auto button = wrapButton->entity();
	const auto name = Ui::CreateChild<Ui::FlatLabel>(button, st.rightLabel);

	auto helper = Ui::Text::CustomEmojiHelper();
	const auto icon = helper.paletteDependent(Ui::Earn::IconCreditsEmoji());
	const auto context = helper.context([=] { name->update(); });
	name->show();
	rpl::combine(
		button->widthValue(),
		tr::lng_manage_peer_bot_balance_credits(),
		state->balance.value()
	) | rpl::on_next([=, &st](
			int width,
			const QString &button,
			CreditsAmount balance) {
		const auto available = width
			- rect::m::sum::h(st.padding)
			- st.style.font->width(button)
			- st::settingsButtonRightSkip;
		name->setMarkedText(
			base::duplicate(icon)
				.append(QChar(' '))
				.append(Lang::FormatCreditsAmountDecimal(balance)),
			context);
		name->resizeToNaturalWidth(available);
		name->moveToRight(st::settingsButtonRightSkip, st.padding.top());
	}, name->lifetime());
	name->setAttribute(Qt::WA_TransparentForMouseEvents);
	wrapButton->finishAnimating();
	return state->balance.value();
}

class DetailsFiller {
public:
	DetailsFiller(
		not_null<Controller*> controller,
		not_null<SectionStack*> stack,
		not_null<PeerData*> peer,
		Origin origin);
	DetailsFiller(
		not_null<Controller*> controller,
		not_null<SectionStack*> stack,
		not_null<Data::SavedSublist*> sublist);
	DetailsFiller(
		not_null<Controller*> controller,
		not_null<SectionStack*> stack,
		not_null<Data::ForumTopic*> topic);

	void buildSections();

private:
	[[nodiscard]] Section makePersonalChannel(not_null<UserData*> user);
	[[nodiscard]] Section makeInfo();
	[[nodiscard]] Section makeAddAsContact(not_null<UserData*> user);
	void addBotVerify();
	void addMainApp(not_null<UserData*> user);
	[[nodiscard]] Section makeBotPermissions(not_null<UserData*> user);
	void addManagedBotFooter(not_null<UserData*> managerUser);
	[[nodiscard]] Section makeReportOrDeleteReaction();
	[[nodiscard]] Section makeViewChannel(not_null<ChannelData*> channel);
	[[nodiscard]] Section makeCommunityLink(not_null<PeerData*> peer);
	void addCommunityHiddenNote();
	[[nodiscard]] Section makeTopicsList(not_null<Data::Forum*> forum);

	[[nodiscard]] Section makeDeleteReactionSection(GroupReactionOrigin data);
	[[nodiscard]] Section makeReportReactionSection(
		GroupReactionOrigin data,
		bool ban);

	not_null<Controller*> _controller;
	not_null<SectionStack*> _stack;
	not_null<PeerData*> _peer;
	Data::ForumTopic *_topic = nullptr;
	Data::SavedSublist *_sublist = nullptr;
	Origin _origin;

};

class ActionsFiller {
public:
	ActionsFiller(
		not_null<Controller*> controller,
		not_null<Ui::RpWidget*> parent,
		not_null<PeerData*> peer);

	object_ptr<Ui::RpWidget> fill();

private:
	void addAffiliateProgram(not_null<UserData*> user);
	void addBalanceActions(not_null<UserData*> user);
	void addInviteToGroupAction(not_null<UserData*> user);
	void addShareContactAction(not_null<UserData*> user);
	void addEditContactAction(not_null<UserData*> user);
	void addDeleteContactAction(not_null<UserData*> user);
	void addBotCommandActions(not_null<UserData*> user);
	void addFastButtonsMode(not_null<UserData*> user);
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
		Api::ReportReaction(
			controller->uiShow(),
			data.group,
			data.messageId,
			participant);
		sent();
		box->closeBox();
	}, st::attentionBoxButton);
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

DetailsFiller::DetailsFiller(
	not_null<Controller*> controller,
	not_null<SectionStack*> stack,
	not_null<PeerData*> peer,
	Origin origin)
: _controller(controller)
, _stack(stack)
, _peer(peer)
, _origin(origin) {
}

DetailsFiller::DetailsFiller(
	not_null<Controller*> controller,
	not_null<SectionStack*> stack,
	not_null<Data::SavedSublist*> sublist)
: _controller(controller)
, _stack(stack)
, _peer(sublist->sublistPeer())
, _sublist(sublist) {
}

DetailsFiller::DetailsFiller(
	not_null<Controller*> controller,
	not_null<SectionStack*> stack,
	not_null<Data::ForumTopic*> topic)
: _controller(controller)
, _stack(stack)
, _peer(topic->peer())
, _topic(topic) {
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

Section DetailsFiller::makeInfo() {
	const auto parent = _stack->layout();
	auto wrap = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		parent,
		object_ptr<Ui::VerticalLayout>(parent));
	const auto raw = wrap.data();
	const auto result = raw->entity();
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
		} else if (handler->url().startsWith(u"internal:~join_date~:"_q)) {
			const auto joinDate = handler->url().split(
				u"show:"_q,
				Qt::SkipEmptyParts).last();
			if (!joinDate.isEmpty()) {
				const auto weak = base::make_weak(window);
				window->session().api().resolveJumpToDate(
					Dialogs::Key(peer->owner().history(peer)),
					base::unixtime::parse(joinDate.toULongLong()).date(),
					[=](not_null<PeerData*> p, MsgId m) {
						const auto f = Window::SectionShow::Way::Forward;
						if (const auto strong = weak.get()) {
							strong->showPeerHistory(p, f, m);
						}
					});
				return false;
			}
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
			if (request.link) {
				const auto &url = request.link->url();
				if (url.startsWith(u"internal:~peer_id~:"_q)) {
					const auto weak = base::make_weak(controller);
					request.menu->addAction(u"Copy ID"_q, [=] {
						Core::App().openInternalUrl(
							url,
							QVariant::fromValue(ClickHandlerContext{
								.sessionWindow = weak,
							}));
					});
					return;
				}
			}
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
			const style::margins &padding = st::infoProfileLabeledPadding,
			const style::PopupMenu &stMenu = st::defaultPopupMenu) {
		auto line = CreateTextWithLabel(
			result,
			v::text::take_marked(std::move(label)),
			std::move(text),
			st::infoLabel,
			textSt,
			padding,
			stMenu);
		tracker.track(result->add(std::move(line.wrap)));

		line.text->setClickHandlerFilter(infoClickFilter);
		return line;
	};
	const auto addInfoLine = [&](
			v::text::data &&label,
			rpl::producer<TextWithEntities> &&text,
			const style::FlatLabel &textSt = st::infoLabeled,
			const style::margins &padding = st::infoProfileLabeledPadding,
			const style::PopupMenu &stMenu = st::defaultPopupMenu) {
		return addInfoLineGeneric(
			std::move(label),
			std::move(text),
			textSt,
			padding,
			stMenu);
	};
	const auto addInfoOneLine = [&](
			v::text::data &&label,
			rpl::producer<TextWithEntities> &&text,
			const QString &contextCopyText,
			const style::margins &padding = st::infoProfileLabeledPadding,
			const style::PopupMenu &stMenu = st::defaultPopupMenu) {
		auto result = addInfoLine(
			std::move(label),
			std::move(text),
			st::infoLabeledOneLine,
			padding,
			stMenu);
		result.text->setDoubleClickSelectsParagraph(true);
		result.text->setContextCopyText(contextCopyText);
		return result;
	};
	const auto fitLabelToButton = [&](
			not_null<Ui::RpWidget*> button,
			not_null<Ui::FlatLabel*> label,
			int rightSkip) {
		const auto parent = label->parentWidget();
		const auto container = result;
		rpl::combine(
			container->widthValue(),
			label->geometryValue(),
			button->sizeValue(),
			button->shownValue()
		) | rpl::on_next([=](
				int width,
				QRect,
				QSize buttonSize,
				bool buttonShown) {
			button->moveToRight(
				rightSkip,
				(parent->height() - buttonSize.height()) / 2);
			const auto x = Ui::MapFrom(container, label, QPoint(0, 0)).x();
			const auto s = buttonShown
				? Ui::MapFrom(container, button, QPoint(0, 0)).x()
				: width;
			label->resizeToWidth(s - x);
		}, button->lifetime());
	};
	const auto controller = _controller->parentController();
	const auto weak = base::make_weak(controller);
	const auto peerIdRaw = QString::number(_peer->id.value);
	const auto lnkHook = [=](Ui::FlatLabel::ContextMenuRequest request) {
		const auto strong = weak.get();
		if (!strong || !request.link) {
			return;
		}
		const auto url = request.link->url();
		if (url.startsWith(u"https://")) {
			request.menu->addAction(
				tr::lng_context_copy_link(tr::now),
				[=] {
					TextUtilities::SetClipboardText({ url });
					if (const auto strong = weak.get()) {
						strong->showToast({
							.text = {
								tr::lng_channel_public_link_copied(tr::now),
							},
							.iconLottie = u"toast/voip_invite"_q,
							.iconLottieSize = st::toastLottieIconSize,
						});
					}
				});
			request.menu->addAction(
				tr::lng_group_invite_share(tr::now),
				[=] {
					if (const auto strong = weak.get()) {
						FastShareLink(strong, url);
					}
				});
			return;
		}
		static const auto kPrefix = QRegularExpression(u"^internal:"
			"(collectible_username|username_link|username_regular)/"
			"([a-zA-Z0-9\\-\\_\\.]+)@"_q);
		const auto match = kPrefix.match(url);
		if (!match.hasMatch()) {
			return;
		}
		const auto username = match.captured(2);
		const auto fullname = username + '@' + peerIdRaw;
		const auto mentionLink = "internal:username_regular/" + fullname;
		const auto linkLink = "internal:username_link/" + fullname;
		const auto context = QVariant::fromValue(ClickHandlerContext{
			.sessionWindow = weak,
		});
		const auto session = &strong->session();
		const auto link = session->createInternalLinkFull(username);
		request.menu->addAction(
			tr::lng_context_copy_mention(tr::now),
			[=] { Core::App().openInternalUrl(mentionLink, context); });
		request.menu->addAction(
			tr::lng_context_copy_link(tr::now),
			[=] { Core::App().openInternalUrl(linkLink, context); });
		request.menu->addAction(
			tr::lng_group_invite_share(tr::now),
			[=] {
				if (const auto strong = weak.get()) {
					FastShareLink(strong, link);
				}
			});
	};
	if (const auto user = _peer->asUser()) {
		if (user->session().supportMode()) {
			addInfoLineGeneric(
				user->session().supportHelper().infoLabelValue(user),
				user->session().supportHelper().infoTextValue(user));
		}

		{
			const auto phoneLabel = addInfoOneLine(
				tr::lng_info_mobile_label(),
				PhoneWithSpoilerValue(user, PhoneOrHiddenValue(user)),
				tr::lng_profile_copy_phone(tr::now),
				st::infoProfileLabeledPadding,
				st::popupMenuWithIcons).text;
			const auto hook = [=](Ui::FlatLabel::ContextMenuRequest request) {
				if (request.selection.empty()) {
					const auto callback = [=] {
						CopyPhoneToClipboard(PhoneOrHiddenValue(user));
					};
					request.menu->addAction(
						tr::lng_profile_copy_phone(tr::now),
						callback,
						&st::menuIconCopy);
				} else {
					phoneLabel->fillContextMenu(request);
				}
				AddPhoneMenu(request.menu, user);
				AddPhoneSpoilerMenu(request.menu, user);
			};
			phoneLabel->setContextMenuHook(hook);
		}
		auto label = user->isBot()
			? tr::lng_info_about_label()
			: tr::lng_info_bio_label();
		const auto about = addInfoLine(
			std::move(label),
			AboutWithAdvancedValue(user));
		addTranslateToMenu(about.text, AboutWithAdvancedValue(user));
		SetupAboutPeerIdDrag(about.text, user);

		const auto usernameLine = addInfoOneLine(
			UsernamesSubtext(_peer, tr::lng_info_username_label()),
			UsernameValue(user, true) | rpl::map([=](TextWithEntities u) {
				return u.text.isEmpty()
					? TextWithEntities()
					: tr::link(u, UsernameUrl(user, u.text.mid(1)));
			}),
			QString(),
			st::infoProfileLabeledUsernamePadding);
		const auto callback = UsernamesLinkCallback(
			_peer,
			controller,
			QString());
		usernameLine.text->overrideLinkClickHandler(callback);
		usernameLine.subtext->overrideLinkClickHandler(callback);
		usernameLine.text->setContextMenuHook(lnkHook);
		usernameLine.subtext->setContextMenuHook(lnkHook);
		UsernameValue(
			user,
			true
		) | rpl::on_next([=, label = usernameLine.text](
				const TextWithEntities &u) {
			if (u.text.isEmpty()) {
				return;
			}
			const auto username = u.text.mid(1);
			label->setLink(1, std::make_shared<DraggableUrlClickHandler>(
				UsernameUrl(user, username),
				user->session().createInternalLinkFull(username)));
		}, usernameLine.text->lifetime());

		const auto qrButton = Ui::CreateChild<Ui::IconButton>(
			usernameLine.text->parentWidget(),
			st::infoProfileLabeledButtonQr);
		qrButton->setAccessibleName(tr::lng_group_invite_context_qr(tr::now));
		UsernamesValue(_peer) | rpl::on_next([=](const auto &u) {
			qrButton->setVisible(!u.empty());
		}, qrButton->lifetime());
		const auto rightSkip = st::infoProfileLabeledButtonQrRightSkip;
		fitLabelToButton(qrButton, usernameLine.text, rightSkip);
		fitLabelToButton(qrButton, usernameLine.subtext, rightSkip);
		qrButton->setClickedCallback([=, show = controller->uiShow()] {
			Ui::DefaultShowFillPeerQrBoxCallback(show, user);
			return false;
		});

		if (!user->isBot()) {
			tracker.track(result->add(
				CreateBirthday(result, controller, user),
				{},
				style::al_justify));
			tracker.track(result->add(
				CreateWorkingHours(result, user), {}, style::al_justify));

			tracker.track(result->add(
				CreateNotes(result, controller, user), {}, style::al_justify));

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
				return tr::link(
					TextUtilities::SingleLine(details.location.address),
					LocationClickHandler::Url(*details.location.point));
			});
			addInfoOneLine(
				tr::lng_info_location_label(),
				std::move(locationText),
				QString()
			).text->setLinksTrusted();
		}
	} else {
		const auto topicRootId = _topic ? _topic->rootId() : 0;
		const auto addToLink = topicRootId
			? ('/' + QString::number(topicRootId.bare))
			: QString();
		auto linkText = LinkValue(
			_peer,
			true,
			topicRootId
		) | rpl::map([=](const LinkWithUrl &link) {
			const auto text = link.text;
			return text.isEmpty()
				? TextWithEntities()
				: tr::link(
					(text.startsWith(u"https://"_q)
						? text.mid(u"https://"_q.size())
						: text) + addToLink,
					(addToLink.isEmpty() ? link.url : (text + addToLink)));
		});
		const auto linkLine = addInfoOneLine(
			(topicRootId
				? TopicSubtext(_peer)
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
		linkLine.text->setContextMenuHook(lnkHook);
		linkLine.subtext->setContextMenuHook(lnkHook);
		LinkValue(
			_peer,
			true,
			topicRootId
		) | rpl::on_next([=, label = linkLine.text](const LinkWithUrl &link) {
			if (link.text.isEmpty()) {
				return;
			}
			label->setLink(1, std::make_shared<DraggableUrlClickHandler>(
				addToLink.isEmpty() ? link.url : (link.text + addToLink),
				link.text + addToLink));
		}, linkLine.text->lifetime());
		if (!topicRootId || !_peer->username().isEmpty()) {
			const auto qr = Ui::CreateChild<Ui::IconButton>(
				linkLine.text->parentWidget(),
				st::infoProfileLabeledButtonQr);
			qr->setAccessibleName(tr::lng_group_invite_context_qr(tr::now));
			UsernamesValue(_peer) | rpl::on_next([=](const auto &u) {
				qr->setVisible(!u.empty());
			}, qr->lifetime());
			const auto rightSkip = st::infoProfileLabeledButtonQrRightSkip;
			fitLabelToButton(qr, linkLine.text, rightSkip);
			fitLabelToButton(qr, linkLine.subtext, rightSkip);
			const auto peer = _peer;
			qr->setClickedCallback([=, show = controller->uiShow()] {
				Ui::DefaultShowFillPeerQrBoxCallback(show, peer);
				return false;
			});
		}

		if (const auto channel = _topic ? nullptr : _peer->asChannel()) {
			auto locationText = LocationValue(
				channel
			) | rpl::map([](const ChannelLocation *location) {
				return location
					? tr::link(
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
			: AboutWithAdvancedValue(_peer));
		if (!_topic) {
			addTranslateToMenu(about.text, AboutWithAdvancedValue(_peer));
			SetupAboutPeerIdDrag(about.text, _peer);
		}
	}
	raw->toggleOn(tracker.atLeastOneShownValue());
	raw->finishAnimating();

	return Section{
		.widget = std::move(wrap),
		.shown = raw->toggledValue(),
	};
}

Section DetailsFiller::makePersonalChannel(not_null<UserData*> user) {
	const auto parent = _stack->layout();
	auto result = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		parent,
		object_ptr<Ui::VerticalLayout>(parent));
	const auto container = result->entity();
	const auto window = _controller->parentController();
	const auto duration = st::slideWrapDuration;

	result->toggleOn(PersonalChannelValue(
		user
	) | rpl::map(rpl::mappers::_1 != nullptr));
	result->finishAnimating();

	auto channel = PersonalChannelValue(
		user
	) | rpl::start_spawning(result->lifetime());

	const auto channelLabelFactory = [=](rpl::producer<ChannelData*> c) {
		return rpl::combine(
			tr::lng_info_personal_channel_label(tr::marked),
			std::move(c)
		) | rpl::map([](TextWithEntities &&text, ChannelData *channel) {
			const auto count = channel ? channel->membersCount() : 0;
			if (count > 1) {
				text.append(' ')
				.append(Ui::kQBullet)
				.append(' ')
				.append(
					tr::lng_chat_status_subscribers(
						tr::now,
						lt_count_decimal,
						count));
			}
			return text;
		});
	};
	if (user->isSelf()) {
		struct State {
			base::unique_qptr<Ui::PopupMenu> menu;
		};
		const auto state = container->lifetime().make_state<State>();
		base::install_event_filter(container, [=](
				not_null<QEvent*> e) {
			if (e->type() == QEvent::ContextMenu) {
				const auto ce = static_cast<QContextMenuEvent*>(e.get());
				state->menu = base::make_unique_q<Ui::PopupMenu>(
					container,
					st::defaultPopupMenu);
				state->menu->addAction(
					tr::lng_settings_channel_menu_remove(tr::now),
					[] {
						UrlClickHandler::Open(
							u"internal:edit_personal_channel:remove"_q);
					});
				state->menu->popup(ce->globalPos());
				return base::EventFilterResult::Cancel;
			}
			return base::EventFilterResult::Continue;
		}, container->lifetime());
	}

	{
		const auto onlyChannelWrap = container->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				container,
				object_ptr<Ui::VerticalLayout>(container)));
		onlyChannelWrap->toggleOn(rpl::duplicate(channel) | rpl::map([=] {
			return user->personalChannelId()
				&& !user->personalChannelMessageId();
		}));
		onlyChannelWrap->finishAnimating();

		auto text = rpl::duplicate(
			channel
		) | rpl::map([=](ChannelData *channel) {
			return channel ? NameValue(channel) : rpl::single(QString());
		}) | rpl::flatten_latest() | rpl::map([](const QString &name) {
			return name.isEmpty() ? TextWithEntities() : tr::link(name);
		});
		auto line = CreateTextWithLabel(
			result,
			channelLabelFactory(rpl::duplicate(channel)),
			std::move(text),
			st::infoLabel,
			st::infoLabeled,
			st::infoProfilePersonalChannelPadding);
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
		messageChannelWrap->setDuration(duration);
		messageChannelWrap->toggleOn(rpl::duplicate(channel) | rpl::map([=] {
			return user->personalChannelId()
				&& user->personalChannelMessageId();
		}));
		messageChannelWrap->finishAnimating();
		messageChannelWrap->toggledValue(
		) | rpl::filter(rpl::mappers::_1) | rpl::on_next([=] {
			messageChannelWrap->resizeToWidth(messageChannelWrap->width());
		}, messageChannelWrap->lifetime());

		const auto clear = [=] {
			while (messageChannelWrap->entity()->count()) {
				delete messageChannelWrap->entity()->widgetAt(0);
			}
		};

		const auto rebuild = [=](
				not_null<HistoryItem*> item,
				anim::type animated) {
			const auto &stUserpic = st::infoPersonalChannelUserpic;
			const auto &stLabeled = st::infoProfilePersonalChannelPadding;

			messageChannelWrap->toggle(false, anim::type::instant);
			clear();

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
			struct State {
				MessageView view;
				HistoryItem *item = nullptr;
				rpl::lifetime lifetime;
			};
			const auto state = lifetime.make_state<State>();
			state->item = item;
			item->history()->session().changes().realtimeMessageUpdates(
				Data::MessageUpdate::Flag::Destroyed
			) | rpl::on_next([=](const Data::MessageUpdate &update) {
				if (update.item == state->item) {
					state->lifetime.destroy();
					state->item = nullptr;
					preview->update();
				}
			}, state->lifetime);

			preview->resize(0, st::infoLabeled.style.font->height);
			preview->paintRequest(
			) | rpl::on_next([=] {
				auto p = Painter(preview);
				const auto item = state->item;
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
				if (!state->view.prepared(item, nullptr, nullptr)) {
					const auto repaint = [=] { preview->update(); };
					state->view.prepare(
						item,
						nullptr,
						nullptr,
						repaint,
						{});
				}
				state->view.paint(p, preview->rect(), {
					.st = &st::defaultDialogRow,
					.currentBg = st::boxBg->b,
				});
			}, preview->lifetime());

			line->sizeValue() | rpl::filter_size(
			) | rpl::on_next([=](const QSize &size) {
				const auto left = stLabeled.left();
				const auto right = st::infoPersonalChannelDateSkip;
				const auto top = stLabeled.top();
				date->moveToRight(right, top, size.width());

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
						st::infoProfilePersonalChannelPadding.left(),
						0,
						st::infoProfilePersonalChannelPadding.right(),
						st::infoProfilePersonalChannelPadding.bottom()));
			}
			{
				const auto button = Ui::CreateSimpleRectButton(
					messageChannelWrap->entity(),
					st::defaultRippleAnimation);
				inner->geometryValue(
				) | rpl::on_next([=](const QRect &rect) {
					button->setGeometry(rect);
				}, button->lifetime());
				const auto channelPeer = item->history()->peer;
				const auto msg = item->fullId().msg;
				const auto openInWindow = [=] {
					window->showInNewWindow(
						Window::SeparateId(channelPeer),
						msg);
				};
				const auto openInCurrent = [=] {
					window->showPeerHistory(
						channelPeer,
						Window::SectionShow::Way::Forward,
						msg);
				};
				button->setAcceptBoth();
				struct State {
					base::unique_qptr<Ui::PopupMenu> menu;
				};
				const auto state
					= button->lifetime().make_state<State>();
				button->addClickHandler([=](Qt::MouseButton mouse) {
					if (mouse == Qt::RightButton) {
						state->menu = base::make_unique_q<Ui::PopupMenu>(
							button,
							st::popupMenuWithIcons);
						state->menu->addAction(
							tr::lng_context_new_window(tr::now),
							[=] {
								base::call_delayed(
									st::popupMenuWithIcons.showDuration,
									crl::guard(button, openInWindow));
							},
							&st::menuIconNewWindow);
						state->menu->popup(QCursor::pos());
						return;
					}
					if (base::IsCtrlPressed()
						|| mouse == Qt::MiddleButton) {
						openInWindow();
					} else {
						openInCurrent();
					}
				});
				button->lower();
				inner->lifetime().make_state<base::unique_qptr<Ui::RpWidget>>(
					button);
				button->setAccessibleName(tr::lng_profile_view_channel(tr::now));
			}
			inner->setAttribute(Qt::WA_TransparentForMouseEvents);

			Ui::ToggleChildrenVisibility(messageChannelWrap->entity(), true);
			Ui::ToggleChildrenVisibility(line, true);
			messageChannelWrap->toggle(true, animated);
		};

		rpl::duplicate(
			channel
		) | rpl::on_next([=](ChannelData *channel) {
			if (!channel && messageChannelWrap->animating()) {
				base::call_delayed(duration, messageChannelWrap, clear);
			} else {
				clear();
			}
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

	const auto raw = result.data();
	return Section{
		.widget = std::move(result),
		.shown = raw->toggledValue(),
	};
}

void DetailsFiller::addMainApp(not_null<UserData*> user) {
	const auto parent = _stack->layout();
	auto wrap = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		parent,
		object_ptr<Ui::VerticalLayout>(parent));
	const auto raw = wrap.data();
	const auto inner = raw->entity();
	const auto button = inner->add(
		object_ptr<Ui::RoundButton>(
			inner,
			tr::lng_profile_open_app(),
			st::infoOpenApp),
		st::infoOpenAppMargin,
		style::al_justify);
	button->setFullRadius(true);

	const auto controller = _controller->parentController();
	button->setClickedCallback([=] {
		user->session().attachWebView().open({
			.bot = user,
			.context = {
				.controller = controller,
				.maySkipConfirmation = true,
			},
			.source = InlineBots::WebViewSourceBotProfile(),
		});
	});

	const auto url = tr::lng_mini_apps_tos_url(tr::now);
	auto textProducer = rpl::combine(
		tr::lng_profile_open_app_about(
			lt_terms,
			tr::lng_profile_open_app_terms(tr::url(url)),
			tr::marked),
		user->session().changes().peerFlagsValue(
			user,
			Data::PeerUpdate::Flag::VerifyInfo)
	) | rpl::map([=](TextWithEntities text, auto) {
		if (const auto verify = user->botVerifyDetails()) {
			text = text.append(u"\n\n"_q).append(verify->description);
		}
		return text;
	});
	auto setup = [url](not_null<Ui::FlatLabel*> label) {
		label->setClickHandlerFilter([=](const auto &...) {
			UrlClickHandler::Open(url);
			return false;
		});
	};

	_stack->add(Section{
		.widget = std::move(wrap),
		.shown = rpl::single(true),
	});
	_stack->addTextSeparator(
		std::move(textProducer),
		rpl::single(true),
		std::move(setup));
}

Section DetailsFiller::makeBotPermissions(not_null<UserData*> user) {
	const auto parent = _stack->layout();
	auto wrap = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		parent,
		object_ptr<Ui::VerticalLayout>(parent));
	const auto raw = wrap.data();
	const auto inner = raw->entity();
	AddSkip(inner);
	AddSubsectionTitle(inner, tr::lng_profile_bot_permissions_title());
	const auto emoji = inner->add(
		object_ptr<Ui::SettingsButton>(
			inner,
			tr::lng_profile_bot_emoji_status_access(),
			st::infoSharedMediaButton));
	object_ptr<Profile::FloatingIcon>(
		emoji,
		st::infoIconEmojiStatusAccess,
		st::infoSharedMediaButtonIconPosition);

	emoji->toggleOn(
		rpl::single(bool(user->botInfo->canManageEmojiStatus))
	)->toggledValue() | rpl::filter([=](bool allowed) {
		return allowed != user->botInfo->canManageEmojiStatus;
	}) | rpl::on_next([=](bool allowed) {
		user->botInfo->canManageEmojiStatus = allowed;
		const auto session = &user->session();
		session->api().request(MTPbots_ToggleUserEmojiStatusPermission(
			user->inputUser(),
			MTP_bool(allowed)
		)).send();
	}, emoji->lifetime());
	AddSkip(inner);
	return Section{
		.widget = std::move(wrap),
		.shown = rpl::single(true),
	};
}

Section DetailsFiller::makeAddAsContact(not_null<UserData*> user) {
	const auto parent = _stack->layout();
	auto wrap = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		parent,
		object_ptr<Ui::VerticalLayout>(parent));
	const auto raw = wrap.data();
	AddMainButton(
		raw->entity(),
		tr::lng_info_add_as_contact(),
		CanAddContactValue(user),
		[=, controller = _controller->parentController()] {
			controller->uiShow()->show(
				Box(EditContactBox, controller, user));
		},
		nullptr,
		nullptr);
	raw->toggleOn(CanAddContactValue(user));
	return Section{
		.widget = std::move(wrap),
		.shown = raw->toggledValue(),
	};
}

void DetailsFiller::addBotVerify() {
	const auto peer = _peer.get();
	auto shown = peer->session().changes().peerFlagsValue(
		peer,
		Data::PeerUpdate::Flag::VerifyInfo
			| Data::PeerUpdate::Flag::FullInfo
	) | rpl::map([=] {
		const auto info = peer->botVerifyDetails();
		if (!info || info->description.empty()) {
			return false;
		}
		if (const auto user = peer->asUser()) {
			if (user->botInfo && user->botInfo->hasMainApp) {
				return false;
			}
		}
		return true;
	}) | rpl::distinct_until_changed();

	auto description = peer->session().changes().peerFlagsValue(
		peer,
		Data::PeerUpdate::Flag::VerifyInfo
	) | rpl::map([=] {
		const auto info = peer->botVerifyDetails();
		return info ? info->description : TextWithEntities();
	});

	_stack->addTextSeparator(std::move(description), std::move(shown));
}

void DetailsFiller::addManagedBotFooter(not_null<UserData*> managerUser) {
	const auto botUsername = managerUser->username();
	const auto linkText = botUsername.isEmpty()
		? managerUser->name()
		: (u"@"_q + botUsername);
	auto text = tr::lng_managed_bot_label(
		lt_icon,
		rpl::single(Ui::Text::IconEmoji(&st::managedBotIconEmoji)),
		lt_bot,
		rpl::single(tr::link(linkText)),
		tr::marked);
	const auto weak = base::make_weak(_controller);
	auto setup = [=](not_null<Ui::FlatLabel*> label) {
		label->setClickHandlerFilter([=](const auto &...) {
			if (const auto strong = weak.get()) {
				strong->showPeerInfo(managerUser);
			}
			return false;
		});
	};
	_stack->addTextSeparator(
		std::move(text),
		rpl::single(true),
		std::move(setup));
}

Section DetailsFiller::makeReportOrDeleteReaction() {
	if (_peer->isSelf()) {
		return Section{ .widget = nullptr };
	}
	auto result = Section{ .widget = nullptr };
	v::match(_origin.data, [&](GroupReactionOrigin data) {
		if (HistoryView::Reactions::CanModerateReactionByDeleteMessages(
				data.group)) {
			result = makeDeleteReactionSection(data);
			return;
		}
		const auto capabilities = Api::GetReactionReportCapabilities(
			data.group,
			_peer);
		if (capabilities.canReport) {
			result = makeReportReactionSection(data, capabilities.canBan);
		}
	}, [](const auto &) {});
	return result;
}

Section DetailsFiller::makeDeleteReactionSection(GroupReactionOrigin data) {
	const auto parent = _stack->layout();
	const auto peer = _peer;
	const auto controller = _controller->parentController();
	auto wrap = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		parent,
		object_ptr<Ui::VerticalLayout>(parent));
	const auto raw = wrap.data();
	auto shown = rpl::single(true);
	raw->toggleOn(rpl::duplicate(shown));
	AddMainButton(
		raw->entity(),
		tr::lng_context_delete_this_reaction(),
		std::move(shown),
		[=] {
			HistoryView::Reactions::ShowModerateReactionBox(
				controller,
				data.group,
				data.messageId,
				peer);
		},
		nullptr,
		nullptr,
		st::infoMainButtonAttention);
	return Section{
		.widget = std::move(wrap),
		.shown = raw->toggledValue(),
	};
}

Section DetailsFiller::makeReportReactionSection(
		GroupReactionOrigin data,
		bool ban) {
	const auto parent = _stack->layout();
	const auto peer = _peer;
	const auto controller = _controller->parentController();
	const auto forceHidden = std::make_shared<rpl::variable<bool>>(false);
	const auto user = peer->asUser();
	auto wrap = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		parent,
		object_ptr<Ui::VerticalLayout>(parent));
	const auto raw = wrap.data();
	auto shown = user
		? rpl::combine(
			Info::Profile::IsContactValue(user),
			forceHidden->value(),
			!rpl::mappers::_1 && !rpl::mappers::_2
		) | rpl::type_erased
		: (forceHidden->value() | rpl::map(!rpl::mappers::_1));
	const auto sent = [=] {
		*forceHidden = true;
	};
	raw->toggleOn(rpl::duplicate(shown));
	AddMainButton(
		raw->entity(),
		(ban
			? tr::lng_report_and_ban()
			: tr::lng_report_reaction()),
		std::move(shown),
		[=] { controller->show(
			Box(ReportReactionBox, controller, peer, data, ban, sent)); },
		nullptr,
		nullptr,
		st::infoMainButtonAttention);
	return Section{
		.widget = std::move(wrap),
		.shown = raw->toggledValue(),
	};
}

Section DetailsFiller::makeViewChannel(not_null<ChannelData*> channel) {
	using namespace rpl::mappers;

	const auto parent = _stack->layout();
	auto wrap = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		parent,
		object_ptr<Ui::VerticalLayout>(parent));
	const auto raw = wrap.data();

	const auto window = _controller->parentController();
	auto activePeerValue = window->activeChatValue(
	) | rpl::map([](Dialogs::Key key) {
		return key.peer();
	});
	auto viewChannelVisible = rpl::combine(
		_controller->wrapValue(),
		std::move(activePeerValue),
		(_1 != Wrap::Side) || (_2 != channel));
	const auto openInWindow = [=] {
		window->showInNewWindow(Window::SeparateId(channel));
	};
	const auto openInCurrent = [=] {
		window->showPeerHistory(
			channel,
			Window::SectionShow::Way::Forward);
	};
	struct State {
		base::unique_qptr<Ui::PopupMenu> menu;
	};
	const auto state = raw->lifetime().make_state<State>();
	auto viewChannel = [=](Qt::MouseButton mouse) {
		if (mouse == Qt::RightButton) {
			return;
		}
		if (base::IsCtrlPressed() || mouse == Qt::MiddleButton) {
			openInWindow();
		} else {
			openInCurrent();
		}
	};
	raw->toggleOn(rpl::duplicate(viewChannelVisible));
	const auto button = AddMainButton(
		raw->entity(),
		tr::lng_profile_view_channel(),
		std::move(viewChannelVisible),
		std::move(viewChannel),
		nullptr,
		nullptr);
	button->setAcceptBoth();
	button->addClickHandler([=](Qt::MouseButton mouse) {
		if (mouse != Qt::RightButton) {
			return;
		}
		state->menu = base::make_unique_q<Ui::PopupMenu>(
			button,
			st::popupMenuWithIcons);
		state->menu->addAction(
			tr::lng_context_new_window(tr::now),
			[=] {
				base::call_delayed(
					st::popupMenuWithIcons.showDuration,
					crl::guard(button, openInWindow));
			},
			&st::menuIconNewWindow);
		state->menu->popup(QCursor::pos());
	});
	return Section{
		.widget = std::move(wrap),
		.shown = raw->toggledValue(),
	};
}

Section DetailsFiller::makeCommunityLink(not_null<PeerData*> peer) {
	const auto parent = _stack->layout();
	auto wrap = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		parent,
		object_ptr<Ui::VerticalLayout>(parent));
	const auto raw = wrap.data();
	const auto container = raw->entity();
	const auto window = _controller->parentController();
	const auto community = peer->owner().channel(
		Data::PeerLinkedCommunityId(peer));

	class Controller final : public PeerListController {
	public:
		Controller(not_null<ChannelData*> community, Fn<void()> open)
		: _community(community)
		, _open(std::move(open)) {
			setStyleOverrides(&st::peerListSingleRow);
		}

		Main::Session &session() const override {
			return _community->session();
		}
		void prepare() override {
			auto row = std::make_unique<PeerListRow>(_community);
			const auto rawRow = row.get();
			const auto updateStatus = [=] {
				const auto info = _community->communityInfo();
				const auto count = info
					? int(info->linkedPeers().size())
					: 0;
				rawRow->setCustomStatus(count
					? tr::lng_community_profile_status(
						tr::now,
						lt_count,
						count)
					: tr::lng_community_title(tr::now));
			};
			updateStatus();
			delegate()->peerListAppendRow(std::move(row));
			delegate()->peerListRefreshRows();
			_community->session().changes().peerUpdates(
				_community,
				Data::PeerUpdate::Flag::FullInfo
			) | rpl::on_next([=] {
				updateStatus();
				delegate()->peerListUpdateRow(rawRow);
			}, lifetime());
		}
		void rowClicked(not_null<PeerListRow*> row) override {
			_open();
		}

	private:
		const not_null<ChannelData*> _community;
		Fn<void()> _open;

	};

	const auto delegate = container->lifetime().make_state<
		PeerListContentDelegateSimple
	>();
	const auto controller = container->lifetime().make_state<Controller>(
		community,
		[=] { window->showPeerInfo(community); });
	const auto content = container->add(object_ptr<PeerListContent>(
		container,
		controller));
	delegate->setContent(content);
	controller->setDelegate(delegate);

	if (!community->wasFullUpdated()) {
		community->session().api().requestFullPeer(community);
	}

	raw->toggle(true, anim::type::instant);
	return Section{
		.widget = std::move(wrap),
		.shown = raw->toggledValue(),
	};
}

void DetailsFiller::addCommunityHiddenNote() {
	const auto peer = _peer.get();
	const auto community = peer->owner().channel(
		Data::PeerLinkedCommunityId(peer));
	auto shown = peer->session().changes().peerFlagsValue(
		community,
		Data::PeerUpdate::Flag::FullInfo
	) | rpl::map([=] {
		return community->communityInfo();
	}) | rpl::map([=](Data::CommunityInfo *info) -> rpl::producer<bool> {
		if (!info) {
			return rpl::single(false);
		}
		return info->linkedPeersValue() | rpl::map([=] {
			return info->isHidden(peer);
		});
	}) | rpl::flatten_latest() | rpl::distinct_until_changed();

	_stack->addTextSeparator(
		tr::lng_community_hidden_chat_about(tr::marked),
		std::move(shown));
}

Section DetailsFiller::makeTopicsList(not_null<Data::Forum*> forum) {
	using namespace rpl::mappers;

	const auto parent = _stack->layout();
	auto wrap = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		parent,
		object_ptr<Ui::VerticalLayout>(parent));
	const auto raw = wrap.data();
	const auto window = _controller->parentController();
	const auto peer = forum->peer();
	auto showTopicsVisible = rpl::combine(
		window->adaptive().oneColumnValue(),
		window->shownForum().value(),
		_1 || (_2 != forum));
	const auto callback = [=] {
		if (const auto forum = peer->forum()) {
			if (peer->useSubsectionTabs()) {
				window->searchInChat(forum->history());
			} else {
				window->showForum(forum);
			}
		}
	};
	raw->toggleOn(rpl::duplicate(showTopicsVisible));
	AddMainButton(
		raw->entity(),
		(forum->peer()->isBot()
			? tr::lng_bot_show_threads_list()
			: tr::lng_forum_show_topics_list()),
		std::move(showTopicsVisible),
		callback,
		nullptr,
		nullptr);
	return Section{
		.widget = std::move(wrap),
		.shown = raw->toggledValue(),
	};
}

void DetailsFiller::buildSections() {
	Expects(!_topic || !_topic->creating());

	if (const auto user = _sublist ? nullptr : _peer->asUser()) {
		_stack->add(makePersonalChannel(user));
		_stack->addPlainSeparator();
	}
	if (Data::PeerLinkedCommunityId(_peer)) {
		_stack->add(makeCommunityLink(_peer));
		addCommunityHiddenNote();
		_stack->addPlainSeparator();
	}
	_stack->add(makeInfo());
	if (const auto user = _peer->asUser()) {
		_stack->add(makeAddAsContact(user));
		addBotVerify();
		if (const auto info = user->botInfo.get()) {
			if (info->hasMainApp) {
				addMainApp(user);
			}
			if (info->canManageEmojiStatus) {
				_stack->add(makeBotPermissions(user));
			}
			if (const auto id = user->botManagerId()) {
				if (const auto mgr = user->owner().userLoaded(id)) {
					addManagedBotFooter(mgr);
				}
			}
		}
		if (!_sublist) {
			auto reactionSection = makeReportOrDeleteReaction();
			if (reactionSection.widget) {
				_stack->add(std::move(reactionSection));
			}
		}
	} else if (const auto channel = _peer->asChannel()) {
		addBotVerify();
		if (!channel->isMegagroup()) {
			_stack->add(makeViewChannel(channel));
		}
		if (const auto forum = channel->forum()) {
			_stack->add(makeTopicsList(forum));
		}
	}
}

ActionsFiller::ActionsFiller(
	not_null<Controller*> controller,
	not_null<Ui::RpWidget*> parent,
	not_null<PeerData*> peer)
: _controller(controller)
, _parent(parent)
, _peer(peer) {
}

void ActionsFiller::addAffiliateProgram(not_null<UserData*> user) {
	if (!user->isBot()) {
		return;
	}

	const auto wrap = _wrap->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			_wrap.data(),
			object_ptr<Ui::VerticalLayout>(_wrap.data())));
	const auto inner = wrap->entity();
	auto program = user->session().changes().peerFlagsValue(
		user,
		Data::PeerUpdate::Flag::StarRefProgram
	) | rpl::map([=] {
		return user->botInfo->starRefProgram;
	}) | rpl::start_spawning(inner->lifetime());
	auto commission = rpl::duplicate(
		program
	) | rpl::filter([=](StarRefProgram program) {
		return program.commission > 0;
	}) | rpl::map([=](StarRefProgram program) {
		return Info::BotStarRef::FormatCommission(program.commission);
	});
	const auto show = _controller->uiShow();

	struct StarRefRecipients {
		std::vector<not_null<PeerData*>> list;
		bool requested = false;
		Fn<void()> open;
	};
	const auto recipients = std::make_shared<StarRefRecipients>();
	recipients->open = [=] {
		if (!recipients->list.empty()) {
			const auto program = user->botInfo->starRefProgram;
			show->show(Info::BotStarRef::JoinStarRefBox(
				{ user, { program } },
				user->session().user(),
				recipients->list));
		} else if (!recipients->requested) {
			recipients->requested = true;
			const auto done = [=](std::vector<not_null<PeerData*>> list) {
				recipients->list = std::move(list);
				recipients->open();
			};
			Info::BotStarRef::ResolveRecipients(&user->session(), done);
		}
	};

	inner->add(EditPeerInfoBox::CreateButton(
		inner,
		tr::lng_manage_peer_bot_star_ref(),
		rpl::duplicate(commission),
		recipients->open,
		st::infoSharedMediaCountButton,
		{ .icon = &st::menuIconSharing }));
	Ui::AddSkip(inner);
	Ui::AddDividerText(
		inner,
		tr::lng_manage_peer_bot_star_ref_about(
			lt_bot,
			rpl::single(TextWithEntities{ user->name() }),
			lt_amount,
			rpl::duplicate(commission) | rpl::map(tr::marked),
			tr::rich));
	Ui::AddSkip(inner);

	wrap->toggleOn(std::move(
		program
	) | rpl::map([](StarRefProgram program) {
		return program.commission > 0;
	}));
	wrap->finishAnimating();
}

void ActionsFiller::addBalanceActions(not_null<UserData*> user) {
	const auto wrap = _wrap->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			_wrap.data(),
			object_ptr<Ui::VerticalLayout>(_wrap.data())));
	const auto inner = wrap->entity();
	Ui::AddSubsectionTitle(inner, tr::lng_manage_peer_bot_balance());
	auto currencyBalance = AddCurrencyAction(user, inner, _controller);
	auto creditsBalance = AddCreditsAction(user, inner, _controller);
	Ui::AddSkip(inner);
	Ui::AddDivider(inner);
	Ui::AddSkip(inner);
	wrap->toggleOn(
		rpl::combine(
			std::move(currencyBalance),
			std::move(creditsBalance)
		) | rpl::map((rpl::mappers::_1 > CreditsAmount(0))
			|| (rpl::mappers::_2 > CreditsAmount(0))));
}

void ActionsFiller::addInviteToGroupAction(not_null<UserData*> user) {
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
	const auto edit = [=] {
		if (controller->showFrozenError()) {
			return;
		}
		controller->window().show(Box(EditContactBox, controller, user));
	};
	AddActionButton(
		_wrap,
		tr::lng_info_edit_contact(),
		IsContactValue(user),
		edit,
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

void ActionsFiller::addFastButtonsMode(not_null<UserData*> user) {
	Expects(user->isBot());

	const auto bots = &user->session().fastButtonsBots();
	const auto button = _wrap->add(object_ptr<Ui::SettingsButton>(
		_wrap,
		rpl::single(u"Fast buttons mode"_q),
		st::infoSharedMediaButton));
	object_ptr<Info::Profile::FloatingIcon>(
		button,
		st::infoIconMediaBot,
		st::infoSharedMediaButtonIconPosition);

	AddSkip(_wrap);
	AddDivider(_wrap);
	AddSkip(_wrap);

	button->toggleOn(bots->enabledValue(user));
	button->toggledValue(
	) | rpl::filter([=](bool value) {
		return value != bots->enabled(user);
	}) | rpl::on_next([=](bool value) {
		bots->setEnabled(user, value);
	}, button->lifetime());
}

void ActionsFiller::addBotCommandActions(not_null<UserData*> user) {
	if (FastButtonsMode()) {
		addFastButtonsMode(user);
	}
	const auto window = _controller->parentController();
	const auto findBotCommand = [user](const QString &command) {
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
	const auto hasBotCommandValue = [=](const QString &command) {
		return user->session().changes().peerFlagsValue(
			user,
			Data::PeerUpdate::Flag::BotCommands
		) | rpl::map([=] {
			return !findBotCommand(command).isEmpty();
		});
	};
	const auto makeOtherContext = [=] {
		return QVariant::fromValue(ClickHandlerContext{
			.sessionWindow = base::make_weak(window),
			.peer = user,
		});
	};
	const auto sendBotCommand = [=](const QString &command) {
		const auto original = findBotCommand(command);
		if (original.isEmpty()) {
			return false;
		}
		BotCommandClickHandler('/' + original).onClick(ClickContext{
			Qt::LeftButton,
			makeOtherContext()
		});
		return true;
	};
	const auto addBotCommand = [=](
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
	addBotCommand(
		tr::lng_profile_bot_settings(),
		u"settings"_q,
		&st::infoIconSettings);
	//addBotCommand(tr::lng_profile_bot_privacy(), u"privacy"_q);
	const auto openUrl = [=](const QString &url) {
		Core::App().iv().openWithIvPreferred(
			&user->session(),
			url,
			makeOtherContext());
	};
	const auto openPrivacyPolicy = [=] {
		if (const auto info = user->botInfo.get()) {
			if (!info->privacyPolicyUrl.isEmpty()) {
				openUrl(info->privacyPolicyUrl);
				return;
			}
		}
		if (!sendBotCommand(u"privacy"_q)) {
			openUrl(tr::lng_profile_bot_privacy_url(tr::now));
		}
	};
	AddActionButton(
		_wrap,
		tr::lng_profile_bot_privacy(),
		rpl::single(true),
		openPrivacyPolicy,
		&st::infoIconPrivacyPolicy);
}

void ActionsFiller::addReportAction() {
	const auto peer = _peer;
	const auto controller = _controller->parentController();
	const auto report = [=] {
		ShowReportMessageBox(controller->uiShow(), peer, {}, {});
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
		addAffiliateProgram(user);
		addBalanceActions(user);
		addInviteToGroupAction(user);
	}
	addShareContactAction(user);
	if (!user->isSelf()) {
		addEditContactAction(user);
		addDeleteContactAction(user);
	}
	if (!user->isSelf() && !user->isSupport() && !user->isVerifyCodes()) {
		if (user->isBot()) {
			addBotCommandActions(user);
			_wrap->add(CreateSkipWidget(_wrap, st::infoBlockButtonSkip));
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
		callback();
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
const char kOptionShowChannelJoinedBelowAbout[] = "show-channel-joined-below-about";

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
	add->setAccessibleName(tr::lng_channel_add_members(tr::now));
	add->showOn(CanAddMemberValue(channel));
	add->addClickHandler([=] {
		Window::PeerMenuAddChannelMembers(navigation, channel);
	});
	parent->widthValue(
	) | rpl::on_next([add](int newWidth) {
		auto availableWidth = newWidth
			- st::infoMembersButtonPosition.x();
		add->moveToLeft(
			availableWidth - add->width(),
			st::infoMembersButtonPosition.y(),
			newWidth);
	}, add->lifetime());
}

object_ptr<Ui::RpWidget> SetupChannelMembersAndManage(
		not_null<Controller*> controller,
		not_null<Ui::RpWidget*> parent,
		not_null<PeerData*> peer) {
	using namespace rpl::mappers;

	auto channel = peer->asChannel();
	if (!channel || channel->isMegagroup()) {
		return { nullptr };
	}

	auto result = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		parent,
		object_ptr<Ui::VerticalLayout>(parent));
	result->entity()->add(CreateSkipWidget(result));

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
			Info::Section::Type::Members));
	};

	const auto membersWrap = result->entity()->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			result->entity(),
			object_ptr<Ui::VerticalLayout>(result->entity())));
	membersWrap->setDuration(
		st::infoSlideDuration
	)->toggleOn(rpl::duplicate(membersShown));

	const auto members = membersWrap->entity();
	{
		auto button = AddActionButton(
			members,
			std::move(membersText),
			rpl::single(true),
			std::move(membersCallback),
			nullptr)->entity();

		SetupAddChannelMember(controller, button, channel);
	}

	object_ptr<FloatingIcon>(
		members,
		st::infoIconMembers,
		st::infoChannelMembersIconPosition);

	auto adminsShown = peer->session().changes().peerFlagsValue(
		channel,
		Data::PeerUpdate::Flag::Rights
	) | rpl::map([=] { return channel->canViewAdmins(); });
	auto adminsText = tr::lng_profile_administrators(
		lt_count_decimal,
		Info::Profile::MigratedOrMeValue(
			channel
		) | rpl::map(
			Info::Profile::AdminsCountValue
		) | rpl::flatten_latest() | tr::to_count());
	auto adminsCallback = [=] {
		ParticipantsBoxController::Start(
			controller,
			channel,
			ParticipantsBoxController::Role::Admins);
	};

	const auto adminsWrap = result->entity()->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			result->entity(),
			object_ptr<Ui::VerticalLayout>(result->entity())));
	adminsWrap->setDuration(
		st::infoSlideDuration
	)->toggleOn(rpl::duplicate(adminsShown));

	const auto admins = adminsWrap->entity();
	AddActionButton(
		admins,
		std::move(adminsText),
		rpl::single(true),
		std::move(adminsCallback),
		nullptr);

	object_ptr<FloatingIcon>(
		admins,
		st::menuIconAdmin,
		st::infoChannelAdminsIconPosition);

	const auto canViewBalance = false
		|| (channel->flags() & ChannelDataFlag::CanViewRevenue)
		|| (channel->flags() & ChannelDataFlag::CanViewCreditsRevenue)
		|| (channel->loadedStatus() != ChannelData::LoadedStatus::Full);
	if (canViewBalance) {
		const auto balanceWrap = result->entity()->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				result->entity(),
				object_ptr<Ui::VerticalLayout>(result->entity())));
		auto refreshed = channel->session().credits().refreshedByPeerId(
			channel->id);
		auto creditsValue = rpl::single(
			rpl::empty_value()
		) | rpl::then(rpl::duplicate(refreshed)) | rpl::map([=] {
			return channel->session().credits().balance(channel->id);
		});
		auto currencyValue = rpl::single(
			rpl::empty_value()
		) | rpl::then(rpl::duplicate(refreshed)) | rpl::map([=] {
			return channel->session().credits().balanceCurrency(channel->id);
		});
		const auto emptyAmount = CreditsAmount(0);
		balanceWrap->toggleOn(
			rpl::combine(
				rpl::duplicate(creditsValue),
				rpl::duplicate(currencyValue)
			) | rpl::map(rpl::mappers::_1 > emptyAmount
				|| rpl::mappers::_2 > emptyAmount),
			anim::type::normal);
		balanceWrap->finishAnimating();

		const auto &st = st::infoSharedMediaButton;

		auto customEmojiFactory = [height = st.style.font->height,
				font = st.rightLabel.style.font,
				color = st.rightLabel.textFg->c](
			QStringView data,
			const Ui::Text::MarkedContext &context
		) -> std::unique_ptr<Ui::Text::CustomEmoji> {
			return (data == Ui::kCreditsCurrency)
				? Ui::MakeCreditsIconEmoji(height, 1)
				: MakeWrappedEmoji<Ui::Text::ShiftedEmoji>(
					Ui::Earn::MakeCurrencyIconEmoji(font, color),
					QPoint(0, st::channelEarnCurrencyCommonMargins.top()));
		};
		const auto context = Ui::Text::MarkedContext{
			.customEmojiFactory = std::move(customEmojiFactory),
		};

		const auto balance = balanceWrap->entity();
		const auto button = AddActionButton(
			balance,
			tr::lng_manage_peer_bot_balance(),
			rpl::single(true),
			[=] { controller->showSection(Info::ChannelEarn::Make(peer)); },
			nullptr);

		::Settings::CreateRightLabel(
			button->entity(),
			rpl::combine(
				std::move(creditsValue),
				std::move(currencyValue)
			) | rpl::map([](CreditsAmount credits, CreditsAmount currency) {
				auto creditsText = (credits > CreditsAmount(0))
					? Ui::MakeCreditsIconEntity()
						.append(QChar(' '))
						.append(Info::ChannelEarn::MajorPart(credits))
						.append(credits.nano()
							? Info::ChannelEarn::MinorPart(credits)
							: QString())
					: TextWithEntities();
				auto currencyText = (currency > CreditsAmount(0))
					? Ui::Text::SingleCustomEmoji("_")
						.append(QChar(' '))
						.append(Info::ChannelEarn::MajorPart(currency))
						.append(Info::ChannelEarn::MinorPart(currency))
					: TextWithEntities();
				return currencyText
					.append(QChar(' '))
					.append(std::move(creditsText));
			}),
			st,
			tr::lng_manage_peer_bot_balance(),
			context);

		object_ptr<FloatingIcon>(
			balance,
			st::menuIconEarn,
			st::infoChannelAdminsIconPosition);
	}

	result->setDuration(st::infoSlideDuration)->toggleOn(
		rpl::combine(
			std::move(membersShown),
			std::move(adminsShown)
		) | rpl::map(rpl::mappers::_1 || rpl::mappers::_2));

	result->entity()->add(CreateSkipWidget(result));

	return result;
}

void BuildProfileDetailsSections(
		SectionStack &stack,
		not_null<Controller*> controller,
		not_null<PeerData*> peer,
		Data::ForumTopic *topic,
		Data::SavedSublist *sublist,
		Origin origin) {
	if (topic) {
		DetailsFiller filler(controller, &stack, topic);
		filler.buildSections();
	} else if (sublist) {
		DetailsFiller filler(controller, &stack, sublist);
		filler.buildSections();
	} else {
		DetailsFiller filler(controller, &stack, peer, origin);
		filler.buildSections();
	}
}

void AddDetails(
		not_null<Ui::VerticalLayout*> container,
		not_null<Controller*> controller,
		not_null<PeerData*> peer,
		Data::ForumTopic *topic,
		Data::SavedSublist *sublist,
		Origin origin) {
	auto layout = object_ptr<Ui::VerticalLayout>(container);
	auto stack = SectionStack(layout.data());
	BuildProfileDetailsSections(
		stack,
		controller,
		peer,
		topic,
		sublist,
		origin);
	stack.finalize();
	container->add(std::move(layout));
}

} // namespace Profile
} // namespace Info
