/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_cloud_file.h"
#include "history/history_item.h"
#include "ui/empty_userpic.h"
#include "ui/effects/animations.h"
#include "ui/chat/message_bubble.h"

struct WebPageData;
class VoiceSeekClickHandler;

namespace Ui {
struct ChatPaintContext;
class ChatStyle;
struct PeerUserpicView;
} // namespace Ui

namespace Data {
class Session;
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
	bool isAnonymousRank = false;
};

struct HistoryMessageEdited : public RuntimeComponent<HistoryMessageEdited, HistoryItem> {
	TimeId date = 0;
};

class HiddenSenderInfo {
public:
	HiddenSenderInfo(const QString &name, bool external);

	QString name;
	QString firstName;
	QString lastName;
	PeerId colorPeerId = 0;
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

	TimeId originalDate = 0;
	PeerData *originalSender = nullptr;
	std::unique_ptr<HiddenSenderInfo> hiddenSenderInfo;
	QString originalAuthor;
	QString psaType;
	MsgId originalId = 0;
	mutable Ui::Text::String text = { 1 };

	PeerData *savedFromPeer = nullptr;
	MsgId savedFromMsgId = 0;
	bool imported = false;
};

struct HistoryMessageSponsored : public RuntimeComponent<HistoryMessageSponsored, HistoryItem> {
	enum class Type : uchar {
		User,
		Group,
		Broadcast,
		Post,
		Bot,
	};
	std::unique_ptr<HiddenSenderInfo> sender;
	Type type = Type::User;
	bool recommended = false;
	bool isForceUserpicDisplay = false;
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

struct HistoryMessageReply
	: public RuntimeComponent<HistoryMessageReply, HistoryItem> {
	HistoryMessageReply() = default;
	HistoryMessageReply(const HistoryMessageReply &other) = delete;
	HistoryMessageReply(HistoryMessageReply &&other) = delete;
	HistoryMessageReply &operator=(
		const HistoryMessageReply &other) = delete;
	HistoryMessageReply &operator=(HistoryMessageReply &&other) = default;
	~HistoryMessageReply() {
		// clearData() should be called by holder.
		Expects(replyToMsg.empty());
		Expects(replyToVia == nullptr);
	}

	bool updateData(not_null<HistoryMessage*> holder, bool force = false);

	// Must be called before destructor.
	void clearData(not_null<HistoryMessage*> holder);

	[[nodiscard]] PeerData *replyToFrom(
		not_null<HistoryMessage*> holder) const;
	[[nodiscard]] QString replyToFromName(
		not_null<HistoryMessage*> holder) const;
	[[nodiscard]] QString replyToFromName(not_null<PeerData*> peer) const;
	[[nodiscard]] bool isNameUpdated(not_null<HistoryMessage*> holder) const;
	void updateName(not_null<HistoryMessage*> holder) const;
	void resize(int width) const;
	void itemRemoved(HistoryMessage *holder, HistoryItem *removed);

	void paint(
		Painter &p,
		not_null<const HistoryView::Element*> holder,
		const Ui::ChatPaintContext &context,
		int x,
		int y,
		int w,
		bool inBubble) const;

	[[nodiscard]] PeerId replyToPeer() const {
		return replyToPeerId;
	}
	[[nodiscard]] MsgId replyToId() const {
		return replyToMsgId;
	}
	[[nodiscard]] MsgId replyToTop() const {
		return replyToMsgTop;
	}
	[[nodiscard]] int replyToWidth() const {
		return maxReplyWidth;
	}
	[[nodiscard]] ClickHandlerPtr replyToLink() const {
		return replyToLnk;
	}
	void setReplyToLinkFrom(
		not_null<HistoryMessage*> holder);

	void refreshReplyToMedia();

	PeerId replyToPeerId = 0;
	MsgId replyToMsgId = 0;
	MsgId replyToMsgTop = 0;
	using ColorKey = PeerId;
	ColorKey replyToColorKey = 0;
	DocumentId replyToDocumentId = 0;
	WebPageId replyToWebPageId = 0;
	ReplyToMessagePointer replyToMsg;
	std::unique_ptr<HistoryMessageVia> replyToVia;
	ClickHandlerPtr replyToLnk;
	mutable Ui::Text::String replyToName, replyToText;
	mutable int replyToVersion = 0;
	mutable int maxReplyWidth = 0;
	int toWidth = 0;
	bool topicPost = false;

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
			const QRect &rect) const = 0;
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
