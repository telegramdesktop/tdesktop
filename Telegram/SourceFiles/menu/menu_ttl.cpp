/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "menu/menu_ttl.h"

#include "lang/lang_keys.h"
#include "ui/boxes/time_picker_box.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/ui_utility.h"
#include "ui/widgets/labels.h"
#if 0
#include "ui/boxes/choose_time.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_dialogs.h" // dialogsScamFont
#include "styles/style_menu_icons.h"
#endif
#include "styles/style_layers.h"

namespace TTLMenu {

namespace {

#if 0
constexpr auto kTTLDurHours1 = crl::time(1);
constexpr auto kTTLDurSeconds1 = kTTLDurHours1 * 3600;
constexpr auto kTTLDurHours2 = crl::time(24);
constexpr auto kTTLDurSeconds2 = kTTLDurHours2 * 3600;
constexpr auto kTTLDurHours3 = crl::time(24 * 7);
constexpr auto kTTLDurSeconds3 = kTTLDurHours3 * 3600;
constexpr auto kTTLDurHours4 = crl::time(24 * 30);
constexpr auto kTTLDurSeconds4 = kTTLDurHours4 * 3600;

// See menu_mute.cpp.
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
	p.setFont(st::dialogsScamFont);
	p.setPen(st::menuIconColor);
	p.drawText(_iconPosition, _text);
}

class TextItem final : public Ui::Menu::ItemBase {
public:
	TextItem(
		not_null<RpWidget*> parent,
		const style::Menu &st,
		rpl::producer<QString> &&text);

	not_null<QAction*> action() const override;
	bool isEnabled() const override;

protected:
	int contentHeight() const override;

private:
	const base::unique_qptr<Ui::FlatLabel> _label;
	const not_null<QAction*> _dummyAction;

};

TextItem::TextItem(
	not_null<RpWidget*> parent,
	const style::Menu &st,
	rpl::producer<QString> &&text)
: ItemBase(parent, st)
, _label(base::make_unique_q<Ui::FlatLabel>(
	this,
	std::move(text),
	st::historyMessagesTTLLabel))
, _dummyAction(Ui::CreateChild<QAction>(parent.get())) {

	setAttribute(Qt::WA_TransparentForMouseEvents);

	setMinWidth(st::historyMessagesTTLLabel.minWidth
		+ st.itemIconPosition.x());

	sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		_label->moveToLeft(
			st.itemIconPosition.x(),
			(s.height() - _label->height()) / 2);
	}, lifetime());

	initResizeHook(parent->sizeValue());
}

not_null<QAction*> TextItem::action() const {
	return _dummyAction;
}

bool TextItem::isEnabled() const {
	return false;
}

int TextItem::contentHeight() const {
	return _label->height();
}

void TTLBoxOld(
		not_null<Ui::GenericBox*> box,
		Fn<void(TimeId)> callback,
		TimeId startTtlPeriod) {
	struct State {
		int lastSeconds = 0;
	};
	const auto startTtl = startTtlPeriod ? startTtlPeriod : kTTLDurSeconds2;
	auto chooseTimeResult = ChooseTimeWidget(box, startTtl);
	box->addRow(std::move(chooseTimeResult.widget));

	const auto state = box->lifetime().make_state<State>();

	box->setTitle(tr::lng_manage_messages_ttl_title());

	auto confirmText = std::move(
		chooseTimeResult.secondsValue
	) | rpl::map([=](int seconds) {
		state->lastSeconds = seconds;
		return !seconds
			? tr::lng_manage_messages_ttl_disable()
			: tr::lng_enable_auto_delete();
	}) | rpl::flatten_latest();
	const auto confirm = box->addButton(std::move(confirmText), [=] {
		callback(state->lastSeconds);
		box->closeBox();
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}
#endif

} // namespace

void TTLBox(not_null<Ui::GenericBox*> box, Args args) {
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		std::move(args.about),
		st::boxLabel));

	const auto ttls = std::vector<TimeId>{
		(86400 * 1),
		(86400 * 2),
		(86400 * 3),
		(86400 * 4),
		(86400 * 5),
		(86400 * 6),
		(86400 * 7 * 1),
		(86400 * 7 * 2),
		(86400 * 7 * 3),
		(86400 * 30 * 1),
		(86400 * 30 * 2),
		(86400 * 30 * 3),
		(86400 * 30 * 4),
		(86400 * 30 * 5),
		(86400 * 30 * 6),
		(86400 * 30 * 12),
	};
	const auto phrases = ranges::views::all(
		ttls
	) | ranges::views::transform(Ui::FormatTTL) | ranges::to_vector;

	const auto pickerTtl = TimePickerBox(box, ttls, phrases, args.startTtl);

	box->addButton(tr::lng_settings_save(), [=] {
		args.callback(pickerTtl());
		box->getDelegate()->hideLayer();
	});

	box->setTitle(tr::lng_manage_messages_ttl_title());

	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	if (args.startTtl) {
		box->addLeftButton(tr::lng_manage_messages_ttl_disable(), [=] {
			args.callback(0);
		});
	}
}

#if 0
void FillTTLMenu(not_null<Ui::PopupMenu*> menu, Args args) {
	const auto &st = menu->st().menu;
	const auto iconTextPosition = st.itemIconPosition
		+ st::menuIconTTLAnyTextPosition;
	const auto addAction = [&](const QString &text, TimeId ttl) {
		auto item = base::make_unique_q<IconWithText>(
			menu,
			st,
			Ui::Menu::CreateAction(
				menu->menu().get(),
				text,
				[=] { args.callback(ttl); }),
			&st::menuIconTTLAny,
			&st::menuIconTTLAny);
		item->setData(Ui::FormatTTLTiny(ttl), iconTextPosition);
		menu->addAction(std::move(item));
	};
	addAction(tr::lng_manage_messages_ttl_after1(tr::now), kTTLDurSeconds1);
	addAction(tr::lng_manage_messages_ttl_after2(tr::now), kTTLDurSeconds2);
	addAction(tr::lng_manage_messages_ttl_after3(tr::now), kTTLDurSeconds3);
	addAction(tr::lng_manage_messages_ttl_after4(tr::now), kTTLDurSeconds4);

	menu->addAction(
		tr::lng_manage_messages_ttl_after_custom(tr::now),
		[a = args] { a.show->showBox(Box(TTLBox, a)); },
		&st::menuIconCustomize);

	if (args.startTtl) {
		menu->addAction({
			.text = tr::lng_manage_messages_ttl_disable(tr::now),
			.handler = [=] { args.callback(0); },
			.icon = &st::menuIconDisableAttention,
			.isAttention = true,
		});
	}

	menu->addSeparator();

	menu->addAction(base::make_unique_q<TextItem>(
		menu,
		menu->st().menu,
		std::move(args.about)));
}

void SetupTTLMenu(
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
			st::popupMenuExpandedSeparator);
		FillTTLMenu(state->menu.get(), args);
		state->menu->popup(QCursor::pos());
	}, parent->lifetime());
}
#endif

} // namespace TTLMenu
