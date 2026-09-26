/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_prepare_links.h"
#include "ui/basic_click_handlers.h"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QUrl>

#include <algorithm>

namespace Iv::Markdown {
namespace {

[[nodiscard]] bool HasUrlScheme(const QString &target) {
	if (target.isEmpty()) {
		return false;
	}
	const auto colon = target.indexOf(QChar(':'));
	if (colon <= 0) {
		return false;
	}
	const auto slash = target.indexOf(QChar('/'));
	const auto question = target.indexOf(QChar('?'));
	const auto hash = target.indexOf(QChar('#'));
	auto limit = target.size();
	for (const auto value : { slash, question, hash }) {
		if (value >= 0) {
			limit = std::min(limit, value);
		}
	}
	if (colon >= limit) {
		return false;
	}
	if (!target[0].isLetter()) {
		return false;
	}
	for (auto i = 1; i != colon; ++i) {
		const auto ch = target[i];
		if (!ch.isLetterOrNumber() && ch != QChar('+') && ch != QChar('-')
			&& ch != QChar('.')) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] bool LooksLikeWindowsDrivePath(const QString &target) {
	return target.size() >= 2
		&& target[0].isLetter()
		&& target[1] == QChar(':');
}

[[nodiscard]] bool LooksLikeFileUrl(const QString &target) {
	return target.size() >= 5
		&& target.left(5).compare(u"file:"_q, Qt::CaseInsensitive) == 0;
}

[[nodiscard]] bool LooksLikeFilesystemTarget(const QString &target) {
	return target.startsWith(u"/"_q)
		|| target.startsWith(u"\\"_q)
		|| target.startsWith(u"//"_q)
		|| target.startsWith(u"\\\\"_q)
		|| LooksLikeWindowsDrivePath(target)
		|| LooksLikeFileUrl(target);
}

[[nodiscard]] QString ComparablePath(QString path) {
	path = QDir::fromNativeSeparators(QDir::cleanPath(path));
	return path.toLower();
}

[[nodiscard]] bool IsContainedPath(
		const QString &baseDirectory,
		const QString &resolvedPath) {
	const auto base = ComparablePath(baseDirectory);
	const auto resolved = ComparablePath(resolvedPath);
	return (resolved == base) || resolved.startsWith(base + u"/"_q);
}

[[nodiscard]] bool IsEscapableAscii(char ch) {
	const auto value = uchar(ch);
	return (value >= 0x21 && value <= 0x2F)
		|| (value >= 0x3A && value <= 0x40)
		|| (value >= 0x5B && value <= 0x60)
		|| (value >= 0x7B && value <= 0x7E);
}

[[nodiscard]] bool AppendHtmlEntityText(
		const QByteArray &entity,
		QString *result) {
	if (entity == "amp") {
		result->append(QChar('&'));
	} else if (entity == "lt") {
		result->append(QChar('<'));
	} else if (entity == "gt") {
		result->append(QChar('>'));
	} else if (entity == "quot") {
		result->append(QChar('"'));
	} else if (entity == "apos") {
		result->append(QChar('\''));
	} else if (entity.startsWith("#x") || entity.startsWith("#X")) {
		auto ok = false;
		const auto value = entity.mid(2).toUInt(&ok, 16);
		if (!ok || value > 0xFFFF) {
			return false;
		}
		result->append(QChar(ushort(value)));
	} else if (entity.startsWith("#")) {
		auto ok = false;
		const auto value = entity.mid(1).toUInt(&ok, 10);
		if (!ok || value > 0xFFFF) {
			return false;
		}
		result->append(QChar(ushort(value)));
	} else {
		return false;
	}
	return true;
}

[[nodiscard]] QString NormalizeMailtoTarget(const QString &target) {
	const auto parsed = QUrl(target);
	auto result = parsed.path(QUrl::FullyDecoded);
	if (result.isEmpty()) {
		result = target.mid(target.indexOf(QChar(':')) + 1);
		const auto question = result.indexOf(QChar('?'));
		if (question >= 0) {
			result = result.left(question);
		}
		result = QString::fromUtf8(
			QByteArray::fromPercentEncoding(result.toUtf8()));
	}
	while (result.startsWith(u"/"_q)) {
		result.remove(0, 1);
	}
	return result.trimmed();
}

} // namespace

QString ExternalLinkDisplayText(const PreparedLink &link) {
	if (link.entityType == EntityType::Email) {
		return link.target;
	}
	const auto original = QUrl(link.target);
	const auto good = QUrl(original.isValid()
		? original.toEncoded()
		: QString());
	return good.isValid() ? good.toDisplayString() : link.target;
}

std::optional<EntityLinkData> ExternalEntityLinkData(
		const PreparedLink &link) {
	if (link.kind != PreparedLinkKind::External || link.target.isEmpty()) {
		return std::nullopt;
	}
	switch (link.entityType) {
	case EntityType::Url:
	case EntityType::CustomUrl:
	case EntityType::Email:
		return EntityLinkData{
			.text = !link.copyText.isEmpty() ? link.copyText : link.target,
			.data = link.target,
			.type = link.entityType,
			.shown = link.shown,
		};
	default:
		return std::nullopt;
	}
}

QString TooltipForPreparedLink(const PreparedLink &link) {
	if (link.kind != PreparedLinkKind::External
		&& link.kind != PreparedLinkKind::InstantViewPage) {
		return QString();
	} else if (link.entityType == EntityType::CustomUrl
		|| link.shown == EntityLinkShown::Partial) {
		return ExternalLinkDisplayText(link);
	}
	return QString();
}

void NormalizePreparedUrlLink(PreparedLink *result, const QString &target) {
	if (!result) {
		return;
	}
	result->fragment = QString();
	if (target.startsWith(u"mailto:"_q, Qt::CaseInsensitive)) {
		result->target = NormalizeMailtoTarget(target);
		result->copyText = result->target;
		result->entityType = EntityType::Email;
		result->shown = EntityLinkShown::Full;
		return;
	}
	result->target = UrlClickHandler::EncodeForOpening(target);
	result->copyText = result->target;
	result->entityType = EntityType::Url;
	result->shown = EntityLinkShown::Full;
}

void FinalizePreparedUrlLink(
		PreparedLink *link,
		QStringView renderedText) {
	if (!link
		|| (link->entityType != EntityType::Url
			&& link->entityType != EntityType::Email)) {
		return;
	}
	if (renderedText == QStringView(ExternalLinkDisplayText(*link))) {
		return;
	}
	if (link->entityType == EntityType::Email) {
		link->shown = EntityLinkShown::Partial;
		return;
	}
	if (UrlClickHandler::EncodeForOpening(renderedText.toString())
		== link->target) {
		link->shown = EntityLinkShown::Partial;
		return;
	}
	link->entityType = EntityType::CustomUrl;
}

QString InternalLinkData(uint16 index) {
	return u"internal:index"_q + QChar(index);
}

QString NormalizeFragmentId(QString fragment) {
	fragment = QString::fromUtf8(
		QByteArray::fromPercentEncoding(fragment.toUtf8()));
	fragment = fragment.trimmed().toLower();
	while (fragment.startsWith(u"#"_q)) {
		fragment.remove(0, 1);
	}
	return fragment;
}

PreparedLink ClassifiedLink(
		uint16 index,
		QString target,
		const PrepareState *state) {
	auto result = PreparedLink();
	result.index = index;
	if (target.startsWith(QChar('#'))) {
		result.kind = PreparedLinkKind::Anchor;
		result.target = NormalizeFragmentId(target.mid(1));
		return result;
	}
	if (LooksLikeFilesystemTarget(target)) {
		result.kind = PreparedLinkKind::RejectedRelative;
		return result;
	}
	if (HasUrlScheme(target)) {
		result.kind = PreparedLinkKind::External;
		NormalizePreparedUrlLink(&result, target);
		return result;
	}

	auto fragmentIndex = target.indexOf(QChar('#'));
	if (fragmentIndex >= 0) {
		result.fragment = NormalizeFragmentId(target.mid(fragmentIndex + 1));
		target = target.left(fragmentIndex);
	}
	result.target = target;

	if (target.isEmpty()) {
		result.kind = PreparedLinkKind::Anchor;
		result.target = result.fragment;
		result.fragment = QString();
		return result;
	}
	if (!state
		|| !state->request
		|| state->request->sourcePath.isEmpty()) {
		result.kind = PreparedLinkKind::RejectedRelative;
		return result;
	}
	if (target.contains(QChar('?'))) {
		result.kind = PreparedLinkKind::RejectedRelative;
		return result;
	}
	const auto baseDirectory = QFileInfo(state->request->sourcePath).absolutePath();
	if (baseDirectory.isEmpty()) {
		result.kind = PreparedLinkKind::RejectedRelative;
		return result;
	}
	const auto resolved = QDir(baseDirectory).absoluteFilePath(target);
	const auto cleanedResolved = QDir::cleanPath(resolved);
	if (!IsContainedPath(baseDirectory, cleanedResolved)
		|| !LooksLikeMarkdownFile(cleanedResolved)) {
		result.kind = PreparedLinkKind::RejectedRelative;
		return result;
	}
	result.kind = PreparedLinkKind::LocalFile;
	result.target = cleanedResolved;
	return result;
}

QString FirstInfoToken(const QString &info) {
	const auto trimmed = info.trimmed();
	for (auto i = 0; i != trimmed.size(); ++i) {
		if (trimmed[i].isSpace()) {
			return trimmed.left(i);
		}
	}
	return trimmed;
}

void SortEntities(TextWithEntities *text) {
	auto &entities = text->entities;
	std::sort(
		entities.begin(),
		entities.end(),
		[](const EntityInText &left, const EntityInText &right) {
			if (left.offset() != right.offset()) {
				return left.offset() < right.offset();
			} else if (left.length() != right.length()) {
				return left.length() > right.length();
			}
			return int(left.type()) < int(right.type());
		});
}

QString DecodeMarkdownTextPrefix(QByteArray bytes) {
	auto result = QString();
	auto plainFrom = 0;
	const auto flushPlain = [&](int till) {
		if (till > plainFrom) {
			result.append(QString::fromUtf8(
				bytes.constData() + plainFrom,
				till - plainFrom));
		}
	};
	for (auto i = 0; i != bytes.size();) {
		if (bytes[i] == '\\'
			&& (i + 1) < bytes.size()
			&& IsEscapableAscii(bytes[i + 1])) {
			flushPlain(i);
			result.append(QChar(ushort(uchar(bytes[i + 1]))));
			i += 2;
			plainFrom = i;
		} else if (bytes[i] == '&') {
			const auto semicolon = bytes.indexOf(';', i + 1);
			if (semicolon > i && semicolon - i <= 32) {
				auto entityText = QString();
				if (AppendHtmlEntityText(
						bytes.mid(i + 1, semicolon - i - 1),
						&entityText)) {
					flushPlain(i);
					result.append(entityText);
					i = semicolon + 1;
					plainFrom = i;
					continue;
				}
			}
			++i;
		} else {
			++i;
		}
	}
	flushPlain(bytes.size());
	return result;
}

} // namespace Iv::Markdown
