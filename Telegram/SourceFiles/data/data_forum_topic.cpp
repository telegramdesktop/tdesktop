/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_forum_topic.h"

#include "data/data_channel.h"
#include "data/data_forum.h"
#include "data/data_histories.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/ui/dialogs_layout.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_item_preview.h"
#include "ui/painter.h"
#include "ui/color_int_conversion.h"
#include "styles/style_dialogs.h"
#include "styles/style_chat_helpers.h"

#include <QtSvg/QSvgRenderer>

namespace Data {

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

ForumTopic::ForumTopic(not_null<History*> history, MsgId rootId)
: Entry(&history->owner(), Type::ForumTopic)
, _history(history)
, _list(forum()->topicsList())
, _rootId(rootId) {
}

ForumTopic::~ForumTopic() = default;

not_null<ChannelData*> ForumTopic::channel() const {
	return _history->peer->asChannel();
}

not_null<History*> ForumTopic::history() const {
	return _history;
}

not_null<Forum*> ForumTopic::forum() const {
	return channel()->forum();
}

MsgId ForumTopic::rootId() const {
	return _rootId;
}

void ForumTopic::setRealRootId(MsgId realId) {
	_rootId = realId;
}

void ForumTopic::applyTopic(const MTPForumTopic &topic) {
	Expects(_rootId == topic.data().vid().v);

	const auto &data = topic.data();
	applyTitle(qs(data.vtitle()));
	if (const auto iconId = data.vicon_emoji_id()) {
		applyIconId(iconId->v);
	} else {
		applyIconId(0);
	}
	applyColorId(data.vicon_color().v);

	const auto pinned = _list->pinned();
#if 0 // #TODO forum pinned
	if (data.is_pinned()) {
		pinned->addPinned(Dialogs::Key(this));
	} else {
		pinned->setPinned(Dialogs::Key(this), false);
	}
#endif

	applyTopicFields(
		data.vunread_count().v,
		data.vread_inbox_max_id().v,
		data.vread_outbox_max_id().v);
	applyTopicTopMessage(data.vtop_message().v);
#if 0 // #TODO forum unread mark
	setUnreadMark(data.is_unread_mark());
#endif
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

void ForumTopic::applyTopicFields(
		int unreadCount,
		MsgId maxInboxRead,
		MsgId maxOutboxRead) {
	if (maxInboxRead + 1 >= _inboxReadBefore.value_or(1)) {
		setUnreadCount(unreadCount);
		setInboxReadTill(maxInboxRead);
	}
	setOutboxReadTill(maxOutboxRead);
}

void ForumTopic::applyTopicTopMessage(MsgId topMessageId) {
	if (topMessageId) {
		const auto itemId = FullMsgId(_history->peer->id, topMessageId);
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

void ForumTopic::setLastServerMessage(HistoryItem *item) {
	_lastServerMessage = item;
	if (_lastMessage
		&& *_lastMessage
		&& !(*_lastMessage)->isRegular()
		&& (!item || (*_lastMessage)->date() > item->date())) {
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

void ForumTopic::setInboxReadTill(MsgId upTo) {
	if (_inboxReadBefore) {
		accumulate_max(*_inboxReadBefore, upTo + 1);
	} else {
		_inboxReadBefore = upTo + 1;
	}
}

void ForumTopic::setOutboxReadTill(MsgId upTo) {
	if (_outboxReadBefore) {
		accumulate_max(*_outboxReadBefore, upTo + 1);
	} else {
		_outboxReadBefore = upTo + 1;
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
	const auto position = QPoint(st->padding.left(), st->padding.top());
	if (_icon) {
		_icon->paint(p, {
			.preview = st::windowBgOver->c,
			.now = context.now,
			.position = position,
			.paused = context.paused,
		});
	} else {
		validateDefaultIcon();
		const auto size = st::defaultForumTopicIcon.size;
		const auto esize = st::emojiSize;
		const auto shift = (esize - size) / 2;
		p.drawImage(
			position + st::forumTopicIconPosition + QPoint(shift, 0),
			_defaultIcon);
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
	if (!chatListMessageKnown()) {
		// #TODO forum
	}
}

TimeId ForumTopic::adjustedChatListTimeId() const {
	if (isGeneral()) {
		return TimeId(1);
	}
	const auto result = chatListTimeId();
#if 0 // #TODO forum
	if (const auto draft = cloudDraft()) {
		if (!Data::draftIsNull(draft) && !session().supportMode()) {
			return std::max(result, draft->date);
		}
	}
#endif
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

QString ForumTopic::title() const {
	return _title;
}

void ForumTopic::applyTitle(const QString &title) {
	if (_title == title || (isGeneral() && !_title.isEmpty())) {
		return;
	}
	_title = isGeneral() ? "General! Topic." : title; // #TODO lang-forum
	++_titleVersion;
	_defaultIcon = QImage();
	indexTitleParts();
	updateChatListEntry();
}

DocumentId ForumTopic::iconId() const {
	return _iconId;
}

void ForumTopic::applyIconId(DocumentId iconId) {
	if (_iconId != iconId) {
		_iconId = iconId;
		_icon = iconId
			? _history->owner().customEmojiManager().create(
				_iconId,
				[=] { updateChatListEntry(); },
				Data::CustomEmojiManager::SizeTag::Normal)
			: nullptr;
		if (iconId) {
			_defaultIcon = QImage();
		}
	}
	updateChatListEntry();
}

int32 ForumTopic::colorId() const {
	return _colorId;
}

void ForumTopic::applyColorId(int32 colorId) {
	_colorId = colorId;
}

void ForumTopic::applyItemAdded(not_null<HistoryItem*> item) {
	setLastMessage(item);
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
		}
	}
}

int ForumTopic::unreadCount() const {
	return _unreadCount ? *_unreadCount : 0;
}

int ForumTopic::unreadCountForBadge() const {
	const auto result = unreadCount();
	return (!result && unreadMark()) ? 1 : result;
}

bool ForumTopic::unreadCountKnown() const {
	return _unreadCount.has_value();
}

void ForumTopic::setUnreadCount(int newUnreadCount) {
	if (_unreadCount == newUnreadCount) {
		return;
	}
	const auto wasForBadge = (unreadCountForBadge() > 0);
	const auto notifier = unreadStateChangeNotifier(true);
	_unreadCount = newUnreadCount;
}

void ForumTopic::setUnreadMark(bool unread) {
	if (_unreadMark == unread) {
		return;
	}
	const auto noUnreadMessages = !unreadCount();
	const auto refresher = gsl::finally([&] {
		if (inChatList() && noUnreadMessages) {
			updateChatListEntry();
		}
	});
	const auto notifier = unreadStateChangeNotifier(noUnreadMessages);
	_unreadMark = unread;
}

bool ForumTopic::unreadMark() const {
	return _unreadMark;
}

int ForumTopic::chatListUnreadCount() const {
	const auto state = chatListUnreadState();
	return state.marks
		+ (Core::App().settings().countUnreadMessages()
			? state.messages
			: state.chats);
}

Dialogs::UnreadState ForumTopic::chatListUnreadState() const {
	auto result = Dialogs::UnreadState();
	const auto count = _unreadCount.value_or(0);
	const auto mark = !count && _unreadMark;
	const auto muted = _history->mute();
	result.messages = count;
	result.messagesMuted = muted ? count : 0;
	result.chats = count ? 1 : 0;
	result.chatsMuted = (count && muted) ? 1 : 0;
	result.marks = mark ? 1 : 0;
	result.marksMuted = (mark && muted) ? 1 : 0;
	result.known = _unreadCount.has_value();
	return result;
}

bool ForumTopic::chatListUnreadMark() const {
	return false;
}

bool ForumTopic::chatListMutedBadge() const {
	return true;
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

const QString &ForumTopic::chatListNameSortKey() const {
	static const auto empty = QString();
	return empty;
}

} // namespace Data
