/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_web_page.h"

#include "main/main_session.h"
#include "apiwrap.h"
#include "mainwidget.h"
#include "data/data_session.h"
#include "data/data_photo.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "core/local_url_handlers.h"
#include "lang/lang_keys.h"
#include "iv/iv_data.h"
#include "ui/image/image.h"
#include "ui/text/text_entity.h"

namespace {

[[nodiscard]] WebPageCollage ExtractCollage(
		not_null<Data::Session*> owner,
		const QVector<MTPPageBlock> &items,
		const QVector<MTPPhoto> &photos,
		const QVector<MTPDocument> &documents) {
	const auto count = items.size();
	if (count < 2) {
		return {};
	}
	const auto bad = ranges::find_if(items, [](mtpTypeId type) {
		return (type != mtpc_pageBlockPhoto && type != mtpc_pageBlockVideo);
	}, [](const MTPPageBlock &item) {
		return item.type();
	});
	if (bad != items.end()) {
		return {};
	}

	for (const auto &photo : photos) {
		owner->processPhoto(photo);
	}
	for (const auto &document : documents) {
		owner->processDocument(document);
	}
	auto result = WebPageCollage();
	result.items.reserve(count);
	for (const auto &item : items) {
		const auto good = item.match([&](const MTPDpageBlockPhoto &data) {
			const auto photo = owner->photo(data.vphoto_id().v);
			if (photo->isNull()) {
				return false;
			}
			result.items.emplace_back(photo);
			return true;
		}, [&](const MTPDpageBlockVideo &data) {
			const auto document = owner->document(data.vvideo_id().v);
			if (!document->isVideoFile()) {
				return false;
			}
			result.items.emplace_back(document);
			return true;
		}, [](const auto &) -> bool {
			Unexpected("Type of block in Collage.");
		});
		if (!good) {
			return {};
		}
	}
	return result;
}

WebPageCollage ExtractCollage(
		not_null<Data::Session*> owner,
		const MTPDwebPage &data) {
	const auto page = data.vcached_page();
	if (!page) {
		return {};
	}
	const auto processMedia = [&] {
		if (const auto photo = data.vphoto()) {
			owner->processPhoto(*photo);
		}
		if (const auto document = data.vdocument()) {
			owner->processDocument(*document);
		}
	};
	return page->match([&](const auto &page) {
		for (const auto &block : page.vblocks().v) {
			switch (block.type()) {
			case mtpc_pageBlockPhoto:
			case mtpc_pageBlockVideo:
			case mtpc_pageBlockCover:
			case mtpc_pageBlockEmbed:
			case mtpc_pageBlockEmbedPost:
			case mtpc_pageBlockAudio:
				return WebPageCollage();
			case mtpc_pageBlockSlideshow:
				processMedia();
				return ExtractCollage(
					owner,
					block.c_pageBlockSlideshow().vitems().v,
					page.vphotos().v,
					page.vdocuments().v);
			case mtpc_pageBlockCollage:
				processMedia();
				return ExtractCollage(
					owner,
					block.c_pageBlockCollage().vitems().v,
					page.vphotos().v,
					page.vdocuments().v);
			default: break;
			}
		}
		return WebPageCollage();
	});
}

} // namespace

WebPageType ParseWebPageType(
		const QString &type,
		const QString &embedUrl,
		bool hasIV) {
	if (type == u"video"_q || type == u"gif"_q || !embedUrl.isEmpty()) {
		return WebPageType::Video;
	} else if (type == u"photo"_q) {
		return WebPageType::Photo;
	} else if (type == u"document"_q) {
		return WebPageType::Document;
	} else if (type == u"profile"_q) {
		return WebPageType::Profile;
	} else if (type == u"telegram_background"_q) {
		return WebPageType::WallPaper;
	} else if (type == u"telegram_theme"_q) {
		return WebPageType::Theme;
	} else if (type == u"telegram_story"_q) {
		return WebPageType::Story;
	} else if (type == u"telegram_channel"_q) {
		return WebPageType::Channel;
	} else if (type == u"telegram_channel_request"_q) {
		return WebPageType::ChannelWithRequest;
	} else if (type == u"telegram_megagroup"_q
		|| type == u"telegram_chat"_q) {
		return WebPageType::Group;
	} else if (type == u"telegram_megagroup_request"_q
		|| type == u"telegram_chat_request"_q) {
		return WebPageType::GroupWithRequest;
	} else if (type == u"telegram_album"_q) {
		return WebPageType::Album;
	} else if (type == u"telegram_message"_q) {
		return WebPageType::Message;
	} else if (type == u"telegram_bot"_q) {
		return WebPageType::Bot;
	} else if (type == u"telegram_voicechat"_q) {
		return WebPageType::VoiceChat;
	} else if (type == u"telegram_livestream"_q) {
		return WebPageType::Livestream;
	} else if (type == u"telegram_user"_q) {
		return WebPageType::User;
	} else if (type == u"telegram_botapp"_q) {
		return WebPageType::BotApp;
	} else if (type == u"telegram_channel_boost"_q) {
		return WebPageType::ChannelBoost;
	} else if (type == u"telegram_group_boost"_q) {
		return WebPageType::GroupBoost;
	} else if (type == u"telegram_giftcode"_q) {
		return WebPageType::Giftcode;
	} else if (type == u"telegram_stickerset"_q) {
		return WebPageType::StickerSet;
	} else if (hasIV) {
		return WebPageType::ArticleWithIV;
	} else {
		return WebPageType::Article;
	}
}

bool IgnoreIv(WebPageType type) {
	return !Iv::ShowButton()
		|| (type == WebPageType::Message)
		|| (type == WebPageType::Album);
}

WebPageType ParseWebPageType(const MTPDwebPage &page) {
	return ParseWebPageType(
		qs(page.vtype().value_or_empty()),
		page.vembed_url().value_or_empty(),
		!!page.vcached_page());
}

WebPageCollage::WebPageCollage(
	not_null<Data::Session*> owner,
	const MTPDwebPage &data)
: WebPageCollage(ExtractCollage(owner, data)) {
}

WebPageData::WebPageData(not_null<Data::Session*> owner, const WebPageId &id)
: id(id)
, _owner(owner) {
}

WebPageData::~WebPageData() = default;

Data::Session &WebPageData::owner() const {
	return *_owner;
}

Main::Session &WebPageData::session() const {
	return _owner->session();
}

bool WebPageData::applyChanges(
		WebPageType newType,
		const QString &newUrl,
		const QString &newDisplayUrl,
		const QString &newSiteName,
		const QString &newTitle,
		const TextWithEntities &newDescription,
		FullStoryId newStoryId,
		PhotoData *newPhoto,
		DocumentData *newDocument,
		WebPageCollage &&newCollage,
		std::unique_ptr<Iv::Data> newIv,
		std::unique_ptr<WebPageStickerSet> newStickerSet,
		std::shared_ptr<Data::UniqueGift> newUniqueGift,
		int newDuration,
		const QString &newAuthor,
		bool newHasLargeMedia,
		bool newPhotoIsVideoCover,
		int newPendingTill) {
	if (newPendingTill != 0
		&& (!url.isEmpty() || failed)
		&& (!pendingTill
			|| pendingTill == newPendingTill
			|| newPendingTill < -1)) {
		return false;
	}

	const auto resultUrl = newUrl;
	const auto resultDisplayUrl = newDisplayUrl;
	const auto possibleSiteName = newSiteName;
	const auto resultTitle = TextUtilities::SingleLine(newTitle);
	const auto resultAuthor = newAuthor;

	const auto viewTitleText = resultTitle.isEmpty()
		? TextUtilities::SingleLine(resultAuthor)
		: resultTitle;
	const auto resultSiteName = [&] {
		if (!possibleSiteName.isEmpty()) {
			return possibleSiteName;
		} else if (!newDescription.text.isEmpty()
			&& viewTitleText.isEmpty()
			&& !resultUrl.isEmpty()) {
			return Iv::SiteNameFromUrl(resultUrl);
		}
		return QString();
	}();
	const auto hasSiteName = !resultSiteName.isEmpty() ? 1 : 0;
	const auto hasTitle = !resultTitle.isEmpty() ? 1 : 0;
	const auto hasDescription = !newDescription.text.isEmpty() ? 1 : 0;
	if (newDocument
		|| !newCollage.items.empty()
		|| !newPhoto
		|| (hasSiteName + hasTitle + hasDescription < 2)) {
		newHasLargeMedia = false;
	}
	if (!newDocument || !newDocument->isVideoFile() || !newPhoto) {
		newPhotoIsVideoCover = false;
	}

	if (type == newType
		&& url == resultUrl
		&& displayUrl == resultDisplayUrl
		&& siteName == resultSiteName
		&& title == resultTitle
		&& description.text == newDescription.text
		&& storyId == newStoryId
		&& photo == newPhoto
		&& document == newDocument
		&& collage.items == newCollage.items
		&& (!iv == !newIv)
		&& (!iv || iv->partial() == newIv->partial())
		&& (!stickerSet == !newStickerSet)
		&& (!uniqueGift == !newUniqueGift)
		&& duration == newDuration
		&& author == resultAuthor
		&& hasLargeMedia == (newHasLargeMedia ? 1 : 0)
		&& photoIsVideoCover == (newPhotoIsVideoCover ? 1 : 0)
		&& pendingTill == newPendingTill) {
		return false;
	}
	if (pendingTill > 0 && newPendingTill <= 0) {
		_owner->session().api().clearWebPageRequest(this);
	}
	type = newType;
	hasLargeMedia = newHasLargeMedia ? 1 : 0;
	photoIsVideoCover = newPhotoIsVideoCover ? 1 : 0;
	url = resultUrl;
	displayUrl = resultDisplayUrl;
	siteName = resultSiteName;
	title = resultTitle;
	description = newDescription;
	storyId = newStoryId;
	photo = newPhoto;
	document = newDocument;
	collage = std::move(newCollage);
	iv = std::move(newIv);
	stickerSet = std::move(newStickerSet);
	uniqueGift = std::move(newUniqueGift);
	duration = newDuration;
	author = resultAuthor;
	pendingTill = newPendingTill;
	++version;

	if (type == WebPageType::WallPaper && document) {
		document->checkWallPaperProperties();
	}

	replaceDocumentGoodThumbnail();

	return true;
}

void WebPageData::replaceDocumentGoodThumbnail() {
	if (document && photo) {
		document->setGoodThumbnailPhoto(photo);
	}
}

void WebPageData::ApplyChanges(
		not_null<Main::Session*> session,
		ChannelData *channel,
		const MTPmessages_Messages &result) {
	result.match([&](
			const MTPDmessages_channelMessages &data) {
		if (channel) {
			channel->ptsReceived(data.vpts().v);
			channel->processTopics(data.vtopics());
		} else {
			LOG(("API Error: received messages.channelMessages "
				"when no channel was passed! (WebPageData::ApplyChanges)"));
		}
	}, [&](const auto &) {
	});
	const auto list = result.match([](
			const MTPDmessages_messagesNotModified &) {
		LOG(("API Error: received messages.messagesNotModified! "
			"(WebPageData::ApplyChanges)"));
		return static_cast<const QVector<MTPMessage>*>(nullptr);
	}, [&](const auto &data) {
		session->data().processUsers(data.vusers());
		session->data().processChats(data.vchats());
		return &data.vmessages().v;
	});
	if (!list) {
		return;
	}

	for (const auto &message : *list) {
		message.match([&](const MTPDmessage &data) {
			if (const auto media = data.vmedia()) {
				media->match([&](const MTPDmessageMediaWebPage &data) {
					session->data().processWebpage(data.vwebpage());
				}, [&](const auto &) {
				});
			}
		}, [&](const auto &) {
		});
	}
	session->data().sendWebPageGamePollNotifications();
}

QString WebPageData::displayedSiteName() const {
	return (document && document->isWallPaper())
		? tr::lng_media_chat_background(tr::now)
		: (document && document->isTheme())
		? tr::lng_media_color_theme(tr::now)
		: siteName;
}

TimeId WebPageData::extractVideoTimestamp() const {
	const auto take = [&](const QStringList &list, int index) {
		return (index >= 0 && index < list.size()) ? list[index] : QString();
	};
	const auto hashed = take(url.split('#'), 0);
	const auto params = take(hashed.split('?'), 1);
	const auto parts = params.split('&');
	for (const auto &part : parts) {
		if (part.startsWith(u"t="_q)) {
			return Core::ParseVideoTimestamp(part.mid(2));
		}
	}
	return 0;
}

bool WebPageData::computeDefaultSmallMedia() const {
	if (!collage.items.empty()) {
		return false;
	} else if (siteName.isEmpty()
		&& title.isEmpty()
		&& description.empty()
		&& author.isEmpty()) {
		return false;
	} else if (!uniqueGift
		&& !document
		&& photo
		&& type != WebPageType::Photo
		&& type != WebPageType::Document
		&& type != WebPageType::Story
		&& type != WebPageType::Video) {
		if (type == WebPageType::Profile) {
			return true;
		} else if (siteName == u"Twitter"_q
			|| siteName == u"Facebook"_q
			|| type == WebPageType::ArticleWithIV) {
			return false;
		} else {
			return true;
		}
	}
	return false;
}

bool WebPageData::suggestEnlargePhoto() const {
	return !siteName.isEmpty() || !title.isEmpty() || !description.empty();
}
