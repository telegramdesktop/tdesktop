/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_self_forwards_tagger.h"

#include "base/call_delayed.h"
#include "base/event_filter.h"
#include "base/timer_rpl.h"
#include "chat_helpers/share_message_phrase_factory.h"
#include "core/ui_integration.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/reactions/history_view_reactions_selector.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/main_session.h"
#include "ui/rect.h"
#include "ui/effects/show_animation.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast_widget.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/tooltip.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"

namespace HistoryView {
namespace {

constexpr auto kInitTimer = crl::time(3000);
constexpr auto kTimerOnLeave = crl::time(2000);

} // namespace

SelfForwardsTagger::SelfForwardsTagger(
	not_null<Window::SessionController*> controller,
	not_null<Ui::RpWidget*> parent,
	Fn<Ui::RpWidget*()> listWidget,
	not_null<QWidget*> scroll,
	Fn<History*()> history)
: _controller(controller)
, _parent(parent)
, _listWidget(std::move(listWidget))
, _scroll(scroll)
, _history(std::move(history)) {
	setup();
}

SelfForwardsTagger::~SelfForwardsTagger() = default;

void SelfForwardsTagger::setup() {
	_controller->session().data().recentSelfForwards(
	) | rpl::start_with_next([=](const Data::RecentSelfForwards &data) {
		const auto history = _history ? _history() : nullptr;
		if (!history || history->peer->id != data.fromPeerId) {
			return;
		}
		showSelectorForMessages(data.ids);
	}, _lifetime);
}

void SelfForwardsTagger::showSelectorForMessages(
		const MessageIdsList &ids) {
	if (ids.empty()) {
		return;
	}
	const auto lastId = ids.back();
	const auto item = _controller->session().data().message(lastId);
	if (!item) {
		return;
	}
	using namespace Reactions;
	const auto reactions = Data::LookupPossibleReactions(item, true);
	if (reactions.recent.empty()) {
		return;
	}

	showToast(
		rpl::variable<TextWithEntities>(
			ChatHelpers::ForwardedMessagePhrase({
			.toCount = 1,
			.singleMessage = (ids.size() == 1),
			.to1 = _controller->session().user(),
			.toSelfWithPremiumIsEmpty = false,
		})).current(),
		nullptr);

	const auto toastWidget = [&]() -> Ui::RpWidget* {
		if (const auto toast = _toast.get()) {
			return toast->widget();
		}
		return nullptr;
	}();
	if (!toastWidget) {
		return;
	}

	const auto toastWidth = toastWidget->width();
	const auto selector = Ui::CreateChild<Selector>(
		toastWidget->parentWidget(),
		st::reactPanelEmojiPan,
		_controller->uiShow(),
		reactions,
		tr::lng_add_tag_selector(
			tr::now,
			lt_count,
			float64(ids.size()),
			TextWithEntities::Simple),
		[](bool) {},
		IconFactory(),
		[] { return false; },
		false);
	selector->setBubbleUp(true);

	const auto hideAndDestroy = [
			selectorWeak = base::make_weak(selector),
			toastWidgetWeak = _toast] {
		const auto selector = selectorWeak.get();
		const auto toastWidget = toastWidgetWeak.get();
		if (!selector || !toastWidget) {
			return;
		}
		Ui::Animations::HideWidgets({ toastWidget->widget(), selector });
		selector->shownValue(
		) | rpl::start_with_next([toastWidgetWeak](bool shown) {
			if (!shown) {
				if (const auto toast = toastWidgetWeak.get()) {
					delete toast->widget();
				}
			}
		}, selector->lifetime());
	};

	selector->chosen(
	) | rpl::start_with_next([=](ChosenReaction reaction) {
		selector->setAttribute(Qt::WA_TransparentForMouseEvents);
		for (const auto &id : ids) {
			if (const auto item = _controller->session().data().message(id)) {
				item->toggleReaction(
					reaction.id,
					HistoryReactionSource::Selector);
			}
		}
		hideAndDestroy();
		base::call_delayed(st::defaultToggle.duration, _parent, [=] {
			showTaggedToast(reaction.id.custom());
		});
	}, selector->lifetime());

	const auto eventFilterCallback = [=](not_null<QEvent*> event) {
		if (event->type() == QEvent::MouseButtonPress) {
			hideAndDestroy();
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	};
	base::install_event_filter(selector, _parent, eventFilterCallback);
	if (const auto list = _listWidget()) {
		list->lifetime().add([=] {
			hideAndDestroy();
		});
		base::install_event_filter(selector, list, eventFilterCallback);
	}

	struct State {
		rpl::lifetime timerLifetime;
		bool expanded = false;
	};
	const auto state = selector->lifetime().make_state<State>();
	const auto restartTimer = [=](crl::time ms) {
		state->timerLifetime.destroy();
		base::timer_once(ms) | rpl::start_with_next([=] {
			hideAndDestroy();
		}, state->timerLifetime);
	};

	selector->willExpand() | rpl::start_with_next([=] {
		state->expanded = true;
	}, selector->lifetime());

	base::install_event_filter(selector, [=](not_null<QEvent*> event) {
		if (event->type() == QEvent::MouseButtonPress) {
			state->timerLifetime.destroy();
			return base::EventFilterResult::Continue;
		} else if (!state->expanded && event->type() == QEvent::Enter) {
			state->timerLifetime.destroy();
			return base::EventFilterResult::Continue;
		} else if (!state->expanded && event->type() == QEvent::Leave) {
			restartTimer(kTimerOnLeave);
			return base::EventFilterResult::Continue;
		}
		return base::EventFilterResult::Continue;
	}, selector->lifetime());

	QObject::connect(
		_toast->widget(),
		&QObject::destroyed,
		selector,
		[=] { delete selector; });

	const auto selectorWidth = toastWidth;
	selector->countWidth(selectorWidth, selectorWidth);
	selector->initGeometry(_parent->height() / 2);

	_toast->widget()->geometryValue(
	) | rpl::start_with_next([=](const QRect &rect) {
		if (rect.isEmpty()) {
			return;
		}
		selector->moveToLeft(
			rect.x() + (rect.width() - selector->width()) / 2,
			rect::bottom(rect) - st::selfForwardsTaggerStripSkip);
	}, selector->lifetime());
	restartTimer(kInitTimer);
	selector->show();
}

void SelfForwardsTagger::showToast(
		const TextWithEntities &text,
		Fn<void()> callback) {
	hideToast();
	_toast = Ui::Toast::Show(_scroll, Ui::Toast::Config{
		.text = text,
		.textContext = Core::TextContext({
			.session = &_controller->session(),
		}),
		.st = &st::selfForwardsTaggerToast,
		.attach = RectPart::Top,
		.infinite = true,
	});
	if (const auto strong = _toast.get()) {
		const auto widget = strong->widget();
		createLottieIcon(widget, u"toast/saved_messages"_q);
		if (callback) {
			QObject::connect(widget, &QObject::destroyed, callback);
		}
	} else if (callback) {
		callback();
	}
}

void SelfForwardsTagger::createLottieIcon(
		not_null<QWidget*> widget,
		const QString &name) {
	const auto lottieWidget = Ui::CreateChild<Ui::RpWidget>(widget);
	struct State {
		std::unique_ptr<Lottie::Icon> lottieIcon;
	};
	const auto state = lottieWidget->lifetime().make_state<State>();
	state->lottieIcon = Lottie::MakeIcon({
		.name = name,
		.sizeOverride = st::selfForwardsTaggerIcon,
	});
	const auto icon = state->lottieIcon.get();
	lottieWidget->resize(st::selfForwardsTaggerIcon);
	lottieWidget->move(st::selfForwardsTaggerToast.iconPosition);
	lottieWidget->show();
	lottieWidget->raise();
	icon->animate(
		[=] { lottieWidget->update(); },
		0,
		icon->framesCount() - 1);
	lottieWidget->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(lottieWidget);
		icon->paint(p, 0, 0);
	}, lottieWidget->lifetime());
}

void SelfForwardsTagger::showTaggedToast(DocumentId reaction) {
	auto text = tr::lng_message_tagged_with(
		tr::now,
		lt_emoji,
		Data::SingleCustomEmoji(reaction),
		Ui::Text::WithEntities);
	hideToast();

	const auto &st = st::selfForwardsTaggerToast;
	const auto viewText = tr::lng_tagged_view_saved(tr::now);
	const auto viewFont = st::historyPremiumViewSet.style.font;
	const auto rightSkip = viewFont->width(viewText)
		+ st::toastUndoSpace;

	_toast = Ui::Toast::Show(_scroll, Ui::Toast::Config{
		.text = text,
		.textContext = Core::TextContext({
			.session = &_controller->session(),
		}),
		.padding = rpl::single(QMargins(0, 0, rightSkip, 0)),
		.st = &st,
		.attach = RectPart::Top,
		.acceptinput = true,
		.duration = crl::time(3000),
	});
	if (const auto strong = _toast.get()) {
		const auto widget = strong->widget();
		createLottieIcon(widget, u"toast/tagged"_q);

		const auto button = Ui::CreateChild<Ui::AbstractButton>(widget.get());
		button->setClickedCallback([=] {
			_controller->showPeerHistory(_controller->session().user());
			hideToast();
		});

		button->paintRequest() | rpl::start_with_next([=] {
			auto p = QPainter(button);
			const auto font = st::historyPremiumViewSet.style.font;
			const auto top = (button->height() - font->height) / 2;
			p.setPen(st::historyPremiumViewSet.textFg);
			p.setFont(font);
			p.drawText(0, top + font->ascent, viewText);
		}, button->lifetime());

		button->resize(
			viewFont->width(viewText),
			st::historyPremiumViewSet.height);

		rpl::combine(
			widget->sizeValue(),
			button->sizeValue()
		) | rpl::start_with_next([=](const QSize &outer, const QSize &inner) {
			button->moveToRight(
				st.padding.right(),
				(outer.height() - inner.height()) / 2,
				outer.width());
		}, widget->lifetime());

		button->show();
	}
}

void SelfForwardsTagger::hideToast() {
	if (const auto strong = _toast.get()) {
		strong->hideAnimated();
	}
}

} // namespace HistoryView
