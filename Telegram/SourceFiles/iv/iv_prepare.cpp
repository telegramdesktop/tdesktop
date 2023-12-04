/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/iv_prepare.h"

#include "base/openssl_help.h"
#include "base/unixtime.h"
#include "iv/iv_data.h"
#include "lang/lang_keys.h"
#include "ui/image/image_prepare.h"
#include "styles/palette.h"

#include <QtCore/QSize>

namespace Iv {
namespace {

struct Attribute {
	QByteArray name;
	std::optional<QByteArray> value;
};
using Attributes = std::vector<Attribute>;

struct Photo {
	uint64 id = 0;
	int width = 0;
	int height = 0;
	QByteArray minithumbnail;
};

struct Document {
	uint64 id = 0;
};

class Parser final {
public:
	Parser(const Source &source, const Options &options);

	[[nodiscard]] Prepared result();

private:
	void process(const Source &source);
	void process(const MTPPhoto &photo);
	void process(const MTPDocument &document);

	template <typename Inner>
	[[nodiscard]] QByteArray list(const MTPVector<Inner> &data);

	[[nodiscard]] QByteArray block(const MTPDpageBlockUnsupported &data);
	[[nodiscard]] QByteArray block(const MTPDpageBlockTitle &data);
	[[nodiscard]] QByteArray block(const MTPDpageBlockSubtitle &data);
	[[nodiscard]] QByteArray block(const MTPDpageBlockAuthorDate &data);
	[[nodiscard]] QByteArray block(const MTPDpageBlockHeader &data);
	[[nodiscard]] QByteArray block(const MTPDpageBlockSubheader &data);
	[[nodiscard]] QByteArray block(const MTPDpageBlockParagraph &data);
	[[nodiscard]] QByteArray block(const MTPDpageBlockPreformatted &data);
	[[nodiscard]] QByteArray block(const MTPDpageBlockFooter &data);
	[[nodiscard]] QByteArray block(const MTPDpageBlockDivider &data);
	[[nodiscard]] QByteArray block(const MTPDpageBlockAnchor &data);
	[[nodiscard]] QByteArray block(const MTPDpageBlockList &data);
	[[nodiscard]] QByteArray block(const MTPDpageBlockBlockquote &data);
	[[nodiscard]] QByteArray block(const MTPDpageBlockPullquote &data);
	[[nodiscard]] QByteArray block(const MTPDpageBlockPhoto &data);
	[[nodiscard]] QByteArray block(const MTPDpageBlockVideo &data);
	[[nodiscard]] QByteArray block(const MTPDpageBlockCover &data);
	[[nodiscard]] QByteArray block(const MTPDpageBlockEmbed &data);
	[[nodiscard]] QByteArray block(const MTPDpageBlockEmbedPost &data);
	[[nodiscard]] QByteArray block(const MTPDpageBlockCollage &data);
	[[nodiscard]] QByteArray block(const MTPDpageBlockSlideshow &data);
	[[nodiscard]] QByteArray block(const MTPDpageBlockChannel &data);
	[[nodiscard]] QByteArray block(const MTPDpageBlockAudio &data);
	[[nodiscard]] QByteArray block(const MTPDpageBlockKicker &data);
	[[nodiscard]] QByteArray block(const MTPDpageBlockTable &data);
	[[nodiscard]] QByteArray block(const MTPDpageBlockOrderedList &data);
	[[nodiscard]] QByteArray block(const MTPDpageBlockDetails &data);
	[[nodiscard]] QByteArray block(
		const MTPDpageBlockRelatedArticles &data);
	[[nodiscard]] QByteArray block(const MTPDpageBlockMap &data);

	[[nodiscard]] QByteArray block(const MTPDpageRelatedArticle &data);

	[[nodiscard]] QByteArray block(const MTPDpageTableRow &data);
	[[nodiscard]] QByteArray block(const MTPDpageTableCell &data);

	[[nodiscard]] QByteArray block(const MTPDpageListItemText &data);
	[[nodiscard]] QByteArray block(const MTPDpageListItemBlocks &data);

	[[nodiscard]] QByteArray block(const MTPDpageListOrderedItemText &data);
	[[nodiscard]] QByteArray block(
		const MTPDpageListOrderedItemBlocks &data);

	[[nodiscard]] QByteArray tag(
		const QByteArray &name,
		const QByteArray &body = {});
	[[nodiscard]] QByteArray tag(
		const QByteArray &name,
		const Attributes &attributes,
		const QByteArray &body = {});
	[[nodiscard]] QByteArray utf(const MTPstring &text);
	[[nodiscard]] QByteArray utf(const tl::conditional<MTPstring> &text);
	[[nodiscard]] QByteArray rich(const MTPRichText &text);
	[[nodiscard]] QByteArray plain(const MTPRichText &text);
	[[nodiscard]] QByteArray caption(const MTPPageCaption &caption);

	[[nodiscard]] Photo parse(const MTPPhoto &photo);
	[[nodiscard]] Document parse(const MTPDocument &document);
	[[nodiscard]] Geo parse(const MTPGeoPoint &geo);

	[[nodiscard]] Photo photoById(uint64 id);
	[[nodiscard]] Document documentById(uint64 id);

	[[nodiscard]] QByteArray photoFullUrl(const Photo &photo);
	[[nodiscard]] QByteArray documentFullUrl(const Document &document);
	[[nodiscard]] QByteArray embedUrl(const QByteArray &html);
	[[nodiscard]] QByteArray mapUrl(
		const Geo &geo,
		int width,
		int height,
		int zoom);
	[[nodiscard]] QByteArray resource(QByteArray id);

	const Options _options;

	base::flat_set<QByteArray> _resources;

	Prepared _result;

	bool _rtl = false;
	bool _captionAsTitle = false;
	bool _captionWrapped = false;
	base::flat_map<uint64, Photo> _photosById;
	base::flat_map<uint64, Document> _documentsById;

};

[[nodiscard]] bool IsVoidElement(const QByteArray &name) {
	// Thanks https://developer.mozilla.org/en-US/docs/Glossary/Void_element
	static const auto voids = base::flat_set<QByteArray>{
		"area"_q,
		"base"_q,
		"br"_q,
		"col"_q,
		"embed"_q,
		"hr"_q,
		"img"_q,
		"input"_q,
		"link"_q,
		"meta"_q,
		"source"_q,
		"track"_q,
		"wbr"_q,
	};
	return voids.contains(name);
}

Parser::Parser(const Source &source, const Options &options)
: _options(options) {
	process(source);
	_result.title = source.title;
	_result.rtl = source.page.data().is_rtl();
	_result.content = list(source.page.data().vblocks());
}

Prepared Parser::result() {
	return _result;
}

void Parser::process(const Source &source) {
	const auto &data = source.page.data();
	for (const auto &photo : data.vphotos().v) {
		process(photo);
	}
	for (const auto &document : data.vdocuments().v) {
		process(document);
	}
	if (source.webpagePhoto) {
		process(*source.webpagePhoto);
	}
	if (source.webpageDocument) {
		process(*source.webpageDocument);
	}
}

void Parser::process(const MTPPhoto &photo) {
	_photosById.emplace(
		photo.match([](const auto &data) { return data.vid().v; }),
		parse(photo));
}

void Parser::process(const MTPDocument &document) {
	_documentsById.emplace(
		document.match([](const auto &data) { return data.vid().v; }),
		parse(document));
}

template <typename Inner>
QByteArray Parser::list(const MTPVector<Inner> &data) {
	auto result = QByteArrayList();
	result.reserve(data.v.size());
	for (const auto &item : data.v) {
		result.append(item.match([&](const auto &data) {
			return block(data);
		}));
	}
	return result.join(QByteArray());
}

QByteArray Parser::block(const MTPDpageBlockUnsupported &data) {
	return "Unsupported."_q;
}

QByteArray Parser::block(const MTPDpageBlockTitle &data) {
	return tag("h1", { { "class", "iv-title" } }, rich(data.vtext()));
}

QByteArray Parser::block(const MTPDpageBlockSubtitle &data) {
	return tag("h2", { { "class", "iv-subtitle" } }, rich(data.vtext()));
}

QByteArray Parser::block(const MTPDpageBlockAuthorDate &data) {
	auto inner = rich(data.vauthor());
	if (const auto date = data.vpublished_date().v) {
		const auto parsed = base::unixtime::parse(date);
		inner += " \xE2\x80\xA2 "
			+ tag("time", langDateTimeFull(parsed).toUtf8());
	}
	return tag("address", inner);
}

QByteArray Parser::block(const MTPDpageBlockHeader &data) {
	return tag("h3", { { "class", "iv-header" } }, rich(data.vtext()));
}

QByteArray Parser::block(const MTPDpageBlockSubheader &data) {
	return tag("h4", { { "class", "iv-subheader" } }, rich(data.vtext()));
}

QByteArray Parser::block(const MTPDpageBlockParagraph &data) {
	return tag("p", rich(data.vtext()));
}

QByteArray Parser::block(const MTPDpageBlockPreformatted &data) {
	auto list = Attributes();
	const auto language = utf(data.vlanguage());
	if (!language.isEmpty()) {
		list.push_back({ "data-language", language });
		list.push_back({ "class", "lang-" + language });
		_result.hasCode = true;
	}
	return tag("pre", list, rich(data.vtext()));
}

QByteArray Parser::block(const MTPDpageBlockFooter &data) {
	return tag("footer", { { "class", "iv-footer" } }, rich(data.vtext()));
}

QByteArray Parser::block(const MTPDpageBlockDivider &data) {
	return tag("hr", { { "class", "iv-divider" } });
}

QByteArray Parser::block(const MTPDpageBlockAnchor &data) {
	return tag("a", { { "name", utf(data.vname())} });
}

QByteArray Parser::block(const MTPDpageBlockList &data) {
	return tag("ul", list(data.vitems()));
}

QByteArray Parser::block(const MTPDpageBlockBlockquote &data) {
	const auto caption = rich(data.vcaption());
	const auto cite = caption.isEmpty()
		? QByteArray()
		: tag("cite", caption);
	return tag("blockquote", rich(data.vtext()) + cite);
}

QByteArray Parser::block(const MTPDpageBlockPullquote &data) {
	const auto caption = rich(data.vcaption());
	const auto cite = caption.isEmpty()
		? QByteArray()
		: tag("cite", caption);
	return tag(
		"div",
		{ { "class", "iv-pullquote" } },
		rich(data.vtext()) + cite);
}

QByteArray Parser::block(const MTPDpageBlockPhoto &data) {
	const auto photo = photoById(data.vphoto_id().v);
	if (!photo.id) {
		return "Photo not found.";
	}
	const auto src = photoFullUrl(photo);
	auto wrapStyle = QByteArray();
	if (photo.width) {
		wrapStyle += "max-width:" + QByteArray::number(photo.width) + "px";
	}
	const auto minithumb = Images::ExpandInlineBytes(photo.minithumbnail);
	if (!minithumb.isEmpty()) {
		const auto image = Images::Read({ .content = minithumb });
		wrapStyle += ";background-image:url('data:image/jpeg;base64,"
			+ minithumb.toBase64()
			+ "');";
	}
	const auto dimension = (photo.width && photo.height)
		? (photo.height / float64(photo.width))
		: (3 / 4.);
	const auto paddingTopPercent = int(base::SafeRound(dimension * 100));
	const auto style = "background-image:url('" + src + "');"
		"padding-top:" + QByteArray::number(paddingTopPercent) + "%";
	const auto inner = tag("div", {
		{ "class", "iv-photo" },
		{ "style", style } });
	auto result = tag(
		"div",
		{ { "class", "iv-photo-wrap" }, { "style", wrapStyle } },
		inner);
	if (const auto url = data.vurl()) {
		result = tag("a", { { "href", utf(*url) } }, result);
	}
	auto attributes = Attributes();
	if (_captionAsTitle) {
		const auto caption = plain(data.vcaption().data().vtext());
		const auto credit = plain(data.vcaption().data().vtext());
		if (!caption.isEmpty() || !credit.isEmpty()) {
			const auto title = (!caption.isEmpty() && !credit.isEmpty())
				? (caption + " / " + credit)
				: (caption + credit);
			attributes.push_back({ "title", title });
		}
	} else {
		result += caption(data.vcaption());
	}
	return tag("figure", attributes, result);
}

QByteArray Parser::block(const MTPDpageBlockVideo &data) {
	const auto video = documentById(data.vvideo_id().v);
	if (!video.id) {
		return "Video not found.";
	}
	const auto src = documentFullUrl(video);
	auto vattributes = Attributes{};
	if (data.is_autoplay()) {
		vattributes.push_back({ "preload", "auto" });
		vattributes.push_back({ "autoplay", std::nullopt });
	} else {
		vattributes.push_back({ "controls", std::nullopt });
	}
	if (data.is_loop()) {
		vattributes.push_back({ "loop", std::nullopt });
	}
	vattributes.push_back({ "muted", std::nullopt });
	auto result = tag(
		"video",
		vattributes,
		tag("source", { { "src", src }, { "type", "video/mp4" } }));
	auto attributes = Attributes();
	if (_captionAsTitle) {
		const auto caption = plain(data.vcaption().data().vtext());
		const auto credit = plain(data.vcaption().data().vtext());
		if (!caption.isEmpty() || !credit.isEmpty()) {
			const auto title = (!caption.isEmpty() && !credit.isEmpty())
				? (caption + " / " + credit)
				: (caption + credit);
			attributes.push_back({ "title", title });
		}
	} else {
		result += caption(data.vcaption());
	}
	return tag("figure", attributes, result);
}

QByteArray Parser::block(const MTPDpageBlockCover &data) {
	return tag("figure", data.vcover().match([&](const auto &data) {
		return block(data);
	}));
}

QByteArray Parser::block(const MTPDpageBlockEmbed &data) {
	_result.hasEmbeds = true;
	auto eclass = data.is_full_width() ? QByteArray() : "nowide";
	auto width = QByteArray();
	auto height = QByteArray();
	auto iframeWidth = QByteArray();
	auto iframeHeight = QByteArray();
	const auto autosize = !data.vw();
	if (autosize) {
		iframeWidth = "100%";
		eclass = "nowide";
	} else if (data.is_full_width() || !data.vw()->v) {
		width = "100%";
		height = QByteArray::number(data.vh()->v) + "px";
		iframeWidth = "100%";
		iframeHeight = height;
	} else {
		const auto percent = data.vh()->v * 100 / data.vw()->v;
		width = QByteArray::number(data.vw()->v) + "px";
		height = QByteArray::number(percent) + "%";
	}
	auto attributes = Attributes();
	if (autosize) {
		attributes.push_back({ "class", "autosize" });
	}
	attributes.push_back({ "width", iframeWidth });
	attributes.push_back({ "height", iframeHeight });
	if (const auto url = data.vurl()) {
		if (!autosize) {
			attributes.push_back({ "src", utf(url) });
		} else {
			attributes.push_back({ "srcdoc", utf(url) });
		}
	} else if (const auto html = data.vhtml()) {
		attributes.push_back({ "src", embedUrl(html->v) });
	}
	if (!data.is_allow_scrolling()) {
		attributes.push_back({ "scrolling", "no" });
	}
	attributes.push_back({ "frameborder", "0" });
	attributes.push_back({ "allowtransparency", "true" });
	attributes.push_back({ "allowfullscreen", "true" });
	auto result = tag("iframe", attributes);
	if (!autosize) {
		result = tag("div", {
			{ "class", "iframe-wrap" },
			{ "style", "width:" + width },
		}, tag("div", {
			{ "style", "padding-bottom: " + height },
		}, result));
	}
	result += caption(data.vcaption());
	return tag("figure", { { "class", eclass } }, result);
}

QByteArray Parser::block(const MTPDpageBlockEmbedPost &data) {
	auto result = QByteArray();
	if (!data.vblocks().v.isEmpty()) {
		auto address = QByteArray();
		const auto photo = photoById(data.vauthor_photo_id().v);
		if (photo.id) {
			const auto src = photoFullUrl(photo);
			address += tag(
				"figure",
				{ { "style", "background-image:url('" + src + "')" } });
		}
		address += tag(
			"a",
			{ { "rel", "author" }, { "onclick", "return false;" } },
			utf(data.vauthor()));
		if (const auto date = data.vdate().v) {
			const auto parsed = base::unixtime::parse(date);
			address += tag("time", langDateTimeFull(parsed).toUtf8());
		}
		const auto inner = tag("address", address) + list(data.vblocks());
		result = tag("blockquote", { { "class", "embed-post" } }, inner);
	} else {
		const auto url = utf(data.vurl());
		const auto inner = tag("strong", utf(data.vauthor()))
			+ tag("small", tag("a", { { "href", url } }, url));
		result = tag("section", { { "class", "embed-post" } }, inner);
	}
	result += caption(data.vcaption());
	return tag("figure", result);
}

QByteArray Parser::block(const MTPDpageBlockCollage &data) {
	auto result = tag(
		"figure",
		{ { "class", "collage" } },
		list(data.vitems()));
	return tag("figure", result + caption(data.vcaption()));
}

QByteArray Parser::block(const MTPDpageBlockSlideshow &data) {
	auto inputs = QByteArrayList();
	auto i = 0;
	for (auto i = 0; i != int(data.vitems().v.size()); ++i) {
		auto attributes = Attributes{
			{ "type", "radio" },
			{ "name", "s" },
			{ "value", QByteArray::number(i) },
			{ "onchange", "return IV.slideshowSlide(this);" },
		};
		if (!i) {
			attributes.push_back({ "checked", std::nullopt });
		}
		inputs.append(tag("label", tag("input", attributes, tag("i"))));
	}
	const auto form = tag(
		"form",
		{ { "class", "slideshow-buttons" } },
		tag("fieldset", inputs.join(QByteArray())));
	auto inner = form + tag("figure", {
		{ "class", "slideshow" },
		{ "onclick", "return IV.slideshowSlide(this, 1);" },
	}, list(data.vitems()));
	auto result = tag(
		"figure",
		{ { "class", "slideshow-wrap" } },
		inner);
	return tag("figure", result + caption(data.vcaption()));
}

QByteArray Parser::block(const MTPDpageBlockChannel &data) {
	auto name = QByteArray();
	auto username = QByteArray();
	auto id = data.vchannel().match([](const auto &data) {
		return QByteArray::number(data.vid().v);
	});
	data.vchannel().match([&](const MTPDchannel &data) {
		if (const auto has = data.vusername()) {
			username = utf(*has);
		}
		name = utf(data.vtitle());
	}, [&](const MTPDchat &data) {
		name = utf(data.vtitle());
	}, [](const auto &) {
	});
	auto result = tag(
		"div",
		{ { "class", "join" }, { "data-context", "join_link" + id } },
		tag("span")
	) + tag("h4", name);
	const auto link = username.isEmpty()
		? "javascript:alert('Channel Link');"
		: "https://t.me/" + username;
	result = tag(
		"a",
		{ { "href", link }, { "data-context", "channel" + id } },
		result);
	_result.channelIds.emplace(id);
	return tag("section", {
		{ "class", "channel joined" },
		{ "data-context", "channel" + id },
	}, result);
}

QByteArray Parser::block(const MTPDpageBlockAudio &data) {
	const auto audio = documentById(data.vaudio_id().v);
	if (!audio.id) {
		return "Audio not found.";
	}
	const auto src = documentFullUrl(audio);
	return tag("figure", tag("audio", {
		{ "src", src },
		{ "controls", std::nullopt },
	}) + caption(data.vcaption()));
}

QByteArray Parser::block(const MTPDpageBlockKicker &data) {
	return tag("h6", { { "class", "iv-kicker" } }, rich(data.vtext()));
}

QByteArray Parser::block(const MTPDpageBlockTable &data) {
	auto classes = QByteArrayList();
	if (data.is_bordered()) {
		classes.push_back("bordered");
	}
	if (data.is_striped()) {
		classes.push_back("striped");
	}
	auto attibutes = Attributes();
	if (!classes.isEmpty()) {
		attibutes.push_back({ "class", classes.join(" ") });
	}
	auto title = rich(data.vtitle());
	if (!title.isEmpty()) {
		title = tag("caption", title);
	}
	auto result = tag("table", attibutes, title + list(data.vrows()));
	result = tag("figure", { { "class", "table" } }, result);
	result = tag("figure", { { "class", "table-wrap" } }, result);
	return tag("figure", result);
}

QByteArray Parser::block(const MTPDpageBlockOrderedList &data) {
	return tag("ol", list(data.vitems()));
}

QByteArray Parser::block(const MTPDpageBlockDetails &data) {
	auto attributes = Attributes();
	if (data.is_open()) {
		attributes.push_back({ "open", std::nullopt });
	}
	return tag(
		"details",
		attributes,
		tag("summary", rich(data.vtitle())) + list(data.vblocks()));
}

QByteArray Parser::block(const MTPDpageBlockRelatedArticles &data) {
	const auto result = list(data.varticles());
	if (result.isEmpty()) {
		return QByteArray();
	}
	auto title = rich(data.vtitle());
	if (!title.isEmpty()) {
		title = tag("h4", { { "class", "iv-related-title" } }, title);
	}
	return tag("section", { { "class", "iv-related" } }, title + result);
}

QByteArray Parser::block(const MTPDpageBlockMap &data) {
	const auto geo = parse(data.vgeo());
	if (!geo.access) {
		return "Map not found.";
	}
	const auto width = 650;
	const auto height = std::min(450, (data.vh().v * width / data.vw().v));
	return tag("figure", tag("img", {
		{ "src", mapUrl(geo, width, height, data.vzoom().v) },
	}) + caption(data.vcaption()));
}

QByteArray Parser::block(const MTPDpageRelatedArticle &data) {
	auto result = QByteArray();
	const auto photo = photoById(data.vphoto_id().value_or_empty());
	if (photo.id) {
		const auto src = photoFullUrl(photo);
		result += tag("i", {
			{ "class", "related-link-thumb" },
			{ "style", "background-image:url('" + src + "')" },
		});
	}
	const auto title = data.vtitle();
	const auto description = data.vdescription();
	const auto author = data.vauthor();
	const auto published = data.vpublished_date();
	if (title || description || author || published) {
		auto inner = QByteArray();
		if (title) {
			inner += tag(
				"span",
				{ { "class", "related-link-title" } },
				utf(*title));
		}
		if (description) {
			inner += tag(
				"span",
				{ { "class", "related-link-desc" } },
				utf(*description));
		}
		if (author || published) {
			const auto separator = (author && published) ? ", " : "";
			const auto parsed = base::unixtime::parse(published->v);
			inner += tag(
				"span",
				{ { "class", "related-link-source" } },
				utf(*author) + separator + langDateTimeFull(parsed).toUtf8());
		}
		result += tag("span", {
			{ "class", "related-link-content" },
		}, inner);
	}
	return tag("a", {
		{ "class", "related-link" },
		{ "href", utf(data.vurl()) },
	}, result);
}

QByteArray Parser::block(const MTPDpageTableRow &data) {
	return tag("tr", list(data.vcells()));
}

QByteArray Parser::block(const MTPDpageTableCell &data) {
	const auto text = data.vtext() ? rich(*data.vtext()) : QByteArray();
	auto style = QByteArray();
	if (data.is_align_right()) {
		style += "text-align:right;";
	} else if (data.is_align_center()) {
		style += "text-align:center;";
	} else {
		style += "text-align:left;";
	}
	if (data.is_valign_bottom()) {
		style += "vertical-align:bottom;";
	} else if (data.is_valign_middle()) {
		style += "vertical-align:middle;";
	} else {
		style += "vertical-align:top;";
	}
	auto attributes = Attributes{ { "style", style } };
	if (const auto cs = data.vcolspan()) {
		attributes.push_back({ "colspan", QByteArray::number(cs->v) });
	}
	if (const auto rs = data.vrowspan()) {
		attributes.push_back({ "rowspan", QByteArray::number(rs->v) });
	}
	return tag(data.is_header() ? "th" : "td", attributes, text);
}

QByteArray Parser::block(const MTPDpageListItemText &data) {
	return tag("li", rich(data.vtext()));
}

QByteArray Parser::block(const MTPDpageListItemBlocks &data) {
	return tag("li", list(data.vblocks()));
}

QByteArray Parser::block(const MTPDpageListOrderedItemText &data) {
	return tag("li", { { "value", utf(data.vnum()) } }, rich(data.vtext()));
}

QByteArray Parser::block(const MTPDpageListOrderedItemBlocks &data) {
	return tag(
		"li",
		{ { "value", utf(data.vnum()) } },
		list(data.vblocks()));
}

QByteArray Parser::utf(const MTPstring &text) {
	return text.v;
}

QByteArray Parser::utf(const tl::conditional<MTPstring> &text) {
	return text ? text->v : QByteArray();
}

QByteArray Parser::tag(
		const QByteArray &name,
		const QByteArray &body) {
	return tag(name, {}, body);
}

QByteArray Parser::tag(
		const QByteArray &name,
		const Attributes &attributes,
		const QByteArray &body) {
	auto list = QByteArrayList();
	list.reserve(attributes.size());
	for (auto &[name, value] : attributes) {
		list.push_back(' ' + name + (value ? "=\"" + *value + "\"" : ""));
	}
	const auto serialized = list.join(QByteArray());
	return IsVoidElement(name)
		? ('<' + name + serialized + " />")
		: ('<' + name + serialized + '>' + body + "</" + name + '>');
}

QByteArray Parser::rich(const MTPRichText &text) {
	return text.match([&](const MTPDtextEmpty &data) {
		return QByteArray();
	}, [&](const MTPDtextPlain &data) {
		struct Replacement {
			QByteArray from;
			QByteArray to;
		};
		const auto replacements = std::vector<Replacement>{
			{ "\xE2\x81\xA6", "<span dir=\"ltr\">" },
			{ "\xE2\x81\xA7", "<span dir=\"rtl\">" },
			{ "\xE2\x81\xA8", "<span dir=\"auto\">" },
			{ "\xE2\x81\xA9", "</span>" },
		};
		auto text = utf(data.vtext());
		for (const auto &[from, to] : replacements) {
			text.replace(from, to);
		}
		return text;
	}, [&](const MTPDtextConcat &data) {
		const auto &list = data.vtexts().v;
		auto result = QByteArrayList();
		result.reserve(list.size());
		for (const auto &item : list) {
			result.append(rich(item));
		}
		return result.join(QByteArray());
	}, [&](const MTPDtextImage &data) {
		const auto image = documentById(data.vdocument_id().v);
		if (!image.id) {
			return "Image not found."_q;
		}
		auto attributes = Attributes{
			{ "class", "pic" },
			{ "src", documentFullUrl(image) },
		};
		if (const auto width = data.vw().v) {
			attributes.push_back({ "width", QByteArray::number(width) });
		}
		if (const auto height = data.vh().v) {
			attributes.push_back({ "height", QByteArray::number(height) });
		}
		return tag("img", attributes);
	}, [&](const MTPDtextBold &data) {
		return tag("b", rich(data.vtext()));
	}, [&](const MTPDtextItalic &data) {
		return tag("i", rich(data.vtext()));
	}, [&](const MTPDtextUnderline &data) {
		return tag("u", rich(data.vtext()));
	}, [&](const MTPDtextStrike &data) {
		return tag("s", rich(data.vtext()));
	}, [&](const MTPDtextFixed &data) {
		return tag("code", rich(data.vtext()));
	}, [&](const MTPDtextUrl &data) {
		return tag("a", {
			{ "href", utf(data.vurl()) },
		}, rich(data.vtext()));
	}, [&](const MTPDtextEmail &data) {
		return tag("a", {
			{ "href", "mailto:" + utf(data.vemail()) },
			}, rich(data.vtext()));
	}, [&](const MTPDtextSubscript &data) {
		return tag("sub", rich(data.vtext()));
	}, [&](const MTPDtextSuperscript &data) {
		return tag("sup", rich(data.vtext()));
	}, [&](const MTPDtextMarked &data) {
		return tag("mark", rich(data.vtext()));
	}, [&](const MTPDtextPhone &data) {
		return tag("a", {
			{ "href", "tel:" + utf(data.vphone()) },
		}, rich(data.vtext()));
	}, [&](const MTPDtextAnchor &data) {
		const auto inner = rich(data.vtext());
		return inner.isEmpty()
			? tag("a", { { "name", utf(data.vname()) } })
			: tag(
				"span",
				{ { "class", "reference" } },
				tag("a", { { "name", utf(data.vname()) } }) + inner);
	});
}

QByteArray Parser::plain(const MTPRichText &text) {
	return text.match([&](const MTPDtextEmpty &data) {
		return QByteArray();
	}, [&](const MTPDtextPlain &data) {
		return utf(data.vtext());
	}, [&](const MTPDtextConcat &data) {
		const auto &list = data.vtexts().v;
		auto result = QByteArrayList();
		result.reserve(list.size());
		for (const auto &item : list) {
			result.append(plain(item));
		}
		return result.join(QByteArray());
	}, [&](const MTPDtextImage &data) {
		return QByteArray();
	}, [&](const auto &data) {
		return plain(data.vtext());
	});
}

QByteArray Parser::caption(const MTPPageCaption &caption) {
	auto text = rich(caption.data().vtext());
	const auto credit = rich(caption.data().vcredit());
	if (_captionWrapped && !text.isEmpty()) {
		text = tag("span", text);
	}
	if (!credit.isEmpty()) {
		text += tag("cite", credit);
	} else if (text.isEmpty()) {
		return QByteArray();
	}
	return tag("figcaption", text);
}

Photo Parser::parse(const MTPPhoto &photo) {
	auto result = Photo{
		.id = photo.match([&](const auto &d) { return d.vid().v; }),
	};
	auto sizes = base::flat_map<QByteArray, QSize>();
	photo.match([](const MTPDphotoEmpty &) {
	}, [&](const MTPDphoto &data) {
		for (const auto &size : data.vsizes().v) {
			size.match([&](const MTPDphotoSizeEmpty &data) {
			}, [&](const MTPDphotoSize &data) {
				sizes.emplace(
					data.vtype().v,
					QSize(data.vw().v, data.vh().v));
			}, [&](const MTPDphotoCachedSize &data) {
				sizes.emplace(
					data.vtype().v,
					QSize(data.vw().v, data.vh().v));
			}, [&](const MTPDphotoStrippedSize &data) {
				result.minithumbnail = data.vbytes().v;
			}, [&](const MTPDphotoSizeProgressive &data) {
				sizes.emplace(
					data.vtype().v,
					QSize(data.vw().v, data.vh().v));
			}, [&](const MTPDphotoPathSize &data) {
			});
		}
	});
	for (const auto attempt : { "y", "x", "w" }) {
		const auto i = sizes.find(QByteArray(attempt));
		if (i != end(sizes)) {
			result.width = i->second.width();
			result.height = i->second.height();
			break;
		}
	}
	return result;
}

Document Parser::parse(const MTPDocument &document) {
	auto result = Document{
		.id = document.match([&](const auto &d) { return d.vid().v; }),
	};
	document.match([](const MTPDdocumentEmpty &) {
	}, [&](const MTPDdocument &data) {
	});
	return result;
}

Geo Parser::parse(const MTPGeoPoint &geo) {
	return geo.match([](const MTPDgeoPointEmpty &) {
		return Geo();
	}, [&](const MTPDgeoPoint &data) {
		return Geo{
			.lat = data.vlat().v,
			.lon = data.vlong().v,
			.access = data.vaccess_hash().v,
		};
	});
}

Photo Parser::photoById(uint64 id) {
	const auto i = _photosById.find(id);
	return (i != end(_photosById)) ? i->second : Photo();
}

Document Parser::documentById(uint64 id) {
	const auto i = _documentsById.find(id);
	return (i != end(_documentsById)) ? i->second : Document();
}

QByteArray Parser::photoFullUrl(const Photo &photo) {
	return resource("photo/" + QByteArray::number(photo.id));
}

QByteArray Parser::documentFullUrl(const Document &document) {
	return resource("document/" + QByteArray::number(document.id));
}

QByteArray Parser::embedUrl(const QByteArray &html) {
	auto binary = std::array<uchar, SHA256_DIGEST_LENGTH>{};
	SHA256(
		reinterpret_cast<const unsigned char*>(html.data()),
		html.size(),
		binary.data());
	const auto hex = [](uchar value) -> char {
		return (value >= 10) ? ('a' + (value - 10)) : ('0' + value);
	};
	auto result = QByteArray();
	result.reserve(binary.size() * 2);
	auto index = 0;
	for (const auto byte : binary) {
		result.push_back(hex(byte / 16));
		result.push_back(hex(byte % 16));
	}
	result += ".html";
	_result.embeds.emplace(result, html);
	return resource("html/" + result);
}

QByteArray Parser::mapUrl(const Geo &geo, int width, int height, int zoom) {
	return resource("map/"
		+ GeoPointId(geo) + "&"
		+ QByteArray::number(width) + ","
		+ QByteArray::number(height) + "&"
		+ QByteArray::number(zoom));
}

QByteArray Parser::resource(QByteArray id) {
	const auto toFolder = !_options.saveToFolder.isEmpty();
	if (toFolder && _resources.emplace(id).second) {
		_result.resources.push_back(id);
	}
	return toFolder ? id : ('/' + id);
}

} // namespace

Prepared Prepare(const Source &source, const Options &options) {
	auto parser = Parser(source, options);
	return parser.result();
}

} // namespace Iv
