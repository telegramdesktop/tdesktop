/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_forum_topic.h"

#include "data/data_channel.h"
#include "data/data_changes.h"
#include "data/data_forum.h"
#include "data/data_histories.h"
#include "data/data_replies_list.h"
#include "data/data_send_action.h"
#include "data/notify/data_notify_settings.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/ui/dialogs_layout.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "apiwrap.h"
#include "api/api_unread_things.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_unread_things.h"
#include "history/view/history_view_item_preview.h"
#include "main/main_session.h"
#include "base/unixtime.h"
#include "ui/painter.h"
#include "ui/color_int_conversion.h"
#include "styles/style_dialogs.h"
#include "styles/style_chat_helpers.h"

#include <QtSvg/QSvgRenderer>

namespace Data {
namespace {

using UpdateFlag = TopicUpdate::Flag;

} // namespace

const base::flat_map<int32, QString> &ForumTopicIcons() {
	static const auto Result = base::flat_map<int32, QString>{
		{ 0x6FB9F0, u"blue"_q },
		{ 0xFFD67E, u"yellow"_q },
		{ 0xCB86DB, u"violet"_q },
		{ 0x8EEE98, u"green"_q },
		{ 0xFF93B2, u"rose"_q },
		{ 0xFB6F5F, u"red"_q },
	};
	return Result;
}

const std::vector<int32> &ForumTopicColorIds() {
	static const auto Result = ForumTopicIcons(
	) | ranges::views::transform([](const auto &pair) {
		return pair.first;
	}) | ranges::to_vector;
	return Result;
}

const QString &ForumTopicDefaultIcon() {
	static const auto Result = u"gray"_q;
	return Result;
}

const QString &ForumTopicIcon(int32 colorId) {
	const auto &icons = ForumTopicIcons();
	const auto i = icons.find(colorId);
	return (i != end(icons)) ? i->second : ForumTopicDefaultIcon();
}

QString ForumTopicIconPath(const QString &name) {
	return u":/gui/topic_icons/%1.svg"_q.arg(name);
}

QImage ForumTopicIconBackground(int32 colorId, int size) {
	const auto ratio = style::DevicePixelRatio();
	auto svg = QSvgRenderer(ForumTopicIconPath(ForumTopicIcon(colorId)));
	auto result = QImage(
		QSize(size, size) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);

	auto p = QPainter(&result);
	svg.render(&p, QRect(0, 0, size, size));
	p.end();

	return result;
}

QString ExtractNonEmojiLetter(const QString &title) {
	const auto begin = title.data();
	const auto end = begin + title.size();
	for (auto ch = begin; ch != end;) {
		auto length = 0;
		if (Ui::Emoji::Find(ch, end, &length)) {
			ch += length;
			continue;
		}
		uint ucs4 = ch->unicode();
		length = 1;
		if (QChar::isHighSurrogate(ucs4) && ch + 1 != end) {
			ushort low = ch[1].unicode();
			if (QChar::isLowSurrogate(low)) {
				ucs4 = QChar::surrogateToUcs4(ucs4, low);
				length = 2;
			}
		}
		if (!QChar::isLetterOrNumber(ucs4)) {
			ch += length;
			continue;
		}
		return QString(ch, length);
	}
	return QString();
}

QImage ForumTopicIconFrame(
		int32 colorId,
		const QString &title,
		const style::ForumTopicIcon &st) {
	auto background = ForumTopicIconBackground(colorId, st.size);

	if (const auto one = ExtractNonEmojiLetter(title); !one.isEmpty()) {
		auto p = QPainter(&background);
		p.setPen(Qt::white);
		p.setFont(st.font);
		p.drawText(
			QRect(0, st.textTop, st.size, st.font->height * 2),
			one,
			style::al_top);
	}

	return background;
}

ForumTopic::ForumTopic(not_null<Forum*> forum, MsgId rootId)
: Thread(&forum->history()->owner(), Type::ForumTopic)
, _forum(forum)
, _list(_forum->topicsList())
, _replies(std::make_shared<RepliesList>(history(), rootId))
, _sendActionPainter(owner().sendActionManager().repliesPainter(
	history(),
	rootId))
, _rootId(rootId)
, _lastKnownServerMessageId(rootId)
, _creatorId(creating() ? forum->session().userPeerId() : 0)
, _creationDate(creating() ? base::unixtime::now() : 0)
, _flags(creating() ? Flag::My : Flag()) {
	Thread::setMuted(owner().notifySettings().isMuted(this));

	_sendActionPainter->setTopic(this);

	_replies->unreadCountValue(
	) | rpl::map([=](std::optional<int> value) {
		return value ? _replies->displayedUnreadCount() : value;
	}) | rpl::distinct_until_changed(
	) | rpl::combine_previous(
	) | rpl::filter([=] {
		return inChatList();
	}) | rpl::start_with_next([=](
			std::optional<int> previous,
			std::optional<int> now) {
		notifyUnreadStateChange(unreadStateFor(
			previous.value_or(0),
			previous.has_value()));
	}, _replies->lifetime());
}

ForumTopic::~ForumTopic() {
	_sendActionPainter->setTopic(nullptr);
	session().api().unreadThings().cancelRequests(this);
}

std::shared_ptr<Data::RepliesList> ForumTopic::replies() const {
	return _replies;
}

not_null<ChannelData*> ForumTopic::channel() const {
	return _forum->channel();
}

not_null<History*> ForumTopic::history() const {
	return _forum->history();
}

not_null<Forum*> ForumTopic::forum() const {
	return _forum;
}

rpl::producer<> ForumTopic::destroyed() const {
	using namespace rpl::mappers;
	return rpl::merge(
		_forum->destroyed(),
		_forum->topicDestroyed() | rpl::filter(_1 == this) | rpl::to_empty);
}

MsgId ForumTopic::rootId() const {
	return _rootId;
}

PeerId ForumTopic::creatorId() const {
	return _creatorId;
}

TimeId ForumTopic::creationDate() const {
	return _creationDate;
}

bool ForumTopic::my() const {
	return (_flags & Flag::My);
}

bool ForumTopic::canWrite() const {
	const auto channel = this->channel();
	return channel->amIn()
		&& !channel->amRestricted(ChatRestriction::SendMessages)
		&& (!closed() || canToggleClosed());
}

bool ForumTopic::canSendPolls() const {
	return canWrite()
		&& !channel()->amRestricted(ChatRestriction::SendPolls);
}

bool ForumTopic::canEdit() const {
	return my() || channel()->canManageTopics();
}

bool ForumTopic::canDelete() const {
	if (creating()) {
		return false;
	} else if (channel()->canDeleteMessages()) {
		return true;
	}
	return my() && replies()->canDeleteMyTopic();
}

bool ForumTopic::canToggleClosed() const {
	return !creating() && canEdit();
}

bool ForumTopic::canTogglePinned() const {
	return !creating() && channel()->canManageTopics();
}

bool ForumTopic::creating() const {
	return _forum->creating(_rootId);
}

void ForumTopic::discard() {
	Expects(creating());

	_forum->discardCreatingId(_rootId);
}

void ForumTopic::setRealRootId(MsgId realId) {
	if (_rootId != realId) {
		_rootId = realId;
		_lastKnownServerMessageId = realId;
		_replies = std::make_shared<RepliesList>(history(), _rootId);
		_sendActionPainter = owner().sendActionManager().repliesPainter(
			history(),
			_rootId);
	}
}

void ForumTopic::readTillEnd() {
	_replies->readTill(_lastKnownServerMessageId);
}

void ForumTopic::applyTopic(const MTPDforumTopic &data) {
	Expects(_rootId == data.vid().v);

	_creatorId = peerFromMTP(data.vfrom_id());
	_creationDate = data.vdate().v;

	applyTitle(qs(data.vtitle()));
	if (const auto iconId = data.vicon_emoji_id()) {
		applyIconId(iconId->v);
	} else {
		applyIconId(0);
	}
	applyColorId(data.vicon_color().v);

	if (data.is_pinned()) {
		owner().setChatPinned(this, 0, true);
	} else {
		_list->pinned()->setPinned(this, false);
	}

	owner().notifySettings().apply(this, data.vnotify_settings());

	const auto draft = data.vdraft();
	if (draft && draft->type() == mtpc_draftMessage) {
		Data::ApplyPeerCloudDraft(
			&session(),
			channel()->id,
			_rootId,
			draft->c_draftMessage());
	}
	if (data.is_my()) {
		_flags |= Flag::My;
	} else {
		_flags &= ~Flag::My;
	}
	setClosed(data.is_closed());

	_replies->setInboxReadTill(
		data.vread_inbox_max_id().v,
		data.vunread_count().v);
	_replies->setOutboxReadTill(data.vread_outbox_max_id().v);
	applyTopicTopMessage(data.vtop_message().v);
	unreadMentions().setCount(data.vunread_mentions_count().v);
	unreadReactions().setCount(data.vunread_reactions_count().v);
}

bool ForumTopic::closed() const {
	return _flags & Flag::Closed;
}

void ForumTopic::setClosed(bool closed) {
	if (this->closed() == closed) {
		return;
	} else if (closed) {
		_flags |= Flag::Closed;
	} else {
		_flags &= ~Flag::Closed;
	}
	session().changes().topicUpdated(this, UpdateFlag::Closed);
}

void ForumTopic::setClosedAndSave(bool closed) {
	setClosed(closed);

	const auto api = &session().api();
	const auto weak = base::make_weak(this);
	api->request(MTPchannels_EditForumTopic(
		MTP_flags(MTPchannels_EditForumTopic::Flag::f_closed),
		channel()->inputChannel,
		MTP_int(_rootId),
		MTPstring(), // title
		MTPlong(), // icon_emoji_id
		MTP_bool(closed)
	)).done([=](const MTPUpdates &result) {
		api->applyUpdates(result);
	}).fail([=](const MTP::Error &error) {
		if (error.type() != u"TOPIC_NOT_MODIFIED") {
			if (const auto topic = weak.get()) {
				topic->forum()->requestTopic(topic->rootId());
			}
		}
	}).send();
}

void ForumTopic::indexTitleParts() {
	_titleWords.clear();
	_titleFirstLetters.clear();
	auto toIndexList = QStringList();
	auto appendToIndex = [&](const QString &value) {
		if (!value.isEmpty()) {
			toIndexList.push_back(TextUtilities::RemoveAccents(value));
		}
	};

	appendToIndex(_title);
	const auto appendTranslit = !toIndexList.isEmpty()
		&& cRussianLetters().match(toIndexList.front()).hasMatch();
	if (appendTranslit) {
		appendToIndex(translitRusEng(toIndexList.front()));
	}
	auto toIndex = toIndexList.join(' ');
	toIndex += ' ' + rusKeyboardLayoutSwitch(toIndex);

	const auto namesList = TextUtilities::PrepareSearchWords(toIndex);
	for (const auto &name : namesList) {
		_titleWords.insert(name);
		_titleFirstLetters.insert(name[0]);
	}
}

int ForumTopic::chatListNameVersion() const {
	return _titleVersion;
}

void ForumTopic::applyTopicTopMessage(MsgId topMessageId) {
	if (topMessageId) {
		growLastKnownServerMessageId(topMessageId);
		const auto itemId = FullMsgId(channel()->id, topMessageId);
		if (const auto item = owner().message(itemId)) {
			setLastServerMessage(item);

			// If we set a single album part, request the full album.
			if (item->groupId() != MessageGroupId()) {
				if (owner().groups().isGroupOfOne(item)
					&& !item->toPreview({
						.hideSender = true,
						.hideCaption = true }).images.empty()
					&& _requestedGroups.emplace(item->fullId()).second) {
					owner().histories().requestGroupAround(item);
				}
			}
		} else {
			setLastServerMessage(nullptr);
		}
	} else {
		setLastServerMessage(nullptr);
	}
}

void ForumTopic::growLastKnownServerMessageId(MsgId id) {
	_lastKnownServerMessageId = std::max(_lastKnownServerMessageId, id);
}

void ForumTopic::setLastServerMessage(HistoryItem *item) {
	if (item) {
		growLastKnownServerMessageId(item->id);
	}
	_lastServerMessage = item;
	if (_lastMessage
		&& *_lastMessage
		&& !(*_lastMessage)->isRegular()
		&& (!item
			|| (*_lastMessage)->date() > item->date()
			|| (*_lastMessage)->isSending())) {
		return;
	}
	setLastMessage(item);
}

void ForumTopic::setLastMessage(HistoryItem *item) {
	if (_lastMessage && *_lastMessage == item) {
		return;
	}
	_lastMessage = item;
	if (!item || item->isRegular()) {
		_lastServerMessage = item;
		if (item) {
			growLastKnownServerMessageId(item->id);
		}
	}
	setChatListMessage(item);
}

void ForumTopic::setChatListMessage(HistoryItem *item) {
	if (_chatListMessage && *_chatListMessage == item) {
		return;
	}
	const auto was = _chatListMessage.value_or(nullptr);
	if (item) {
		if (item->isSponsored()) {
			return;
		}
		if (_chatListMessage
			&& *_chatListMessage
			&& !(*_chatListMessage)->isRegular()
			&& (*_chatListMessage)->date() > item->date()) {
			return;
		}
		_chatListMessage = item;
		setChatListTimeId(item->date());
	} else if (!_chatListMessage || *_chatListMessage) {
		_chatListMessage = nullptr;
		updateChatListEntry();
	}
}

void ForumTopic::loadUserpic() {
	if (_icon) {
		[[maybe_unused]] const auto preload = _icon->ready();
	}
}

void ForumTopic::paintUserpic(
		Painter &p,
		std::shared_ptr<Data::CloudImageView> &view,
		const Dialogs::Ui::PaintContext &context) const {
	const auto &st = context.st;
	auto position = QPoint(st->padding.left(), st->padding.top());
	if (_icon) {
		if (context.narrow) {
			const auto ratio = style::DevicePixelRatio();
			const auto tag = Data::CustomEmojiManager::SizeTag::Normal;
			const auto size = Data::FrameSizeFromTag(tag) / ratio;
			position = QPoint(
				(context.width - size) / 2,
				(st->height - size) / 2);
		}
		_icon->paint(p, {
			.preview = st::windowBgOver->c,
			.now = context.now,
			.position = position,
			.paused = context.paused,
		});
	} else {
		validateDefaultIcon();
		const auto size = st::defaultForumTopicIcon.size;
		if (context.narrow) {
			position = QPoint(
				(context.width - size) / 2,
				(st->height - size) / 2);
		} else {
			const auto esize = st::emojiSize;
			const auto shift = (esize - size) / 2;
			position += st::forumTopicIconPosition + QPoint(shift, 0);
		}
		p.drawImage(position, _defaultIcon);
	}
}

void ForumTopic::validateDefaultIcon() const {
	if (_defaultIcon.isNull()) {
		_defaultIcon = ForumTopicIconFrame(
			_colorId,
			_title,
			st::defaultForumTopicIcon);
	}
}

void ForumTopic::requestChatListMessage() {
	if (!chatListMessageKnown() && !forum()->creating(_rootId)) {
		forum()->requestTopic(_rootId);
	}
}

TimeId ForumTopic::adjustedChatListTimeId() const {
	const auto result = chatListTimeId();
	if (const auto draft = history()->cloudDraft(_rootId)) {
		if (!Data::DraftIsNull(draft) && !session().supportMode()) {
			return std::max(result, draft->date);
		}
	}
	return result;
}

int ForumTopic::fixedOnTopIndex() const {
	return 0;
}

bool ForumTopic::shouldBeInChatList() const {
	return isPinnedDialog(FilterId())
		|| !lastMessageKnown()
		|| (lastMessage() != nullptr);
}

HistoryItem *ForumTopic::lastMessage() const {
	return _lastMessage.value_or(nullptr);
}

bool ForumTopic::lastMessageKnown() const {
	return _lastMessage.has_value();
}

HistoryItem *ForumTopic::lastServerMessage() const {
	return _lastServerMessage.value_or(nullptr);
}

bool ForumTopic::lastServerMessageKnown() const {
	return _lastServerMessage.has_value();
}

MsgId ForumTopic::lastKnownServerMessageId() const {
	return _lastKnownServerMessageId;
}

QString ForumTopic::title() const {
	return _title;
}

void ForumTopic::applyTitle(const QString &title) {
	if (_title == title) {
		return;
	}
	_title = title;
	++_titleVersion;
	_defaultIcon = QImage();
	indexTitleParts();
	updateChatListEntry();
	session().changes().topicUpdated(this, UpdateFlag::Title);
}

DocumentId ForumTopic::iconId() const {
	return _iconId;
}

void ForumTopic::applyIconId(DocumentId iconId) {
	if (_iconId == iconId) {
		return;
	}
	_iconId = iconId;
	_icon = iconId
		? owner().customEmojiManager().create(
			_iconId,
			[=] { updateChatListEntry(); },
			Data::CustomEmojiManager::SizeTag::Normal)
		: nullptr;
	if (iconId) {
		_defaultIcon = QImage();
	}
	updateChatListEntry();
	session().changes().topicUpdated(this, UpdateFlag::IconId);
}

int32 ForumTopic::colorId() const {
	return _colorId;
}

void ForumTopic::applyColorId(int32 colorId) {
	if (_colorId != colorId) {
		_colorId = colorId;
		session().changes().topicUpdated(this, UpdateFlag::ColorId);
	}
}

void ForumTopic::applyItemAdded(not_null<HistoryItem*> item) {
	if (item->isRegular()) {
		setLastServerMessage(item);
	} else {
		setLastMessage(item);
	}
}

void ForumTopic::maybeSetLastMessage(not_null<HistoryItem*> item) {
	Expects(item->topicRootId() == _rootId);

	if (!_lastMessage
		|| ((*_lastMessage)->date() < item->date())
		|| ((*_lastMessage)->date() == item->date()
			&& (*_lastMessage)->id < item->id)) {
		setLastMessage(item);
	}
}

void ForumTopic::applyItemRemoved(MsgId id) {
	if (const auto lastItem = lastMessage()) {
		if (lastItem->id == id) {
			_lastMessage = std::nullopt;
		}
	}
	if (const auto lastServerItem = lastServerMessage()) {
		if (lastServerItem->id == id) {
			_lastServerMessage = std::nullopt;
		}
	}
	if (const auto chatListItem = _chatListMessage.value_or(nullptr)) {
		if (chatListItem->id == id) {
			_chatListMessage = std::nullopt;
			requestChatListMessage();
		}
	}
}

bool ForumTopic::isServerSideUnread(
		not_null<const HistoryItem*> item) const {
	return _replies->isServerSideUnread(item);
}

void ForumTopic::setMuted(bool muted) {
	if (this->muted() == muted) {
		return;
	}
	const auto state = chatListBadgesState();
	const auto notify = state.unread || state.reaction;
	const auto notifier = unreadStateChangeNotifier(notify);
	Thread::setMuted(muted);
	session().changes().topicUpdated(this, UpdateFlag::Notifications);
}

not_null<HistoryView::SendActionPainter*> ForumTopic::sendActionPainter() {
	return _sendActionPainter.get();
}

Dialogs::UnreadState ForumTopic::chatListUnreadState() const {
	return unreadStateFor(
		_replies->displayedUnreadCount(),
		_replies->unreadCountKnown());
}

Dialogs::BadgesState ForumTopic::chatListBadgesState() const {
	return Dialogs::BadgesForUnread(
		chatListUnreadState(),
		Dialogs::CountInBadge::Messages,
		Dialogs::IncludeInBadge::All);
}

Dialogs::UnreadState ForumTopic::unreadStateFor(
		int count,
		bool known) const {
	auto result = Dialogs::UnreadState();
	const auto muted = this->muted();
	result.messages = count;
	result.chats = count ? 1 : 0;
	result.mentions = unreadMentions().has() ? 1 : 0;
	result.reactions = unreadReactions().has() ? 1 : 0;
	result.messagesMuted = muted ? result.messages : 0;
	result.chatsMuted = muted ? result.chats : 0;
	result.reactionsMuted = muted ? result.reactions : 0;
	result.known = known;
	return result;
}

HistoryItem *ForumTopic::chatListMessage() const {
	return _lastMessage.value_or(nullptr);
}

bool ForumTopic::chatListMessageKnown() const {
	return _lastMessage.has_value();
}

const QString &ForumTopic::chatListName() const {
	return _title;
}

const base::flat_set<QString> &ForumTopic::chatListNameWords() const {
	return _titleWords;
}

const base::flat_set<QChar> &ForumTopic::chatListFirstLetters() const {
	return _titleFirstLetters;
}

void ForumTopic::hasUnreadMentionChanged(bool has) {
	auto was = chatListUnreadState();
	if (has) {
		was.mentions = 0;
	} else {
		was.mentions = 1;
	}
	notifyUnreadStateChange(was);
}

void ForumTopic::hasUnreadReactionChanged(bool has) {
	auto was = chatListUnreadState();
	if (has) {
		was.reactions = was.reactionsMuted = 0;
	} else {
		was.reactions = 1;
		was.reactionsMuted = muted() ? was.reactions : 0;
	}
	notifyUnreadStateChange(was);
}

const QString &ForumTopic::chatListNameSortKey() const {
	static const auto empty = QString();
	return empty;
}

} // namespace Data
