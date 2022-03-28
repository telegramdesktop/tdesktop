/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "menu/menu_mute.h"

#include "base/qt_signal_producer.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "menu/menu_check_item.h"
#include "ui/effects/animation_value.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/time_part_input_with_placeholder.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h" // infoTopBarMenu
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

namespace MuteMenu {

namespace {

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
, _isMuted(peer->owner().notifyIsMuted(peer)) {

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
		peer->owner().updateNotifySettings(
			peer,
			_isMuted ? 0 : Data::NotifySettings::kDefaultMutePeriod);
	});
}

void MuteItem::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto progress = _animation.value(_isMuted ? 1. : 0.);
	const auto color = anim::color(
		st::settingsIconBg1,
		st::settingsIconBg2,
		progress);
	p.setPen(color);
	Action::paintText(p);

	const auto &icon = _isMuted ? st::menuIconUnmute : st::menuIconMute;
	icon.paint(p, _itemIconPosition, width(), color);
}

void FillSoundMenu(
		not_null<Ui::PopupMenu*> menu,
		not_null<PeerData*> peer,
		rpl::producer<QString> &&soundOnText,
		rpl::producer<QString> &&soundOffText,
		Fn<void(bool)> notifySound) {
	const auto createView = [&](rpl::producer<QString> &&text, bool checked) {
		auto item = base::make_unique_q<Menu::ItemWithCheck>(
			menu->menu(),
			st::popupMenuWithIcons.menu,
			new QAction(QString(), menu->menu()),
			nullptr,
			nullptr);
		std::move(
			text
		) | rpl::start_with_next([action = item->action()](QString text) {
			action->setText(text);
		}, item->lifetime());
		item->init(checked);
		const auto view = item->checkView();
		menu->addAction(std::move(item));
		return view;
	};

	const auto soundIsNone = peer->owner().notifySoundIsNone(peer);
	const auto soundOn = createView(std::move(soundOnText), !soundIsNone);
	const auto soundOff = createView(std::move(soundOffText), soundIsNone);

	soundOn->checkedChanges(
	) | rpl::start_with_next([=](bool checked) {
		soundOff->setChecked(!checked, anim::type::normal);
		notifySound(!checked);
	}, menu->lifetime());
	soundOff->checkedChanges(
	) | rpl::start_with_next([=](bool checked) {
		soundOn->setChecked(!checked, anim::type::normal);
		notifySound(checked);
	}, menu->lifetime());
}

void MuteBox(not_null<Ui::GenericBox*> box, not_null<PeerData*> peer) {
	using TimeField = Ui::TimePartWithPlaceholder;
	const auto putNext = [](not_null<TimeField*> field, QChar ch) {
		field->setCursorPosition(0);
		if (ch.unicode()) {
			field->setText(ch + field->getLastText());
			field->setCursorPosition(1);
		}
		field->onTextEdited();
		field->setFocus();
	};

	const auto erasePrevious = [](not_null<TimeField*> field) {
		const auto text = field->getLastText();
		if (!text.isEmpty()) {
			field->setCursorPosition(text.size() - 1);
			field->setText(text.mid(0, text.size() - 1));
		}
		field->setFocus();
	};

	struct State {
		not_null<TimeField*> day;
		not_null<TimeField*> hour;
		not_null<TimeField*> minute;

		base::unique_qptr<Ui::PopupMenu> menu;

		rpl::variable<bool> noSoundChanges;
		int valueInSeconds = 0;
	};

	const auto content = box->addRow(
		object_ptr<Ui::FixedHeightWidget>(box, st::scheduleHeight));

	const auto state = box->lifetime().make_state<State>(State{
		.day = Ui::CreateChild<TimeField>(
			content,
			st::muteBoxTimeField,
			rpl::never<QString>(),
			QString::number(0)),
		.hour = Ui::CreateChild<TimeField>(
			content,
			st::muteBoxTimeField,
			rpl::never<QString>(),
			QString::number(0)),
		.minute = Ui::CreateChild<TimeField>(
			content,
			st::muteBoxTimeField,
			rpl::never<QString>(),
			QString::number(0)),
		.noSoundChanges = false,
	});

	const auto day = Ui::MakeWeak(state->day);
	const auto hour = Ui::MakeWeak(state->hour);
	const auto minute = Ui::MakeWeak(state->minute);

	day->setPhrase(tr::lng_mute_box_days);
	day->setMaxValue(31);
	day->setWheelStep(1);
	day->putNext() | rpl::start_with_next([=](QChar ch) {
		putNext(hour, ch);
	}, box->lifetime());

	hour->setPhrase(tr::lng_mute_box_hours);
	hour->setMaxValue(23);
	hour->setWheelStep(1);
	hour->putNext() | rpl::start_with_next([=](QChar ch) {
		putNext(minute, ch);
	}, box->lifetime());
	hour->erasePrevious() | rpl::start_with_next([=] {
		erasePrevious(day);
	}, box->lifetime());

	minute->setPhrase(tr::lng_mute_box_minutes);
	minute->setMaxValue(59);
	minute->setWheelStep(10);
	minute->erasePrevious() | rpl::start_with_next([=] {
		erasePrevious(hour);
	}, box->lifetime());

	content->sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		const auto inputWidth = s.width() / 3;
		auto rect = QRect(
			0,
			(s.height() - day->height()) / 2,
			inputWidth,
			day->height());
		for (const auto &input : { day, hour, minute }) {
			input->setGeometry(rect - st::muteBoxTimeFieldPadding);
			rect.translate(inputWidth, 0);
		}
	}, box->lifetime());

	box->setTitle(tr::lng_mute_box_title());

	const auto topButton = box->addTopButton(st::infoTopBarMenu);
	topButton->setClickedCallback([=] {
		if (state->menu) {
			return;
		}
		state->menu = base::make_unique_q<Ui::PopupMenu>(
			topButton,
			st::popupMenuWithIcons);
		FillSoundMenu(
			state->menu.get(),
			peer,
			tr::lng_mute_box_no_notifications(),
			tr::lng_mute_box_silent_notifications(),
			[=](bool silent) {
				state->noSoundChanges = silent;
			});
		state->menu->popup(QCursor::pos());
		return;
	});

	const auto updateValueInSeconds = [=] {
		state->valueInSeconds = 0
			+ day->getLastText().toUInt() * 3600 * 24
			+ hour->getLastText().toUInt() * 3600
			+ minute->getLastText().toUInt() * 60;
	};

	using Field = Ui::MaskedInputField;
	auto confirmText = rpl::merge(
		base::qt_signal_producer(day.data(), &Field::changed),
		base::qt_signal_producer(hour.data(), &Field::changed),
		base::qt_signal_producer(minute.data(), &Field::changed),
		state->noSoundChanges.value() | rpl::to_empty
	) | rpl::map([=] {
		updateValueInSeconds();
		return !state->valueInSeconds
			? tr::lng_mute_menu_unmute()
			: state->noSoundChanges.current()
			? tr::lng_mute_box_silent_notifications()
			: tr::lng_mute_menu_mute();
	}) | rpl::flatten_latest();
	const auto confirm = box->addButton(std::move(confirmText), [=] {
		peer->owner().updateNotifySettings(
			peer,
			state->valueInSeconds,
			std::nullopt,
			state->noSoundChanges.current());
		box->closeBox();
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

} // namespace

void FillMuteMenu(
		not_null<Ui::PopupMenu*> menu,
		Args args) {
	const auto peer = args.peer;

	FillSoundMenu(
		menu,
		peer,
		tr::lng_mute_menu_sound_on(),
		tr::lng_mute_menu_sound_off(),
		[peer](bool silent) {
			peer->owner().updateNotifySettings(peer, {}, {}, silent);
		});

	menu->addSeparator();

	menu->addAction(
		tr::lng_mute_menu_duration(tr::now),
		[=, show = args.show] { show->showBox(Box(MuteBox, peer)); },
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
