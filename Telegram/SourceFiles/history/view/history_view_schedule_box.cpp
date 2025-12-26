/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_schedule_box.h"

#include "api/api_common.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_peer.h"
#include "data/data_peer_values.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "base/event_filter.h"
#include "base/qt/qt_key_modifiers.h"
#include "base/unixtime.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/padding_wrap.h"
#include "main/main_session.h"
#include "menu/menu_send.h"
#include "settings/settings_premium.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_chat.h"
#include "styles/style_menu_icons.h"

namespace HistoryView::details {

not_null<Main::Session*> SessionFromShow(
		const std::shared_ptr<ChatHelpers::Show> &show) {
	return &show->session();
}

} // namespace HistoryView::details

namespace HistoryView {
namespace {

void FillSendUntilOnlineMenu(
		not_null<Ui::IconButton*> button,
		Fn<void()> callback,
		const ScheduleBoxStyleArgs &style) {
	const auto menu = std::make_shared<base::unique_qptr<Ui::PopupMenu>>();
	button->setClickedCallback([=] {
		*menu = base::make_unique_q<Ui::PopupMenu>(
			button,
			*style.popupMenuStyle);
		(*menu)->addAction(
			tr::lng_scheduled_send_until_online(tr::now),
			std::move(callback),
			&st::menuIconWhenOnline);
		(*menu)->popup(QCursor::pos());
		return true;
	});
}

} // namespace

ScheduleBoxStyleArgs::ScheduleBoxStyleArgs()
: topButtonStyle(&st::infoTopBarMenu)
, popupMenuStyle(&st::popupMenuWithIcons)
, chooseDateTimeArgs({}) {
}

TimeId DefaultScheduleTime() {
	return base::unixtime::now() + 600;
}

bool CanScheduleUntilOnline(not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		return !user->isSelf()
			&& !user->isBot()
			&& !user->lastseen().isHidden()
			&& !user->starsPerMessageChecked();
	}
	return false;
}

void ScheduleBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		std::shared_ptr<ChatHelpers::Show> maybeShow,
		const Api::SendOptions &initialOptions,
		const SendMenu::Details &details,
		Fn<void(Api::SendOptions)> done,
		TimeId time,
		ScheduleBoxStyleArgs style) {
	const auto repeat = std::make_shared<TimeId>(
		initialOptions.scheduleRepeatPeriod);
	const auto submit = [=](Api::SendOptions options) {
		if (!options.scheduled) {
			return;
		}
		// Pro tip: Hold Ctrl key to send a silent scheduled message!
		if (base::IsCtrlPressed()) {
			options.silent = true;
		}
		if (repeat) {
			options.scheduleRepeatPeriod = *repeat;
		}
		const auto copy = done;
		box->closeBox();
		copy(options);
	};
	const auto with = [=](TimeId scheduled) {
		auto result = initialOptions;
		result.scheduled = scheduled;
		return result;
	};
	auto descriptor = Ui::ChooseDateTimeBox(box, {
		.title = (details.type == SendMenu::Type::Reminder
			? tr::lng_remind_title()
			: tr::lng_schedule_title()),
		.submit = tr::lng_schedule_button(),
		.done = [=](TimeId result) { submit(with(result)); },
		.time = time,
		.style = style.chooseDateTimeArgs,
	});

	if (repeat) {
		const auto boxShow = box->uiShow();
		const auto showPremiumPromo = [=] {
			if (session->premium()) {
				return false;
			}
			Settings::ShowPremiumPromoToast(
				Main::MakeSessionShow(boxShow, session),
				ChatHelpers::ResolveWindowDefault(),
				tr::lng_schedule_repeat_promo(
					tr::now,
					lt_link,
					tr::link(
						tr::bold(
							tr::lng_schedule_repeat_promo_link(tr::now))),
					tr::rich),
				u"schedule_repeat"_q);
			return true;
		};
		auto locked = Data::AmPremiumValue(
			session
		) | rpl::map([=](bool premium) {
			return !premium;
		});
		const auto row = box->addRow(Ui::ChooseRepeatPeriod(box, {
			.value = session->premium() ? *repeat : TimeId(),
			.locked = std::move(locked),
			.filter = showPremiumPromo,
			.changed = [=](TimeId value) { *repeat = value; },
			.test = session->isTestMode(),
		}), style::al_top);
		std::move(descriptor.width) | rpl::on_next([=](int width) {
			row->setNaturalWidth(width);
		}, row->lifetime());
	}

	using namespace SendMenu;
	const auto childType = (details.type == Type::Disabled)
		? Type::Disabled
		: Type::SilentOnly;
	const auto childDetails = Details{
		.type = childType,
		.effectAllowed = details.effectAllowed,
	};
	const auto sendAction = crl::guard(box, [=](Action action, Details) {
		Expects(action.type == ActionType::Send);

		auto options = with(descriptor.collect());
		if (action.options.silent) {
			options.silent = action.options.silent;
		}
		if (action.options.effectId) {
			options.effectId = action.options.effectId;
		}
		submit(options);
	});
	SetupMenuAndShortcuts(
		descriptor.submit.data(),
		maybeShow,
		[=] { return childDetails; },
		sendAction);

	if (details.type == Type::ScheduledToUser) {
		const auto sendUntilOnline = box->addTopButton(*style.topButtonStyle);
		const auto timestamp = Api::kScheduledUntilOnlineTimestamp;
		FillSendUntilOnlineMenu(
			sendUntilOnline.data(),
			[=] { submit(with(timestamp)); },
			style);
	}
}

} // namespace HistoryView
