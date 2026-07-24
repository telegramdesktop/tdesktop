/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/markdown/iv_markdown_prepare_state.h"

#include <QtCore/QStringView>

#include <optional>

namespace Iv::Markdown {

[[nodiscard]] QString InternalLinkData(uint16 index);
[[nodiscard]] QString NormalizeFragmentId(QString fragment);
[[nodiscard]] QString ExternalLinkDisplayText(const PreparedLink &link);
[[nodiscard]] std::optional<EntityLinkData> ExternalEntityLinkData(
	const PreparedLink &link);
[[nodiscard]] QString TooltipForPreparedLink(const PreparedLink &link);
void NormalizePreparedUrlLink(PreparedLink *result, const QString &target);
void FinalizePreparedUrlLink(PreparedLink *link, QStringView renderedText);
[[nodiscard]] PreparedLink ClassifiedLink(
	uint16 index,
	QString target,
	const PrepareState *state);
void SortEntities(TextWithEntities *text);
[[nodiscard]] QString FirstInfoToken(const QString &info);
[[nodiscard]] QString DecodeMarkdownTextPrefix(QByteArray bytes);

} // namespace Iv::Markdown
