/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "data/data_drafts.h"
#include "chat_helpers/message_field.h"
#include "mtproto/sender.h"

class History;

namespace Ui {
class InputField;
} // namespace Ui

namespace HistoryView::Controls {

struct WebPageText {
	QString title;
	QString description;
};

[[nodiscard]] WebPageText TitleAndDescriptionFromWebPage(
	not_null<WebPageData*> data);

bool DrawWebPageDataPreview(
	QPainter &p,
	not_null<WebPageData*> webpage,
	not_null<PeerData*> context,
	QRect to);

[[nodiscard]] bool ShowWebPagePreview(WebPageData *page);
[[nodiscard]] WebPageText ProcessWebPageData(WebPageData *page);

struct WebpageParsed {
	Fn<bool(QPainter &p, QRect to)> drawPreview;
	QString title;
	QString description;

	explicit operator bool() const {
		return drawPreview != nullptr;
	}
};

class WebpageProcessor final : public base::has_weak_ptr {
public:
	WebpageProcessor(
		not_null<History*> history,
		not_null<Ui::InputField*> field);

	void checkNow(bool force);

	// If editing a message without a preview we don't want to show
	// parsed preview until links set is changed in the message.
	//
	// If writing a new message we want to parse links immediately,
	// unless preview was removed in the draft or manual.
	void apply(Data::WebPageDraft draft, bool reparse = true);
	[[nodiscard]] Data::WebPageDraft draft() const;

	[[nodiscard]] rpl::producer<> repaintRequests() const;
	[[nodiscard]] rpl::producer<WebpageParsed> parsedValue() const;

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	void updateFromData();
	void checkPreview();
	void request();

	const not_null<History*> _history;
	MTP::Sender _api;
	MessageLinksParser _parser;

	QStringList _parsedLinks;
	QStringList _links;
	QString _link;
	WebPageData *_data = nullptr;
	base::flat_map<QString, WebPageData*> _cache;
	Data::WebPageDraft _draft;

	mtpRequestId _requestId = 0;

	rpl::event_stream<> _repaintRequests;
	rpl::variable<WebpageParsed> _parsed;

	base::Timer _timer;

	rpl::lifetime _lifetime;

};

} // namespace HistoryView::Controls
