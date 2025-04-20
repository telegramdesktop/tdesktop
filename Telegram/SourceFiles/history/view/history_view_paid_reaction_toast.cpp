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
#include "lang/lang_keys.h"
#include "ui/effects/numbers_animation.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/toast/toast_widget.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/painter.h"
#include "lottie/lottie_single_player.h"
#include "styles/style_chat.h"
#include "styles/style_premium.h"

namespace HistoryView {
namespace {

constexpr auto kPremiumToastDuration = 5 * crl::time(1000);

[[nodiscard]] not_null<Ui::AbstractButton*> MakeUndoButton(
		not_null<QWidget*> parent,
		int width,
		const QString &text,
		rpl::producer<crl::time> finish,
		crl::time total,
		Fn<void()> click,
		Fn<void()> timeout) {
	const auto result = Ui::CreateChild<Ui::AbstractButton>(parent);
	result->setClickedCallback(std::move(click));

	struct State {
		explicit State(not_null<QWidget*> button)
		: countdown(
			st::toastUndoFont,
			[=] { button->update(); }) {
		}

		Ui::NumbersAnimation countdown;
		crl::time finish = 0;
		int secondsLeft = 0;
		Ui::Animations::Basic animation;
		Fn<void()> update;
		base::Timer timer;
	};
	const auto state = result->lifetime().make_state<State>(result);
	const auto updateLeft = [=] {
		const auto now = crl::now();
		const auto left = state->finish - now;
		if (left > 0) {
			const auto seconds = int((left + 999) / 1000);
			if (state->secondsLeft != seconds) {
				state->secondsLeft = seconds;
				state->countdown.setText(QString::number(seconds), seconds);
			}
			state->timer.callOnce((left % 1000) + 1);
		} else {
			state->animation.stop();
			state->timer.cancel();
			timeout();
		}
		if (anim::Disabled()) {
		}
	};
	state->update = [=] {
		if (anim::Disabled()) {
			state->animation.stop();
		} else {
			if (!state->animation.animating()) {
				state->animation.start();
			}
			state->timer.cancel();
		}
		updateLeft();
		result->update();
	};

	result->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(result);

		const auto font = st::historyPremiumViewSet.style.font;
		const auto top = (result->height() - font->height) / 2;
		auto pen = st::historyPremiumViewSet.textFg->p;
		p.setPen(pen);
		p.setFont(font);
		p.drawText(0, top + font->ascent, text);

		const auto inner = QRect(
			width - st::toastUndoSkip - st::toastUndoDiameter,
			(result->height() - st::toastUndoDiameter) / 2,
			st::toastUndoDiameter,
			st::toastUndoDiameter);
		p.setFont(st::toastUndoFont);
		state->countdown.paint(
			p,
			inner.x() + (inner.width() - state->countdown.countWidth()) / 2,
			inner.y() + (inner.height() - st::toastUndoFont->height) / 2,
			width);

		const auto progress = (state->finish - crl::now()) / float64(total);
		const auto len = int(base::SafeRound(arc::kFullLength * progress));
		if (len > 0) {
			const auto from = arc::kFullLength / 4;
			auto hq = PainterHighQualityEnabler(p);
			pen.setWidthF(st::toastUndoStroke);
			p.setPen(pen);
			p.drawArc(inner, from, len);
		}
	}, result->lifetime());
	result->resize(width, st::historyPremiumViewSet.height);

	std::move(finish) | rpl::start_with_next([=](crl::time value) {
		state->finish = value;
		state->update();
	}, result->lifetime());
	state->animation.init(state->update);
	state->timer.setCallback(state->update);
	state->update();

	result->show();
	return result;
}

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

bool PaidReactionToast::maybeShowFor(not_null<HistoryItem*> item) {
	const auto count = item->reactionsPaidScheduled();
	const auto shownPeer = item->reactionsLocalShownPeer();
	const auto at = _owner->reactions().sendingScheduledPaidAt(item);
	if (!count || !at) {
		return false;
	}
	const auto total = Data::Reactions::ScheduledPaidDelay();
	const auto ignore = total % 1000;
	if (at <= crl::now() + ignore) {
		return false;
	}
	showFor(item->fullId(), count, shownPeer, at - ignore, total);
	return true;
}

void PaidReactionToast::showFor(
		FullMsgId itemId,
		int count,
		PeerId shownPeer,
		crl::time finish,
		crl::time total) {
	const auto old = _weak.get();
	const auto i = ranges::find(_stack, itemId);
	if (i != end(_stack)) {
		if (old && i + 1 == end(_stack)) {
			_count = count;
			_shownPeer = shownPeer;
			_timeFinish = finish;
			return;
		}
		_stack.erase(i);
	}
	_stack.push_back(itemId);

	clearHiddenHiding();
	if (old) {
		old->hideAnimated();
		_hiding.push_back(base::take(_weak));
	}
	_count.reset();
	_shownPeer.reset();
	_timeFinish.reset();
	_count = count;
	_shownPeer = shownPeer;
	_timeFinish = finish;
	auto text = rpl::combine(
		rpl::conditional(
			_shownPeer.value() | rpl::map(rpl::mappers::_1 == PeerId()),
			tr::lng_paid_react_toast_anonymous(
				lt_count,
				_count.value() | tr::to_count(),
				Ui::Text::Bold),
			tr::lng_paid_react_toast(
				lt_count,
				_count.value() | tr::to_count(),
				Ui::Text::Bold)),
		tr::lng_paid_react_toast_text(
			lt_count_decimal,
			_count.value() | tr::to_count(),
			Ui::Text::RichLangValue)
	) | rpl::map([](TextWithEntities &&title, TextWithEntities &&body) {
		title.append('\n').append(body);
		return std::move(title);
	});
	const auto &st = st::historyPremiumToast;
	const auto skip = st.padding.top();
	const auto size = st.style.font->height * 2;
	const auto undoText = tr::lng_paid_react_undo(tr::now);

	auto content = object_ptr<Ui::RpWidget>((QWidget*)nullptr);
	const auto child = Ui::CreateChild<Ui::FlatLabel>(
		content.data(),
		std::move(text),
		st::paidReactToastLabel);
	content->resize(child->naturalWidth() * 1.5, child->height());
	child->show();

	const auto leftSkip = skip + size + skip - st.padding.left();
	const auto undoFont = st::historyPremiumViewSet.style.font;

	const auto rightSkip = undoFont->width(undoText)
		+ st::toastUndoSpace
		+ st::toastUndoDiameter
		+ st::toastUndoSkip
		- st.padding.right();
	_weak = Ui::Toast::Show(_parent, Ui::Toast::Config{
		.content = std::move(content),
		.padding = rpl::single(QMargins(leftSkip, 0, rightSkip, 0)),
		.st = &st,
		.attach = RectPart::Top,
		.acceptinput = true,
		.infinite = true,
	});
	const auto strong = _weak.get();
	if (!strong) {
		return;
	}
	const auto widget = strong->widget();
	const auto hideToast = [=, weak = _weak] {
		if (const auto strong = weak.get()) {
			if (strong == _weak.get()) {
				_stack.erase(ranges::remove(_stack, itemId), end(_stack));

				_hiding.push_back(base::take(_weak));
				strong->hideAnimated();

				while (!_stack.empty()) {
					if (const auto item = _owner->message(_stack.back())) {
						if (maybeShowFor(item)) {
							break;
						}
					}
					_stack.pop_back();
				}
			}
		}
	};

	const auto undo = [=] {
		if (const auto item = _owner->message(itemId)) {
			_owner->reactions().undoScheduledPaid(item);
		}
		hideToast();
	};
	const auto button = MakeUndoButton(
		widget.get(),
		rightSkip + st.padding.right(),
		undoText,
		_timeFinish.value(),
		total,
		undo,
		hideToast);

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
	const auto document = _owner->reactions().paidToastAnimation();

	const auto bytes = document->createMediaView()->bytes();
	const auto filepath = document->filepath();
	const auto ratio = style::DevicePixelRatio();
	const auto player = widget->lifetime().make_state<Lottie::SinglePlayer>(
		Lottie::ReadContent(bytes, filepath),
		Lottie::FrameRequest{ QSize(size, size) * ratio },
		Lottie::Quality::Default);

	widget->paintRequest(
	) | rpl::start_with_next([=] {
		if (!player->ready()) {
			return;
		}
		const auto image = player->frame();
		QPainter(widget).drawImage(
			QRect(QPoint(), image.size() / ratio),
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
