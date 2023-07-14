/*
This file is part of exteraGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/xmdnx/exteraGramDesktop/blob/dev/LEGAL
*/
#pragma once

namespace HistoryView {

struct WebPageText {
	QString title;
	QString description;
};

WebPageText TitleAndDescriptionFromWebPage(not_null<WebPageData*> d);
bool DrawWebPageDataPreview(
	QPainter &p,
	not_null<WebPageData*> webpage,
	not_null<PeerData*> context,
	QRect to);

} // namespace HistoryView
