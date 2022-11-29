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
#include "ui/image/image.h"
#include "ui/text/text_entity.h"

namespace {

QString SiteNameFromUrl(const QString &url) {
	const auto u = QUrl(url);
	QString pretty = u.isValid() ? u.toDisplayString() : url;
	const auto m = QRegularExpression(u"^[a-zA-Z0-9]+://"_q).match(pretty);
	if (m.hasMatch()) pretty = pretty.mid(m.capturedLength());
	int32 slash = pretty.indexOf('/');
	if (slash > 0) pretty = pretty.mid(0, slash);
	QStringList components = pretty.split('.', Qt::SkipEmptyParts);
	if (components.size() >= 2) {
		components = components.mid(components.size() - 2);
		return components.at(0).at(0).toUpper()
			+ components.at(0).mid(1)
			+ '.'
			+ components.at(1);
	}
	return QString();
}

WebPageCollage ExtractCollage(
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
	if (type == u"video"_q || !embedUrl.isEmpty()) {
		return WebPageType::Video;
	} else if (type == u"photo"_q) {
		return WebPageType::Photo;
	} else if (type == u"profile"_q) {
		return WebPageType::Profile;
	} else if (type == u"telegram_background"_q) {
		return WebPageType::WallPaper;
	} else if (type == u"telegram_theme"_q) {
		return WebPageType::Theme;
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
	}  else if (type == u"telegram_message"_q) {
		return WebPageType::Message;
	}  else if (type == u"telegram_bot"_q) {
		return WebPageType::Bot;
	}  else if (type == u"telegram_voicechat"_q) {
		return WebPageType::VoiceChat;
	}  else if (type == u"telegram_livestream"_q) {
		return WebPageType::Livestream;
	}  else if (type == u"telegram_user"_q) {
		return WebPageType::User;
	} else if (hasIV) {
		return WebPageType::ArticleWithIV;
	} else {
		return WebPageType::Article;
	}
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
		PhotoData *newPhoto,
		DocumentData *newDocument,
		WebPageCollage &&newCollage,
		int newDuration,
		const QString &newAuthor,
		int newPendingTill) {
	if (newPendingTill != 0
		&& (!url.isEmpty() || pendingTill < 0)
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
			return SiteNameFromUrl(resultUrl);
		}
		return QString();
	}();

	if (type == newType
		&& url == resultUrl
		&& displayUrl == resultDisplayUrl
		&& siteName == resultSiteName
		&& title == resultTitle
		&& description.text == newDescription.text
		&& photo == newPhoto
		&& document == newDocument
		&& collage.items == newCollage.items
		&& duration == newDuration
		&& author == resultAuthor
		&& pendingTill == newPendingTill) {
		return false;
	}
	if (pendingTill > 0 && newPendingTill <= 0) {
		_owner->session().api().clearWebPageRequest(this);
	}
	type = newType;
	url = resultUrl;
	displayUrl = resultDisplayUrl;
	siteName = resultSiteName;
	title = resultTitle;
	description = newDescription;
	photo = newPhoto;
	document = newDocument;
	collage = std::move(newCollage);
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
