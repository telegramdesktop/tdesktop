/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/channel_statistics/earn/info_earn_inner_widget.h"

#include "core/ui_integration.h" // Core::MarkedTextContext.
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "info/info_controller.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/labels.h"
#include "styles/style_channel_earn.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"

namespace Info::ChannelEarn {
namespace {
} // namespace

InnerWidget::InnerWidget(
	QWidget *parent,
	not_null<Controller*> controller,
	not_null<PeerData*> peer)
: VerticalLayout(parent)
, _controller(controller)
, _peer(peer)
, _show(controller->uiShow()) {
}

void InnerWidget::load() {
}

void InnerWidget::fill() {
	const auto container = this;

	const auto session = &_peer->session();
	{
		const auto emoji = Ui::Text::SingleCustomEmoji(
			session->data().customEmojiManager().registerInternalEmoji(
				st::topicButtonArrow,
				st::channelEarnLearnArrowMargins,
				false));
		auto label = object_ptr<Ui::FlatLabel>(
			container,
			st::boxDividerLabel);
		const auto raw = label.data();
		tr::lng_channel_earn_about(
			lt_link,
			tr::lng_channel_earn_about_link(
				lt_emoji,
				rpl::single(emoji),
				Ui::Text::RichLangValue
			) | rpl::map([](TextWithEntities text) {
				return Ui::Text::Link(std::move(text), 1);
			}),
			Ui::Text::RichLangValue
		) | rpl::start_with_next([=](const TextWithEntities &text) {
			raw->setMarkedText(
				text,
				Core::MarkedTextContext{ .session = session });
		}, label->lifetime());
		label->setLink(1, std::make_shared<LambdaClickHandler>([=] {
		}));
		container->add(object_ptr<Ui::DividerLabel>(
			container,
			std::move(label),
			st::defaultBoxDividerLabelPadding,
			RectPart::Top | RectPart::Bottom));
	}
}

void InnerWidget::saveState(not_null<Memento*> memento) {
	// memento->setState(base::take(_state));
}

void InnerWidget::restoreState(not_null<Memento*> memento) {
	// _state = memento->state();
	// if (!_state.link.isEmpty()) {
		fill();
	// } else {
	// 	load();
	// }
	Ui::RpWidget::resizeToWidth(width());
}

rpl::producer<Ui::ScrollToRequest> InnerWidget::scrollToRequests() const {
	return _scrollToRequests.events();
}

auto InnerWidget::showRequests() const -> rpl::producer<ShowRequest> {
	return _showRequests.events();
}

void InnerWidget::showFinished() {
	_showFinished.fire({});
}

not_null<PeerData*> InnerWidget::peer() const {
	return _peer;
}

} // namespace Info::ChannelEarn

