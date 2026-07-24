/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "menu/menu_item_rate_transcribe.h"

#include "base/call_delayed.h"
#include "lang/lang_keys.h"
#include "ui/rect.h"
#include "ui/toast/toast.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"

namespace Menu {
namespace {

constexpr auto kDuration = crl::time(5000);

} // namespace

RateTranscribe::RateTranscribe(
	not_null<Ui::PopupMenu*> popupMenu,
	const style::Menu &st,
	Fn<void(bool)> rate)
: Ui::Menu::ItemBase(popupMenu->menu(), st)
, _dummyAction(Ui::CreateChild<QAction>(this))
, _leftButton(Ui::CreateSimpleCircleButton(
	this,
	st::defaultRippleAnimation))
, _rightButton(Ui::CreateSimpleCircleButton(
	this,
	st::defaultRippleAnimation)) {
	setAcceptBoth(true);

	fitToMenuWidth();

	enableMouseSelecting();

	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	Ui::AddSkip(content);

	const auto label = content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			tr::lng_context_rate_transcription(),
			st::boxDividerLabel),
		style::margins(),
		style::al_top);
	setMinWidth(
		label->st().style.font->width(
			tr::lng_context_rate_transcription(tr::now)));
	widthValue() | rpl::on_next([=, menu = popupMenu->menu()](int w) {
		content->resizeToWidth(menu->width());
	}, content->lifetime());
	Ui::AddSkip(content);

	// const auto leftButton = Ui::CreateChild<Ui::IconButton>(
	// 	this,
	// 	st::menuTranscribeDummyButton);
	// const auto rightButton = Ui::CreateChild<Ui::IconButton>(
	// 	this,
	// 	st::menuTranscribeDummyButton);
	{
		_leftButton->resize(Size(st::menuTranscribeDummyButton.width));
		const auto label = Ui::CreateChild<Ui::FlatLabel>(
			_leftButton,
			QString::fromUtf8("\U0001F44D"));
		label->setAttribute(Qt::WA_TransparentForMouseEvents, true);
		_leftButton->sizeValue() | rpl::on_next([=](QSize s) {
			label->moveToLeft(
				(s.width() - label->width()) / 2,
				(s.height() - label->height()) / 2);
		}, label->lifetime());
	}
	{
		_rightButton->resize(Size(st::menuTranscribeDummyButton.width));
		const auto label = Ui::CreateChild<Ui::FlatLabel>(
			_rightButton,
			QString::fromUtf8("\U0001F44E"));
		label->setAttribute(Qt::WA_TransparentForMouseEvents, true);
		_rightButton->sizeValue() | rpl::on_next([=](QSize s) {
			label->moveToLeft(
				(s.width() - label->width()) / 2,
				(s.height() - label->height()) / 2);
		}, label->lifetime());
	}
	{
		const auto showToast = [=,
				weak = base::make_weak(popupMenu->parentWidget())]{
			if (const auto strong = weak.get()) {
				base::call_delayed(
					st::universalDuration * 1.1,
					crl::guard(strong, [=] {
						Ui::Toast::Show(strong->window(), {
							.text = tr::lng_toast_sent_rate_transcription(
								tr::now,
								TextWithEntities::Simple),
							.duration = kDuration,
						});
					}));
			}
		};
		const auto hideMenu = [=, weak = base::make_weak(popupMenu)] {
			if (const auto strong = weak.get()) {
				base::call_delayed(
					st::universalDuration,
					crl::guard(strong, [=] { strong->hideMenu(false); }));
			}
		};
		_leftButton->setClickedCallback([=] {
			rate(true);
			showToast();
			hideMenu();
		});
		_rightButton->setClickedCallback([=] {
			rate(false);
			showToast();
			hideMenu();
		});
	}
	_desiredHeight = rect::m::sum::v(st::menuTranscribeItemPadding)
		+ st::menuTranscribeDummyButton.height
		+ label->st().style.font->height;
	rpl::combine(
		content->geometryValue(),
		label->geometryValue()
	) | rpl::on_next([=](
			const QRect &contentRect,
			const QRect &labelRect) {
		_leftButton->moveToLeft(
			labelRect.x(),
			rect::bottom(contentRect));
		_rightButton->moveToLeft(
			rect::right(labelRect) - _rightButton->width(),
			rect::bottom(contentRect));
		_desiredHeight = rect::m::sum::v(st::menuTranscribeItemPadding)
			+ _leftButton->height()
			+ labelRect.height();
	}, _leftButton->lifetime());
	_leftButton->show();
	_rightButton->show();

	selects() | rpl::on_next([=](const Ui::Menu::CallbackData &data) {
		if (data.selected
			&& data.source == Ui::Menu::TriggeredSource::Keyboard) {
			_leftButton->setForceRippled(true);
			_selectedButton = SelectedButton::Left;
		} else if (!data.selected) {
			_leftButton->setForceRippled(false);
			_rightButton->setForceRippled(false);
			_selectedButton = SelectedButton::None;
		}
	}, lifetime());
}

not_null<QAction*> RateTranscribe::action() const {
	return _dummyAction;
}

bool RateTranscribe::isEnabled() const {
	return true;
}

int RateTranscribe::contentHeight() const {
	return _desiredHeight;
}

void RateTranscribe::handleKeyPress(not_null<QKeyEvent*> e) {
	const auto key = e->key();
	if (key == Qt::Key_Left) {
		if (_selectedButton == SelectedButton::Right) {
			_rightButton->setForceRippled(false);
			_leftButton->setForceRippled(true);
			_selectedButton = SelectedButton::Left;
		} else if (_selectedButton == SelectedButton::Left) {
			_leftButton->setForceRippled(false);
			_rightButton->setForceRippled(true);
			_selectedButton = SelectedButton::Right;
		} else {
			_leftButton->setForceRippled(true);
			_selectedButton = SelectedButton::Left;
		}
	} else if (key == Qt::Key_Right) {
		if (_selectedButton == SelectedButton::Left) {
			_leftButton->setForceRippled(false);
			_rightButton->setForceRippled(true);
			_selectedButton = SelectedButton::Right;
		} else if (_selectedButton == SelectedButton::Right) {
			_rightButton->setForceRippled(false);
			_leftButton->setForceRippled(true);
			_selectedButton = SelectedButton::Left;
		} else {
			_leftButton->setForceRippled(true);
			_selectedButton = SelectedButton::Left;
		}
	} else if (key == Qt::Key_Return
			|| key == Qt::Key_Enter
			|| key == Qt::Key_Space) {
		if (_selectedButton == SelectedButton::Left) {
			_leftButton->clicked(Qt::KeyboardModifiers(), Qt::LeftButton);
		} else if (_selectedButton == SelectedButton::Right) {
			_rightButton->clicked(Qt::KeyboardModifiers(), Qt::LeftButton);
		}
	}
}

} // namespace Menu