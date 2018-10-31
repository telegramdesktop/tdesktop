/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_web_page.h"

#include "auth_session.h"
#include "apiwrap.h"
#include "mainwidget.h"
#include "data/data_session.h"
#include "data/data_photo.h"
#include "data/data_document.h"
#include "ui/image/image.h"
#include "ui/image/image_source.h"
#include "ui/text/text_entity.h"

namespace {

QString SiteNameFromUrl(const QString &url) {
	QUrl u(url);
	QString pretty = u.isValid() ? u.toDisplayString() : url;
	QRegularExpressionMatch m = QRegularExpression(qsl("^[a-zA-Z0-9]+://")).match(pretty);
	if (m.hasMatch()) pretty = pretty.mid(m.capturedLength());
	int32 slash = pretty.indexOf('/');
	if (slash > 0) pretty = pretty.mid(0, slash);
	QStringList components = pretty.split('.', QString::SkipEmptyParts);
	if (components.size() >= 2) {
		components = components.mid(components.size() - 2);
		return components.at(0).at(0).toUpper() + components.at(0).mid(1) + '.' + components.at(1);
	}
	return QString();
}

WebPageCollage ExtractCollage(
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

	auto &storage = Auth().data();
	for (const auto &photo : photos) {
		storage.photo(photo);
	}
	for (const auto &document : documents) {
		storage.document(document);
	}
	auto result = WebPageCollage();
	result.items.reserve(count);
	for (const auto &item : items) {
		const auto good = item.match([&](const MTPDpageBlockPhoto &data) {
			const auto photo = storage.photo(data.vphoto_id.v);
			if (photo->full->isNull()) {
				return false;
			}
			result.items.push_back(photo);
			return true;
		}, [&](const MTPDpageBlockVideo &data) {
			const auto document = storage.document(data.vvideo_id.v);
			if (!document->isVideoFile()) {
				return false;
			}
			result.items.push_back(document);
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

WebPageCollage ExtractCollage(const MTPDwebPage &data) {
	if (!data.has_cached_page()) {
		return {};
	}
	const auto parseMedia = [&] {
		if (data.has_photo()) {
			Auth().data().photo(data.vphoto);
		}
		if (data.has_document()) {
			Auth().data().document(data.vdocument);
		}
	};
	return data.vcached_page.match([&](const auto &page) {
		for (const auto &block : page.vblocks.v) {
			switch (block.type()) {
			case mtpc_pageBlockPhoto:
			case mtpc_pageBlockVideo:
			case mtpc_pageBlockCover:
			case mtpc_pageBlockEmbed:
			case mtpc_pageBlockEmbedPost:
			case mtpc_pageBlockAudio:
				return WebPageCollage();
			case mtpc_pageBlockSlideshow:
				parseMedia();
				return ExtractCollage(
					block.c_pageBlockSlideshow().vitems.v,
					page.vphotos.v,
					page.vdocuments.v);
			case mtpc_pageBlockCollage:
				parseMedia();
				return ExtractCollage(
					block.c_pageBlockCollage().vitems.v,
					page.vphotos.v,
					page.vdocuments.v);
			default: break;
			}
		}
		return WebPageCollage();
	});
}

} // namespace

WebPageType ParseWebPageType(const MTPDwebPage &page) {
	const auto type = page.has_type() ? qs(page.vtype) : QString();
	if (type == qstr("photo")) return WebPageType::Photo;
	if (type == qstr("video")) return WebPageType::Video;
	if (type == qstr("profile")) return WebPageType::Profile;
	return page.has_cached_page()
		? WebPageType::ArticleWithIV
		: WebPageType::Article;
}

WebPageCollage::WebPageCollage(const MTPDwebPage &data)
: WebPageCollage(ExtractCollage(data)) {
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

	const auto resultUrl = TextUtilities::Clean(newUrl);
	const auto resultDisplayUrl = TextUtilities::Clean(
		newDisplayUrl);
	const auto possibleSiteName = TextUtilities::Clean(
		newSiteName);
	const auto resultTitle = TextUtilities::SingleLine(
		newTitle);
	const auto resultAuthor = TextUtilities::Clean(newAuthor);

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
		Auth().api().clearWebPageRequest(this);
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

	replaceDocumentGoodThumbnail();

	return true;
}

void WebPageData::replaceDocumentGoodThumbnail() {
	if (!document || !photo || !document->goodThumbnail()) {
		return;
	}
	const auto &location = photo->full->location();
	if (!location.isNull()) {
		document->replaceGoodThumbnail(
			std::make_unique<Images::StorageSource>(
				location,
				photo->full->bytesSize()));
	}

}
