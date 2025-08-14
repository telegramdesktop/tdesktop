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
	not_null<Ui::PopupMenu*> parent,
	const style::Menu &st,
	Fn<void(bool)> rate)
: Ui::Menu::ItemBase(parent, st)
, _dummyAction(Ui::CreateChild<QAction>(this)) {
	setAcceptBoth(true);

	initResizeHook(parent->sizeValue());

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
	widthValue() | rpl::start_with_next([=](int w) {
		content->resizeToWidth(parentWidget()->width());
	}, content->lifetime());
	Ui::AddSkip(content);

	// const auto leftButton = Ui::CreateChild<Ui::IconButton>(
	// 	this,
	// 	st::menuTranscribeDummyButton);
	// const auto rightButton = Ui::CreateChild<Ui::IconButton>(
	// 	this,
	// 	st::menuTranscribeDummyButton);
	const auto leftButton = Ui::CreateSimpleCircleButton(
		this,
		st::defaultRippleAnimation);
	{
		leftButton->resize(Size(st::menuTranscribeDummyButton.width));
		const auto label = Ui::CreateChild<Ui::FlatLabel>(
			leftButton,
			QString::fromUtf8("\U0001F44D"));
		label->setAttribute(Qt::WA_TransparentForMouseEvents, true);
		leftButton->sizeValue() | rpl::start_with_next([=](QSize s) {
			label->moveToLeft(
				(s.width() - label->width()) / 2,
				(s.height() - label->height()) / 2);
		}, label->lifetime());
	}
	const auto rightButton = Ui::CreateSimpleCircleButton(
		this,
		st::defaultRippleAnimation);
	{
		rightButton->resize(Size(st::menuTranscribeDummyButton.width));
		const auto label = Ui::CreateChild<Ui::FlatLabel>(
			rightButton,
			QString::fromUtf8("\U0001F44E"));
		label->setAttribute(Qt::WA_TransparentForMouseEvents, true);
		rightButton->sizeValue() | rpl::start_with_next([=](QSize s) {
			label->moveToLeft(
				(s.width() - label->width()) / 2,
				(s.height() - label->height()) / 2);
		}, label->lifetime());
	}
	{
		const auto showToast = [=,
				weak = base::make_weak(parent->parentWidget())]{
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
		const auto hideMenu = [=, weak = base::make_weak(parent)] {
			if (const auto strong = weak.get()) {
				base::call_delayed(
					st::universalDuration,
					crl::guard(strong, [=] { strong->hideMenu(false); }));
			}
		};
		leftButton->setClickedCallback([=] {
			rate(true);
			showToast();
			hideMenu();
		});
		rightButton->setClickedCallback([=] {
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
	) | rpl::start_with_next([=](
			const QRect &contentRect,
			const QRect &labelRect) {
		leftButton->moveToLeft(
			labelRect.x(),
			rect::bottom(contentRect));
		rightButton->moveToLeft(
			rect::right(labelRect) - rightButton->width(),
			rect::bottom(contentRect));
		_desiredHeight = rect::m::sum::v(st::menuTranscribeItemPadding)
			+ leftButton->height()
			+ labelRect.height();
	}, leftButton->lifetime());
	leftButton->show();
	rightButton->show();
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

} // namespace Menu
