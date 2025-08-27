/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_item_helpers.h"

#include "api/api_text_entities.h"
#include "boxes/premium_preview_box.h"
#include "calls/calls_instance.h"
#include "data/components/sponsored_messages.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/notify/data_notify_settings.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_changes.h"
#include "data/data_document.h"
#include "data/data_group_call.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_message_reactions.h"
#include "data/data_session.h"
#include "data/data_stories.h"
#include "data/data_user.h"
#include "history/view/controls/history_view_suggest_options.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "menu/menu_sponsored.h"
#include "platform/platform_notifications_manager.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "apiwrap.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "core/click_handler_types.h" // ClickHandlerContext.
#include "settings/settings_credits_graphics.h"
#include "storage/storage_account.h"
#include "ui/boxes/confirm_box.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/checkbox.h"
#include "ui/item_text_options.h"
#include "lang/lang_keys.h"

namespace {

bool PeerCallKnown(not_null<PeerData*> peer) {
	if (peer->groupCall() != nullptr) {
		return true;
	} else if (const auto chat = peer->asChat()) {
		return !(chat->flags() & ChatDataFlag::CallActive);
	} else if (const auto channel = peer->asChannel()) {
		return !(channel->flags() & ChannelDataFlag::CallActive);
	}
	return true;
}

} // namespace

int ComputeSendingMessagesCount(
		not_null<History*> history,
		const SendingErrorRequest &request) {
	auto result = 0;
	if (request.text && !request.text->empty()) {
		auto sending = TextWithEntities();
		auto left = TextWithEntities{
			request.text->text,
			TextUtilities::ConvertTextTagsToEntities(request.text->tags)
		};
		auto prepareFlags = Ui::ItemTextOptions(
			history,
			history->session().user()).flags;
		TextUtilities::PrepareForSending(left, prepareFlags);

		while (TextUtilities::CutPart(sending, left, MaxMessageSize)) {
			++result;
		}
		if (!result) {
			++result;
		}
	}
	return result
		+ (request.story ? 1 : 0)
		+ (request.forward ? int(request.forward->size()) : 0);
}

Data::SendError GetErrorForSending(
		not_null<PeerData*> peer,
		SendingErrorRequest request) {
	const auto forum = request.topicRootId ? peer->forum() : nullptr;
	const auto topic = forum
		? forum->topicFor(request.topicRootId)
		: nullptr;
	const auto thread = topic
		? not_null<Data::Thread*>(topic)
		: peer->owner().history(peer);
	if (request.story) {
		if (const auto error = request.story->errorTextForForward(thread)) {
			return error;
		}
	}
	if (request.forward) {
		for (const auto &item : *request.forward) {
			if (const auto error = item->errorTextForForward(thread)) {
				return error;
			}
		}
	}
	const auto hasText = (request.text && !request.text->empty());
	if (hasText) {
		const auto error = Data::RestrictionError(
			peer,
			ChatRestriction::SendOther);
		if (error) {
			return error;
		} else if (!Data::CanSendTexts(thread)) {
			return tr::lng_forward_cant(tr::now);
		}
	}
	if (peer->slowmodeApplied()) {
		const auto count = request.messagesCount
			? request.messagesCount
			: ComputeSendingMessagesCount(thread->owningHistory(), request);
		if (const auto history = peer->owner().historyLoaded(peer)) {
			if (!request.ignoreSlowmodeCountdown
				&& (history->latestSendingMessage() != nullptr)
				&& (count > 0)) {
				return tr::lng_slowmode_no_many(tr::now);
			}
		}
		if (request.text && request.text->text.size() > MaxMessageSize) {
			return tr::lng_slowmode_too_long(tr::now);
		} else if ((hasText || request.story) && count > 1) {
			return tr::lng_slowmode_no_many(tr::now);
		} else if (count > 1) {
			const auto albumForward = [&] {
				const auto first = request.forward->front();
				if (const auto groupId = first->groupId()) {
					for (const auto &item : *request.forward) {
						if (item->groupId() != groupId) {
							return false;
						}
					}
					return true;
				}
				return false;
			}();
			if (!albumForward) {
				return tr::lng_slowmode_no_many(tr::now);
			}
		}
	}
	if (const auto left = peer->slowmodeSecondsLeft()) {
		if (!request.ignoreSlowmodeCountdown) {
			return tr::lng_slowmode_enabled(
				tr::now,
				lt_left,
				Ui::FormatDurationWordsSlowmode(left));
		}
	}
	return {};
}

Data::SendError GetErrorForSending(
		not_null<Data::Thread*> thread,
		SendingErrorRequest request) {
	request.topicRootId = thread->topicRootId();
	return GetErrorForSending(thread->peer(), std::move(request));
}

Data::SendErrorWithThread GetErrorForSending(
		const std::vector<not_null<Data::Thread*>> &threads,
		SendingErrorRequest request) {
	for (const auto thread : threads) {
		const auto error = GetErrorForSending(thread, request);
		if (error) {
			return Data::SendErrorWithThread{ error, thread };
		}
	}
	return {};
}

std::optional<SendPaymentDetails> ComputePaymentDetails(
		not_null<PeerData*> peer,
		int messagesCount) {
	const auto user = peer->asUser();
	const auto channel = user ? nullptr : peer->asChannel();
	const auto has = (user && user->hasStarsPerMessage())
		|| (channel && channel->hasStarsPerMessage());
	if (!has) {
		return SendPaymentDetails();
	}

	const auto known1 = peer->session().credits().loaded();
	if (!known1) {
		peer->session().credits().load();
	}

	const auto known2 = user
		? user->messageMoneyRestrictionsKnown()
		: channel->starsPerMessageKnown();
	if (!known2) {
		peer->updateFull();
	}

	if (!known1 || !known2) {
		return {};
	} else if (const auto perMessage = peer->starsPerMessageChecked()) {
		return SendPaymentDetails{
			.messages = messagesCount,
			.stars = messagesCount * perMessage,
		};
	}
	return SendPaymentDetails();
}

bool SuggestPaymentDataReady(
		not_null<PeerData*> peer,
		SuggestPostOptions suggest) {
	if (!suggest.exists || !suggest.price() || peer->amMonoforumAdmin()) {
		return true;
	} else if (suggest.ton && !peer->session().credits().tonLoaded()) {
		peer->session().credits().tonLoad();
		return false;
	} else if (!suggest.ton && !peer->session().credits().loaded()) {
		peer->session().credits().load();
		return false;
	}
	return true;
}

object_ptr<Ui::BoxContent> MakeSendErrorBox(
		const Data::SendErrorWithThread &error,
		bool withTitle) {
	Expects(error.error.has_value() && error.thread != nullptr);

	auto text = TextWithEntities();
	if (withTitle) {
		text.append(
			Ui::Text::Bold(error.thread->chatListName())
		).append("\n\n");
	}
	if (error.error.boostsToLift) {
		text.append(Ui::Text::Link(error.error.text));
	} else {
		text.append(error.error.text);
	}
	const auto peer = error.thread->peer();
	const auto lifting = error.error.boostsToLift;
	const auto filter = [=](const auto &...) {
		Expects(peer->isChannel());

		const auto window = ChatHelpers::ResolveWindowDefault()(
			&peer->session());
		window->resolveBoostState(peer->asChannel(), lifting);
		return false;
	};
	return Ui::MakeInformBox({
		.text = text,
		.labelFilter = filter,
	});
}

void ShowSendPaidConfirm(
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer,
		SendPaymentDetails details,
		Fn<void()> confirmed,
		PaidConfirmStyles styles,
		int suggestStarsPrice) {
	return ShowSendPaidConfirm(
		navigation->uiShow(),
		peer,
		details,
		confirmed,
		styles,
		suggestStarsPrice);
}

void ShowSendPaidConfirm(
		std::shared_ptr<Main::SessionShow> show,
		not_null<PeerData*> peer,
		SendPaymentDetails details,
		Fn<void()> confirmed,
		PaidConfirmStyles styles,
		int suggestStarsPrice) {
	ShowSendPaidConfirm(
		std::move(show),
		std::vector<not_null<PeerData*>>{ peer },
		details,
		confirmed,
		styles,
		suggestStarsPrice);
}

void ShowSendPaidConfirm(
		std::shared_ptr<Main::SessionShow> show,
		const std::vector<not_null<PeerData*>> &peers,
		SendPaymentDetails details,
		Fn<void()> confirmed,
		PaidConfirmStyles styles,
		int suggestStarsPrice) {
	Expects(!peers.empty());

	const auto singlePeer = (peers.size() > 1)
		? (PeerData*)nullptr
		: peers.front().get();
	const auto singlePeerId = singlePeer ? singlePeer->id : PeerId();
	const auto check = [=] {
		const auto required = details.stars + suggestStarsPrice;
		if (!required) {
			return;
		}
		const auto done = [=](Settings::SmallBalanceResult result) {
			if (result == Settings::SmallBalanceResult::Success
				|| result == Settings::SmallBalanceResult::Already) {
				confirmed();
			}
		};
		using namespace Settings;
		MaybeRequestBalanceIncrease(
			show,
			required,
			(suggestStarsPrice
				? SmallBalanceSource(SmallBalanceForSuggest{ singlePeerId })
				: SmallBalanceForMessage{ singlePeerId }),
			done);
	};
	auto usersOnly = true;
	for (const auto &peer : peers) {
		if (!peer->isUser()) {
			usersOnly = false;
			break;
		}
	}
	const auto singlePeerStars = singlePeer
		? singlePeer->starsPerMessageChecked()
		: 0;
	if (singlePeer) {
		const auto session = &singlePeer->session();
		const auto trusted = session->local().isPeerTrustedPayForMessage(
			singlePeerId,
			singlePeerStars);
		if (trusted) {
			check();
			return;
		}
	}
	const auto messages = details.messages;
	const auto stars = details.stars;
	show->showBox(Box([=](not_null<Ui::GenericBox*> box) {
		const auto trust = std::make_shared<base::weak_qptr<Ui::Checkbox>>();
		const auto proceed = [=](Fn<void()> close) {
			if (singlePeer && (*trust)->checked()) {
				const auto session = &singlePeer->session();
				session->local().markPeerTrustedPayForMessage(
					singlePeerId,
					singlePeerStars);
			}
			check();
			close();
		};
		Ui::ConfirmBox(box, {
			.text = (singlePeer
				? tr::lng_payment_confirm_text(
					tr::now,
					lt_count,
					stars / messages,
					lt_name,
					Ui::Text::Bold(singlePeer->shortName()),
					Ui::Text::RichLangValue)
				: (usersOnly
					? tr::lng_payment_confirm_users
					: tr::lng_payment_confirm_chats)(
						tr::now,
						lt_count,
						int(peers.size()),
						Ui::Text::RichLangValue)).append(' ').append(
							tr::lng_payment_confirm_sure(
								tr::now,
								lt_count,
								messages,
								lt_amount,
								tr::lng_payment_confirm_amount(
									tr::now,
									lt_count,
									stars,
									Ui::Text::RichLangValue),
								Ui::Text::RichLangValue)),
			.confirmed = proceed,
			.confirmText = tr::lng_payment_confirm_button(
				lt_count,
				rpl::single(messages * 1.)),
			.labelStyle = styles.label,
			.title = tr::lng_payment_confirm_title(),
		});
		if (singlePeer) {
			const auto skip = st::defaultCheckbox.margin.top();
			*trust = box->addRow(
				object_ptr<Ui::Checkbox>(
					box,
					tr::lng_payment_confirm_dont_ask(tr::now),
					false,
					(styles.checkbox
						? *styles.checkbox
						: st::defaultCheckbox)),
				st::boxRowPadding + QMargins(0, skip, 0, skip));
		}
	}));
}

bool SendPaymentHelper::check(
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer,
		Api::SendOptions options,
		int messagesCount,
		Fn<void(int)> resend,
		PaidConfirmStyles styles) {
	return check(
		navigation->uiShow(),
		peer,
		options,
		messagesCount,
		std::move(resend),
		styles);
}

bool SendPaymentHelper::check(
		std::shared_ptr<Main::SessionShow> show,
		not_null<PeerData*> peer,
		Api::SendOptions options,
		int messagesCount,
		Fn<void(int)> resend,
		PaidConfirmStyles styles) {
	clear();

	const auto admin = peer->amMonoforumAdmin();
	const auto suggest = options.suggest;
	const auto starsApproved = options.starsApproved;
	const auto checkSuggestPriceStars = (admin || suggest.ton)
		? 0
		: int(base::SafeRound(suggest.price().value()));
	const auto checkSuggestPriceTon = (!admin && suggest.ton)
		? suggest.price()
		: CreditsAmount();
	const auto details = ComputePaymentDetails(peer, messagesCount);
	const auto suggestDetails = SuggestPaymentDataReady(peer, suggest);
	if (!details || !suggestDetails) {
		_resend = [=] { resend(starsApproved); };

		if ((!details || !suggest.ton)
			&& !peer->session().credits().loaded()) {
			peer->session().credits().loadedValue(
			) | rpl::filter(
				rpl::mappers::_1
			) | rpl::take(1) | rpl::start_with_next([=] {
				if (const auto callback = base::take(_resend)) {
					callback();
				}
			}, _lifetime);
		}

		if ((!suggestDetails && suggest.ton)
			&& !peer->session().credits().tonLoaded()) {
			peer->session().credits().tonLoadedValue(
			) | rpl::filter(
				rpl::mappers::_1
			) | rpl::take(1) | rpl::start_with_next([=] {
				if (const auto callback = base::take(_resend)) {
					callback();
				}
			}, _lifetime);
		}

		peer->session().changes().peerUpdates(
			peer,
			Data::PeerUpdate::Flag::FullInfo
		) | rpl::start_with_next([=] {
			if (const auto callback = base::take(_resend)) {
				callback();
			}
		}, _lifetime);

		return false;
	} else if (const auto stars = details->stars; stars > starsApproved) {
		ShowSendPaidConfirm(show, peer, *details, [=] {
			resend(stars);
		}, styles, checkSuggestPriceStars);
		return false;
	} else if (checkSuggestPriceStars
		&& (CreditsAmount(details->stars + checkSuggestPriceStars)
			> peer->session().credits().balance())) {
		using namespace Settings;
		const auto broadcast = peer->monoforumBroadcast();
		const auto broadcastId = (broadcast ? broadcast : peer)->id;
		const auto forMessages = details->stars;
		const auto required = forMessages + checkSuggestPriceStars;
		const auto done = [=](SmallBalanceResult result) {
			if (result == SmallBalanceResult::Success
				|| result == SmallBalanceResult::Already) {
				resend(forMessages);
			}
		};
		MaybeRequestBalanceIncrease(
			show,
			required,
			SmallBalanceForSuggest{ broadcastId },
			done);
		return false;
	}
	if (checkSuggestPriceTon
		&& checkSuggestPriceTon > peer->session().credits().tonBalance()) {
		using namespace HistoryView;
		show->show(Box(InsufficientTonBox, peer, checkSuggestPriceTon));
		return false;
	}
	return true;
}

void SendPaymentHelper::clear() {
	_lifetime.destroy();
	_resend = nullptr;
}

void RequestDependentMessageItem(
		not_null<HistoryItem*> item,
		PeerId peerId,
		MsgId msgId) {
	if (!IsServerMsgId(msgId)) {
		return;
	}
	const auto fullId = item->fullId();
	const auto history = item->history();
	const auto session = &history->session();
	const auto done = [=] {
		if (const auto item = session->data().message(fullId)) {
			item->updateDependencyItem();
		}
	};
	history->session().api().requestMessageData(
		(peerId ? history->owner().peer(peerId) : history->peer),
		msgId,
		done);
}

void RequestDependentMessageStory(
		not_null<HistoryItem*> item,
		PeerId peerId,
		StoryId storyId) {
	const auto fullId = item->fullId();
	const auto history = item->history();
	const auto session = &history->session();
	const auto done = [=] {
		if (const auto item = session->data().message(fullId)) {
			item->updateDependencyItem();
		}
	};
	history->owner().stories().resolve(
		{ peerId ? peerId : history->peer->id, storyId },
		done);
}

MessageFlags NewMessageFlags(not_null<PeerData*> peer) {
	return MessageFlag::BeingSent
		| (peer->isSelf() ? MessageFlag() : MessageFlag::Outgoing);
}

TimeId NewMessageDate(TimeId scheduled) {
	return scheduled ? scheduled : base::unixtime::now();
}

TimeId NewMessageDate(const Api::SendOptions &options) {
	return options.shortcutId ? 1 : NewMessageDate(options.scheduled);
}

PeerId NewMessageFromId(const Api::SendAction &action) {
	return action.options.sendAs
		? action.options.sendAs->id
		: action.history->peer->amMonoforumAdmin()
		? action.history->peer->monoforumBroadcast()->id
		: action.history->peer->amAnonymous()
		? PeerId()
		: action.history->session().userPeerId();
}

QString NewMessagePostAuthor(const Api::SendAction &action) {
	return !action.history->peer->isBroadcast()
		? QString()
		: (action.options.sendAs == action.history->peer)
		? QString()
		: action.options.sendAs
		? action.options.sendAs->name()
		: action.history->session().user()->name();
}

bool ShouldSendSilent(
		not_null<PeerData*> peer,
		const Api::SendOptions &options) {
	return options.silent
		|| (peer->isBroadcast()
			&& peer->owner().notifySettings().silentPosts(peer))
		|| (peer->session().supportMode()
			&& peer->session().settings().supportAllSilent());
}

HistoryItem *LookupReplyTo(not_null<History*> history, FullMsgId replyTo) {
	return history->owner().message(replyTo);
}

MsgId LookupReplyToTop(not_null<History*> history, HistoryItem *replyTo) {
	return (replyTo && replyTo->history() == history)
		? replyTo->replyToTop()
		: 0;
}

MsgId LookupReplyToTop(not_null<History*> history, FullReplyTo replyTo) {
	return replyTo.topicRootId
		? replyTo.topicRootId
		: LookupReplyToTop(
			history,
			LookupReplyTo(history, replyTo.messageId));
}

bool LookupReplyIsTopicPost(HistoryItem *replyTo) {
	return replyTo
		&& (replyTo->topicRootId() != Data::ForumTopic::kGeneralId);
}

TextWithEntities DropDisallowedCustomEmoji(
		not_null<PeerData*> to,
		TextWithEntities text) {
	if (to->session().premium() || to->isSelf()) {
		return text;
	}
	const auto channel = to->asMegagroup();
	const auto allowSetId = channel ? channel->mgInfo->emojiSet.id : 0;
	if (!allowSetId) {
		text.entities.erase(
			ranges::remove(
				text.entities,
				EntityType::CustomEmoji,
				&EntityInText::type),
			text.entities.end());
	} else {
		const auto predicate = [&](const EntityInText &entity) {
			if (entity.type() != EntityType::CustomEmoji) {
				return false;
			}
			if (const auto id = Data::ParseCustomEmojiData(entity.data())) {
				const auto document = to->owner().document(id);
				if (const auto sticker = document->sticker()) {
					if (sticker->set.id == allowSetId) {
						return false;
					}
				}
			}
			return true;
		};
		text.entities.erase(
			ranges::remove_if(text.entities, predicate),
			text.entities.end());
	}
	return text;
}

Main::Session *SessionByUniqueId(uint64 sessionUniqueId) {
	if (!sessionUniqueId) {
		return nullptr;
	}
	for (const auto &[index, account] : Core::App().domain().accounts()) {
		if (const auto session = account->maybeSession()) {
			if (session->uniqueId() == sessionUniqueId) {
				return session;
			}
		}
	}
	return nullptr;
}

HistoryItem *MessageByGlobalId(GlobalMsgId globalId) {
	const auto sessionId = globalId.itemId ? globalId.sessionUniqueId : 0;
	if (const auto session = SessionByUniqueId(sessionId)) {
		return session->data().message(globalId.itemId);
	}
	return nullptr;
}

QDateTime ItemDateTime(not_null<const HistoryItem*> item) {
	return base::unixtime::parse(item->date());
}

QString ItemDateText(not_null<const HistoryItem*> item, bool isUntilOnline) {
	const auto dateText = langDayOfMonthFull(ItemDateTime(item).date());
	return !item->isScheduled()
		? dateText
		: isUntilOnline
			? tr::lng_scheduled_date_until_online(tr::now)
			: tr::lng_scheduled_date(tr::now, lt_date, dateText);
}

bool IsItemScheduledUntilOnline(not_null<const HistoryItem*> item) {
	return item->isScheduled()
		&& (item->date() == Api::kScheduledUntilOnlineTimestamp);
}

ClickHandlerPtr JumpToMessageClickHandler(
		not_null<HistoryItem*> item,
		FullMsgId returnToId,
		MessageHighlightId highlight) {
	return JumpToMessageClickHandler(
		item->history()->peer,
		item->id,
		returnToId,
		std::move(highlight));
}

ClickHandlerPtr JumpToMessageClickHandler(
		not_null<PeerData*> peer,
		MsgId msgId,
		FullMsgId returnToId,
		MessageHighlightId highlight) {
	return std::make_shared<LambdaClickHandler>([=] {
		const auto separate = Core::App().separateWindowFor(peer);
		const auto controller = separate
			? separate->sessionController()
			: peer->session().tryResolveWindow(peer);
		if (controller) {
			auto params = Window::SectionShow{
				Window::SectionShow::Way::Forward
			};
			params.highlight = highlight;
			params.origin = Window::SectionShow::OriginMessage{
				returnToId
			};
			if (const auto item = peer->owner().message(peer, msgId)) {
				controller->showMessage(item, params);
			} else {
				controller->showPeerHistory(peer, params, msgId);
			}
		}
	});
}

ClickHandlerPtr JumpToStoryClickHandler(not_null<Data::Story*> story) {
	return JumpToStoryClickHandler(story->peer(), story->id());
}

ClickHandlerPtr JumpToStoryClickHandler(
		not_null<PeerData*> peer,
		StoryId storyId) {
	return std::make_shared<LambdaClickHandler>([=] {
		const auto separate = Core::App().separateWindowFor(peer);
		const auto controller = separate
			? separate->sessionController()
			: peer->session().tryResolveWindow();
		if (controller) {
			controller->openPeerStory(
				peer,
				storyId,
				{ Data::StoriesContextSingle() });
		}
	});
}

ClickHandlerPtr HideSponsoredClickHandler() {
	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		if (const auto controller = my.sessionWindow.get()) {
			const auto &session = controller->session();
			if (session.premium()) {
				using Result = Data::SponsoredReportResult;
				session.sponsoredMessages().createReportCallback(
					my.itemId
				).callback(Result::Id("-1"), [](const auto &) {});
			} else {
				ShowPremiumPreviewBox(controller, PremiumFeature::NoAds);
			}
		}
	});
}

ClickHandlerPtr ReportSponsoredClickHandler(not_null<HistoryItem*> item) {
	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		if (const auto controller = my.sessionWindow.get()) {
			Menu::ShowSponsored(
				controller->widget(),
				controller->uiShow(),
				item->fullId());
		}
	});
}

ClickHandlerPtr AboutSponsoredClickHandler() {
	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		if (const auto controller = my.sessionWindow.get()) {
			Menu::ShowSponsoredAbout(controller->uiShow(), my.itemId);
		}
	});
}


MessageFlags FlagsFromMTP(
		MsgId id,
		MTPDmessage::Flags flags,
		MessageFlags localFlags) {
	using Flag = MessageFlag;
	using MTP = MTPDmessage::Flag;
	return localFlags
		| (IsServerMsgId(id) ? Flag::HistoryEntry : Flag())
		| ((flags & MTP::f_out) ? Flag::Outgoing : Flag())
		| ((flags & MTP::f_mentioned) ? Flag::MentionsMe : Flag())
		| ((flags & MTP::f_media_unread) ? Flag::MediaIsUnread : Flag())
		| ((flags & MTP::f_silent) ? Flag::Silent : Flag())
		| ((flags & MTP::f_post) ? Flag::Post : Flag())
		| ((flags & MTP::f_legacy) ? Flag::Legacy : Flag())
		| ((flags & MTP::f_edit_hide) ? Flag::HideEdited : Flag())
		| ((flags & MTP::f_pinned) ? Flag::Pinned : Flag())
		| ((flags & MTP::f_from_id) ? Flag::HasFromId : Flag())
		| ((flags & MTP::f_reply_to) ? Flag::HasReplyInfo : Flag())
		| ((flags & MTP::f_reply_markup) ? Flag::HasReplyMarkup : Flag())
		| ((flags & MTP::f_quick_reply_shortcut_id)
			? Flag::ShortcutMessage
			: Flag())
		| ((flags & MTP::f_from_scheduled)
			? Flag::IsOrWasScheduled
			: Flag())
		| ((flags & MTP::f_views) ? Flag::HasViews : Flag())
		| ((flags & MTP::f_noforwards) ? Flag::NoForwards : Flag())
		| ((flags & MTP::f_invert_media) ? Flag::InvertMedia : Flag())
		| ((flags & MTP::f_video_processing_pending)
			? Flag::EstimatedDate
			: Flag())
		| ((flags & MTP::f_paid_suggested_post_ton)
			? Flag::TonPaidSuggested
			: (flags & MTP::f_paid_suggested_post_stars)
			? Flag::StarsPaidSuggested
			: Flag());
}

MessageFlags FlagsFromMTP(
		MsgId id,
		MTPDmessageService::Flags flags,
		MessageFlags localFlags) {
	using Flag = MessageFlag;
	using MTP = MTPDmessageService::Flag;
	return localFlags
		| (IsServerMsgId(id) ? Flag::HistoryEntry : Flag())
		| ((flags & MTP::f_out) ? Flag::Outgoing : Flag())
		| ((flags & MTP::f_mentioned) ? Flag::MentionsMe : Flag())
		| ((flags & MTP::f_media_unread) ? Flag::MediaIsUnread : Flag())
		| ((flags & MTP::f_silent) ? Flag::Silent : Flag())
		| ((flags & MTP::f_post) ? Flag::Post : Flag())
		| ((flags & MTP::f_legacy) ? Flag::Legacy : Flag())
		| ((flags & MTP::f_from_id) ? Flag::HasFromId : Flag())
		| ((flags & MTP::f_reply_to) ? Flag::HasReplyInfo : Flag())
		| ((flags & MTP::f_reactions_are_possible)
			? Flag::ReactionsAllowed
			: Flag());
}

MTPMessageReplyHeader NewMessageReplyHeader(const Api::SendAction &action) {
	if (const auto replyTo = action.replyTo) {
		if (replyTo.storyId) {
			return MTP_messageReplyStoryHeader(
				peerToMTP(replyTo.storyId.peer),
				MTP_int(replyTo.storyId.story));
		}
		using Flag = MTPDmessageReplyHeader::Flag;
		const auto historyPeer = action.history->peer->id;
		const auto externalPeerId = (replyTo.messageId.peer == historyPeer)
			? PeerId()
			: replyTo.messageId.peer;
		const auto replyToTop = LookupReplyToTop(action.history, replyTo);
		auto quoteEntities = Api::EntitiesToMTP(
			&action.history->session(),
			replyTo.quote.entities,
			Api::ConvertOption::SkipLocal);
		return MTP_messageReplyHeader(
			MTP_flags(Flag::f_reply_to_msg_id
				| (replyToTop ? Flag::f_reply_to_top_id : Flag())
				| (externalPeerId ? Flag::f_reply_to_peer_id : Flag())
				| (replyTo.quote.empty()
					? Flag()
					: (Flag::f_quote
						| Flag::f_quote_text
						| Flag::f_quote_offset))
				| (quoteEntities.v.empty()
					? Flag()
					: Flag::f_quote_entities)
				| (replyTo.todoItemId ? Flag::f_todo_item_id : Flag())),
			MTP_int(replyTo.messageId.msg),
			peerToMTP(externalPeerId),
			MTPMessageFwdHeader(), // reply_from
			MTPMessageMedia(), // reply_media
			MTP_int(replyToTop),
			MTP_string(replyTo.quote.text),
			quoteEntities,
			MTP_int(replyTo.quoteOffset),
			MTP_int(replyTo.todoItemId));
	}
	return MTPMessageReplyHeader();
}

MediaCheckResult CheckMessageMedia(const MTPMessageMedia &media) {
	using Result = MediaCheckResult;
	return media.match([](const MTPDmessageMediaEmpty &) {
		return Result::Good;
	}, [](const MTPDmessageMediaContact &) {
		return Result::Good;
	}, [](const MTPDmessageMediaGeo &data) {
		return data.vgeo().match([](const MTPDgeoPoint &) {
			return Result::Good;
		}, [](const MTPDgeoPointEmpty &) {
			return Result::Empty;
		});
	}, [](const MTPDmessageMediaVenue &data) {
		return data.vgeo().match([](const MTPDgeoPoint &) {
			return Result::Good;
		}, [](const MTPDgeoPointEmpty &) {
			return Result::Empty;
		});
	}, [](const MTPDmessageMediaGeoLive &data) {
		return data.vgeo().match([](const MTPDgeoPoint &) {
			return Result::Good;
		}, [](const MTPDgeoPointEmpty &) {
			return Result::Empty;
		});
	}, [](const MTPDmessageMediaPhoto &data) {
		const auto photo = data.vphoto();
		if (data.vttl_seconds()) {
			return Result::HasUnsupportedTimeToLive;
		} else if (!photo) {
			return Result::Empty;
		}
		return photo->match([](const MTPDphoto &) {
			return Result::Good;
		}, [](const MTPDphotoEmpty &) {
			return Result::Empty;
		});
	}, [](const MTPDmessageMediaDocument &data) {
		const auto document = data.vdocument();
		if (data.vttl_seconds()) {
			if (data.is_video()) {
				return Result::HasUnsupportedTimeToLive;
			} else if (!document) {
				return Result::HasExpiredMediaTimeToLive;
			}
		} else if (!document) {
			return Result::Empty;
		}
		return document->match([](const MTPDdocument &) {
			return Result::Good;
		}, [](const MTPDdocumentEmpty &) {
			return Result::Empty;
		});
	}, [](const MTPDmessageMediaWebPage &data) {
		return data.vwebpage().match([](const MTPDwebPage &) {
			return Result::Good;
		}, [](const MTPDwebPageEmpty &) {
			return Result::Good;
		}, [](const MTPDwebPagePending &) {
			return Result::Good;
		}, [](const MTPDwebPageNotModified &) {
			return Result::Unsupported;
		});
	}, [](const MTPDmessageMediaGame &data) {
		return data.vgame().match([](const MTPDgame &) {
			return Result::Good;
		});
	}, [](const MTPDmessageMediaInvoice &) {
		return Result::Good;
	}, [](const MTPDmessageMediaPoll &) {
		return Result::Good;
	}, [](const MTPDmessageMediaToDo &) {
		return Result::Good;
	}, [](const MTPDmessageMediaDice &) {
		return Result::Good;
	}, [](const MTPDmessageMediaStory &data) {
		return data.is_via_mention()
			? Result::HasStoryMention
			: Result::Good;
	}, [](const MTPDmessageMediaGiveaway &) {
		return Result::Good;
	}, [](const MTPDmessageMediaGiveawayResults &) {
		return Result::Good;
	}, [](const MTPDmessageMediaPaidMedia &) {
		return Result::Good;
	}, [](const MTPDmessageMediaUnsupported &) {
		return Result::Unsupported;
	});
}

[[nodiscard]] CallId CallIdFromInput(const MTPInputGroupCall &data) {
	return data.match([&](const MTPDinputGroupCall &data) {
		return data.vid().v;
	}, [](const auto &) -> CallId {
		Unexpected("slug/msg in CallIdFromInput.");
	});
}

std::vector<not_null<UserData*>> ParseInvitedToCallUsers(
		not_null<HistoryItem*> item,
		const QVector<MTPlong> &users) {
	auto &owner = item->history()->owner();
	return ranges::views::all(
		users
	) | ranges::views::transform([&](const MTPlong &id) {
		return owner.user(id.v);
	}) | ranges::to_vector;
}

PreparedServiceText GenerateJoinedText(
		not_null<History*> history,
		not_null<UserData*> inviter,
		bool viaRequest) {
	if (inviter->id != history->session().userPeerId()) {
		auto result = PreparedServiceText();
		result.links.push_back(inviter->createOpenLink());
		result.text = (history->peer->isMegagroup()
			? tr::lng_action_add_you_group
			: tr::lng_action_add_you)(
				tr::now,
				lt_from,
				Ui::Text::Link(inviter->name(), QString()),
				Ui::Text::WithEntities);
		return result;
	} else if (history->peer->isMegagroup()) {
		if (viaRequest) {
			return { tr::lng_action_you_joined_by_request(
				tr::now,
				Ui::Text::WithEntities) };
		}
		auto self = history->session().user();
		auto result = PreparedServiceText();
		result.links.push_back(self->createOpenLink());
		result.text = tr::lng_action_user_joined(
			tr::now,
			lt_from,
			Ui::Text::Link(self->name(), QString()),
			Ui::Text::WithEntities);
		return result;
	}
	return { viaRequest
		? tr::lng_action_you_joined_by_request_channel(
			tr::now,
			Ui::Text::WithEntities)
		: tr::lng_action_you_joined(tr::now, Ui::Text::WithEntities) };
}

not_null<HistoryItem*> GenerateJoinedMessage(
		not_null<History*> history,
		TimeId inviteDate,
		not_null<UserData*> inviter,
		bool viaRequest) {
	return history->makeMessage({
		.id = history->owner().nextLocalMessageId(),
		.flags = MessageFlag::Local | MessageFlag::ShowSimilarChannels,
		.date = inviteDate,
	}, GenerateJoinedText(history, inviter, viaRequest));
}

std::optional<bool> PeerHasThisCall(
		not_null<PeerData*> peer,
		CallId id) {
	const auto call = peer->groupCall();
	return call
		? std::make_optional(call->id() == id)
		: PeerCallKnown(peer)
		? std::make_optional(false)
		: std::nullopt;
}
[[nodiscard]] rpl::producer<bool> PeerHasThisCallValue(
		not_null<PeerData*> peer,
		CallId id) {
	return peer->session().changes().peerFlagsValue(
		peer,
		Data::PeerUpdate::Flag::GroupCall
	) | rpl::filter([=] {
		return PeerCallKnown(peer);
	}) | rpl::map([=] {
		const auto call = peer->groupCall();
		return (call && call->id() == id);
	}) | rpl::distinct_until_changed(
	) | rpl::take_while([=](bool hasThisCall) {
		return hasThisCall;
	}) | rpl::then(
		rpl::single(false)
	);
}

[[nodiscard]] ClickHandlerPtr GroupCallClickHandler(
		not_null<PeerData*> peer,
		CallId callId) {
	return std::make_shared<LambdaClickHandler>([=] {
		const auto call = peer->groupCall();
		if (call && call->id() == callId) {
			const auto &windows = peer->session().windows();
			if (windows.empty()) {
				Core::App().domain().activate(&peer->session().account());
				if (windows.empty()) {
					return;
				}
			}
			windows.front()->startOrJoinGroupCall(peer, {});
		}
	});
}

[[nodiscard]] MessageFlags FinalizeMessageFlags(
		not_null<History*> history,
		MessageFlags flags) {
	if (!(flags & MessageFlag::FakeHistoryItem)
		&& !(flags & MessageFlag::IsOrWasScheduled)
		&& !(flags & MessageFlag::ShortcutMessage)
		&& !(flags & MessageFlag::AdminLogEntry)) {
		flags |= MessageFlag::HistoryEntry;
		if (history->peer->isSelf()) {
			flags |= MessageFlag::ReactionsAreTags;
		}
	}
	return flags;
}

using OnStackUsers = std::array<UserData*, kMaxUnreadReactions>;
[[nodiscard]] OnStackUsers LookupRecentUnreadReactedUsers(
		not_null<HistoryItem*> item) {
	auto result = OnStackUsers();
	auto index = 0;
	for (const auto &[emoji, reactions] : item->recentReactions()) {
		for (const auto &reaction : reactions) {
			if (!reaction.unread) {
				continue;
			}
			if (const auto user = reaction.peer->asUser()) {
				result[index++] = user;
				if (index == result.size()) {
					return result;
				}
			}
		}
	}
	return result;
}

void CheckReactionNotificationSchedule(
		not_null<HistoryItem*> item,
		const OnStackUsers &wasUsers) {
	// Call to addToUnreadThings may have read the reaction already.
	if (!item->hasUnreadReaction()) {
		return;
	}
	for (const auto &[emoji, reactions] : item->recentReactions()) {
		for (const auto &reaction : reactions) {
			if (!reaction.unread) {
				continue;
			}
			const auto user = reaction.peer->asUser();
			if (!user
				|| !user->isContact()
				|| ranges::contains(wasUsers, user)) {
				continue;
			}
			using Status = PeerData::BlockStatus;
			if (user->blockStatus() == Status::Unknown) {
				user->updateFull();
			}
			const auto notification = Data::ItemNotification{
				.item = item,
				.reactionSender = user,
				.type = Data::ItemNotificationType::Reaction,
			};
			item->notificationThread()->pushNotification(notification);
			Core::App().notifications().schedule(notification);
			return;
		}
	}
}

[[nodiscard]] MessageFlags NewForwardedFlags(
		not_null<PeerData*> peer,
		PeerId from,
		not_null<HistoryItem*> fwd) {
	auto result = NewMessageFlags(peer);
	if (from) {
		result |= MessageFlag::HasFromId;
	}
	if (const auto media = fwd->media()) {
		if ((!peer->isChannel() || peer->isMegagroup())
			&& media->forwardedBecomesUnread()) {
			result |= MessageFlag::MediaIsUnread;
		}
	}
	if (fwd->hasViews()) {
		result |= MessageFlag::HasViews;
	}
	return result;
}

[[nodiscard]] bool CopyMarkupToForward(not_null<const HistoryItem*> item) {
	auto mediaOriginal = item->media();
	if (mediaOriginal && mediaOriginal->game()) {
		// Copy inline keyboard when forwarding messages with a game.
		return true;
	}
	const auto markup = item->inlineReplyMarkup();
	if (!markup) {
		return false;
	}
	using Type = HistoryMessageMarkupButton::Type;
	for (const auto &row : markup->data.rows) {
		for (const auto &button : row) {
			const auto switchInline = (button.type == Type::SwitchInline)
				|| (button.type == Type::SwitchInlineSame);
			const auto url = (button.type == Type::Url)
				|| (button.type == Type::Auth);
			if ((!switchInline || !item->viaBot()) && !url) {
				return false;
			}
		}
	}
	return true;
}

[[nodiscard]] TextWithEntities EnsureNonEmpty(
		const TextWithEntities &text) {
	return !text.text.isEmpty() ? text : TextWithEntities{ u":-("_q };
}

[[nodiscard]] TextWithEntities UnsupportedMessageText() {
	const auto siteLink = u"https://desktop.telegram.org"_q;
	auto result = TextWithEntities{
		tr::lng_message_unsupported(tr::now, lt_link, siteLink)
	};
	TextUtilities::ParseEntities(result, Ui::ItemTextNoMonoOptions().flags);
	result.entities.push_front(
		EntityInText(EntityType::Italic, 0, result.text.size()));
	return result;
}

void ShowTrialTranscribesToast(int left, TimeId until) {
	const auto window = Core::App().activeWindow();
	if (!window) {
		return;
	}
	const auto filter = [=](const auto &...) {
		if (const auto controller = window->sessionController()) {
			ShowPremiumPreviewBox(controller, PremiumFeature::VoiceToText);
			window->activate();
		}
		return false;
	};
	const auto date = langDateTime(base::unixtime::parse(until));
	constexpr auto kToastDuration = crl::time(4000);
	const auto text = left
		? tr::lng_audio_transcribe_trials_left(
			tr::now,
			lt_count,
			left,
			lt_date,
			{ date },
			Ui::Text::WithEntities)
		: tr::lng_audio_transcribe_trials_over(
			tr::now,
			lt_date,
			Ui::Text::Bold(date),
			lt_link,
			Ui::Text::Link(tr::lng_settings_privacy_premium_link(tr::now)),
			Ui::Text::WithEntities);
	window->uiShow()->showToast(Ui::Toast::Config{
		.text = text,
		.filter = filter,
		.duration = kToastDuration,
	});
}

int ItemsForwardSendersCount(const HistoryItemsList &list) {
	auto peers = base::flat_set<not_null<PeerData*>>();
	auto names = base::flat_set<QString>();
	for (const auto &item : list) {
		if (const auto peer = item->originalSender()) {
			peers.emplace(peer);
		} else {
			names.emplace(item->originalHiddenSenderInfo()->name);
		}
	}
	return int(peers.size()) + int(names.size());
}

int ItemsForwardCaptionsCount(const HistoryItemsList &list) {
	auto result = 0;
	for (const auto &item : list) {
		if (const auto media = item->media()) {
			if (!item->originalText().text.isEmpty()
				&& media->allowsEditCaption()) {
				++result;
			}
		}
	}
	return result;
}
