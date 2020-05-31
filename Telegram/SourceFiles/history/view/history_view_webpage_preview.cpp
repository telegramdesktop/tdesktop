/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_webpage_preview.h"

#include "data/data_web_page.h"

namespace HistoryView {

WebPageText TitleAndDescriptionFromWebPage(not_null<WebPageData*> d) {
	QString resultTitle, resultDescription;
	const auto document = d->document;
	const auto photo = d->photo;
	const auto author = d->author;
	const auto siteName = d->siteName;
	const auto title = d->title;
	const auto description = d->description;
	const auto filenameOrUrl = [&] {
		return ((document && !document->filename().isEmpty())
			? document->filename()
			: d->url);
	};
	const auto authorOrFilename = [&] {
		return (author.isEmpty()
			? filenameOrUrl()
			: author);
	};
	const auto descriptionOrAuthor = [&] {
		return (description.text.isEmpty()
			? authorOrFilename()
			: description.text);
	};
	if (siteName.isEmpty()) {
		if (title.isEmpty()) {
			if (description.text.isEmpty()) {
				resultTitle = author;
				resultDescription = filenameOrUrl();
			} else {
				resultTitle = description.text;
				resultDescription = authorOrFilename();
			}
		} else {
			resultTitle = title;
			resultDescription = descriptionOrAuthor();
		}
	} else {
		resultTitle = siteName;
		resultDescription = title.isEmpty()
			? descriptionOrAuthor()
			: title;
	}
	return { resultTitle, resultDescription };
}

} // namespace HistoryView
