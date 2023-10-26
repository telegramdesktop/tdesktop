/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_drafts.h"
#include "chat_helpers/message_field.h"
#include "mtproto/sender.h"

class History;

namespace Main {
class Session;
} // namespace Main

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

class WebpageResolver final {
public:
	explicit WebpageResolver(not_null<Main::Session*> session);

	[[nodiscard]] std::optional<WebPageData*> lookup(
			const QString &link) const;
	[[nodiscard]] rpl::producer<QString> resolved() const {
		return _resolved.events();
	}

	[[nodiscard]] QString find(not_null<WebPageData*> page) const;

	void request(const QString &link);
	void cancel(const QString &link);

private:
	const not_null<Main::Session*> _session;
	MTP::Sender _api;
	base::flat_map<QString, WebPageData*> _cache;
	rpl::event_stream<QString> _resolved;

	QString _requestLink;
	mtpRequestId _requestId = 0;

};

class WebpageProcessor final {
public:
	WebpageProcessor(
		not_null<History*> history,
		not_null<Ui::InputField*> field);

	void setDisabled(bool disabled);
	void checkNow(bool force);

	// If editing a message without a preview we don't want to show
	// parsed preview until links set is changed in the message.
	//
	// If writing a new message we want to parse links immediately,
	// unless preview was removed in the draft or manual.
	void apply(Data::WebPageDraft draft, bool reparse = true);
	[[nodiscard]] Data::WebPageDraft draft() const;
	[[nodiscard]] std::shared_ptr<WebpageResolver> resolver() const;
	[[nodiscard]] const std::vector<MessageLinkRange> &links() const;
	[[nodiscard]] QString link() const;

	[[nodiscard]] rpl::producer<> repaintRequests() const;
	[[nodiscard]] rpl::producer<WebpageParsed> parsedValue() const;

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	void updateFromData();
	void checkPreview();

	const not_null<History*> _history;
	const std::shared_ptr<WebpageResolver> _resolver;
	MessageLinksParser _parser;

	QStringList _parsedLinks;
	QStringList _links;
	QString _link;
	WebPageData *_data = nullptr;
	Data::WebPageDraft _draft;

	rpl::event_stream<> _repaintRequests;
	rpl::variable<WebpageParsed> _parsed;

	base::Timer _timer;

	rpl::lifetime _lifetime;

};

} // namespace HistoryView::Controls
