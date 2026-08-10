/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/iv_rich_message_html_export.h"

#include "base/base_file_utilities.h"
#include "base/random.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "core/file_utilities.h"
#include "core/mime_type.h"
#include "data/data_cloud_file.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_download_manager.h"
#include "data/data_file_origin.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_session.h"
#include "history/view/history_view_list_widget.h"
#include "history/history.h"
#include "history/history_inner_widget.h"
#include "history/history_item.h"
#include "iv/editor/iv_editor_clipboard.h"
#include "iv/markdown/iv_markdown_prepare_links.h"
#include "iv/markdown/iv_markdown_prepare_serialize.h"
#include "iv/iv_instance.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/image/image.h"
#include "ui/style/style_core.h"
#include "ui/toast/toast.h"
#include "ui/widgets/popup_menu.h"
#include "ui/grouped_layout.h"
#include "window/window_session_controller.h"

#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

#include <QtCore/QBuffer>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QLocale>
#include <QtCore/QMimeData>
#include <QtCore/QUrl>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

namespace Iv {
namespace {

constexpr auto kProgressInterval = crl::time(300);
constexpr auto kMaxFolderNameLength = 48;
constexpr auto kMinBytesForDownloadsEntry = int64(10) * 1024 * 1024;
constexpr auto kClipboardMediaLimit = int64(4) * 1024 * 1024;
constexpr auto kClipboardFormulaLimit = int64(2) * 1024 * 1024;

[[nodiscard]] QString TgMediaSource(
		RichPage::BlockKind kind,
		uint64 photoId,
		uint64 documentId) {
	if (kind == RichPage::BlockKind::Photo) {
		return photoId
			? u" data-tg-src=\"tg://photo?id=%1\""_q.arg(photoId)
			: QString();
	} else if (!documentId) {
		return QString();
	}
	return u" data-tg-src=\"tg://%1?id=%2\""_q.arg(
		((kind == RichPage::BlockKind::Audio) ? u"audio"_q : u"video"_q),
		QString::number(documentId));
}

struct RenderedFormulaImage {
	QString relative;
	QSize logicalSize;
	int logicalDepth = 0;
};

struct HtmlContext {
	const base::flat_map<uint64, QString> *photoPaths = nullptr;
	const base::flat_map<uint64, QString> *documentPaths = nullptr;
	const base::flat_map<uint64, QString> *documentPosters = nullptr;
	Fn<std::optional<RenderedFormulaImage>(
		const QString &tex,
		bool display)> renderFormula;
	bool *slideshowUsed = nullptr;
	bool clipboard = false;
};

void SerializeBlocks(
	QString *out,
	const std::vector<RichPage::Block> &blocks,
	const HtmlContext &context);

[[nodiscard]] QString EscapeHtml(QStringView value) {
	auto result = QString();
	result.reserve(value.size());
	for (const auto ch : value) {
		if (ch == QChar('<')) {
			result += u"&lt;"_q;
		} else if (ch == QChar('>')) {
			result += u"&gt;"_q;
		} else if (ch == QChar('&')) {
			result += u"&amp;"_q;
		} else if (ch == QChar('"')) {
			result += u"&quot;"_q;
		} else if (ch == QChar('\'')) {
			result += u"&#39;"_q;
		} else {
			result += ch;
		}
	}
	return result;
}

void AppendEscaped(QString *out, QStringView value, bool lineBreaks) {
	for (const auto ch : value) {
		if (ch == QChar('\n') && lineBreaks) {
			*out += u"<br>\n"_q;
		} else if (ch == QChar('<')) {
			*out += u"&lt;"_q;
		} else if (ch == QChar('>')) {
			*out += u"&gt;"_q;
		} else if (ch == QChar('&')) {
			*out += u"&amp;"_q;
		} else if (ch == QChar('"')) {
			*out += u"&quot;"_q;
		} else if (ch == QChar('\'')) {
			*out += u"&#39;"_q;
		} else {
			*out += ch;
		}
	}
}

[[nodiscard]] QString SafeUrl(const QString &url) {
	const auto trimmed = url.trimmed();
	if (trimmed.startsWith(QChar('#'))) {
		return QChar('#')
			+ Markdown::NormalizeFragmentId(trimmed.mid(1));
	}
	const auto lower = trimmed.toLower();
	const auto allowed = std::array{
		u"http://"_q,
		u"https://"_q,
		u"tg://"_q,
		u"mailto:"_q,
		u"tel:"_q,
		u"ftp://"_q,
	};
	for (const auto &prefix : allowed) {
		if (lower.startsWith(prefix)) {
			return trimmed;
		}
	}
	return QString();
}

[[nodiscard]] QString FormattedDateFormat(FormattedDateFlags flags) {
	using Flag = FormattedDateFlag;
	if (flags & Flag::Relative) {
		return u"r"_q;
	}
	auto result = QString();
	if (flags & Flag::DayOfWeek) {
		result += QChar('w');
	}
	if (flags & Flag::LongDate) {
		result += QChar('D');
	} else if (flags & Flag::ShortDate) {
		result += QChar('d');
	}
	if (flags & Flag::LongTime) {
		result += QChar('T');
	} else if (flags & Flag::ShortTime) {
		result += QChar('t');
	}
	return result;
}

[[nodiscard]] QString LinkUrl(const QString &data) {
	if (const auto decoded = DecodeRichPageLinkUrl(data)) {
		return decoded->url;
	}
	return data;
}

[[nodiscard]] QString IdAttribute(const QString &anchorId) {
	return anchorId.isEmpty()
		? QString()
		: u" id=\"%1\""_q.arg(EscapeHtml(anchorId));
}

[[nodiscard]] QString AnchorTag(const QString &anchorId) {
	return anchorId.isEmpty()
		? QString()
		: u"<a name=\"%1\"></a>"_q.arg(EscapeHtml(anchorId));
}

[[nodiscard]] QString MediaHref(const QString &relative) {
	if (relative.startsWith(u"data:"_q)) {
		return EscapeHtml(relative);
	}
	return EscapeHtml(QString::fromLatin1(
		QUrl::toPercentEncoding(relative, "/")));
}

void AppendExtraAnchors(QString *out, const RichPage::RichText &text) {
	const auto append = [&](const QString &id) {
		*out += AnchorTag(id);
	};
	append(text.anchorId);
	for (const auto &id : text.anchorIds) {
		append(id);
	}
}

struct EntityTags {
	QString open;
	QString close;
	QString replace;
	bool replaced = false;
	bool rawInner = false;
};

[[nodiscard]] QString AnchorHref(const QString &url) {
	const auto safe = SafeUrl(url);
	return safe.isEmpty()
		? QString()
		: u" href=\"%1\""_q.arg(EscapeHtml(safe));
}

[[nodiscard]] EntityTags LinkTags(const QString &url) {
	const auto href = AnchorHref(url);
	if (href.isEmpty()) {
		return {};
	}
	return { u"<a%1>"_q.arg(href), u"</a>"_q };
}

[[nodiscard]] EntityTags CustomEmojiStickerTags(const EntityInText &entity) {
	const auto id = ::Data::ParseCustomEmojiData(entity.data());
	return id
		? EntityTags{
			.open = u"<tg-emoji emoji-id=\"%1\">"_q.arg(id),
			.close = u"</tg-emoji>"_q,
		}
		: EntityTags();
}

[[nodiscard]] EntityTags CustomEmojiTags(
		const EntityInText &entity,
		QStringView inner,
		const HtmlContext &context) {
	const auto object = Markdown::ParseInlineTextObjectEntity(entity.data());
	if (!object) {
		return {};
	} else if (object->kind == Markdown::InlineTextObjectKind::Formula) {
		const auto data = std::get_if<Markdown::InlineTextObjectFormulaData>(
			&object->data);
		const auto tex = data ? data->trimmedTex : QString();
		if (context.renderFormula && !tex.isEmpty()) {
			if (const auto image = context.renderFormula(tex, false)) {
				return {
					.replace = u"<img class=\"math-inline\" "
						u"data-tg-math=\"%6\" src=\"%1\" "
						u"width=\"%2\" height=\"%3\" "
						u"style=\"vertical-align:%4px\" alt=\"%5\">"_q
						.arg(
							MediaHref(image->relative),
							QString::number(image->logicalSize.width()),
							QString::number(image->logicalSize.height()),
							QString::number(-image->logicalDepth),
							EscapeHtml(tex),
							EscapeHtml(data->copySource.isEmpty()
								? Markdown::InlineFormulaCopySource(tex)
								: data->copySource)),
					.replaced = true,
				};
			}
		}
		return {
			.open = u"<tg-math>"_q,
			.close = u"</tg-math>"_q,
			.rawInner = true,
		};
	}
	const auto data = std::get_if<Markdown::InlineTextObjectIvImageData>(
		&object->data);
	if (!data) {
		return CustomEmojiStickerTags(entity);
	}
	if (context.documentPaths) {
		const auto i = context.documentPaths->find(data->documentId);
		if (i != context.documentPaths->end()) {
			return {
				.replace = u"<img class=\"inline-image\" "
					u"src=\"%1\" alt=\"%2\">"_q
					.arg(
						MediaHref(i->second),
						EscapeHtml(data->replacementText)),
				.replaced = true,
			};
		}
	}
	const auto fallback = data->replacementText.isEmpty()
		? inner.toString()
		: data->replacementText;
	return { .replace = EscapeHtml(fallback), .replaced = true };
}

[[nodiscard]] EntityTags EntityTagsFor(
		const EntityInText &entity,
		QStringView inner,
		const HtmlContext &context) {
	switch (entity.type()) {
	case EntityType::Bold:
	case EntityType::Semibold:
		return { u"<strong>"_q, u"</strong>"_q };
	case EntityType::Italic:
		return { u"<em>"_q, u"</em>"_q };
	case EntityType::Underline:
		return { u"<u>"_q, u"</u>"_q };
	case EntityType::StrikeOut:
		return { u"<s>"_q, u"</s>"_q };
	case EntityType::Code:
		return {
			.open = u"<code>"_q,
			.close = u"</code>"_q,
			.rawInner = true,
		};
	case EntityType::Pre:
		return {
			.open = u"<pre>"_q,
			.close = u"</pre>"_q,
			.rawInner = true,
		};
	case EntityType::Blockquote:
		return { u"<blockquote>"_q, u"</blockquote>"_q };
	case EntityType::Spoiler:
		return { u"<tg-spoiler>"_q, u"</tg-spoiler>"_q };
	case EntityType::Subscript:
		return { u"<sub>"_q, u"</sub>"_q };
	case EntityType::Superscript:
		return { u"<sup>"_q, u"</sup>"_q };
	case EntityType::Marked:
		return { u"<mark>"_q, u"</mark>"_q };
	case EntityType::Url:
		return LinkTags(inner.toString());
	case EntityType::CustomUrl:
		return LinkTags(LinkUrl(entity.data()));
	case EntityType::Email:
		return LinkTags(u"mailto:"_q + inner.toString());
	case EntityType::Phone:
		return LinkTags(u"tel:"_q + inner.toString());
	case EntityType::Mention:
		return LinkTags(u"https://t.me/"_q
			+ inner.toString().mid(inner.startsWith(QChar('@')) ? 1 : 0));
	case EntityType::MentionName: {
		const auto fields = TextUtilities::MentionNameDataToFields(
			entity.data());
		return fields.userId
			? LinkTags(u"tg://user?id=%1"_q.arg(fields.userId))
			: EntityTags();
	}
	case EntityType::CustomEmoji:
		return CustomEmojiTags(entity, inner, context);
	case EntityType::FormattedDate: {
		const auto parsed = DeserializeFormattedDateData(entity.data());
		return {
			.open = u"<tg-time unix=\"%1\" format=\"%2\">"_q.arg(
				QString::number(parsed.first),
				FormattedDateFormat(parsed.second)),
			.close = u"</tg-time>"_q,
		};
	}
	default:
		return {};
	}
}

void SerializeTextPart(
		QString *out,
		const QString &text,
		const EntitiesInText &entities,
		int from,
		int till,
		int entityFrom,
		bool lineBreaks,
		const HtmlContext &context) {
	auto pos = from;
	for (auto i = entityFrom; i != int(entities.size()); ++i) {
		const auto &entity = entities[i];
		if (entity.offset() >= till) {
			break;
		} else if (entity.offset() < pos || entity.length() <= 0) {
			continue;
		}
		const auto end = std::min(entity.offset() + entity.length(), till);
		AppendEscaped(
			out,
			QStringView(text).mid(pos, entity.offset() - pos),
			lineBreaks);
		const auto inner = QStringView(text).mid(
			entity.offset(),
			end - entity.offset());
		const auto tags = EntityTagsFor(entity, inner, context);
		if (tags.replaced) {
			*out += tags.replace;
		} else {
			*out += tags.open;
			SerializeTextPart(
				out,
				text,
				entities,
				entity.offset(),
				end,
				i + 1,
				lineBreaks && !tags.rawInner,
				context);
			*out += tags.close;
		}
		pos = end;
	}
	AppendEscaped(out, QStringView(text).mid(pos, till - pos), lineBreaks);
}

void SerializeText(
		QString *out,
		const TextWithEntities &text,
		const HtmlContext &context,
		bool lineBreaks = true) {
	auto sorted = text.entities;
	std::stable_sort(sorted.begin(), sorted.end(), [](
			const EntityInText &a,
			const EntityInText &b) {
		return (a.offset() < b.offset())
			|| (a.offset() == b.offset() && a.length() > b.length());
	});
	SerializeTextPart(
		out,
		text.text,
		sorted,
		0,
		int(text.text.size()),
		0,
		lineBreaks,
		context);
}

void SerializeRichText(
		QString *out,
		const RichPage::RichText &text,
		const HtmlContext &context,
		bool lineBreaks = true) {
	AppendExtraAnchors(out, text);
	SerializeText(out, text.text, context, lineBreaks);
}

[[nodiscard]] QString FormatExportDate(TimeId date) {
	return date
		? QLocale().toString(
			base::unixtime::parse(date).date(),
			QLocale::LongFormat)
		: QString();
}

void AppendMissingMedia(QString *out, const QString &label = QString()) {
	*out += u"<div class=\"media-missing\">%1</div>"_q.arg(label.isEmpty()
		? EscapeHtml(tr::lng_export_html_media_missing(tr::now))
		: EscapeHtml(label));
}

void SerializeCaption(
		QString *out,
		const RichPage::RichText &caption,
		const HtmlContext &context) {
	if (!caption.text.empty()) {
		*out += u"<figcaption>"_q;
		SerializeRichText(out, caption, context);
		*out += u"</figcaption>"_q;
	}
}

void SerializeMediaItem(
		QString *out,
		RichPage::BlockKind kind,
		uint64 photoId,
		uint64 documentId,
		int width,
		int height,
		bool autoplay,
		bool loop,
		bool spoiler,
		const HtmlContext &context) {
	const auto dimensions = ((width > 0 && height > 0)
		? u" width=\"%1\" height=\"%2\""_q.arg(
			QString::number(width),
			QString::number(height))
		: QString());
	const auto identity = TgMediaSource(kind, photoId, documentId);
	const auto hidden = spoiler ? u" tg-spoiler"_q : QString();
	if (kind == RichPage::BlockKind::Photo) {
		const auto i = context.photoPaths->find(photoId);
		if (i == context.photoPaths->end()) {
			if (!context.clipboard) {
				AppendMissingMedia(out);
			} else if (!identity.isEmpty()) {
				*out += u"<img%1%2%3>"_q.arg(dimensions, identity, hidden);
			}
			return;
		}
		*out += u"<img src=\"%1\" loading=\"lazy\"%2%3%4>"_q.arg(
			MediaHref(i->second),
			dimensions,
			identity,
			hidden);
	} else {
		const auto flags = autoplay
			? u" autoplay muted loop playsinline"_q
			: (loop
				? u" controls loop preload=\"metadata\""_q
				: u" controls preload=\"metadata\""_q);
		const auto poster = [&] {
			if (!context.documentPosters) {
				return QString();
			}
			const auto i = context.documentPosters->find(documentId);
			return (i == context.documentPosters->end())
				? QString()
				: u" poster=\"%1\""_q.arg(MediaHref(i->second));
		}();
		const auto i = context.documentPaths->find(documentId);
		if (i == context.documentPaths->end()) {
			if (!context.clipboard) {
				AppendMissingMedia(out);
			} else if (!identity.isEmpty()) {
				*out += u"<video%1%2%3%4%5></video>"_q.arg(
					poster,
					flags,
					dimensions,
					identity,
					hidden);
			}
			return;
		}
		*out += u"<video src=\"%1\"%2%3%4%5%6></video>"_q.arg(
			MediaHref(i->second),
			poster,
			flags,
			dimensions,
			identity,
			hidden);
	}
}

void SerializeMathBlock(
		QString *out,
		const RichPage::Block &block,
		const HtmlContext &context) {
	const auto tex = block.formula.trimmed();
	if (context.renderFormula && !tex.isEmpty()) {
		if (const auto image = context.renderFormula(tex, true)) {
			*out += u"<div class=\"math\"%1>"
				u"<img class=\"math-image\" data-tg-math=\"%2\" "
				u"src=\"%3\" width=\"%4\" height=\"%5\" alt=\"%2\">"_q
				.arg(
					IdAttribute(block.anchorId),
					EscapeHtml(tex),
					MediaHref(image->relative),
					QString::number(image->logicalSize.width()),
					QString::number(image->logicalSize.height()));
			*out += u"<tg-math-block class=\"math-source\">%1"
				u"</tg-math-block></div>\n"_q.arg(EscapeHtml(tex));
			return;
		}
	}
	*out += u"<tg-math-block%1>%2</tg-math-block>\n"_q
		.arg(IdAttribute(block.anchorId), EscapeHtml(block.formula));
}

void SerializeFillMedia(
		QString *out,
		const RichPage::GroupedMediaItem &item,
		const HtmlContext &context) {
	SerializeMediaItem(
		out,
		item.kind,
		item.photoId,
		item.documentId,
		item.width,
		item.height,
		item.autoplay,
		item.loop,
		item.spoiler,
		context);
}

void SerializeCollage(
		QString *out,
		const RichPage::Block &block,
		const HtmlContext &context) {
	auto sizes = std::vector<QSize>();
	sizes.reserve(block.mediaItems.size());
	for (const auto &item : block.mediaItems) {
		sizes.push_back(QSize(
			std::max(item.width, 1),
			std::max(item.height, 1)));
	}
	const auto layout = Ui::LayoutMediaGroup(
		sizes,
		st::historyGroupWidthMax,
		st::historyGroupWidthMin,
		st::historyGroupSkip);
	auto full = QRect();
	for (const auto &part : layout) {
		full = full.united(part.geometry);
	}
	const auto count = int(std::min(layout.size(), block.mediaItems.size()));
	if (!count || full.isEmpty()) {
		return;
	}
	*out += u"<tg-collage%1>"_q.arg(IdAttribute(block.anchorId));
	*out += u"<div class=\"collage-box\" style=\"aspect-ratio:%1/%2\">"_q
		.arg(QString::number(full.width()), QString::number(full.height()));
	const auto percent = [](int value, int total) {
		return QString::number(value * 100. / total, 'f', 3);
	};
	for (auto i = 0; i != count; ++i) {
		const auto &geometry = layout[i].geometry;
		*out += u"<div class=\"collage-item\" "
			u"style=\"left:%1%;top:%2%;width:%3%;height:%4%\">"_q
			.arg(
				percent(geometry.x() - full.x(), full.width()),
				percent(geometry.y() - full.y(), full.height()),
				percent(geometry.width(), full.width()),
				percent(geometry.height(), full.height()));
		SerializeFillMedia(out, block.mediaItems[i], context);
		*out += u"</div>"_q;
	}
	*out += u"</div>"_q;
	SerializeCaption(out, block.caption, context);
	*out += u"</tg-collage>\n"_q;
}

void SerializeSlideshow(
		QString *out,
		const RichPage::Block &block,
		const HtmlContext &context) {
	if (block.mediaItems.empty()) {
		return;
	}
	auto best = QSize(1, 1);
	auto bestRatio = 0.;
	for (const auto &item : block.mediaItems) {
		const auto width = std::max(item.width, 1);
		const auto height = std::max(item.height, 1);
		const auto ratio = double(width) / height;
		if (ratio > bestRatio) {
			bestRatio = ratio;
			best = QSize(width, height);
		}
	}
	const auto controls = (block.mediaItems.size() > 1);
	if (controls && context.slideshowUsed) {
		*context.slideshowUsed = true;
	}
	*out += u"<tg-slideshow%1>"_q.arg(IdAttribute(block.anchorId));
	*out += u"<div class=\"slideshow-box\" style=\"aspect-ratio:%1/%2\">"_q
		.arg(QString::number(best.width()), QString::number(best.height()));
	*out += u"<div class=\"slideshow-track\">"_q;
	for (const auto &item : block.mediaItems) {
		*out += u"<div class=\"slide\">"_q;
		SerializeFillMedia(out, item, context);
		*out += u"</div>"_q;
	}
	*out += u"</div>"_q;
	if (controls) {
		*out += u"<button class=\"slideshow-prev\" type=\"button\">"
			u"&#8592;</button>"_q;
		*out += u"<button class=\"slideshow-next\" type=\"button\">"
			u"&#8594;</button>"_q;
		*out += u"<div class=\"slideshow-dots\">"_q;
		for (auto i = 0; i != int(block.mediaItems.size()); ++i) {
			*out += u"<button class=\"slideshow-dot\" type=\"button\">"
				u"</button>"_q;
		}
		*out += u"</div>"_q;
	}
	*out += u"</div>"_q;
	SerializeCaption(out, block.caption, context);
	*out += u"</tg-slideshow>\n"_q;
}

void SerializeListItems(
		QString *out,
		const RichPage::Block &block,
		const HtmlContext &context) {
	for (const auto &item : block.listItems) {
		*out += u"<li%1"_q.arg(IdAttribute(item.anchorId));
		if (item.number.value.has_value()) {
			*out += u" value=\"%1\""_q.arg(*item.number.value);
		}
		*out += u">"_q;
		if (item.taskState != RichPage::TaskState::None) {
			*out += u"<input type=\"checkbox\" disabled%1> "_q.arg(
				(item.taskState == RichPage::TaskState::Checked)
					? u" checked"_q
					: QString());
		}
		SerializeRichText(out, item.text, context);
		SerializeBlocks(out, item.blocks, context);
		*out += u"</li>\n"_q;
	}
}

void SerializeList(
		QString *out,
		const RichPage::Block &block,
		const HtmlContext &context) {
	if (block.listKind == RichPage::ListKind::Ordered) {
		auto attributes = IdAttribute(block.anchorId);
		if (block.orderedList.reversed) {
			attributes += u" reversed"_q;
		}
		if (block.orderedList.start.has_value()) {
			attributes += u" start=\"%1\""_q.arg(*block.orderedList.start);
		}
		const auto type = block.orderedList.type.value_or(QString());
		const auto known = std::array{
			u"1"_q,
			u"a"_q,
			u"A"_q,
			u"i"_q,
			u"I"_q,
		};
		for (const auto &value : known) {
			if (type == value) {
				attributes += u" type=\"%1\""_q.arg(type);
				break;
			}
		}
		*out += u"<ol%1>\n"_q.arg(attributes);
		SerializeListItems(out, block, context);
		*out += u"</ol>\n"_q;
	} else {
		*out += u"<ul%1>\n"_q.arg(IdAttribute(block.anchorId));
		SerializeListItems(out, block, context);
		*out += u"</ul>\n"_q;
	}
}

void SerializeTable(
		QString *out,
		const RichPage::Block &block,
		const HtmlContext &context) {
	*out += u"<figure class=\"table-wrap\"%1>"_q.arg(
		IdAttribute(block.anchorId));
	if (!block.text.text.empty()) {
		*out += u"<div class=\"table-title\">"_q;
		SerializeRichText(out, block.text, context);
		*out += u"</div>"_q;
	}
	auto classes = QStringList();
	if (block.bordered) {
		classes.push_back(u"bordered"_q);
	}
	if (block.striped) {
		classes.push_back(u"striped"_q);
	}
	if (block.compact) {
		classes.push_back(u"compact"_q);
	}
	*out += classes.isEmpty()
		? u"<table>\n"_q
		: u"<table class=\"%1\">\n"_q.arg(classes.join(QChar(' ')));
	for (const auto &row : block.tableRows) {
		*out += u"<tr>"_q;
		for (const auto &cell : row.cells) {
			const auto tag = cell.header ? u"th"_q : u"td"_q;
			auto attributes = QString();
			if (cell.colspan > 1) {
				attributes += u" colspan=\"%1\""_q.arg(cell.colspan);
			}
			if (cell.rowspan > 1) {
				attributes += u" rowspan=\"%1\""_q.arg(cell.rowspan);
			}
			auto style = QString();
			if (cell.alignment == RichPage::TableAlignment::Center) {
				style += u"text-align:center;"_q;
			} else if (cell.alignment == RichPage::TableAlignment::Right) {
				style += u"text-align:right;"_q;
			}
			using VerticalAlignment = RichPage::TableVerticalAlignment;
			if (cell.verticalAlignment == VerticalAlignment::Middle) {
				style += u"vertical-align:middle;"_q;
			} else if (cell.verticalAlignment == VerticalAlignment::Bottom) {
				style += u"vertical-align:bottom;"_q;
			}
			if (!style.isEmpty()) {
				attributes += u" style=\"%1\""_q.arg(style);
			}
			*out += u"<%1%2>"_q.arg(tag).arg(attributes);
			SerializeRichText(out, cell.text, context);
			*out += u"</%1>"_q.arg(tag);
		}
		*out += u"</tr>\n"_q;
	}
	*out += u"</table></figure>\n"_q;
}

void SerializeRelatedArticles(
		QString *out,
		const RichPage::Block &block,
		const HtmlContext &context) {
	*out += u"<section class=\"related\"%1>"_q.arg(
		IdAttribute(block.anchorId));
	if (!block.text.text.empty()) {
		*out += u"<div class=\"related-title\">"_q;
		SerializeRichText(out, block.text, context);
		*out += u"</div>"_q;
	}
	for (const auto &article : block.relatedArticles) {
		const auto href = AnchorHref(article.url);
		*out += href.isEmpty()
			? u"<div class=\"related-card\">"_q
			: u"<a class=\"related-card\"%1>"_q.arg(href);
		const auto i = context.photoPaths->find(article.photoId);
		if (i != context.photoPaths->end()) {
			*out += u"<img src=\"%1\">"_q.arg(MediaHref(i->second));
		}
		if (!article.title.isEmpty()) {
			*out += u"<div class=\"related-name\">%1</div>"_q.arg(
				EscapeHtml(article.title));
		}
		if (!article.description.isEmpty()) {
			*out += u"<div class=\"related-desc\">%1</div>"_q.arg(
				EscapeHtml(article.description));
		}
		auto meta = QStringList();
		if (!article.author.isEmpty()) {
			meta.push_back(EscapeHtml(article.author));
		}
		if (const auto date = FormatExportDate(article.publishedDate)
			; !date.isEmpty()) {
			meta.push_back(EscapeHtml(date));
		}
		if (!meta.isEmpty()) {
			*out += u"<div class=\"related-meta\">%1</div>"_q.arg(
				meta.join(u" · "_q));
		}
		*out += href.isEmpty() ? u"</div>\n"_q : u"</a>\n"_q;
	}
	*out += u"</section>\n"_q;
}

void SerializeBlock(
		QString *out,
		const RichPage::Block &block,
		const HtmlContext &context) {
	using Kind = RichPage::BlockKind;
	switch (block.kind) {
	case Kind::Heading: {
		const auto level = std::clamp(block.headingLevel, 1, 6);
		*out += u"<h%1%2>"_q.arg(level).arg(IdAttribute(block.anchorId));
		SerializeRichText(out, block.text, context);
		*out += u"</h%1>\n"_q.arg(level);
	} break;
	case Kind::Paragraph:
		*out += u"<p%1>"_q.arg(IdAttribute(block.anchorId));
		SerializeRichText(out, block.text, context);
		*out += u"</p>\n"_q;
		break;
	case Kind::Footer:
		*out += u"<footer%1>"_q.arg(IdAttribute(block.anchorId));
		SerializeRichText(out, block.text, context);
		*out += u"</footer>\n"_q;
		break;
	case Kind::Thinking:
		*out += u"<blockquote class=\"thinking\"%1>"_q.arg(
			IdAttribute(block.anchorId));
		SerializeRichText(out, block.text, context);
		*out += u"</blockquote>\n"_q;
		break;
	case Kind::AuthorDate: {
		*out += u"<p class=\"byline\"%1>"_q.arg(IdAttribute(block.anchorId));
		SerializeRichText(out, block.text, context);
		if (const auto date = FormatExportDate(block.date)
			; !date.isEmpty()) {
			if (!block.text.text.empty()) {
				*out += u" · "_q;
			}
			*out += EscapeHtml(date);
		}
		*out += u"</p>\n"_q;
	} break;
	case Kind::Code: {
		*out += u"<pre class=\"code\"%1><code%2>"_q
			.arg(IdAttribute(block.anchorId), block.language.isEmpty()
				? QString()
				: u" class=\"language-%1\""_q.arg(
					EscapeHtml(block.language)));
		SerializeRichText(out, block.text, context, false);
		*out += u"</code></pre>\n"_q;
	} break;
	case Kind::Divider:
		*out += u"<hr>\n"_q;
		break;
	case Kind::Anchor:
		*out += AnchorTag(block.anchorId) + QChar('\n');
		break;
	case Kind::List:
		SerializeList(out, block, context);
		break;
	case Kind::Quote: {
		const auto tag = block.pullquote ? u"aside"_q : u"blockquote"_q;
		*out += u"<%1%2>"_q.arg(tag, IdAttribute(block.anchorId));
		SerializeRichText(out, block.text, context);
		SerializeBlocks(out, block.blocks, context);
		if (!block.caption.text.empty()) {
			*out += u"<cite>"_q;
			SerializeRichText(out, block.caption, context);
			*out += u"</cite>"_q;
		}
		*out += u"</%1>\n"_q.arg(tag);
	} break;
	case Kind::Photo: {
		*out += u"<figure class=\"media\"%1>"_q.arg(
			IdAttribute(block.anchorId));
		const auto href = AnchorHref(block.url);
		if (!href.isEmpty()) {
			*out += u"<a%1>"_q.arg(href);
		}
		SerializeMediaItem(
			out,
			Kind::Photo,
			block.photoId,
			0,
			block.width,
			block.height,
			false,
			false,
			block.spoiler,
			context);
		if (!href.isEmpty()) {
			*out += u"</a>"_q;
		}
		SerializeCaption(out, block.caption, context);
		*out += u"</figure>\n"_q;
	} break;
	case Kind::Video:
		*out += u"<figure class=\"media\"%1>"_q.arg(
			IdAttribute(block.anchorId));
		SerializeMediaItem(
			out,
			Kind::Video,
			0,
			block.documentId,
			block.width,
			block.height,
			block.autoplay,
			block.loop,
			block.spoiler,
			context);
		SerializeCaption(out, block.caption, context);
		*out += u"</figure>\n"_q;
		break;
	case Kind::Audio: {
		*out += u"<figure class=\"audio\"%1>"_q.arg(
			IdAttribute(block.anchorId));
		const auto i = context.documentPaths->find(block.documentId);
		const auto identity = TgMediaSource(Kind::Audio, 0, block.documentId);
		if (i != context.documentPaths->end()) {
			*out += u"<audio controls src=\"%1\"%2></audio>"_q.arg(
				MediaHref(i->second),
				identity);
		} else if (!context.clipboard) {
			AppendMissingMedia(out, block.fileName);
		} else if (!identity.isEmpty()) {
			*out += u"<audio controls%1></audio>"_q.arg(identity);
		}
		auto meta = QStringList();
		if (!block.audioPerformer.isEmpty()) {
			meta.push_back(EscapeHtml(block.audioPerformer));
		}
		if (!block.audioTitle.isEmpty()) {
			meta.push_back(EscapeHtml(block.audioTitle));
		}
		if (!meta.isEmpty()) {
			*out += u"<div class=\"audio-meta\">%1</div>"_q.arg(
				meta.join(u" — "_q));
		}
		SerializeCaption(out, block.caption, context);
		*out += u"</figure>\n"_q;
	} break;
	case Kind::GroupedMedia:
		if (block.mediaIntent == RichPage::GroupedMediaIntent::Slideshow) {
			SerializeSlideshow(out, block, context);
		} else {
			SerializeCollage(out, block, context);
		}
		break;
	case Kind::Channel: {
		const auto link = block.username.isEmpty()
			? QString()
			: u"https://t.me/"_q + block.username;
		const auto href = AnchorHref(link);
		*out += u"<div class=\"channel\"%1>"_q.arg(
			IdAttribute(block.anchorId));
		if (!href.isEmpty()) {
			*out += u"<a%1>%2</a>"_q
				.arg(href, EscapeHtml(block.channelTitle));
		} else {
			*out += EscapeHtml(block.channelTitle);
		}
		*out += u"</div>\n"_q;
	} break;
	case Kind::Embed: {
		*out += u"<figure class=\"embed\"%1>"_q.arg(
			IdAttribute(block.anchorId));
		const auto href = AnchorHref(block.url);
		if (!href.isEmpty()) {
			*out += u"<a%1>%2</a>"_q.arg(href, EscapeHtml(block.url));
		} else {
			AppendMissingMedia(out, tr::lng_export_html_embed(tr::now));
		}
		SerializeCaption(out, block.caption, context);
		*out += u"</figure>\n"_q;
	} break;
	case Kind::EmbedPost: {
		*out += u"<blockquote class=\"embed-post\"%1>"_q.arg(
			IdAttribute(block.anchorId));
		auto header = QStringList();
		if (!block.author.isEmpty()) {
			header.push_back(EscapeHtml(block.author));
		}
		if (const auto date = FormatExportDate(block.date)
			; !date.isEmpty()) {
			header.push_back(EscapeHtml(date));
		}
		const auto photo = context.photoPaths->find(block.photoId);
		if (photo != context.photoPaths->end()) {
			*out += u"<img class=\"embed-post-photo\" src=\"%1\">"_q.arg(
				MediaHref(photo->second));
		}
		if (!header.isEmpty()) {
			const auto href = AnchorHref(block.url);
			const auto joined = header.join(u" · "_q);
			*out += u"<div class=\"embed-post-header\">%1</div>"_q.arg(
				href.isEmpty()
					? joined
					: u"<a%1>%2</a>"_q.arg(href, joined));
		}
		SerializeBlocks(out, block.blocks, context);
		SerializeCaption(out, block.caption, context);
		*out += u"</blockquote>\n"_q;
	} break;
	case Kind::Math:
		SerializeMathBlock(out, block, context);
		break;
	case Kind::Table:
		SerializeTable(out, block, context);
		break;
	case Kind::Details:
		*out += u"<details%1%2><summary>"_q
			.arg(
				IdAttribute(block.anchorId),
				(block.open ? u" open"_q : QString()));
		SerializeRichText(out, block.text, context);
		*out += u"</summary>\n"_q;
		SerializeBlocks(out, block.blocks, context);
		*out += u"</details>\n"_q;
		break;
	case Kind::RelatedArticles:
		SerializeRelatedArticles(out, block, context);
		break;
	case Kind::Map: {
		*out += u"<figure class=\"map\"%1>"_q.arg(
			IdAttribute(block.anchorId));
		const auto zoom = (block.zoom > 0) ? block.zoom : 15;
		const auto latitude = QString::number(block.latitude);
		const auto longitude = QString::number(block.longitude);
		const auto url = u"https://www.openstreetmap.org/"
			u"?mlat=%1&mlon=%2#map=%3/%1/%2"_q
			.arg(latitude, longitude, QString::number(zoom));
		*out += u"<tg-map lat=\"%1\" long=\"%2\" zoom=\"%3\"></tg-map>"_q
			.arg(latitude, longitude, QString::number(zoom));
		*out += u"<a%1>%2, %3</a>"_q
			.arg(
				AnchorHref(url),
				EscapeHtml(latitude),
				EscapeHtml(longitude));
		SerializeCaption(out, block.caption, context);
		*out += u"</figure>\n"_q;
	} break;
	case Kind::Unsupported:
		break;
	}
}

void SerializeBlocks(
		QString *out,
		const std::vector<RichPage::Block> &blocks,
		const HtmlContext &context) {
	for (const auto &block : blocks) {
		SerializeBlock(out, block, context);
	}
}

[[nodiscard]] QString ReadHtmlResource(const QString &name) {
	auto file = QFile(u":/export_rich/"_q + name);
	return file.open(QIODevice::ReadOnly)
		? QString::fromUtf8(file.readAll())
		: QString();
}

[[nodiscard]] QByteArray GenerateHtml(
		const RichPage &page,
		const QString &title,
		HtmlContext context) {
	auto slideshowUsed = false;
	context.slideshowUsed = &slideshowUsed;
	auto body = QString();
	SerializeBlocks(&body, page.blocks, context);
	auto result = QString();
	result += u"<!DOCTYPE html>\n<html%1>\n<head>\n"_q.arg(
		page.rtl ? u" dir=\"rtl\""_q : QString());
	result += u"<meta charset=\"utf-8\">\n"_q;
	result += u"<meta name=\"generator\" content=\"%1\">\n"_q.arg(
		RichExportGeneratorMarker());
	result += u"<meta name=\"viewport\" "
		u"content=\"width=device-width, initial-scale=1\">\n"_q;
	result += u"<title>%1</title>\n"_q.arg(EscapeHtml(title));
	result += u"<style>\n"_q
		+ ReadHtmlResource(u"css/rich_message.css"_q)
		+ u"</style>\n"_q;
	result += u"</head>\n<body>\n<article>\n"_q;
	result += body;
	result += u"</article>\n"_q;
	if (slideshowUsed && !context.clipboard) {
		result += u"<script>\n"_q
			+ ReadHtmlResource(u"js/rich_message.js"_q)
			+ u"</script>\n"_q;
	}
	result += u"</body>\n</html>\n"_q;
	return result.toUtf8();
}

[[nodiscard]] QString TitleFromPage(const RichPage &page) {
	for (const auto &block : page.blocks) {
		if (block.kind == RichPage::BlockKind::Heading) {
			const auto text = block.text.text.text.trimmed();
			if (!text.isEmpty()) {
				return text;
			}
		}
	}
	for (const auto &block : page.blocks) {
		if (block.kind == RichPage::BlockKind::Paragraph) {
			const auto text = block.text.text.text.trimmed();
			if (!text.isEmpty()) {
				return text.left(kMaxFolderNameLength).trimmed();
			}
		}
	}
	return QString();
}

[[nodiscard]] QString SanitizeFileNamePart(
		const QString &value,
		const QString &keepExtension = QString()) {
	auto result = base::FileNameFromUserString(value.simplified()).trimmed();
	if (result.size() > kMaxFolderNameLength) {
		const auto tail = keepExtension.isEmpty()
			? QString()
			: base::FileNameFromUserString(keepExtension);
		result = result.left(kMaxFolderNameLength - tail.size()) + tail;
	}
	if (!result.isEmpty() && QChar(result.back()).isHighSurrogate()) {
		result.chop(1);
	}
	while (!result.isEmpty()
		&& (result.endsWith(QChar('.')) || result.endsWith(QChar(' ')))) {
		result.chop(1);
	}
	return result;
}

[[nodiscard]] QString DocumentFileExtension(
		not_null<DocumentData*> document) {
	const auto name = document->filename();
	const auto dot = name.lastIndexOf(QChar('.'));
	if (dot > 0 && dot + 1 < name.size() && name.size() - dot <= 8) {
		return name.mid(dot);
	} else if (document->isVideoFile() || document->isAnimation()) {
		return u".mp4"_q;
	} else if (document->isVoiceMessage()) {
		return u".ogg"_q;
	} else if (document->isSong() || document->isAudioFile()) {
		return u".mp3"_q;
	}
	const auto patterns = Core::MimeTypeForName(
		document->mimeString()).globPatterns();
	for (const auto &pattern : patterns) {
		if (pattern.startsWith(u"*."_q) && pattern.size() <= 10) {
			return pattern.mid(1);
		}
	}
	return u".bin"_q;
}

struct ClipboardMedia {
	base::flat_map<uint64, QString> photoPaths;
	base::flat_map<uint64, QString> documentPaths;
	base::flat_map<uint64, QString> documentPosters;
	int64 budget = kClipboardMediaLimit;
};

[[nodiscard]] QString DataUri(const QByteArray &bytes, const QString &mime) {
	return u"data:%1;base64,%2"_q.arg(
		mime,
		QString::fromLatin1(bytes.toBase64()));
}

[[nodiscard]] QString BytesDataUri(
		const QByteArray &bytes,
		const QString &mime,
		not_null<int64*> budget) {
	if (bytes.isEmpty() || mime.isEmpty() || bytes.size() > *budget) {
		return QString();
	}
	*budget -= bytes.size();
	return DataUri(bytes, mime);
}

[[nodiscard]] QString ImageDataUri(
		const QImage &image,
		not_null<int64*> budget) {
	if (image.isNull()) {
		return QString();
	}
	auto bytes = QByteArray();
	auto buffer = QBuffer(&bytes);
	if (!buffer.open(QIODevice::WriteOnly)
		|| !image.save(&buffer, "JPG", 87)) {
		return QString();
	}
	buffer.close();
	return BytesDataUri(bytes, u"image/jpeg"_q, budget);
}

[[nodiscard]] QString PhotoDataUri(
		PhotoData *photo,
		not_null<int64*> budget) {
	const auto media = photo ? photo->activeMediaView() : nullptr;
	if (!media) {
		return QString();
	}
	const auto sizes = {
		::Data::PhotoSize::Large,
		::Data::PhotoSize::Thumbnail,
		::Data::PhotoSize::Small,
	};
	for (const auto size : sizes) {
		const auto bytes = media->imageBytes(size);
		if (!bytes.isEmpty()) {
			return BytesDataUri(bytes, u"image/jpeg"_q, budget);
		}
	}
	for (const auto size : sizes) {
		if (const auto image = media->image(size)) {
			return ImageDataUri(image->original(), budget);
		}
	}
	return QString();
}

[[nodiscard]] bool EmbeddableMime(const QString &mime) {
	return mime.startsWith(u"image/"_q)
		|| mime.startsWith(u"video/"_q)
		|| mime.startsWith(u"audio/"_q);
}

[[nodiscard]] QString DocumentDataUri(
		DocumentData *document,
		not_null<int64*> budget) {
	if (!document || document->isNull()) {
		return QString();
	}
	const auto mime = document->mimeString();
	if (!EmbeddableMime(mime) || document->size > *budget) {
		return QString();
	}
	const auto media = document->activeMediaView();
	if (media) {
		const auto bytes = media->bytes();
		if (!bytes.isEmpty()) {
			return BytesDataUri(bytes, mime, budget);
		}
	}
	const auto path = document->filepath(true);
	if (path.isEmpty()) {
		return QString();
	}
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly) || file.size() > *budget) {
		return QString();
	}
	return BytesDataUri(file.readAll(), mime, budget);
}

[[nodiscard]] QString DocumentPosterDataUri(
		DocumentData *document,
		not_null<int64*> budget) {
	const auto media = document ? document->activeMediaView() : nullptr;
	if (!media) {
		return QString();
	}
	const auto thumbnail = media->thumbnail();
	return thumbnail
		? ImageDataUri(thumbnail->original(), budget)
		: QString();
}

void CollectClipboardPhoto(
		ClipboardMedia &media,
		PhotoData *photo,
		uint64 photoId) {
	if (!photo || !photoId || media.photoPaths.contains(photoId)) {
		return;
	}
	const auto uri = PhotoDataUri(photo, &media.budget);
	if (!uri.isEmpty()) {
		media.photoPaths.emplace(photoId, uri);
	}
}

void CollectClipboardDocument(
		ClipboardMedia &media,
		DocumentData *document,
		uint64 documentId) {
	if (!document
		|| !documentId
		|| media.documentPaths.contains(documentId)) {
		return;
	}
	const auto uri = DocumentDataUri(document, &media.budget);
	if (!uri.isEmpty()) {
		media.documentPaths.emplace(documentId, uri);
		return;
	} else if (media.documentPosters.contains(documentId)) {
		return;
	}
	const auto poster = DocumentPosterDataUri(document, &media.budget);
	if (!poster.isEmpty()) {
		media.documentPosters.emplace(documentId, poster);
	}
}

void CollectClipboardMediaFromText(
		ClipboardMedia &media,
		Main::Session *session,
		const TextWithEntities &text) {
	if (!session) {
		return;
	}
	for (const auto &entity : text.entities) {
		if (entity.type() != EntityType::CustomEmoji) {
			continue;
		}
		const auto object = Markdown::ParseInlineTextObjectEntity(
			entity.data());
		if (!object
			|| object->kind != Markdown::InlineTextObjectKind::IvImage) {
			continue;
		}
		const auto data = std::get_if<Markdown::InlineTextObjectIvImageData>(
			&object->data);
		if (!data || !data->documentId) {
			continue;
		}
		const auto document = session->data().document(data->documentId);
		if (!document->isNull()) {
			CollectClipboardDocument(media, document, data->documentId);
		}
	}
}

[[nodiscard]] Main::Session *SessionFromBlocks(
		const std::vector<RichPage::Block> &blocks) {
	for (const auto &block : blocks) {
		if (block.photo) {
			return &block.photo->session();
		} else if (block.document) {
			return &block.document->session();
		} else if (block.peer) {
			return &block.peer->session();
		}
		for (const auto &item : block.mediaItems) {
			if (item.photo) {
				return &item.photo->session();
			} else if (item.document) {
				return &item.document->session();
			}
		}
		for (const auto &article : block.relatedArticles) {
			if (article.photo) {
				return &article.photo->session();
			}
		}
		for (const auto &item : block.listItems) {
			if (const auto session = SessionFromBlocks(item.blocks)) {
				return session;
			}
		}
		if (const auto session = SessionFromBlocks(block.blocks)) {
			return session;
		}
	}
	return nullptr;
}

void CollectClipboardMedia(
		ClipboardMedia &media,
		Main::Session *session,
		const std::vector<RichPage::Block> &blocks) {
	using Kind = RichPage::BlockKind;
	for (const auto &block : blocks) {
		CollectClipboardMediaFromText(media, session, block.text.text);
		CollectClipboardMediaFromText(media, session, block.caption.text);
		switch (block.kind) {
		case Kind::Photo:
		case Kind::EmbedPost:
			CollectClipboardPhoto(media, block.photo, block.photoId);
			break;
		case Kind::Video:
		case Kind::Audio:
			CollectClipboardDocument(media, block.document, block.documentId);
			break;
		case Kind::GroupedMedia:
			for (const auto &item : block.mediaItems) {
				if (item.kind == Kind::Photo) {
					CollectClipboardPhoto(media, item.photo, item.photoId);
				} else {
					CollectClipboardDocument(
						media,
						item.document,
						item.documentId);
				}
			}
			break;
		case Kind::RelatedArticles:
			for (const auto &article : block.relatedArticles) {
				CollectClipboardPhoto(media, article.photo, article.photoId);
			}
			break;
		default:
			break;
		}
		for (const auto &item : block.listItems) {
			CollectClipboardMediaFromText(media, session, item.text.text);
			CollectClipboardMedia(media, session, item.blocks);
		}
		for (const auto &row : block.tableRows) {
			for (const auto &cell : row.cells) {
				CollectClipboardMediaFromText(media, session, cell.text.text);
			}
		}
		CollectClipboardMedia(media, session, block.blocks);
	}
}

} // namespace

QByteArray RichBlocksClipboardHtml(
		const RichPageBlocksSlice &slice,
		Main::Session *session) {
	if (slice.empty()) {
		return QByteArray();
	}
	auto media = ClipboardMedia();
	CollectClipboardMedia(
		media,
		session ? session : SessionFromBlocks(slice.blocks),
		slice.blocks);

	auto page = RichPage();
	page.blocks = slice.blocks;
	page.rtl = slice.rtl;

	auto renderer = Markdown::MathRenderer();
	auto formulaBudget = kClipboardFormulaLimit;
	auto formulaCache = base::flat_map<
		QString,
		std::optional<RenderedFormulaImage>>();
	const auto dimensions = Markdown::CaptureMarkdownPrepareDimensions();
	const auto renderFormulaImage = [&](const QString &tex, bool display)
	-> std::optional<RenderedFormulaImage> {
		const auto key = (display ? u"d:"_q : u"i:"_q) + tex;
		const auto cached = formulaCache.find(key);
		if (cached != formulaCache.end()) {
			return cached->second;
		}
		const auto render = [&]() -> std::optional<RenderedFormulaImage> {
			const auto rendered = renderer.renderFormula({
				.trimmedTex = tex,
				.kind = (display
					? Markdown::MathKind::Display
					: Markdown::MathKind::Inline),
				.textSize = (display
					? dimensions.displayMathTextSize
					: dimensions.bodyTextSize),
				.renderWidthCap = dimensions.displayMathMaxRenderWidth,
				.renderHeightCap = dimensions.displayMathMaxRenderHeight,
				.devicePixelRatio = 2,
			});
			if (!rendered.success || rendered.image.isNull()) {
				return std::nullopt;
			}
			const auto colorized = style::colorizeImage(
				rendered.image,
				QColor(0, 0, 0));
			auto bytes = QByteArray();
			auto buffer = QBuffer(&bytes);
			if (!buffer.open(QIODevice::WriteOnly)
				|| !colorized.save(&buffer, "PNG")) {
				return std::nullopt;
			}
			buffer.close();
			const auto uri = BytesDataUri(
				bytes,
				u"image/png"_q,
				&formulaBudget);
			if (uri.isEmpty()) {
				return std::nullopt;
			}
			const auto logical = rendered.logicalSize.isEmpty()
				? (rendered.image.size() / 2)
				: rendered.logicalSize;
			return RenderedFormulaImage{
				.relative = uri,
				.logicalSize = logical,
				.logicalDepth = rendered.logicalDepth,
			};
		};
		const auto result = render();
		formulaCache.emplace(key, result);
		return result;
	};
	return GenerateHtml(page, TitleFromPage(page), {
		.photoPaths = &media.photoPaths,
		.documentPaths = &media.documentPaths,
		.documentPosters = &media.documentPosters,
		.renderFormula = renderFormulaImage,
		.clipboard = true,
	});
}

void SetRichBlocksClipboard(
		const TextForMimeData &text,
		RichPageBlocksSlice slice,
		Main::Session *session) {
	if (slice.empty()) {
		TextUtilities::SetClipboardText(text);
		return;
	}
	const auto html = RichBlocksClipboardHtml(slice, session);
	auto data = Editor::ClipboardBlockData();
	data.blocks = std::move(slice.blocks);
	auto mimeData = Editor::MimeDataFromClipboardData(
		Editor::ClipboardData(std::move(data)));
	if (const auto textMimeData = TextUtilities::MimeDataFromText(text)) {
		for (const auto &format : textMimeData->formats()) {
			mimeData->setData(format, textMimeData->data(format));
		}
	}
	if (!html.isEmpty()) {
		mimeData->setHtml(QString::fromUtf8(html));
	}
	QGuiApplication::clipboard()->setMimeData(mimeData.release());
}

RichMessageHtmlExport::RichMessageHtmlExport(
	not_null<HistoryItem*> item,
	std::shared_ptr<const RichPage> page,
	const QString &basePath,
	base::weak_ptr<Window::SessionController> controller,
	Fn<void()> finished)
: _session(&item->history()->session())
, _itemId(item->fullId())
, _page(std::move(page))
, _basePath(basePath.endsWith(QChar('/')) ? basePath : basePath + QChar('/'))
, _controller(controller)
, _finished(std::move(finished))
, _timer([=] { checkJobs(); }) {
	const auto title = TitleFromPage(*_page);
	_title = title.isEmpty() ? item->history()->peer->name() : title;
}

RichMessageHtmlExport::~RichMessageHtmlExport() {
	if (_settled) {
		return;
	}
	_settled = true;
	stopJobs();
	if (_registered && _fakeItem) {
		Core::App().downloadManager().removeLoadingExternal(_fakeItem);
	}
	cleanupFiles();
}

not_null<Main::Session*> RichMessageHtmlExport::session() const {
	return _session;
}

void RichMessageHtmlExport::start() {
	if (!chooseFolder()) {
		fail();
		return;
	}
	collectJobs();
	if (!_jobs.empty() && !QDir().mkpath(_folder + u"/media"_q)) {
		fail();
		return;
	}
	registerLoading();
	if (_jobs.empty()) {
		finalize();
		return;
	}
	startJobs();
	_session->downloaderTaskFinished(
	) | rpl::on_next([=] {
		checkJobs();
	}, _downloadLifetime);
	_timer.callEach(kProgressInterval);
	checkJobs();
}

bool RichMessageHtmlExport::chooseFolder() {
	if (!QDir().mkpath(_basePath)) {
		return false;
	}
	auto name = SanitizeFileNamePart(_title);
	if (name.isEmpty()) {
		name = u"message"_q;
	}
	for (auto i = 0; i != 100; ++i) {
		const auto candidate = i
			? u"%1 (%2)"_q.arg(name).arg(i + 1)
			: name;
		const auto folder = _basePath + candidate;
		if (!QFileInfo::exists(folder)) {
			if (!QDir().mkpath(folder)) {
				return false;
			}
			_folder = folder;
			_htmlPath = folder + QChar('/') + candidate + u".html"_q;
			return true;
		}
	}
	return false;
}

void RichMessageHtmlExport::collectJobs() {
	collectFromBlocks(_page->blocks);
}

void RichMessageHtmlExport::collectFromBlocks(
		const std::vector<RichPage::Block> &blocks) {
	using Kind = RichPage::BlockKind;
	for (const auto &block : blocks) {
		collectFromText(block.text.text);
		collectFromText(block.caption.text);
		switch (block.kind) {
		case Kind::Photo:
			addPhotoJob(block.photo, block.photoId);
			break;
		case Kind::Video:
			addDocumentJob(block.document, block.documentId, u"video"_q);
			break;
		case Kind::Audio:
			addDocumentJob(block.document, block.documentId, u"audio"_q);
			break;
		case Kind::GroupedMedia:
			for (const auto &item : block.mediaItems) {
				if (item.kind == Kind::Photo) {
					addPhotoJob(item.photo, item.photoId);
				} else if (item.kind == Kind::Video) {
					addDocumentJob(
						item.document,
						item.documentId,
						u"video"_q);
				}
			}
			break;
		case Kind::EmbedPost:
			addPhotoJob(block.photo, block.photoId);
			break;
		case Kind::RelatedArticles:
			for (const auto &article : block.relatedArticles) {
				addPhotoJob(article.photo, article.photoId);
			}
			break;
		default:
			break;
		}
		for (const auto &item : block.listItems) {
			collectFromText(item.text.text);
			collectFromBlocks(item.blocks);
		}
		for (const auto &row : block.tableRows) {
			for (const auto &cell : row.cells) {
				collectFromText(cell.text.text);
			}
		}
		collectFromBlocks(block.blocks);
	}
}

void RichMessageHtmlExport::collectFromText(const TextWithEntities &text) {
	for (const auto &entity : text.entities) {
		if (entity.type() != EntityType::CustomEmoji) {
			continue;
		}
		const auto object = Markdown::ParseInlineTextObjectEntity(
			entity.data());
		if (!object
			|| object->kind != Markdown::InlineTextObjectKind::IvImage) {
			continue;
		}
		const auto data = std::get_if<Markdown::InlineTextObjectIvImageData>(
			&object->data);
		if (!data || !data->documentId) {
			continue;
		}
		const auto document = _session->data().document(data->documentId);
		if (!document->isNull()) {
			addDocumentJob(document, data->documentId, u"image"_q);
		}
	}
}

void RichMessageHtmlExport::addPhotoJob(PhotoData *photo, uint64 photoId) {
	if (!photo || !photoId || _photoPaths.contains(photoId)) {
		return;
	}
	const auto relative = reserveMediaName(
		photoId,
		u"photo_%1.jpg"_q.arg(photoId));
	_photoPaths.emplace(photoId, relative);
	_jobs.push_back({
		.photo = photo,
		.relative = relative,
		.total = std::max(
			int64(photo->imageByteSize(::Data::PhotoSize::Large)),
			int64(1)),
	});
}

void RichMessageHtmlExport::addDocumentJob(
		DocumentData *document,
		uint64 documentId,
		const QString &fallbackPrefix) {
	if (!document
		|| !documentId
		|| document->isNull()
		|| _documentPaths.contains(documentId)) {
		return;
	}
	const auto extension = DocumentFileExtension(document);
	const auto sanitized = SanitizeFileNamePart(
		document->filename(),
		extension);
	const auto name = sanitized.isEmpty()
		? u"%1_%2%3"_q
			.arg(
				fallbackPrefix,
				QString::number(documentId),
				extension)
		: sanitized;
	const auto relative = reserveMediaName(documentId, name);
	_documentPaths.emplace(documentId, relative);
	_jobs.push_back({
		.document = document,
		.relative = relative,
		.total = std::max(document->size, int64(1)),
	});
}

QString RichMessageHtmlExport::reserveMediaName(
		uint64 id,
		const QString &name) {
	auto final = base::FileNameFromUserString(name);
	if (_usedNames.contains(final.toLower())) {
		const auto dot = final.lastIndexOf(QChar('.'));
		final = (dot > 0)
			? u"%1_%2%3"_q.arg(final.left(dot)).arg(id).arg(final.mid(dot))
			: u"%1_%2"_q.arg(final).arg(id);
	}
	_usedNames.emplace(final.toLower());
	return u"media/"_q + final;
}

void RichMessageHtmlExport::registerLoading() {
	auto total = int64();
	for (const auto &job : _jobs) {
		total += job.total;
	}
	if (total < kMinBytesForDownloadsEntry) {
		return;
	}
	_fakeDocument = _session->data().document(
		base::RandomValue<DocumentId>(),
		0, // accessHash
		QByteArray(), // fileReference
		base::unixtime::now(),
		QVector<MTPDocumentAttribute>(
			1,
			MTP_documentAttributeFilename(
				MTP_string(QFileInfo(_htmlPath).fileName()))),
		u"text/html"_q,
		InlineImageLocation(), // inlineThumbnail
		ImageWithLocation(), // thumbnail
		ImageWithLocation(), // videoThumbnail
		false, // isPremiumSticker
		0, // dc
		total);
	auto &manager = Core::App().downloadManager();
	_fakeItem = manager.generateExternalItem(_fakeDocument);
	manager.addLoadingExternal(
		{ .item = _fakeItem, .document = _fakeDocument },
		_folder,
		total,
		crl::guard(this, [=] { cancelFromManager(); }));
	_registered = true;
}

void RichMessageHtmlExport::startJobs() {
	const auto origin = ::Data::FileOrigin(_itemId);
	for (auto &job : _jobs) {
		if (job.photo) {
			job.photoMedia = job.photo->createMediaView();
			job.photoMedia->wanted(::Data::PhotoSize::Large, origin);
		} else if (job.document) {
			startDocumentJob(job);
		}
	}
}

void RichMessageHtmlExport::startDocumentJob(MediaJob &job) {
	const auto target = _folder + QChar('/') + job.relative;
	const auto existing = job.document->filepath(true);
	if (!existing.isEmpty()) {
		job.copying = true;
		const auto weak = base::make_weak(this);
		const auto relative = job.relative;
		crl::async([=] {
			const auto ok = QFile::copy(existing, target);
			crl::on_main(weak, [=] {
				finishCopy(relative, ok);
			});
		});
	} else {
		job.document->save(::Data::FileOrigin(_itemId), target);
	}
}

void RichMessageHtmlExport::finishCopy(const QString &relative, bool ok) {
	for (auto &job : _jobs) {
		if (job.relative == relative) {
			job.copying = false;
			job.done = ok;
			job.failed = !ok;
		}
	}
	checkJobs();
}

void RichMessageHtmlExport::checkJobs() {
	if (_settled) {
		return;
	}
	auto pending = false;
	for (auto &job : _jobs) {
		if (job.done || job.failed) {
			continue;
		} else if (job.copying) {
			pending = true;
		} else if (job.photo) {
			if (job.photoMedia
				&& !job.photoMedia->imageBytes(
					::Data::PhotoSize::Large).isEmpty()) {
				const auto target = _folder + QChar('/') + job.relative;
				job.done = job.photoMedia->saveToFile(target);
				job.failed = !job.done;
				job.photoMedia = nullptr;
			} else if (!job.photo->loading()) {
				job.failed = true;
				job.photoMedia = nullptr;
			} else {
				pending = true;
			}
		} else if (job.document) {
			if (!job.document->loading()) {
				const auto target = _folder + QChar('/') + job.relative;
				job.done = QFile::exists(target);
				job.failed = !job.done;
			} else {
				pending = true;
			}
		}
	}
	if (!pending) {
		finalize();
	} else {
		updateProgress();
	}
}

void RichMessageHtmlExport::updateProgress() {
	if (!_registered || _settled) {
		return;
	}
	auto ready = int64();
	auto total = int64();
	for (const auto &job : _jobs) {
		total += job.total;
		if (job.done || job.failed) {
			ready += job.total;
		} else if (job.photo) {
			ready += std::clamp(
				int64(job.photo->progress() * job.total),
				int64(),
				job.total);
		} else if (job.document) {
			ready += std::clamp(
				job.document->loadOffset(),
				int64(),
				job.total);
		}
	}
	Core::App().downloadManager().updateLoadingExternal(
		_fakeItem,
		ready,
		total);
}

void RichMessageHtmlExport::finalize() {
	if (_settled) {
		return;
	}
	_settled = true;
	_timer.cancel();
	_downloadLifetime.destroy();
	for (const auto &job : _jobs) {
		if (!job.failed) {
			continue;
		}
		for (auto i = _photoPaths.begin(); i != _photoPaths.end();) {
			if (i->second == job.relative) {
				i = _photoPaths.erase(i);
			} else {
				++i;
			}
		}
		for (auto i = _documentPaths.begin(); i != _documentPaths.end();) {
			if (i->second == job.relative) {
				i = _documentPaths.erase(i);
			} else {
				++i;
			}
		}
	}
	auto renderer = Markdown::MathRenderer();
	auto formulaIndex = 0;
	auto formulaCache = base::flat_map<
		QString,
		std::optional<RenderedFormulaImage>>();
	const auto dimensions = Markdown::CaptureMarkdownPrepareDimensions();
	const auto renderFormulaImage = [&](const QString &tex, bool display)
	-> std::optional<RenderedFormulaImage> {
		const auto key = (display ? u"d:"_q : u"i:"_q) + tex;
		const auto cached = formulaCache.find(key);
		if (cached != formulaCache.end()) {
			return cached->second;
		}
		const auto save = [&]() -> std::optional<RenderedFormulaImage> {
			const auto rendered = renderer.renderFormula({
				.trimmedTex = tex,
				.kind = (display
					? Markdown::MathKind::Display
					: Markdown::MathKind::Inline),
				.textSize = (display
					? dimensions.displayMathTextSize
					: dimensions.bodyTextSize),
				.renderWidthCap = dimensions.displayMathMaxRenderWidth,
				.renderHeightCap = dimensions.displayMathMaxRenderHeight,
				.devicePixelRatio = 2,
			});
			if (!rendered.success || rendered.image.isNull()) {
				return std::nullopt;
			}
			if (!QDir().mkpath(_folder + u"/media"_q)) {
				return std::nullopt;
			}
			const auto index = ++formulaIndex;
			const auto relative = reserveMediaName(
				index,
				u"formula_%1.png"_q.arg(index));
			const auto colorized = style::colorizeImage(
				rendered.image,
				QColor(0, 0, 0));
			if (!colorized.save(_folder + QChar('/') + relative, "PNG")) {
				return std::nullopt;
			}
			const auto logical = rendered.logicalSize.isEmpty()
				? (rendered.image.size() / 2)
				: rendered.logicalSize;
			return RenderedFormulaImage{
				.relative = relative,
				.logicalSize = logical,
				.logicalDepth = rendered.logicalDepth,
			};
		};
		const auto result = save();
		formulaCache.emplace(key, result);
		return result;
	};
	const auto html = GenerateHtml(*_page, _title, {
		.photoPaths = &_photoPaths,
		.documentPaths = &_documentPaths,
		.renderFormula = renderFormulaImage,
	});
	auto file = QFile(_htmlPath);
	auto written = file.open(QIODevice::WriteOnly)
		&& (file.write(html) == html.size())
		&& file.flush();
	file.close();
	if (written && file.error() != QFileDevice::NoError) {
		written = false;
	}
	auto &manager = Core::App().downloadManager();
	if (!written) {
		if (_registered) {
			manager.removeLoadingExternal(_fakeItem);
		}
		cleanupFiles();
		showFailToast();
		notifyFinished();
		return;
	}
	if (_registered) {
		const auto info = QFileInfo(_htmlPath);
		_fakeDocument->size = info.size();
		_fakeDocument->setLocation(Core::FileLocation(info));
		manager.finishLoadingExternal(_fakeItem, _htmlPath);
	}
	showDoneToast();
	notifyFinished();
}

void RichMessageHtmlExport::fail() {
	if (_settled) {
		return;
	}
	_settled = true;
	_timer.cancel();
	_downloadLifetime.destroy();
	showFailToast();
	notifyFinished();
}

void RichMessageHtmlExport::cancelFromManager() {
	if (_settled) {
		return;
	}
	_settled = true;
	_timer.cancel();
	_downloadLifetime.destroy();
	stopJobs();
	cleanupFiles();
	notifyFinished();
}

void RichMessageHtmlExport::stopJobs() {
	for (auto &job : _jobs) {
		if (job.done || job.failed || job.copying) {
			continue;
		} else if (job.photo && job.photo->loading()) {
			job.photo->cancel();
		} else if (job.document && job.document->loading()) {
			job.document->cancel();
		}
		job.photoMedia = nullptr;
	}
}

void RichMessageHtmlExport::cleanupFiles() {
	if (!_folder.isEmpty()) {
		QDir(_folder).removeRecursively();
	}
}

void RichMessageHtmlExport::showDoneToast() {
	const auto strong = _controller.get();
	const auto controller = strong
		? strong
		: _session->tryResolveWindow();
	if (!controller) {
		return;
	}
	const auto path = _htmlPath;
	const auto filter = [path](const auto ...) {
		File::ShowInFolder(path);
		return false;
	};
	controller->showToast({
		.text = tr::lng_export_html_saved_to(
			tr::now,
			lt_downloads,
			tr::link(
				tr::lng_mediaview_downloads(tr::now),
				"internal:show_saved_message"),
			tr::marked),
		.filter = filter,
		.iconLottie = u"toast/save_to_gallery"_q,
		.iconLottieSize = st::toastLottieIconSize,
		.st = &st::defaultToast,
	});
}

void RichMessageHtmlExport::showFailToast() {
	const auto strong = _controller.get();
	const auto controller = strong
		? strong
		: _session->tryResolveWindow();
	if (controller) {
		controller->showToast(tr::lng_export_html_failed(tr::now));
	}
}

void RichMessageHtmlExport::notifyFinished() {
	if (const auto finished = _finished) {
		crl::on_main(this, [=] {
			finished();
		});
	}
}

namespace {

void AddSaveRichMessageHtmlActionForItem(
		not_null<Ui::PopupMenu*> menu,
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item,
		Fn<void()> done) {
	if (!item->richPage() || item->forbidsForward()) {
		return;
	}
	const auto itemId = item->fullId();
	menu->addAction(tr::lng_context_save_html(tr::now), [=] {
		Core::App().iv().exportRichMessageHtml(controller, itemId);
		if (done) {
			done();
		}
	}, &st::menuIconExport);
}

} // namespace

void AddSaveRichMessageHtmlAction(
		not_null<Ui::PopupMenu*> menu,
		not_null<Window::SessionController*> controller,
		const std::vector<HistoryView::SelectedItem> &selectedItems,
		not_null<HistoryView::ListWidget*> list) {
	if (selectedItems.size() != 1 || list->hasCopyRestrictionForSelected()) {
		return;
	}
	const auto item = controller->session().data().message(
		selectedItems.front().msgId);
	if (!item) {
		return;
	}
	AddSaveRichMessageHtmlActionForItem(
		menu,
		controller,
		item,
		[weak = base::make_weak(list)] {
			if (const auto strong = weak.get()) {
				strong->cancelSelection();
			}
		});
}

void AddSaveRichMessageHtmlAction(
		not_null<Ui::PopupMenu*> menu,
		not_null<Window::SessionController*> controller,
		const std::vector<not_null<HistoryItem*>> &items,
		not_null<HistoryInner*> list) {
	if (items.size() != 1) {
		return;
	}
	AddSaveRichMessageHtmlActionForItem(
		menu,
		controller,
		items.front(),
		[weak = base::make_weak(list)] {
			if (const auto strong = weak.get()) {
				strong->clearSelected();
			}
		});
}

} // namespace Iv
