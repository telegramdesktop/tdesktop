/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/stories/media_stories_reply.h"

#include "base/call_delayed.h"
#include "chat_helpers/compose/compose_show.h"
#include "chat_helpers/tabbed_selector.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/view/controls/compose_controls_common.h"
#include "history/view/controls/history_view_compose_controls.h"
#include "inline_bots/inline_bot_result.h"
#include "media/stories/media_stories_controller.h"
#include "menu/menu_send.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_media_view.h"

namespace Media::Stories {

ReplyArea::ReplyArea(not_null<Controller*> controller)
: _controller(controller)
, _controls(std::make_unique<HistoryView::ComposeControls>(
	_controller->wrap(),
	HistoryView::ComposeControlsDescriptor{
		.stOverride = &st::storiesComposeControls,
		.show = _controller->uiShow(),
		.unavailableEmojiPasted = [=](not_null<DocumentData*> emoji) {
			showPremiumToast(emoji);
		},
		.mode = HistoryView::ComposeControlsMode::Normal,
		.sendMenuType = SendMenu::Type::SilentOnly,
		.stickerOrEmojiChosen = _controller->stickerOrEmojiChosen(),
		.features = {
			.sendAs = false,
			.ttlInfo = false,
			.botCommandSend = false,
			.silentBroadcastToggle = false,
			.attachBotsMenu = false,
			.inlineBots = false,
			.megagroupSet = false,
			.stickersSettings = false,
			.openStickerSets = false,
			.autocompleteHashtags = false,
			.autocompleteMentions = false,
			.autocompleteCommands = false,
		},
	}
)) {
	initGeometry();
	initActions();
}

ReplyArea::~ReplyArea() {
}

void ReplyArea::initGeometry() {
	rpl::combine(
		_controller->layoutValue(),
		_controls->height()
	) | rpl::start_with_next([=](const Layout &layout, int height) {
		const auto content = layout.content;
		_controls->resizeToWidth(content.width());
		if (_controls->heightCurrent() == height) {
			const auto position = layout.controlsBottomPosition
				- QPoint(0, height);
			_controls->move(position.x(), position.y());
			const auto &tabbed = st::storiesComposeControls.tabbed;
			const auto upper = QRect(
				content.x(),
				content.y(),
				content.width(),
				(position.y()
					+ tabbed.autocompleteBottomSkip
					- content.y()));
			_controls->setAutocompleteBoundingRect(
				layout.autocompleteRect.intersected(upper));
		}
	}, _lifetime);
}

void ReplyArea::send(Api::SendOptions options) {
	// #TODO stories
}

void ReplyArea::sendVoice(VoiceToSend &&data) {
	// #TODO stories
}

void ReplyArea::chooseAttach(std::optional<bool> overrideCompress) {
	// #TODO stories
}

void ReplyArea::initActions() {
	_controls->cancelRequests(
	) | rpl::start_with_next([=] {
		_controller->unfocusReply();
	}, _lifetime);

	_controls->sendRequests(
	) | rpl::start_with_next([=](Api::SendOptions options) {
		send(options);
	}, _lifetime);

	_controls->sendVoiceRequests(
	) | rpl::start_with_next([=](VoiceToSend &&data) {
		sendVoice(std::move(data));
	}, _lifetime);

	_controls->attachRequests(
	) | rpl::filter([=] {
		return !_choosingAttach;
	}) | rpl::start_with_next([=](std::optional<bool> overrideCompress) {
		_choosingAttach = true;
		base::call_delayed(
			st::storiesAttach.ripple.hideDuration,
			this,
			[=] { chooseAttach(overrideCompress); });
	}, _lifetime);

	_controls->fileChosen(
	) | rpl::start_with_next([=](ChatHelpers::FileChosen data) {
		_controller->uiShow()->hideLayer();
		//controller()->sendingAnimation().appendSending(
		//	data.messageSendingFrom);
		//const auto localId = data.messageSendingFrom.localId;
		//sendExistingDocument(data.document, data.options, localId);
	}, _lifetime);

	_controls->photoChosen(
	) | rpl::start_with_next([=](ChatHelpers::PhotoChosen chosen) {
		//sendExistingPhoto(chosen.photo, chosen.options);
	}, _lifetime);

	_controls->inlineResultChosen(
	) | rpl::start_with_next([=](ChatHelpers::InlineChosen chosen) {
		//controller()->sendingAnimation().appendSending(
		//	chosen.messageSendingFrom);
		//const auto localId = chosen.messageSendingFrom.localId;
		//sendInlineResult(chosen.result, chosen.bot, chosen.options, localId);
	}, _lifetime);

	_controls->setMimeDataHook([=](
			not_null<const QMimeData*> data,
			Ui::InputField::MimeAction action) {
		if (action == Ui::InputField::MimeAction::Check) {
			return false;// checkSendingFiles(data);
		} else if (action == Ui::InputField::MimeAction::Insert) {
			return false;/* confirmSendingFiles(
				data,
				std::nullopt,
				Core::ReadMimeText(data));*/
		}
		Unexpected("action in MimeData hook.");
	});

	_controls->lockShowStarts(
	) | rpl::start_with_next([=] {
	}, _lifetime);

	_controls->show();
	_controls->finishAnimating();
	_controls->showFinished();
}

void ReplyArea::show(ReplyAreaData data) {
	if (_data == data) {
		return;
	}
	const auto userChanged = (_data.user != data.user);
	_data = data;
	if (!userChanged) {
		if (_data.user) {
			_controls->clear();
		}
		return;
	}
	const auto user = data.user;
	const auto history = user ? user->owner().history(user).get() : nullptr;
	_controls->setHistory({
		.history = history,
	});
	_controls->clear();
}

rpl::producer<bool> ReplyArea::focusedValue() const {
	return _controls->focusedValue();
}

void ReplyArea::showPremiumToast(not_null<DocumentData*> emoji) {
	// #TODO stories
}

} // namespace Media::Stories
