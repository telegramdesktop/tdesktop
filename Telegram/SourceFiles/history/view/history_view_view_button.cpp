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
#include "data/data_cloud_themes.h"
#include "data/data_session.h"
#include "data/data_sponsored_messages.h"
#include "data/data_user.h"
#include "data/data_web_page.h"
#include "history/history_item_components.h"
#include "history/view/history_view_cursor_state.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/click_handler.h"
#include "ui/effects/ripple_animation.h"
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
		}
		Unexpected("SponsoredType in SponsoredPhrase.");
	}();
	return Ui::Text::Upper(phrase(tr::now));
}

inline auto WebPageToPhrase(not_null<WebPageData*> webpage) {
	const auto type = webpage->type;
	return Ui::Text::Upper((type == WebPageType::Theme)
		? tr::lng_view_button_theme(tr::now)
		: (type == WebPageType::Message)
		? tr::lng_view_button_message(tr::now)
		: (type == WebPageType::Group)
		? tr::lng_view_button_group(tr::now)
		: (type == WebPageType::WallPaper)
		? tr::lng_view_button_background(tr::now)
		: (type == WebPageType::Channel)
		? tr::lng_view_button_channel(tr::now)
		: (type == WebPageType::GroupWithRequest
			|| type == WebPageType::ChannelWithRequest)
		? tr::lng_view_button_request_join(tr::now)
		: (type == WebPageType::VoiceChat)
		? tr::lng_view_button_voice_chat(tr::now)
		: (type == WebPageType::Livestream)
		? tr::lng_view_button_voice_chat_channel(tr::now)
		: (type == WebPageType::Bot)
		? tr::lng_view_button_bot(tr::now)
		: (type == WebPageType::User)
		? tr::lng_view_button_user(tr::now)
		: QString());
}

} // namespace

struct ViewButton::Inner {
	Inner(
		not_null<HistoryMessageSponsored*> sponsored,
		Fn<void()> updateCallback);
	Inner(not_null<Data::Media*> media, Fn<void()> updateCallback);

	void updateMask(int height);
	void toggleRipple(bool pressed);

	const style::margins &margins;
	const ClickHandlerPtr link;
	const Fn<void()> updateCallback;
	bool belowInfo = true;
	int lastWidth = 0;
	QPoint lastPoint;
	std::unique_ptr<Ui::RippleAnimation> ripple;
	Ui::Text::String text;
};

bool ViewButton::MediaHasViewButton(not_null<Data::Media*> media) {
	return media->webpage()
		? MediaHasViewButton(media->webpage())
		: false;
}

bool ViewButton::MediaHasViewButton(
		not_null<WebPageData*> webpage) {
	const auto type = webpage->type;
	return (type == WebPageType::Message)
		|| (type == WebPageType::Group)
		|| (type == WebPageType::Channel)
		// || (type == WebPageType::Bot)
		// || (type == WebPageType::User)
		|| (type == WebPageType::VoiceChat)
		|| (type == WebPageType::Livestream)
		|| ((type == WebPageType::Theme)
			&& webpage->document
			&& webpage->document->isTheme())
		|| ((type == WebPageType::WallPaper)
			&& webpage->document
			&& webpage->document->isWallPaper());
}

ViewButton::Inner::Inner(
	not_null<HistoryMessageSponsored*> sponsored,
	Fn<void()> updateCallback)
: margins(st::historyViewButtonMargins)
, link(std::make_shared<LambdaClickHandler>([=](ClickContext context) {
	const auto my = context.other.value<ClickHandlerContext>();
	if (const auto controller = my.sessionWindow.get()) {
		const auto &data = controller->session().data();
		const auto itemId = my.itemId;
		const auto details = data.sponsoredMessages().lookupDetails(itemId);
		if (details.hash) {
			Api::CheckChatInvite(controller, *details.hash);
		} else if (details.peer) {
			controller->showPeerHistory(
				details.peer,
				Window::SectionShow::Way::Forward,
				details.msgId);
		}
	}
}))
, updateCallback(std::move(updateCallback))
, text(st::historyViewButtonTextStyle, SponsoredPhrase(sponsored->type)) {
}

ViewButton::Inner::Inner(
	not_null<Data::Media*> media,
	Fn<void()> updateCallback)
: margins(st::historyViewButtonMargins)
, link(std::make_shared<LambdaClickHandler>([=](ClickContext context) {
	const auto my = context.other.value<ClickHandlerContext>();
	if (const auto controller = my.sessionWindow.get()) {
		const auto webpage = media->webpage();
		if (!webpage) {
			return;
		}
		HiddenUrlClickHandler::Open(webpage->url, context.other);
	}
}))
, updateCallback(std::move(updateCallback))
, belowInfo(false)
, text(st::historyViewButtonTextStyle, WebPageToPhrase(media->webpage())) {
}

void ViewButton::Inner::updateMask(int height) {
	ripple = std::make_unique<Ui::RippleAnimation>(
		st::defaultRippleAnimation,
		Ui::RippleAnimation::roundRectMask(
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
	Fn<void()> updateCallback)
: _inner(std::make_unique<Inner>(sponsored, std::move(updateCallback))) {
}

ViewButton::ViewButton(
	not_null<Data::Media*> media,
	Fn<void()> updateCallback)
: _inner(std::make_unique<Inner>(media, std::move(updateCallback))) {
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
	return _inner->belowInfo;
}

void ViewButton::draw(
		Painter &p,
		const QRect &r,
		const Ui::ChatPaintContext &context) {
	const auto stm = context.messageStyle();

	if (_inner->ripple && !_inner->ripple->empty()) {
		const auto opacity = p.opacity();
		p.setOpacity(st::historyPollRippleOpacity);
		const auto colorOverride = &stm->msgWaveformInactive->c;
		_inner->ripple->paint(p, r.left(), r.top(), r.width(), colorOverride);
		p.setOpacity(opacity);
	}

	p.save();
	{
		PainterHighQualityEnabler hq(p);
		auto pen = stm->fwdTextPalette.linkFg->p;
		pen.setWidth(st::lineWidth);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		const auto half = st::lineWidth / 2.;
		const auto rf = QRectF(r).marginsRemoved({ half, half, half, half });
		p.drawRoundedRect(rf, st::roundRadiusLarge, st::roundRadiusLarge);

		_inner->text.drawElided(
			p,
			r.left(),
			r.top() + (r.height() - _inner->text.minHeight()) / 2,
			r.width(),
			1,
			style::al_center);
	}
	p.restore();
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
