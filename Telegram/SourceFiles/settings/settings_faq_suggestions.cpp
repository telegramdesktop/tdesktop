/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_faq_suggestions.h"

#include "apiwrap.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"

namespace Settings {
namespace {

[[nodiscard]] QString ExtractPlainText(const MTPRichText &text) {
	return text.match([&](const MTPDtextPlain &data) {
		return qs(data.vtext());
	}, [&](const MTPDtextConcat &data) {
		auto result = QString();
		for (const auto &part : data.vtexts().v) {
			result += ExtractPlainText(part);
		}
		return result;
	}, [&](const MTPDtextBold &data) {
		return ExtractPlainText(data.vtext());
	}, [&](const MTPDtextItalic &data) {
		return ExtractPlainText(data.vtext());
	}, [&](const MTPDtextUrl &data) {
		return ExtractPlainText(data.vtext());
	}, [&](const auto &) {
		return QString();
	});
}

struct TocTextUrl {
	QString title;
	QString url;
};

[[nodiscard]] std::optional<TocTextUrl> ExtractTextUrl(
		const MTPRichText &text,
		const QString &baseUrl) {
	return text.match([&](const MTPDtextUrl &data) -> std::optional<TocTextUrl> {
		const auto url = qs(data.vurl());
		if (!url.startsWith(baseUrl + '#')) {
			return std::nullopt;
		}
		return TocTextUrl{
			.title = ExtractPlainText(data.vtext()),
			.url = url,
		};
	}, [&](const auto &) -> std::optional<TocTextUrl> {
		return std::nullopt;
	});
}

[[nodiscard]] std::optional<QString> ExtractBoldSection(
		const MTPRichText &text) {
	return text.match([&](const MTPDtextBold &data) -> std::optional<QString> {
		return ExtractPlainText(data.vtext());
	}, [&](const auto &) -> std::optional<QString> {
		return std::nullopt;
	});
}

} // namespace

FaqSuggestions::FaqSuggestions(not_null<Main::Session*> session)
: _session(session) {
}

FaqSuggestions::~FaqSuggestions() {
	if (_requestId) {
		_session->api().request(_requestId).cancel();
	}
}

void FaqSuggestions::request() {
	if (_loaded.current() || _requestId) {
		return;
	}
	auto link = tr::lng_settings_faq_link(tr::now);
	const auto hash = link.indexOf('#');
	const auto url = (hash > 0) ? link.mid(0, hash) : link;

	_requestId = _session->api().request(MTPmessages_GetWebPage(
		MTP_string(url),
		MTP_int(0)
	)).done([=](const MTPmessages_WebPage &result) {
		_requestId = 0;
		result.data().vwebpage().match([&](const MTPDwebPage &data) {
			parse(data);
		}, [&](const auto &) {});
		_loaded = true;
	}).fail([=] {
		_requestId = 0;
		_loaded = true;
	}).send();
}

bool FaqSuggestions::loaded() const {
	return _loaded.current();
}

rpl::producer<bool> FaqSuggestions::loadedValue() const {
	return _loaded.value();
}

const std::vector<FaqEntry> &FaqSuggestions::entries() const {
	return _entries;
}

void FaqSuggestions::parse(const MTPDwebPage &page) {
	const auto cachedPage = page.vcached_page();
	if (!cachedPage) {
		return;
	}
	const auto baseUrl = qs(page.vurl());
	const auto &data = cachedPage->data();

	auto currentSection = QString();
	auto inToc = false;

	for (const auto &block : data.vblocks().v) {
		block.match([&](const MTPDpageBlockParagraph &data) {
			if (const auto section = ExtractBoldSection(data.vtext())) {
				currentSection = *section;
				inToc = true;
			} else {
				inToc = false;
			}
		}, [&](const MTPDpageBlockList &data) {
			if (!inToc || currentSection.isEmpty()) {
				return;
			}
			for (const auto &item : data.vitems().v) {
				item.match([&](const MTPDpageListItemText &data) {
					if (auto entry = ExtractTextUrl(data.vtext(), baseUrl)) {
						_entries.push_back({
							.section = currentSection,
							.title = std::move(entry->title),
							.url = std::move(entry->url),
						});
					}
				}, [&](const auto &) {});
			}
		}, [&](const MTPDpageBlockDivider &) {
			inToc = false;
		}, [&](const MTPDpageBlockHeader &) {
			inToc = false;
		}, [&](const auto &) {});

		if (!inToc && !_entries.empty()) {
			break;
		}
	}
}

} // namespace Settings
