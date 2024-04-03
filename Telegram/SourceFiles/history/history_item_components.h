/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_cloud_file.h"
#include "history/history_item.h"
#include "spellcheck/spellcheck_types.h" // LanguageId.
#include "ui/empty_userpic.h"
#include "ui/effects/animations.h"
#include "ui/effects/ripple_animation.h"
#include "ui/chat/message_bubble.h"

struct WebPageData;
class VoiceSeekClickHandler;

namespace Ui {
struct ChatPaintContext;
class ChatStyle;
struct PeerUserpicView;
} // namespace Ui

namespace Ui::Text {
struct GeometryDescriptor;
} // namespace Ui::Text

namespace Data {
class Session;
class Story;
} // namespace Data

namespace Media::Player {
class RoundPainter;
} // namespace Media::Player

namespace Images {
struct CornersMaskRef;
} // namespace Images

namespace HistoryView {
class Element;
class Document;
class TranscribeButton;
} // namespace HistoryView

struct HistoryMessageVia : public RuntimeComponent<HistoryMessageVia, HistoryItem> {
	void create(not_null<Data::Session*> owner, UserId userId);
	void resize(int32 availw) const;

	UserData *bot = nullptr;
	mutable QString text;
	mutable int width = 0;
	mutable int maxWidth = 0;
	ClickHandlerPtr link;
};

struct HistoryMessageViews : public RuntimeComponent<HistoryMessageViews, HistoryItem> {
	static constexpr auto kMaxRecentRepliers = 3;

	struct Part {
		QString text;
		int textWidth = 0;
		int count = -1;
	};
	std::vector<PeerId> recentRepliers;
	Part views;
	Part replies;
	Part repliesSmall;
	ChannelId commentsMegagroupId = 0;
	MsgId commentsRootId = 0;
	MsgId commentsInboxReadTillId = 0;
	MsgId commentsMaxId = 0;
	int forwardsCount = 0;
};

struct HistoryMessageSigned : public RuntimeComponent<HistoryMessageSigned, HistoryItem> {
	QString author;
	UserData *viaBusinessBot = nullptr;
	bool isAnonymousRank = false;
};

struct HistoryMessageEdited : public RuntimeComponent<HistoryMessageEdited, HistoryItem> {
	TimeId date = 0;
};

class HiddenSenderInfo {
public:
	HiddenSenderInfo(
		const QString &name,
		bool external,
		std::optional<uint8> colorIndex = {});

	QString name;
	QString firstName;
	QString lastName;
	uint8 colorIndex = 0;
	Ui::EmptyUserpic emptyUserpic;
	mutable Data::CloudImage customUserpic;

	[[nodiscard]] static ClickHandlerPtr ForwardClickHandler();

	[[nodiscard]] const Ui::Text::String &nameText() const;
	[[nodiscard]] bool paintCustomUserpic(
		Painter &p,
		Ui::PeerUserpicView &view,
		int x,
		int y,
		int outerWidth,
		int size) const;

	inline bool operator==(const HiddenSenderInfo &other) const {
		return name == other.name;
	}
	inline bool operator!=(const HiddenSenderInfo &other) const {
		return !(*this == other);
	}

private:
	mutable Ui::Text::String _nameText;

};

struct HistoryMessageForwarded : public RuntimeComponent<HistoryMessageForwarded, HistoryItem> {
	void create(const HistoryMessageVia *via) const;

	[[nodiscard]] bool forwardOfForward() const {
		return savedFromSender || savedFromHiddenSenderInfo;
	}

	TimeId originalDate = 0;
	PeerData *originalSender = nullptr;
	std::unique_ptr<HiddenSenderInfo> originalHiddenSenderInfo;
	QString originalPostAuthor;
	QString psaType;
	MsgId originalId = 0;
	mutable Ui::Text::String text = { 1 };

	PeerData *savedFromPeer = nullptr;
	MsgId savedFromMsgId = 0;

	PeerData *savedFromSender = nullptr;
	std::unique_ptr<HiddenSenderInfo> savedFromHiddenSenderInfo;

	bool savedFromOutgoing = false;
	bool imported = false;
	bool story = false;
};

struct HistoryMessageSavedMediaData : public RuntimeComponent<HistoryMessageSavedMediaData, HistoryItem> {
	TextWithEntities text;
	std::unique_ptr<Data::Media> media;
};

struct HistoryMessageSaved : public RuntimeComponent<HistoryMessageSaved, HistoryItem> {
	Data::SavedSublist *sublist = nullptr;
};

class ReplyToMessagePointer final {
public:
	ReplyToMessagePointer(HistoryItem *item = nullptr) : _data(item) {
	}
	ReplyToMessagePointer(ReplyToMessagePointer &&other)
	: _data(base::take(other._data)) {
	}
	ReplyToMessagePointer &operator=(ReplyToMessagePointer &&other) {
		_data = base::take(other._data);
		return *this;
	}
	ReplyToMessagePointer &operator=(HistoryItem *item) {
		_data = item;
		return *this;
	}

	[[nodiscard]] bool empty() const {
		return !_data;
	}
	[[nodiscard]] HistoryItem *get() const {
		return _data;
	}
	explicit operator bool() const {
		return !empty();
	}

	[[nodiscard]] HistoryItem *operator->() const {
		return _data;
	}
	[[nodiscard]] HistoryItem &operator*() const {
		return *_data;
	}

private:
	HistoryItem *_data = nullptr;

};

class ReplyToStoryPointer final {
public:
	ReplyToStoryPointer(Data::Story *story = nullptr) : _data(story) {
	}
	ReplyToStoryPointer(ReplyToStoryPointer &&other)
	: _data(base::take(other._data)) {
	}
	ReplyToStoryPointer &operator=(ReplyToStoryPointer &&other) {
		_data = base::take(other._data);
		return *this;
	}
	ReplyToStoryPointer &operator=(Data::Story *item) {
		_data = item;
		return *this;
	}

	[[nodiscard]] bool empty() const {
		return !_data;
	}
	[[nodiscard]] Data::Story *get() const {
		return _data;
	}
	explicit operator bool() const {
		return !empty();
	}

	[[nodiscard]] Data::Story *operator->() const {
		return _data;
	}
	[[nodiscard]] Data::Story &operator*() const {
		return *_data;
	}

private:
	Data::Story *_data = nullptr;

};

struct ReplyFields {
	[[nodiscard]] ReplyFields clone(not_null<HistoryItem*> parent) const;

	TextWithEntities quote;
	std::unique_ptr<Data::Media> externalMedia;
	PeerId externalSenderId = 0;
	QString externalSenderName;
	QString externalPostAuthor;
	PeerId externalPeerId = 0;
	MsgId messageId = 0;
	MsgId topMessageId = 0;
	StoryId storyId = 0;
	uint32 quoteOffset : 30 = 0;
	uint32 manualQuote : 1 = 0;
	uint32 topicPost : 1 = 0;
};

[[nodiscard]] ReplyFields ReplyFieldsFromMTP(
	not_null<HistoryItem*> item,
	const MTPMessageReplyHeader &reply);

[[nodiscard]] FullReplyTo ReplyToFromMTP(
	not_null<History*> history,
	const MTPInputReplyTo &reply);

struct HistoryMessageReply
	: public RuntimeComponent<HistoryMessageReply, HistoryItem> {
	HistoryMessageReply();
	HistoryMessageReply(const HistoryMessageReply &other) = delete;
	HistoryMessageReply(HistoryMessageReply &&other) = delete;
	HistoryMessageReply &operator=(
		const HistoryMessageReply &other) = delete;
	HistoryMessageReply &operator=(HistoryMessageReply &&other);
	~HistoryMessageReply();

	void set(ReplyFields fields);

	void updateFields(
		not_null<HistoryItem*> holder,
		MsgId messageId,
		MsgId topMessageId,
		bool topicPost);
	void updateData(not_null<HistoryItem*> holder, bool force = false);

	// Must be called before destructor.
	void clearData(not_null<HistoryItem*> holder);

	[[nodiscard]] bool external() const;
	[[nodiscard]] bool displayAsExternal(
		not_null<HistoryItem*> holder) const;
	void itemRemoved(
		not_null<HistoryItem*> holder,
		not_null<HistoryItem*> removed);
	void storyRemoved(
		not_null<HistoryItem*> holder,
		not_null<Data::Story*> removed);

	[[nodiscard]] const ReplyFields &fields() const {
		return _fields;
	}
	[[nodiscard]] PeerId externalPeerId() const {
		return _fields.externalPeerId;
	}
	[[nodiscard]] MsgId messageId() const {
		return _fields.messageId;
	}
	[[nodiscard]] StoryId storyId() const {
		return _fields.storyId;
	}
	[[nodiscard]] MsgId topMessageId() const {
		return _fields.topMessageId;
	}
	[[nodiscard]] bool topicPost() const {
		return _fields.topicPost;
	}
	[[nodiscard]] bool manualQuote() const {
		return _fields.manualQuote;
	}
	[[nodiscard]] bool unavailable() const {
		return _unavailable;
	}
	[[nodiscard]] bool displaying() const {
		return _displaying;
	}
	[[nodiscard]] bool multiline() const {
		return _multiline;
	}

	[[nodiscard]] bool acquireResolve();

	void setTopMessageId(MsgId topMessageId);

	void refreshReplyToMedia();

	DocumentId replyToDocumentId = 0;
	WebPageId replyToWebPageId = 0;
	ReplyToMessagePointer resolvedMessage;
	ReplyToStoryPointer resolvedStory;

private:
	ReplyFields _fields;
	uint8 _unavailable : 1 = 0;
	uint8 _displaying : 1 = 0;
	uint8 _multiline : 1 = 0;
	uint8 _pendingResolve : 1 = 0;
	uint8 _requestedResolve : 1 = 0;

};

struct HistoryMessageTranslation
	: public RuntimeComponent<HistoryMessageTranslation, HistoryItem> {
	TextWithEntities text;
	LanguageId to;
	bool requested = false;
	bool failed = false;
	bool used = false;
};

struct HistoryMessageReplyMarkup
	: public RuntimeComponent<HistoryMessageReplyMarkup, HistoryItem> {
	using Button = HistoryMessageMarkupButton;

	void createForwarded(const HistoryMessageReplyMarkup &original);
	void updateData(HistoryMessageMarkupData &&markup);

	[[nodiscard]] bool hiddenBy(Data::Media *media) const;

	HistoryMessageMarkupData data;
	std::unique_ptr<ReplyKeyboard> inlineKeyboard;

};

class ReplyMarkupClickHandler : public ClickHandler {
public:
	ReplyMarkupClickHandler(
		not_null<Data::Session*> owner,
		int row,
		int column,
		FullMsgId context);

	QString tooltip() const override;

	void setFullDisplayed(bool full) {
		_fullDisplayed = full;
	}

	// Copy to clipboard support.
	QString copyToClipboardText() const override;
	QString copyToClipboardContextItemText() const override;

	// Finds the corresponding button in the items markup struct.
	// If the button is not found it returns nullptr.
	// Note: it is possible that we will point to the different button
	// than the one was used when constructing the handler, but not a big deal.
	const HistoryMessageMarkupButton *getButton() const;

	const HistoryMessageMarkupButton *getUrlButton() const;

	// We hold only FullMsgId, not HistoryItem*, because all click handlers
	// are activated async and the item may be already destroyed.
	void setMessageId(const FullMsgId &msgId) {
		_itemId = msgId;
	}

	void onClick(ClickContext context) const override;

private:
	const not_null<Data::Session*> _owner;
	FullMsgId _itemId;
	int _row = 0;
	int _column = 0;
	bool _fullDisplayed = true;

	// Returns the full text of the corresponding button.
	QString buttonText() const;

};

class ReplyKeyboard {
private:
	struct Button;

public:
	class Style {
	public:
		Style(const style::BotKeyboardButton &st) : _st(&st) {
		}

		virtual void startPaint(
			QPainter &p,
			const Ui::ChatStyle *st) const = 0;
		virtual const style::TextStyle &textStyle() const = 0;

		int buttonSkip() const;
		int buttonPadding() const;
		int buttonHeight() const;
		[[nodiscard]] virtual Images::CornersMaskRef buttonRounding(
			Ui::BubbleRounding outer,
			RectParts sides) const = 0;

		virtual void repaint(not_null<const HistoryItem*> item) const = 0;
		virtual ~Style() {
		}

	protected:
		virtual void paintButtonBg(
			QPainter &p,
			const Ui::ChatStyle *st,
			const QRect &rect,
			Ui::BubbleRounding rounding,
			float64 howMuchOver) const = 0;
		virtual void paintButtonIcon(
			QPainter &p,
			const Ui::ChatStyle *st,
			const QRect &rect,
			int outerWidth,
			HistoryMessageMarkupButton::Type type) const = 0;
		virtual void paintButtonLoading(
			QPainter &p,
			const Ui::ChatStyle *st,
			const QRect &rect,
			int outerWidth,
			Ui::BubbleRounding rounding) const = 0;
		virtual int minButtonWidth(
			HistoryMessageMarkupButton::Type type) const = 0;

	private:
		const style::BotKeyboardButton *_st;

		void paintButton(
			Painter &p,
			const Ui::ChatStyle *st,
			int outerWidth,
			const ReplyKeyboard::Button &button,
			Ui::BubbleRounding rounding) const;
		friend class ReplyKeyboard;

	};

	ReplyKeyboard(
		not_null<const HistoryItem*> item,
		std::unique_ptr<Style> &&s);
	ReplyKeyboard(const ReplyKeyboard &other) = delete;
	ReplyKeyboard &operator=(const ReplyKeyboard &other) = delete;

	bool isEnoughSpace(int width, const style::BotKeyboardButton &st) const;
	void setStyle(std::unique_ptr<Style> &&s);
	void resize(int width, int height);

	// what width and height will best fit this keyboard
	int naturalWidth() const;
	int naturalHeight() const;

	void paint(
		Painter &p,
		const Ui::ChatStyle *st,
		Ui::BubbleRounding rounding,
		int outerWidth,
		const QRect &clip) const;
	ClickHandlerPtr getLink(QPoint point) const;

	void clickHandlerActiveChanged(
		const ClickHandlerPtr &p,
		bool active);
	void clickHandlerPressedChanged(
		const ClickHandlerPtr &p,
		bool pressed,
		Ui::BubbleRounding rounding);

	void clearSelection();
	void updateMessageId();

private:
	friend class Style;
	struct Button {
		Button();
		Button(Button &&other);
		Button &operator=(Button &&other);
		~Button();

		Ui::Text::String text = { 1 };
		QRect rect;
		int characters = 0;
		float64 howMuchOver = 0.;
		HistoryMessageMarkupButton::Type type;
		std::shared_ptr<ReplyMarkupClickHandler> link;
		mutable std::unique_ptr<Ui::RippleAnimation> ripple;
	};
	struct ButtonCoords {
		int i, j;
	};

	void startAnimation(int i, int j, int direction);

	ButtonCoords findButtonCoordsByClickHandler(const ClickHandlerPtr &p);

	bool selectedAnimationCallback(crl::time now);

	const not_null<const HistoryItem*> _item;
	int _width = 0;

	std::vector<std::vector<Button>> _rows;

	base::flat_map<int, crl::time> _animations;
	Ui::Animations::Basic _selectedAnimation;
	std::unique_ptr<Style> _st;

	ClickHandlerPtr _savedPressed;
	ClickHandlerPtr _savedActive;
	mutable QPoint _savedCoords;

};

// Special type of Component for the channel actions log.
struct HistoryMessageLogEntryOriginal
: public RuntimeComponent<HistoryMessageLogEntryOriginal, HistoryItem> {
	HistoryMessageLogEntryOriginal();
	HistoryMessageLogEntryOriginal(HistoryMessageLogEntryOriginal &&other);
	HistoryMessageLogEntryOriginal &operator=(HistoryMessageLogEntryOriginal &&other);
	~HistoryMessageLogEntryOriginal();

	WebPageData *page = nullptr;

};

struct HistoryServiceData
: public RuntimeComponent<HistoryServiceData, HistoryItem> {
	std::vector<ClickHandlerPtr> textLinks;
};

struct HistoryServiceDependentData {
	PeerId peerId = 0;
	HistoryItem *msg = nullptr;
	ClickHandlerPtr lnk;
	MsgId msgId = 0;
	MsgId topId = 0;
	bool topicPost = false;
	bool pendingResolve = false;
	bool requestedResolve = false;
};

struct HistoryServicePinned
: public RuntimeComponent<HistoryServicePinned, HistoryItem>
, public HistoryServiceDependentData {
};

struct HistoryServiceTopicInfo
: public RuntimeComponent<HistoryServiceTopicInfo, HistoryItem>
, public HistoryServiceDependentData {
	QString title;
	DocumentId iconId = 0;
	bool closed = false;
	bool reopened = false;
	bool reiconed = false;
	bool renamed = false;
	bool hidden = false;
	bool unhidden = false;

	[[nodiscard]] bool created() const {
		return !closed
			&& !reopened
			&& !reiconed
			&& !renamed
			&& !hidden
			&& !unhidden;
	}
};

struct HistoryServiceGameScore
: public RuntimeComponent<HistoryServiceGameScore, HistoryItem>
, public HistoryServiceDependentData {
	int score = 0;
};

struct HistoryServicePayment
: public RuntimeComponent<HistoryServicePayment, HistoryItem>
, public HistoryServiceDependentData {
	QString slug;
	QString amount;
	ClickHandlerPtr invoiceLink;
	bool recurringInit = false;
	bool recurringUsed = false;
};

struct HistoryServiceSameBackground
: public RuntimeComponent<HistoryServiceSameBackground, HistoryItem>
, public HistoryServiceDependentData {
};

struct HistoryServiceGiveawayResults
: public RuntimeComponent<HistoryServiceGiveawayResults, HistoryItem>
, public HistoryServiceDependentData {
};

struct HistoryServiceCustomLink
: public RuntimeComponent<HistoryServiceCustomLink, HistoryItem> {
	ClickHandlerPtr link;
};

enum class HistorySelfDestructType {
	Photo,
	Video,
};

struct TimeToLiveSingleView {
	friend inline auto operator<=>(
		TimeToLiveSingleView,
		TimeToLiveSingleView) = default;
	friend inline bool operator==(
		TimeToLiveSingleView,
		TimeToLiveSingleView) = default;
};

struct HistoryServiceSelfDestruct
: public RuntimeComponent<HistoryServiceSelfDestruct, HistoryItem> {
	using Type = HistorySelfDestructType;

	Type type = Type::Photo;
	std::variant<crl::time, TimeToLiveSingleView> timeToLive = crl::time();
	std::variant<crl::time, TimeToLiveSingleView> destructAt = crl::time();
};

struct HistoryServiceOngoingCall
: public RuntimeComponent<HistoryServiceOngoingCall, HistoryItem> {
	CallId id = 0;
	ClickHandlerPtr link;
	rpl::lifetime lifetime;
};

struct HistoryServiceChatThemeChange
: public RuntimeComponent<HistoryServiceChatThemeChange, HistoryItem> {
	ClickHandlerPtr link;
};

struct HistoryServiceTTLChange
: public RuntimeComponent<HistoryServiceTTLChange, HistoryItem> {
	ClickHandlerPtr link;
};

class FileClickHandler;
struct HistoryDocumentThumbed : public RuntimeComponent<HistoryDocumentThumbed, HistoryView::Document> {
	std::shared_ptr<FileClickHandler> linksavel;
	std::shared_ptr<FileClickHandler> linkopenwithl;
	std::shared_ptr<FileClickHandler> linkcancell;
	mutable QImage thumbnail;
	mutable QString link;
	int thumbw = 0;
	mutable int linkw = 0;
	mutable Ui::BubbleRounding rounding;
	mutable bool blurred : 1 = false;
};

struct HistoryDocumentCaptioned : public RuntimeComponent<HistoryDocumentCaptioned, HistoryView::Document> {
	HistoryDocumentCaptioned();

	Ui::Text::String caption;
};

struct HistoryDocumentNamed : public RuntimeComponent<HistoryDocumentNamed, HistoryView::Document> {
	QString name;
	int namew = 0;
};

struct HistoryDocumentVoicePlayback {
	HistoryDocumentVoicePlayback(const HistoryView::Document *that);

	int32 position = 0;
	anim::value progress;
	Ui::Animations::Basic progressAnimation;
};

class HistoryDocumentVoice : public RuntimeComponent<HistoryDocumentVoice, HistoryView::Document> {
	// We don't use float64 because components should align to pointer even on 32bit systems.
	static constexpr float64 kFloatToIntMultiplier = 65536.;

public:
	void ensurePlayback(const HistoryView::Document *interfaces) const;
	void checkPlaybackFinished() const;

	mutable std::unique_ptr<HistoryDocumentVoicePlayback> playback;
	std::shared_ptr<VoiceSeekClickHandler> seekl;
	mutable int lastDurationMs = 0;

	[[nodiscard]] bool seeking() const;
	void startSeeking();
	void stopSeeking();
	[[nodiscard]] float64 seekingStart() const;
	void setSeekingStart(float64 seekingStart) const;
	[[nodiscard]] float64 seekingCurrent() const;
	void setSeekingCurrent(float64 seekingCurrent);

	std::unique_ptr<HistoryView::TranscribeButton> transcribe;
	Ui::Text::String transcribeText;
	std::unique_ptr<Media::Player::RoundPainter> round;

private:
	bool _seeking = false;

	mutable int _seekingStart = 0;
	mutable int _seekingCurrent = 0;

};
