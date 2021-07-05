/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/history_item.h"
#include "ui/empty_userpic.h"
#include "ui/effects/animations.h"

struct WebPageData;
class VoiceSeekClickHandler;

namespace Data {
class Session;
} // namespace Data

namespace HistoryView {
class Element;
class Document;
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
	MsgId repliesInboxReadTillId = 0;
	MsgId repliesOutboxReadTillId = 0;
	MsgId repliesMaxId = 0;
	ChannelId commentsMegagroupId = 0;
	MsgId commentsRootId = 0;
};

struct HistoryMessageSigned : public RuntimeComponent<HistoryMessageSigned, HistoryItem> {
	void refresh(const QString &date);
	int maxWidth() const;

	QString author;
	Ui::Text::String signature;
	bool isElided = false;
	bool isAnonymousRank = false;
};

struct HistoryMessageEdited : public RuntimeComponent<HistoryMessageEdited, HistoryItem> {
	void refresh(const QString &date, bool displayed);
	int maxWidth() const;

	TimeId date = 0;
	Ui::Text::String text;
};

struct HiddenSenderInfo {
	HiddenSenderInfo(const QString &name, bool external);

	QString name;
	QString firstName;
	QString lastName;
	PeerId colorPeerId = 0;
	Ui::EmptyUserpic userpic;
	Ui::Text::String nameText;

	inline bool operator==(const HiddenSenderInfo &other) const {
		return name == other.name;
	}
	inline bool operator!=(const HiddenSenderInfo &other) const {
		return !(*this == other);
	}
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

struct HistoryMessageReply : public RuntimeComponent<HistoryMessageReply, HistoryItem> {
	HistoryMessageReply() = default;
	HistoryMessageReply(const HistoryMessageReply &other) = delete;
	HistoryMessageReply(HistoryMessageReply &&other) = delete;
	HistoryMessageReply &operator=(const HistoryMessageReply &other) = delete;
	HistoryMessageReply &operator=(HistoryMessageReply &&other) {
		replyToPeerId = other.replyToPeerId;
		replyToMsgId = other.replyToMsgId;
		replyToMsgTop = other.replyToMsgTop;
		replyToDocumentId = other.replyToDocumentId;
		std::swap(replyToMsg, other.replyToMsg);
		replyToLnk = std::move(other.replyToLnk);
		replyToName = std::move(other.replyToName);
		replyToText = std::move(other.replyToText);
		replyToVersion = other.replyToVersion;
		maxReplyWidth = other.maxReplyWidth;
		replyToVia = std::move(other.replyToVia);
		return *this;
	}
	~HistoryMessageReply() {
		// clearData() should be called by holder.
		Expects(replyToMsg == nullptr);
		Expects(replyToVia == nullptr);
	}

	bool updateData(not_null<HistoryMessage*> holder, bool force = false);

	// Must be called before destructor.
	void clearData(not_null<HistoryMessage*> holder);

	bool isNameUpdated() const;
	void updateName() const;
	void resize(int width) const;
	void itemRemoved(HistoryMessage *holder, HistoryItem *removed);

	enum class PaintFlag {
		InBubble = (1 << 0),
		Selected = (1 << 1),
	};
	using PaintFlags = base::flags<PaintFlag>;
	friend inline constexpr auto is_flag_type(PaintFlag) { return true; };
	void paint(
		Painter &p,
		not_null<const HistoryView::Element*> holder,
		int x,
		int y,
		int w,
		PaintFlags flags) const;

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

	void refreshReplyToDocument();

	PeerId replyToPeerId = 0;
	MsgId replyToMsgId = 0;
	MsgId replyToMsgTop = 0;
	HistoryItem *replyToMsg = nullptr;
	DocumentId replyToDocumentId = 0;
	ClickHandlerPtr replyToLnk;
	mutable Ui::Text::String replyToName, replyToText;
	mutable int replyToVersion = 0;
	mutable int maxReplyWidth = 0;
	std::unique_ptr<HistoryMessageVia> replyToVia;
	int toWidth = 0;

};

struct HistoryMessageMarkupButton {
	enum class Type {
		Default,
		Url,
		Callback,
		CallbackWithPassword,
		RequestPhone,
		RequestLocation,
		RequestPoll,
		SwitchInline,
		SwitchInlineSame,
		Game,
		Buy,
		Auth,
	};

	HistoryMessageMarkupButton(
		Type type,
		const QString &text,
		const QByteArray &data = QByteArray(),
		const QString &forwardText = QString(),
		int32 buttonId = 0);

	static HistoryMessageMarkupButton *Get(
		not_null<Data::Session*> owner,
		FullMsgId itemId,
		int row,
		int column);

	Type type;
	QString text, forwardText;
	QByteArray data;
	int32 buttonId = 0;
	mutable mtpRequestId requestId = 0;

};

struct HistoryMessageReplyMarkup : public RuntimeComponent<HistoryMessageReplyMarkup, HistoryItem> {
	using Button = HistoryMessageMarkupButton;

	HistoryMessageReplyMarkup() = default;
	HistoryMessageReplyMarkup(MTPDreplyKeyboardMarkup::Flags f) : flags(f) {
	}

	void create(const MTPReplyMarkup &markup);
	void create(const HistoryMessageReplyMarkup &markup);

	std::vector<std::vector<Button>> rows;
	MTPDreplyKeyboardMarkup::Flags flags = 0;
	QString placeholder;

	std::unique_ptr<ReplyKeyboard> inlineKeyboard;

private:
	void createFromButtonRows(const QVector<MTPKeyboardButtonRow> &v);

};

class ReplyMarkupClickHandler : public LeftButtonClickHandler {
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

protected:
	void onClickImpl() const override;

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

		virtual void startPaint(Painter &p) const = 0;
		virtual const style::TextStyle &textStyle() const = 0;

		int buttonSkip() const;
		int buttonPadding() const;
		int buttonHeight() const;
		virtual int buttonRadius() const = 0;

		virtual void repaint(not_null<const HistoryItem*> item) const = 0;
		virtual ~Style() {
		}

	protected:
		virtual void paintButtonBg(
			Painter &p,
			const QRect &rect,
			float64 howMuchOver) const = 0;
		virtual void paintButtonIcon(
			Painter &p,
			const QRect &rect,
			int outerWidth,
			HistoryMessageMarkupButton::Type type) const = 0;
		virtual void paintButtonLoading(
			Painter &p,
			const QRect &rect) const = 0;
		virtual int minButtonWidth(
			HistoryMessageMarkupButton::Type type) const = 0;

	private:
		const style::BotKeyboardButton *_st;

		void paintButton(Painter &p, int outerWidth, const ReplyKeyboard::Button &button) const;
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

	void paint(Painter &p, int outerWidth, const QRect &clip) const;
	ClickHandlerPtr getLink(QPoint point) const;

	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active);
	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed);

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
	std::shared_ptr<FileClickHandler> _linksavel;
	std::shared_ptr<FileClickHandler> _linkopenwithl;
	std::shared_ptr<FileClickHandler> _linkcancell;
	int _thumbw = 0;

	mutable int _linkw = 0;
	mutable QString _link;
};

struct HistoryDocumentCaptioned : public RuntimeComponent<HistoryDocumentCaptioned, HistoryView::Document> {
	HistoryDocumentCaptioned();

	Ui::Text::String _caption;
};

struct HistoryDocumentNamed : public RuntimeComponent<HistoryDocumentNamed, HistoryView::Document> {
	QString _name;
	int _namew = 0;
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

	mutable std::unique_ptr<HistoryDocumentVoicePlayback> _playback;
	std::shared_ptr<VoiceSeekClickHandler> _seekl;
	mutable int _lastDurationMs = 0;

	bool seeking() const {
		return _seeking;
	}
	void startSeeking();
	void stopSeeking();
	float64 seekingStart() const {
		return _seekingStart / kFloatToIntMultiplier;
	}
	void setSeekingStart(float64 seekingStart) const {
		_seekingStart = qRound(seekingStart * kFloatToIntMultiplier);
	}
	float64 seekingCurrent() const {
		return _seekingCurrent / kFloatToIntMultiplier;
	}
	void setSeekingCurrent(float64 seekingCurrent) {
		_seekingCurrent = qRound(seekingCurrent * kFloatToIntMultiplier);
	}

private:
	bool _seeking = false;

	mutable int _seekingStart = 0;
	mutable int _seekingCurrent = 0;

};
