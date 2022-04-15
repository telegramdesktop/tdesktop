/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "menu/menu_mute.h"

#include "boxes/ringtones_box.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/notify/data_notify_settings.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "ui/boxes/choose_time.h"
#include "ui/boxes/confirm_box.h"
#include "ui/boxes/time_picker_box.h"
#include "ui/effects/animation_value.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h" // infoTopBarMenu
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

namespace MuteMenu {

namespace {

constexpr auto kMuteDurSecondsDefault = crl::time(8) * 3600;

class IconWithText final : public Ui::Menu::Action {
public:
	using Ui::Menu::Action::Action;

	void setData(const QString &text, const QPoint &iconPosition);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	QPoint _iconPosition;
	QString _text;

};

void IconWithText::setData(const QString &text, const QPoint &iconPosition) {
	_iconPosition = iconPosition;
	_text = text;
}

void IconWithText::paintEvent(QPaintEvent *e) {
	Ui::Menu::Action::paintEvent(e);

	Painter p(this);
	p.setFont(st::menuIconMuteForAnyTextFont);
	p.setPen(st::menuIconColor);
	p.drawText(_iconPosition, _text);
}

class MuteItem final : public Ui::Menu::Action {
public:
	MuteItem(
		not_null<RpWidget*> parent,
		const style::Menu &st,
		not_null<PeerData*> peer);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const QPoint _itemIconPosition;
	Ui::Animations::Simple _animation;
	bool _isMuted = false;

};

MuteItem::MuteItem(
	not_null<RpWidget*> parent,
	const style::Menu &st,
	not_null<PeerData*> peer)
: Ui::Menu::Action(
	parent,
	st,
	Ui::CreateChild<QAction>(parent.get()),
	nullptr,
	nullptr)
, _itemIconPosition(st.itemIconPosition)
, _isMuted(peer->owner().notifySettings().isMuted(peer)) {

	Info::Profile::NotificationsEnabledValue(
		peer
	) | rpl::start_with_next([=](bool isUnmuted) {
		const auto isMuted = !isUnmuted;
		action()->setText(isMuted
			? tr::lng_mute_menu_duration_unmute(tr::now)
			: tr::lng_mute_menu_duration_forever(tr::now));
		if (isMuted == _isMuted) {
			return;
		}
		_isMuted = isMuted;
		_animation.start(
			[=] { update(); },
			isMuted ? 0. : 1.,
			isMuted ? 1. : 0.,
			st::defaultPopupMenu.showDuration);
	}, lifetime());

	setClickedCallback([=] {
		peer->owner().notifySettings().update(
			peer,
			_isMuted ? 0 : Data::PeerNotifySettings::kDefaultMutePeriod);
	});
}

void MuteItem::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto progress = _animation.value(_isMuted ? 1. : 0.);
	const auto color = anim::color(
		st::menuIconAttentionColor,
		st::settingsIconBg2,
		progress);
	p.setPen(color);
	Action::paintText(p);

	const auto &icon = _isMuted ? st::menuIconUnmute : st::menuIconMute;
	icon.paint(p, _itemIconPosition, width(), color);
}

void MuteBox(not_null<Ui::GenericBox*> box, not_null<PeerData*> peer) {
	struct State {
		int lastSeconds = 0;
	};

	auto chooseTimeResult = ChooseTimeWidget(box, kMuteDurSecondsDefault);
	box->addRow(std::move(chooseTimeResult.widget));

	const auto state = box->lifetime().make_state<State>();

	box->setTitle(tr::lng_mute_box_title());

	auto confirmText = std::move(
		chooseTimeResult.secondsValue
	) | rpl::map([=](int seconds) {
		state->lastSeconds = seconds;
		return !seconds
			? tr::lng_mute_menu_unmute()
			: tr::lng_mute_menu_mute();
	}) | rpl::flatten_latest();
	Ui::ConfirmBox(box, {
		.confirmed = [=] {
			peer->owner().notifySettings().update(peer, state->lastSeconds);
			box->getDelegate()->hideLayer();
		},
		.confirmText = std::move(confirmText),
		.cancelText = tr::lng_cancel(),
	});
}

void PickMuteBox(not_null<Ui::GenericBox*> box, not_null<PeerData*> peer) {
	struct State {
		base::unique_qptr<Ui::PopupMenu> menu;
	};
	const auto seconds = std::vector<TimeId>{
		(60 * 15),
		(60 * 30),
		(3600 * 1),
		(3600 * 2),
		(3600 * 3),
		(3600 * 4),
		(3600 * 8),
		(3600 * 12),
		(84600 * 1),
		(84600 * 2),
		(84600 * 3),
		(84600 * 7 * 1),
		(84600 * 7 * 2),
		(84600 * 30 * 1),
		(84600 * 30 * 2),
		(84600 * 30 * 3),
	};
	const auto phrases = ranges::views::all(
		seconds
	) | ranges::views::transform(Ui::FormatMuteFor) | ranges::to_vector;

	const auto state = box->lifetime().make_state<State>();

	const auto pickerCallback = TimePickerBox(box, seconds, phrases, 0);

	Ui::ConfirmBox(box, {
		.confirmed = [=] {
			const auto muteFor = pickerCallback();
			peer->owner().notifySettings().update(peer, muteFor);
			peer->session().settings().addMutePeriod(muteFor);
			peer->session().saveSettings();
			box->closeBox();
		},
		.confirmText = tr::lng_mute_menu_mute(),
		.cancelText = tr::lng_cancel(),
	});

	box->setTitle(tr::lng_mute_box_title());

	const auto top = box->addTopButton(st::infoTopBarMenu);
	top->setClickedCallback([=] {
		if (state->menu) {
			return;
		}
		state->menu = base::make_unique_q<Ui::PopupMenu>(
			top,
			st::popupMenuWithIcons);
		state->menu->addAction(
			tr::lng_manage_messages_ttl_after_custom(tr::now),
			[=] { box->getDelegate()->show(Box(MuteBox, peer)); },
			&st::menuIconCustomize);
		state->menu->setDestroyedCallback(crl::guard(top, [=] {
			top->setForceRippled(false);
		}));
		top->setForceRippled(true);
		state->menu->popup(QCursor::pos());
	});
}

} // namespace

void FillMuteMenu(
		not_null<Ui::PopupMenu*> menu,
		Args args) {
	const auto peer = args.peer;

	menu->addAction(
		tr::lng_mute_menu_sound_select(tr::now),
		[=, show = args.show] {
			show->showBox(Box(PeerRingtonesBox, peer));
		},
		&st::menuIconSoundSelect);

	const auto soundIsNone = peer->owner().notifySettings().sound(peer).none;
	menu->addAction(
		soundIsNone
			? tr::lng_mute_menu_sound_on(tr::now)
			: tr::lng_mute_menu_sound_off(tr::now),
		[=] {
			auto &notifySettings = peer->owner().notifySettings();
			auto sound = notifySettings.sound(peer);
			sound.none = !sound.none;
			notifySettings.update(peer, {}, {}, sound);
		},
		soundIsNone ? &st::menuIconSoundOn : &st::menuIconSoundOff);

	const auto &st = menu->st().menu;
	const auto iconTextPosition = st.itemIconPosition
		+ st::menuIconMuteForAnyTextPosition;
	for (const auto &muteFor : peer->session().settings().mutePeriods()) {
		const auto callback = [=] {
			peer->owner().notifySettings().update(peer, muteFor);
		};

		auto item = base::make_unique_q<IconWithText>(
			menu,
			st,
			Ui::Menu::CreateAction(
				menu->menu().get(),
				tr::lng_mute_menu_duration_any(
					tr::now,
					lt_duration,
					Ui::FormatMuteFor(muteFor)),
				callback),
			&st::menuIconMuteForAny,
			&st::menuIconMuteForAny);
		item->setData(Ui::FormatMuteForTiny(muteFor), iconTextPosition);
		menu->addAction(std::move(item));
	}

	menu->addAction(
		tr::lng_mute_menu_duration(tr::now),
		[=, show = args.show] { show->showBox(Box(PickMuteBox, peer)); },
		&st::menuIconMuteFor);

	menu->addAction(
		base::make_unique_q<MuteItem>(menu, menu->st().menu, peer));
}

void SetupMuteMenu(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<> triggers,
		Args args) {
	struct State {
		base::unique_qptr<Ui::PopupMenu> menu;
	};
	const auto state = parent->lifetime().make_state<State>();
	std::move(
		triggers
	) | rpl::start_with_next([=] {
		if (state->menu) {
			return;
		}
		state->menu = base::make_unique_q<Ui::PopupMenu>(
			parent,
			st::popupMenuWithIcons);
		FillMuteMenu(state->menu.get(), args);
		state->menu->popup(QCursor::pos());
	}, parent->lifetime());
}

} // namespace MuteMenu
