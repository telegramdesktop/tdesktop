/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/stories/media_stories_share.h"

#include "api/api_common.h"
#include "apiwrap.h"
#include "base/random.h"
#include "boxes/share_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/business/data_shortcut_messages.h"
#include "data/data_channel.h"
#include "data/data_chat_participant_status.h"
#include "data/data_forum_topic.h"
#include "data/data_histories.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_stories.h"
#include "data/data_thread.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item_helpers.h" // GetErrorForSending.
#include "history/view/history_view_context_menu.h" // CopyStoryLink.
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_credits_graphics.h"
#include "ui/boxes/confirm_box.h"
#include "ui/text/text_utilities.h"
#include "styles/style_calls.h"

namespace Media::Stories {

[[nodiscard]] object_ptr<Ui::BoxContent> PrepareShareBox(
		std::shared_ptr<ChatHelpers::Show> show,
		FullStoryId id,
		bool viewerStyle) {
	const auto session = &show->session();
	const auto resolve = [=] {
		const auto maybeStory = session->data().stories().lookup(id);
		return maybeStory ? maybeStory->get() : nullptr;
	};
	const auto story = resolve();
	if (!story) {
		return { nullptr };
	}
	const auto canCopyLink = story->hasDirectLink();

	auto copyCallback = [=] {
		const auto story = resolve();
		if (!story) {
			return;
		}
		if (story->hasDirectLink()) {
			using namespace HistoryView;
			CopyStoryLink(show, story->fullId());
		}
	};

	struct State {
		int requests = 0;
	};
	const auto state = std::make_shared<State>();
	auto filterCallback = [=](not_null<Data::Thread*> thread) {
		if (const auto user = thread->peer()->asUser()) {
			if (user->canSendIgnoreRequirePremium()) {
				return true;
			}
		}
		return Data::CanSend(thread, ChatRestriction::SendPhotos)
			&& Data::CanSend(thread, ChatRestriction::SendVideos);
	};
	auto copyLinkCallback = canCopyLink
		? Fn<void()>(std::move(copyCallback))
		: Fn<void()>();
	auto submitCallback = [=](
			std::vector<not_null<Data::Thread*>> &&result,
			TextWithTags &&comment,
			Api::SendOptions options,
			Data::ForwardOptions forwardOptions) {
		if (state->requests) {
			return; // Share clicked already.
		}
		const auto story = resolve();
		if (!story) {
			return;
		}
		const auto peer = story->peer();
		const auto error = GetErrorForSending(
			result,
			{ .story = story, .text = &comment });
		if (error.error) {
			show->showBox(MakeSendErrorBox(error, result.size() > 1));
			return;
		}

		const auto api = &story->owner().session().api();
		auto &histories = story->owner().histories();
		for (const auto thread : result) {
			const auto action = Api::SendAction(thread, options);
			if (!comment.text.isEmpty()) {
				auto message = Api::MessageToSend(action);
				message.textWithTags = comment;
				message.action.clearDraft = false;
				api->sendMessage(std::move(message));
			}
			const auto session = &thread->session();
			const auto threadPeer = thread->peer();
			const auto threadHistory = thread->owningHistory();
			const auto randomId = base::RandomValue<uint64>();
			auto sendFlags = MTPmessages_SendMedia::Flags(0);
			if (action.replyTo) {
				sendFlags |= MTPmessages_SendMedia::Flag::f_reply_to;
			}
			const auto silentPost = ShouldSendSilent(threadPeer, options);
			if (silentPost) {
				sendFlags |= MTPmessages_SendMedia::Flag::f_silent;
			}
			if (options.scheduled) {
				sendFlags |= MTPmessages_SendMedia::Flag::f_schedule_date;
			}
			if (options.shortcutId) {
				sendFlags |= MTPmessages_SendMedia::Flag::f_quick_reply_shortcut;
			}
			if (options.effectId) {
				sendFlags |= MTPmessages_SendMedia::Flag::f_effect;
			}
			if (options.invertCaption) {
				sendFlags |= MTPmessages_SendMedia::Flag::f_invert_media;
			}
			const auto done = [=] {
				if (!--state->requests) {
					if (show->valid()) {
						show->showToast(tr::lng_share_done(tr::now));
						show->hideLayer();
					}
				}
			};
			histories.sendPreparedMessage(
				threadHistory,
				action.replyTo,
				randomId,
				Data::Histories::PrepareMessage<MTPmessages_SendMedia>(
					MTP_flags(sendFlags),
					threadPeer->input,
					Data::Histories::ReplyToPlaceholder(),
					MTP_inputMediaStory(peer->input, MTP_int(id.story)),
					MTPstring(),
					MTP_long(randomId),
					MTPReplyMarkup(),
					MTPVector<MTPMessageEntity>(),
					MTP_int(options.scheduled),
					MTP_inputPeerEmpty(),
					Data::ShortcutIdToMTP(session, options.shortcutId),
					MTP_long(options.effectId)
				), [=](
						const MTPUpdates &result,
						const MTP::Response &response) {
					done();
				}, [=](
						const MTP::Error &error,
						const MTP::Response &response) {
					api->sendMessageFail(error, threadPeer, randomId);
					done();
				});
			++state->requests;
		}
	};
	const auto st = viewerStyle
		? ::Settings::DarkCreditsEntryBoxStyle()
		: ::Settings::CreditsEntryBoxStyleOverrides();
	return Box<ShareBox>(ShareBox::Descriptor{
		.session = session,
		.copyCallback = std::move(copyLinkCallback),
		.submitCallback = std::move(submitCallback),
		.filterCallback = std::move(filterCallback),
		.st = st.shareBox ? *st.shareBox : ShareBoxStyleOverrides(),
		.premiumRequiredError = SharePremiumRequiredError(),
	});
}

QString FormatShareAtTime(TimeId seconds) {
	const auto minutes = seconds / 60;
	const auto h = minutes / 60;
	const auto m = minutes % 60;
	const auto s = seconds % 60;
	const auto zero = QChar('0');
	return h
		? u"%1:%2:%3"_q.arg(h).arg(m, 2, 10, zero).arg(s, 2, 10, zero)
		: u"%1:%2"_q.arg(m).arg(s, 2, 10, zero);
}

object_ptr<Ui::BoxContent> PrepareShareAtTimeBox(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<HistoryItem*> item,
		TimeId videoTimestamp) {
	const auto id = item->fullId();
	const auto history = item->history();
	const auto owner = &history->owner();
	const auto session = &history->session();
	const auto canCopyLink = item->hasDirectLink()
		&& history->peer->isBroadcast()
		&& history->peer->asBroadcast()->hasUsername();
	const auto hasCaptions = item->media()
		&& !item->originalText().text.isEmpty()
		&& item->media()->allowsEditCaption();
	const auto hasOnlyForcedForwardedInfo = !hasCaptions
		&& item->media()
		&& item->media()->forceForwardedInfo();

	auto copyCallback = [=] {
		const auto item = owner->message(id);
		if (!item) {
			return;
		}
		CopyPostLink(
			show,
			item->fullId(),
			HistoryView::Context::History,
			videoTimestamp);
	};

	const auto requiredRight = item->requiredSendRight();
	const auto requiresInline = item->requiresSendInlineRight();
	auto filterCallback = [=](not_null<Data::Thread*> thread) {
		if (const auto user = thread->peer()->asUser()) {
			if (user->canSendIgnoreRequirePremium()) {
				return true;
			}
		}
		return Data::CanSend(thread, requiredRight)
			&& (!requiresInline
				|| Data::CanSend(thread, ChatRestriction::SendInline));
	};
	auto copyLinkCallback = canCopyLink
		? Fn<void()>(std::move(copyCallback))
		: Fn<void()>();
	const auto st = ::Settings::DarkCreditsEntryBoxStyle();
	return Box<ShareBox>(ShareBox::Descriptor{
		.session = session,
		.copyCallback = std::move(copyLinkCallback),
		.submitCallback = ShareBox::DefaultForwardCallback(
			show,
			history,
			{ id },
			videoTimestamp),
		.filterCallback = std::move(filterCallback),
		.titleOverride = tr::lng_share_at_time_title(
			lt_time,
			rpl::single(FormatShareAtTime(videoTimestamp))),
		.st = st.shareBox ? *st.shareBox : ShareBoxStyleOverrides(),
		.forwardOptions = {
			.sendersCount = ItemsForwardSendersCount({ item }),
			.captionsCount = ItemsForwardCaptionsCount({ item }),
			.show = !hasOnlyForcedForwardedInfo,
		},
		.premiumRequiredError = SharePremiumRequiredError(),
	});
}

} // namespace Media::Stories
