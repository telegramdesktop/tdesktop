/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "data/data_location.h"
#include "data/data_wall_paper.h"

class Image;
class History;
class HistoryItem;

namespace base {
template <typename Enum>
class enum_mask;
} // namespace base

namespace Storage {
enum class SharedMediaType : signed char;
using SharedMediaTypesMask = base::enum_mask<SharedMediaType>;
} // namespace Storage

namespace HistoryView {
enum class Context : char;
class Element;
class Media;
struct ItemPreview;
struct ItemPreviewImage;
struct ToPreviewOptions;
} // namespace HistoryView

namespace Data {

class CloudImage;
class WallPaper;
class Session;

enum class CallFinishReason : char {
	Missed,
	Busy,
	Disconnected,
	Hangup,
};

struct SharedContact {
	UserId userId = 0;
	QString firstName;
	QString lastName;
	QString phoneNumber;
};

struct Call {
	using FinishReason = CallFinishReason;

	int duration = 0;
	FinishReason finishReason = FinishReason::Missed;
	bool video = false;

};

struct ExtendedPreview {
	QByteArray inlineThumbnailBytes;
	QSize dimensions;
	TimeId videoDuration = -1;

	[[nodiscard]] bool empty() const {
		return dimensions.isEmpty();
	}
	explicit operator bool() const {
		return !empty();
	}
};

class Media;

struct Invoice {
	MsgId receiptMsgId = 0;
	uint64 amount = 0;
	QString currency;
	QString title;
	TextWithEntities description;
	ExtendedPreview extendedPreview;
	std::unique_ptr<Media> extendedMedia;
	PhotoData *photo = nullptr;
	bool isTest = false;
};

struct GiveawayStart {
	std::vector<not_null<ChannelData*>> channels;
	std::vector<QString> countries;
	QString additionalPrize;
	TimeId untilDate = 0;
	int quantity = 0;
	int months = 0;
	bool all = false;
};

struct GiveawayResults {
	not_null<ChannelData*> channel;
	std::vector<not_null<PeerData*>> winners;
	QString additionalPrize;
	TimeId untilDate = 0;
	MsgId launchId = 0;
	int additionalPeersCount = 0;
	int winnersCount = 0;
	int unclaimedCount = 0;
	int months = 0;
	bool refunded = false;
	bool all = false;
};

struct GiftCode {
	QString slug;
	ChannelData *channel = nullptr;
	int months = 0;
	bool viaGiveaway = false;
	bool unclaimed = false;
};

class Media {
public:
	Media(not_null<HistoryItem*> parent);
	virtual ~Media() = default;

	not_null<HistoryItem*> parent() const;

	using ToPreviewOptions = HistoryView::ToPreviewOptions;
	using ItemPreviewImage = HistoryView::ItemPreviewImage;
	using ItemPreview = HistoryView::ItemPreview;

	virtual std::unique_ptr<Media> clone(not_null<HistoryItem*> parent) = 0;

	virtual DocumentData *document() const;
	virtual PhotoData *photo() const;
	virtual WebPageData *webpage() const;
	virtual MediaWebPageFlags webpageFlags() const;
	virtual const SharedContact *sharedContact() const;
	virtual const Call *call() const;
	virtual GameData *game() const;
	virtual const Invoice *invoice() const;
	virtual CloudImage *location() const;
	virtual PollData *poll() const;
	virtual const WallPaper *paper() const;
	virtual bool paperForBoth() const;
	virtual FullStoryId storyId() const;
	virtual bool storyExpired(bool revalidate = false);
	virtual bool storyMention() const;
	virtual const GiveawayStart *giveawayStart() const;
	virtual const GiveawayResults *giveawayResults() const;

	virtual bool uploading() const;
	virtual Storage::SharedMediaTypesMask sharedMediaTypes() const;
	virtual bool canBeGrouped() const;
	virtual bool hasReplyPreview() const;
	virtual Image *replyPreview() const;
	virtual bool replyPreviewLoaded() const;
	// Returns text with link-start and link-end commands for service-color highlighting.
	// Example: "[link1-start]You:[link1-end] [link1-start]Photo,[link1-end] caption text"
	virtual ItemPreview toPreview(ToPreviewOptions way) const;
	virtual TextWithEntities notificationText() const = 0;
	virtual QString pinnedTextSubstring() const = 0;
	virtual TextForMimeData clipboardText() const = 0;
	virtual bool allowsForward() const;
	virtual bool allowsEdit() const;
	virtual bool allowsEditCaption() const;
	virtual bool allowsEditMedia() const;
	virtual bool allowsRevoke(TimeId now) const;
	virtual bool forwardedBecomesUnread() const;
	virtual bool dropForwardedInfo() const;
	virtual bool forceForwardedInfo() const;
	[[nodiscard]] virtual bool hasSpoiler() const;
	[[nodiscard]] virtual crl::time ttlSeconds() const;

	[[nodiscard]] virtual bool consumeMessageText(
		const TextWithEntities &text);
	[[nodiscard]] virtual TextWithEntities consumedMessageText() const;

	// After sending an inline result we may want to completely recreate
	// the media (all media that was generated on client side, for example).
	virtual bool updateInlineResultMedia(const MTPMessageMedia &media) = 0;
	virtual bool updateSentMedia(const MTPMessageMedia &media) = 0;
	virtual bool updateExtendedMedia(
			not_null<HistoryItem*> item,
			const MTPMessageExtendedMedia &media) {
		return false;
	}
	virtual std::unique_ptr<HistoryView::Media> createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing = nullptr) = 0;
	std::unique_ptr<HistoryView::Media> createView(
		not_null<HistoryView::Element*> message,
		HistoryView::Element *replacing = nullptr);

protected:
	[[nodiscard]] ItemPreview toGroupPreview(
		const HistoryItemsList &items,
		ToPreviewOptions options) const;

private:
	const not_null<HistoryItem*> _parent;

};

class MediaPhoto final : public Media {
public:
	MediaPhoto(
		not_null<HistoryItem*> parent,
		not_null<PhotoData*> photo,
		bool spoiler);
	MediaPhoto(
		not_null<HistoryItem*> parent,
		not_null<PeerData*> chat,
		not_null<PhotoData*> photo);
	~MediaPhoto();

	std::unique_ptr<Media> clone(not_null<HistoryItem*> parent) override;

	PhotoData *photo() const override;

	bool uploading() const override;
	Storage::SharedMediaTypesMask sharedMediaTypes() const override;
	bool canBeGrouped() const override;
	bool hasReplyPreview() const override;
	Image *replyPreview() const override;
	bool replyPreviewLoaded() const override;
	ItemPreview toPreview(ToPreviewOptions options) const override;
	TextWithEntities notificationText() const override;
	QString pinnedTextSubstring() const override;
	TextForMimeData clipboardText() const override;
	bool allowsEditCaption() const override;
	bool allowsEditMedia() const override;
	bool hasSpoiler() const override;

	bool updateInlineResultMedia(const MTPMessageMedia &media) override;
	bool updateSentMedia(const MTPMessageMedia &media) override;
	std::unique_ptr<HistoryView::Media> createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing = nullptr) override;

private:
	not_null<PhotoData*> _photo;
	PeerData *_chat = nullptr;
	bool _spoiler = false;

};

class MediaFile final : public Media {
public:
	MediaFile(
		not_null<HistoryItem*> parent,
		not_null<DocumentData*> document,
		bool skipPremiumEffect,
		bool spoiler,
		crl::time ttlSeconds);
	~MediaFile();

	std::unique_ptr<Media> clone(not_null<HistoryItem*> parent) override;

	DocumentData *document() const override;

	bool uploading() const override;
	Storage::SharedMediaTypesMask sharedMediaTypes() const override;
	bool canBeGrouped() const override;
	bool hasReplyPreview() const override;
	Image *replyPreview() const override;
	bool replyPreviewLoaded() const override;
	ItemPreview toPreview(ToPreviewOptions options) const override;
	TextWithEntities notificationText() const override;
	QString pinnedTextSubstring() const override;
	TextForMimeData clipboardText() const override;
	bool allowsEditCaption() const override;
	bool allowsEditMedia() const override;
	bool forwardedBecomesUnread() const override;
	bool dropForwardedInfo() const override;
	bool hasSpoiler() const override;
	crl::time ttlSeconds() const override;
	bool allowsForward() const override;

	bool updateInlineResultMedia(const MTPMessageMedia &media) override;
	bool updateSentMedia(const MTPMessageMedia &media) override;
	std::unique_ptr<HistoryView::Media> createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing = nullptr) override;

private:
	not_null<DocumentData*> _document;
	QString _emoji;
	bool _skipPremiumEffect = false;
	bool _spoiler = false;

	// Video (unsupported) / Voice / Round.
	crl::time _ttlSeconds = 0;

};

class MediaContact final : public Media {
public:
	MediaContact(
		not_null<HistoryItem*> parent,
		UserId userId,
		const QString &firstName,
		const QString &lastName,
		const QString &phoneNumber);
	~MediaContact();

	std::unique_ptr<Media> clone(not_null<HistoryItem*> parent) override;

	const SharedContact *sharedContact() const override;
	TextWithEntities notificationText() const override;
	QString pinnedTextSubstring() const override;
	TextForMimeData clipboardText() const override;

	bool updateInlineResultMedia(const MTPMessageMedia &media) override;
	bool updateSentMedia(const MTPMessageMedia &media) override;
	std::unique_ptr<HistoryView::Media> createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing = nullptr) override;

private:
	SharedContact _contact;

};

class MediaLocation final : public Media {
public:
	MediaLocation(
		not_null<HistoryItem*> parent,
		const LocationPoint &point);
	MediaLocation(
		not_null<HistoryItem*> parent,
		const LocationPoint &point,
		const QString &title,
		const QString &description);

	std::unique_ptr<Media> clone(not_null<HistoryItem*> parent) override;

	CloudImage *location() const override;
	ItemPreview toPreview(ToPreviewOptions options) const override;
	TextWithEntities notificationText() const override;
	QString pinnedTextSubstring() const override;
	TextForMimeData clipboardText() const override;

	bool updateInlineResultMedia(const MTPMessageMedia &media) override;
	bool updateSentMedia(const MTPMessageMedia &media) override;
	std::unique_ptr<HistoryView::Media> createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing = nullptr) override;

private:
	LocationPoint _point;
	not_null<CloudImage*> _location;
	QString _title;
	QString _description;

};

class MediaCall final : public Media {
public:
	MediaCall(not_null<HistoryItem*> parent, const Call &call);
	~MediaCall();

	std::unique_ptr<Media> clone(not_null<HistoryItem*> parent) override;

	const Call *call() const override;
	TextWithEntities notificationText() const override;
	QString pinnedTextSubstring() const override;
	TextForMimeData clipboardText() const override;
	bool allowsForward() const override;

	bool updateInlineResultMedia(const MTPMessageMedia &media) override;
	bool updateSentMedia(const MTPMessageMedia &media) override;
	std::unique_ptr<HistoryView::Media> createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing = nullptr) override;

	static QString Text(
		not_null<HistoryItem*> item,
		CallFinishReason reason,
		bool video);

private:
	Call _call;

};

class MediaWebPage final : public Media {
public:
	MediaWebPage(
		not_null<HistoryItem*> parent,
		not_null<WebPageData*> page,
		MediaWebPageFlags flags);
	~MediaWebPage();

	std::unique_ptr<Media> clone(not_null<HistoryItem*> parent) override;

	DocumentData *document() const override;
	PhotoData *photo() const override;
	WebPageData *webpage() const override;
	MediaWebPageFlags webpageFlags() const override;

	bool hasReplyPreview() const override;
	Image *replyPreview() const override;
	bool replyPreviewLoaded() const override;
	ItemPreview toPreview(ToPreviewOptions options) const override;
	TextWithEntities notificationText() const override;
	QString pinnedTextSubstring() const override;
	TextForMimeData clipboardText() const override;
	bool allowsEdit() const override;

	bool updateInlineResultMedia(const MTPMessageMedia &media) override;
	bool updateSentMedia(const MTPMessageMedia &media) override;
	std::unique_ptr<HistoryView::Media> createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing = nullptr) override;

private:
	const not_null<WebPageData*> _page;
	const MediaWebPageFlags _flags;

};

class MediaGame final : public Media {
public:
	MediaGame(
		not_null<HistoryItem*> parent,
		not_null<GameData*> game);

	std::unique_ptr<Media> clone(not_null<HistoryItem*> parent) override;

	GameData *game() const override;

	bool hasReplyPreview() const override;
	Image *replyPreview() const override;
	bool replyPreviewLoaded() const override;
	TextWithEntities notificationText() const override;
	QString pinnedTextSubstring() const override;
	TextForMimeData clipboardText() const override;
	bool dropForwardedInfo() const override;

	bool consumeMessageText(const TextWithEntities &text) override;
	TextWithEntities consumedMessageText() const override;

	bool updateInlineResultMedia(const MTPMessageMedia &media) override;
	bool updateSentMedia(const MTPMessageMedia &media) override;
	std::unique_ptr<HistoryView::Media> createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing = nullptr) override;

private:
	not_null<GameData*> _game;
	TextWithEntities _consumedText;

};

class MediaInvoice final : public Media {
public:
	MediaInvoice(
		not_null<HistoryItem*> parent,
		const Invoice &data);

	std::unique_ptr<Media> clone(not_null<HistoryItem*> parent) override;

	const Invoice *invoice() const override;

	bool hasReplyPreview() const override;
	Image *replyPreview() const override;
	bool replyPreviewLoaded() const override;
	TextWithEntities notificationText() const override;
	QString pinnedTextSubstring() const override;
	TextForMimeData clipboardText() const override;

	bool updateInlineResultMedia(const MTPMessageMedia &media) override;
	bool updateSentMedia(const MTPMessageMedia &media) override;
	bool updateExtendedMedia(
		not_null<HistoryItem*> item,
		const MTPMessageExtendedMedia &media) override;
	std::unique_ptr<HistoryView::Media> createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing = nullptr) override;

private:
	Invoice _invoice;

};

class MediaPoll final : public Media {
public:
	MediaPoll(
		not_null<HistoryItem*> parent,
		not_null<PollData*> poll);
	~MediaPoll();

	std::unique_ptr<Media> clone(not_null<HistoryItem*> parent) override;

	PollData *poll() const override;

	TextWithEntities notificationText() const override;
	QString pinnedTextSubstring() const override;
	TextForMimeData clipboardText() const override;

	bool updateInlineResultMedia(const MTPMessageMedia &media) override;
	bool updateSentMedia(const MTPMessageMedia &media) override;
	std::unique_ptr<HistoryView::Media> createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing = nullptr) override;

private:
	not_null<PollData*> _poll;

};

class MediaDice final : public Media {
public:
	MediaDice(not_null<HistoryItem*> parent, QString emoji, int value);

	std::unique_ptr<Media> clone(not_null<HistoryItem*> parent) override;

	[[nodiscard]] QString emoji() const;
	[[nodiscard]] int value() const;

	bool allowsRevoke(TimeId now) const override;
	TextWithEntities notificationText() const override;
	QString pinnedTextSubstring() const override;
	TextForMimeData clipboardText() const override;
	bool forceForwardedInfo() const override;

	bool updateInlineResultMedia(const MTPMessageMedia &media) override;
	bool updateSentMedia(const MTPMessageMedia &media) override;
	std::unique_ptr<HistoryView::Media> createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing = nullptr) override;

	[[nodiscard]] ClickHandlerPtr makeHandler() const;
	[[nodiscard]] static ClickHandlerPtr MakeHandler(
		not_null<History*> history,
		const QString &emoji);

private:
	QString _emoji;
	int _value = 0;

};

class MediaGiftBox final : public Media {
public:
	MediaGiftBox(
		not_null<HistoryItem*> parent,
		not_null<PeerData*> from,
		int months);
	MediaGiftBox(
		not_null<HistoryItem*> parent,
		not_null<PeerData*> from,
		GiftCode data);

	std::unique_ptr<Media> clone(not_null<HistoryItem*> parent) override;

	[[nodiscard]] not_null<PeerData*> from() const;
	[[nodiscard]] const GiftCode &data() const;

	TextWithEntities notificationText() const override;
	QString pinnedTextSubstring() const override;
	TextForMimeData clipboardText() const override;

	bool updateInlineResultMedia(const MTPMessageMedia &media) override;
	bool updateSentMedia(const MTPMessageMedia &media) override;
	std::unique_ptr<HistoryView::Media> createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing = nullptr) override;

private:
	not_null<PeerData*> _from;
	GiftCode _data;

};

class MediaWallPaper final : public Media {
public:
	MediaWallPaper(
		not_null<HistoryItem*> parent,
		const WallPaper &paper,
		bool paperForBoth);
	~MediaWallPaper();

	std::unique_ptr<Media> clone(not_null<HistoryItem*> parent) override;

	const WallPaper *paper() const override;
	bool paperForBoth() const override;

	TextWithEntities notificationText() const override;
	QString pinnedTextSubstring() const override;
	TextForMimeData clipboardText() const override;

	bool updateInlineResultMedia(const MTPMessageMedia &media) override;
	bool updateSentMedia(const MTPMessageMedia &media) override;
	std::unique_ptr<HistoryView::Media> createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing = nullptr) override;

private:
	const WallPaper _paper;
	const bool _paperForBoth = false;

};

class MediaStory final : public Media, public base::has_weak_ptr {
public:
	MediaStory(
		not_null<HistoryItem*> parent,
		FullStoryId storyId,
		bool mention);
	~MediaStory();

	std::unique_ptr<Media> clone(not_null<HistoryItem*> parent) override;

	FullStoryId storyId() const override;
	bool storyExpired(bool revalidate = false) override;
	bool storyMention() const override;

	TextWithEntities notificationText() const override;
	QString pinnedTextSubstring() const override;
	TextForMimeData clipboardText() const override;
	bool dropForwardedInfo() const override;

	bool updateInlineResultMedia(const MTPMessageMedia &media) override;
	bool updateSentMedia(const MTPMessageMedia &media) override;
	std::unique_ptr<HistoryView::Media> createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing = nullptr) override;

	[[nodiscard]] static not_null<PhotoData*> LoadingStoryPhoto(
		not_null<Session*> owner);

private:
	const FullStoryId _storyId;
	const bool _mention = false;
	bool _viewMayExist = false;
	bool _expired = false;

};

class MediaGiveawayStart final : public Media {
public:
	MediaGiveawayStart(
		not_null<HistoryItem*> parent,
		const GiveawayStart &data);

	std::unique_ptr<Media> clone(not_null<HistoryItem*> parent) override;

	const GiveawayStart *giveawayStart() const override;

	TextWithEntities notificationText() const override;
	QString pinnedTextSubstring() const override;
	TextForMimeData clipboardText() const override;

	bool updateInlineResultMedia(const MTPMessageMedia &media) override;
	bool updateSentMedia(const MTPMessageMedia &media) override;
	std::unique_ptr<HistoryView::Media> createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing = nullptr) override;

private:
	GiveawayStart _data;

};

class MediaGiveawayResults final : public Media {
public:
	MediaGiveawayResults(
		not_null<HistoryItem*> parent,
		const GiveawayResults &data);

	std::unique_ptr<Media> clone(not_null<HistoryItem*> parent) override;

	const GiveawayResults *giveawayResults() const override;

	TextWithEntities notificationText() const override;
	QString pinnedTextSubstring() const override;
	TextForMimeData clipboardText() const override;

	bool updateInlineResultMedia(const MTPMessageMedia &media) override;
	bool updateSentMedia(const MTPMessageMedia &media) override;
	std::unique_ptr<HistoryView::Media> createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing = nullptr) override;

private:
	GiveawayResults _data;

};

[[nodiscard]] Invoice ComputeInvoiceData(
	not_null<HistoryItem*> item,
	const MTPDmessageMediaInvoice &data);

[[nodiscard]] Call ComputeCallData(const MTPDmessageActionPhoneCall &call);

[[nodiscard]] GiveawayStart ComputeGiveawayStartData(
	not_null<HistoryItem*> item,
	const MTPDmessageMediaGiveaway &data);

[[nodiscard]] GiveawayResults ComputeGiveawayResultsData(
	not_null<HistoryItem*> item,
	const MTPDmessageMediaGiveawayResults &data);

} // namespace Data
