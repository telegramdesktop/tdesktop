/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_paid_reaction_toast.h"

#include "chat_helpers/stickers_lottie.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_message_reactions.h"
#include "data/data_session.h"
#include "history/view/history_view_element.h"
#include "history/history_item.h"
//#include "main/main_session.h"
#include "lang/lang_keys.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/toast/toast_widget.h"
#include "ui/widgets/buttons.h"
//#include "boxes/sticker_set_box.h"
//#include "boxes/premium_preview_box.h"
#include "lottie/lottie_single_player.h"
//#include "window/window_session_controller.h"
//#include "settings/settings_premium.h"
//#include "apiwrap.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

constexpr auto kPremiumToastDuration = 5 * crl::time(1000);

} // namespace

PaidReactionToast::PaidReactionToast(
	not_null<Ui::RpWidget*> parent,
	not_null<Data::Session*> owner,
	rpl::producer<int> topOffset,
	Fn<bool(not_null<const Element*> view)> mine)
: _parent(parent)
, _owner(owner)
, _topOffset(std::move(topOffset)) {
	_owner->viewPaidReactionSent(
	) | rpl::filter(
		std::move(mine)
	) | rpl::start_with_next([=](not_null<const Element*> view) {
		maybeShowFor(view->data());
	}, _lifetime);
}

PaidReactionToast::~PaidReactionToast() {
	_hiding.push_back(_weak);
	for (const auto &weak : base::take(_hiding)) {
		if (const auto strong = weak.get()) {
			delete strong->widget();
		}
	}
}

void PaidReactionToast::maybeShowFor(not_null<HistoryItem*> item) {
	const auto count = item->reactionsPaidScheduled();
	const auto at = _owner->reactions().sendingScheduledPaidAt(item);
	if (!count || !at) {
		return;
	}
	const auto left = at - crl::now();
	const auto total = Data::Reactions::ScheduledPaidDelay();
	const auto ignore = total % 1000;
	if (left > ignore) {
		showFor(item->fullId(), count, left - ignore, total);
	}
}

void PaidReactionToast::showFor(
		FullMsgId itemId,
		int count,
		crl::time left,
		crl::time total) {
	const auto old = _weak.get();
	const auto i = ranges::find(_stack, itemId);
	if (i != end(_stack)) {
		if (old && i + 1 == end(_stack)) {
			update(old, count, left, total);
			return;
		}
		_stack.erase(i);
	}
	_stack.push_back(itemId);

	clearHiddenHiding();
	if (old) {
		old->hideAnimated();
		_hiding.push_back(_weak);
	}
	const auto text = tr::lng_paid_react_toast_title(
		tr::now,
		Ui::Text::Bold
	).append('\n').append(tr::lng_paid_react_toast_text(
		tr::now,
		lt_count,
		count,
		Ui::Text::RichLangValue
	));
	_st = st::historyPremiumToast;
	const auto skip = _st.padding.top();
	const auto size = _st.style.font->height * 2;
	const auto undo = tr::lng_paid_react_undo(tr::now);
	_st.padding.setLeft(skip + size + skip);
	_st.padding.setRight(st::historyPremiumViewSet.font->width(undo)
		- st::historyPremiumViewSet.width);

	_weak = Ui::Toast::Show(_parent, Ui::Toast::Config{
		.text = text,
		.st = &_st,
		.duration = -1,
		.multiline = true,
		.dark = true,
		.slideSide = RectPart::Top,
	});
	const auto strong = _weak.get();
	if (!strong) {
		return;
	}
	strong->setInputUsed(true);
	const auto widget = strong->widget();
	const auto hideToast = [weak = _weak] {
		if (const auto strong = weak.get()) {
			strong->hideAnimated();
		}
	};

	const auto button = Ui::CreateChild<Ui::RoundButton>(
		widget.get(),
		rpl::single(undo),
		st::historyPremiumViewSet);
	button->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
	button->show();
	rpl::combine(
		widget->sizeValue(),
		button->sizeValue()
	) | rpl::start_with_next([=](QSize outer, QSize inner) {
		button->moveToRight(
			0,
			(outer.height() - inner.height()) / 2,
			outer.width());
	}, widget->lifetime());
	const auto preview = Ui::CreateChild<Ui::RpWidget>(widget.get());
	preview->moveToLeft(skip, skip);
	preview->resize(size, size);
	preview->show();

	setupLottiePreview(preview, size);
	button->setClickedCallback([=] {
		if (const auto item = _owner->message(itemId)) {
			_owner->reactions().undoScheduledPaid(item);
		}
		hideToast();
	});
}

void PaidReactionToast::update(
	not_null<Ui::Toast::Instance*> toast,
	int count,
	crl::time left,
	crl::time total) {
}

void PaidReactionToast::clearHiddenHiding() {
	_hiding.erase(
		ranges::remove(
			_hiding,
			nullptr,
			&base::weak_ptr<Ui::Toast::Instance>::get),
		end(_hiding));
}

void PaidReactionToast::setupLottiePreview(
		not_null<Ui::RpWidget*> widget,
		int size) {
	const auto generate = [&](const QString &name) {
		const auto session = &_owner->session();
		return ChatHelpers::GenerateLocalTgsSticker(session, name);
	};
	const auto document = _owner->reactions().paidToastAnimation();

	const auto bytes = document->createMediaView()->bytes();
	const auto filepath = document->filepath();
	const auto player = widget->lifetime().make_state<Lottie::SinglePlayer>(
		Lottie::ReadContent(bytes, filepath),
		Lottie::FrameRequest{ QSize(size, size) },
		Lottie::Quality::Default);

	widget->paintRequest(
	) | rpl::start_with_next([=] {
		if (!player->ready()) {
			return;
		}
		const auto image = player->frame();
		QPainter(widget).drawImage(
			QRect(QPoint(), image.size() / image.devicePixelRatio()),
			image);
		if (player->frameIndex() + 1 != player->framesCount()) {
			player->markFrameShown();
		}
	}, widget->lifetime());

	player->updates(
	) | rpl::start_with_next([=] {
		widget->update();
	}, widget->lifetime());
}

} // namespace HistoryView
