/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_view_button.h"

#include "api/api_chat_invite.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "core/file_utilities.h"
#include "data/data_cloud_themes.h"
#include "data/data_session.h"
#include "data/data_sponsored_messages.h"
#include "data/data_user.h"
#include "data/data_web_page.h"
#include "history/view/history_view_cursor_state.h"
#include "history/history_item_components.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/click_handler.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "ui/round_rect.h"
#include "ui/text/text_utilities.h" // Ui::Text::ToUpper
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"

namespace HistoryView {
namespace {

using SponsoredType = HistoryMessageSponsored::Type;

inline auto SponsoredPhrase(SponsoredType type) {
	const auto phrase = [&] {
		switch (type) {
		case SponsoredType::User: return tr::lng_view_button_user;
		case SponsoredType::Group: return tr::lng_view_button_group;
		case SponsoredType::Broadcast: return tr::lng_view_button_channel;
		case SponsoredType::Post: return tr::lng_view_button_message;
		case SponsoredType::Bot: return tr::lng_view_button_bot;
		case SponsoredType::ExternalLink:
			return tr::lng_view_button_external_link;
		}
		Unexpected("SponsoredType in SponsoredPhrase.");
	}();
	return Ui::Text::Upper(phrase(tr::now));
}

[[nodiscard]] ClickHandlerPtr MakeMediaButtonClickHandler(
		not_null<Data::Media*> media) {
	const auto giveaway = media->giveaway();
	Assert(giveaway != nullptr);
	const auto peer = media->parent()->history()->peer;
	const auto messageId = media->parent()->id;
	if (media->parent()->isSending() || media->parent()->hasFailed()) {
		return nullptr;
	}
	const auto info = *giveaway;
	return std::make_shared<LambdaClickHandler>([=](
			ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		const auto controller = my.sessionWindow.get();
		if (!controller) {
			return;
		}
		ResolveGiveawayInfo(controller, peer, messageId, info);
	});
}

[[nodiscard]] QString MakeMediaButtonText(not_null<Data::Media*> media) {
	const auto giveaway = media->giveaway();
	Assert(giveaway != nullptr);
	return Ui::Text::Upper(tr::lng_prizes_how_works(tr::now));
}

[[nodiscard]] ClickHandlerPtr SponsoredLink(
		not_null<HistoryMessageSponsored*> sponsored) {
	if (!sponsored->externalLink.isEmpty()) {
		class ClickHandler : public UrlClickHandler {
		public:
			using UrlClickHandler::UrlClickHandler;

			QString copyToClipboardContextItemText() const override {
				return QString();
			}

		};

		return std::make_shared<ClickHandler>(
			sponsored->externalLink,
			false);
	} else {
		return std::make_shared<LambdaClickHandler>([](ClickContext context) {
			const auto my = context.other.value<ClickHandlerContext>();
			const auto controller = my.sessionWindow.get();
			if (!controller) {
				return;
			}
			const auto &data = controller->session().data();
			const auto details = data.sponsoredMessages().lookupDetails(
				my.itemId);
			if (!details.externalLink.isEmpty()) {
				File::OpenUrl(details.externalLink);
			} else if (details.hash) {
				Api::CheckChatInvite(controller, *details.hash);
			} else if (details.peer) {
				controller->showPeerHistory(
					details.peer,
					Window::SectionShow::Way::Forward,
					details.msgId);
			}
		});
	}
}

} // namespace

struct ViewButton::Inner {
	Inner(
		not_null<HistoryMessageSponsored*> sponsored,
		uint8 colorIndex,
		Fn<void()> updateCallback);
	Inner(
		not_null<Data::Media*> media,
		uint8 colorIndex,
		Fn<void()> updateCallback);

	void updateMask(int height);
	void toggleRipple(bool pressed);

	const style::margins &margins;
	const ClickHandlerPtr link;
	const Fn<void()> updateCallback;
	uint32 lastWidth : 24 = 0;
	uint32 colorIndex : 6 = 0;
	uint32 aboveInfo : 1 = 0;
	uint32 externalLink : 1 = 0;
	QPoint lastPoint;
	std::unique_ptr<Ui::RippleAnimation> ripple;
	Ui::Text::String text;
};

bool ViewButton::MediaHasViewButton(not_null<Data::Media*> media) {
	return (media->giveaway() != nullptr);
}

ViewButton::Inner::Inner(
	not_null<HistoryMessageSponsored*> sponsored,
	uint8 colorIndex,
	Fn<void()> updateCallback)
: margins(st::historyViewButtonMargins)
, link(SponsoredLink(sponsored))
, updateCallback(std::move(updateCallback))
, colorIndex(colorIndex)
, externalLink((sponsored->type == SponsoredType::ExternalLink) ? 1 : 0)
, text(st::historyViewButtonTextStyle, SponsoredPhrase(sponsored->type)) {
}

ViewButton::Inner::Inner(
	not_null<Data::Media*> media,
	uint8 colorIndex,
	Fn<void()> updateCallback)
: margins(st::historyViewButtonMargins)
, link(MakeMediaButtonClickHandler(media))
, updateCallback(std::move(updateCallback))
, colorIndex(colorIndex)
, aboveInfo(1)
, text(st::historyViewButtonTextStyle, MakeMediaButtonText(media)) {
}

void ViewButton::Inner::updateMask(int height) {
	ripple = std::make_unique<Ui::RippleAnimation>(
		st::defaultRippleAnimation,
		Ui::RippleAnimation::RoundRectMask(
			QSize(lastWidth, height - margins.top() - margins.bottom()),
			st::roundRadiusLarge),
		updateCallback);
}

void ViewButton::Inner::toggleRipple(bool pressed) {
	if (ripple) {
		if (pressed) {
			ripple->add(lastPoint);
		} else {
			ripple->lastStop();
		}
	}
}

ViewButton::ViewButton(
	not_null<HistoryMessageSponsored*> sponsored,
	uint8 colorIndex,
	Fn<void()> updateCallback)
: _inner(std::make_unique<Inner>(
	sponsored,
	colorIndex,
	std::move(updateCallback))) {
}

ViewButton::ViewButton(
	not_null<Data::Media*> media,
	uint8 colorIndex,
	Fn<void()> updateCallback)
: _inner(std::make_unique<Inner>(
	media,
	colorIndex,
	std::move(updateCallback))) {
}

ViewButton::~ViewButton() {
}

void ViewButton::resized() const {
	_inner->updateMask(height());
}

int ViewButton::height() const {
	return st::historyViewButtonHeight;
}

bool ViewButton::belowMessageInfo() const {
	return !_inner->aboveInfo;
}

void ViewButton::draw(
		Painter &p,
		const QRect &r,
		const Ui::ChatPaintContext &context) {
	const auto st = context.st;
	const auto stm = context.messageStyle();
	const auto selected = context.selected();
	const auto cache = context.outbg
		? stm->replyCache[st->colorPatternIndex(_inner->colorIndex)].get()
		: st->coloredReplyCache(selected, _inner->colorIndex).get();
	const auto radius = st::historyPagePreview.radius;

	if (_inner->ripple && !_inner->ripple->empty()) {
		_inner->ripple->paint(p, r.left(), r.top(), r.width(), &cache->bg);
	}

	PainterHighQualityEnabler hq(p);
	p.setPen(Qt::NoPen);
	p.setBrush(cache->bg);
	p.drawRoundedRect(r, radius, radius);

	p.setPen(cache->icon);
	_inner->text.drawElided(
		p,
		r.left(),
		r.top() + (r.height() - _inner->text.minHeight()) / 2,
		r.width(),
		1,
		style::al_top);

	if (_inner->externalLink) {
		const auto &icon = st::msgBotKbUrlIcon;
		const auto padding = st::msgBotKbIconPadding;
		icon.paint(
			p,
			r.left() + r.width() - icon.width() - padding,
			r.top() + padding,
			r.width(),
			cache->icon);
	}
	if (_inner->lastWidth != r.width()) {
		_inner->lastWidth = r.width();
		resized();
	}
}

const ClickHandlerPtr &ViewButton::link() const {
	return _inner->link;
}

bool ViewButton::checkLink(const ClickHandlerPtr &other, bool pressed) {
	if (_inner->link != other) {
		return false;
	}
	_inner->toggleRipple(pressed);
	return true;
}

bool ViewButton::getState(
		QPoint point,
		const QRect &g,
		not_null<TextState*> outResult) const {
	if (!g.contains(point)) {
		return false;
	}
	outResult->link = _inner->link;
	_inner->lastPoint = point - g.topLeft();
	return true;
}

QRect ViewButton::countRect(const QRect &r) const {
	return QRect(
		r.left(),
		r.top() + r.height() - height(),
		r.width(),
		height()) - _inner->margins;
}

} // namespace HistoryView
