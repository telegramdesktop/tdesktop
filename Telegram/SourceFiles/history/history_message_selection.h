/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/markdown/iv_markdown_article.h"
#include "ui/text/text.h"

namespace HistoryView {

enum class MessageSelectionKind : char {
	None,
	Flat,
	RichPage,
};

struct MessageSelectionFlatEndpoint {
	uint16 symbol = 0;
	bool afterSymbol = false;

	[[nodiscard]] uint16 offset() const {
		return uint16(symbol + (afterSymbol ? 1 : 0));
	}

	[[nodiscard]] bool operator==(
		const MessageSelectionFlatEndpoint &other) const = default;
};

struct MessageSelectionEndpoint {
	MessageSelectionKind kind = MessageSelectionKind::None;
	MessageSelectionFlatEndpoint flat;
	Iv::Markdown::MarkdownArticleSelectionPosition richPagePosition;
	Iv::Markdown::MarkdownArticleSelectionEndpoint richPage;

	[[nodiscard]] static MessageSelectionEndpoint Flat(
			MessageSelectionFlatEndpoint value) {
		auto result = MessageSelectionEndpoint();
		result.kind = MessageSelectionKind::Flat;
		result.flat = value;
		return result;
	}

	[[nodiscard]] static MessageSelectionEndpoint RichPage(
			Iv::Markdown::MarkdownArticleSelectionPosition position,
			Iv::Markdown::MarkdownArticleSelectionEndpoint value) {
		auto result = MessageSelectionEndpoint();
		result.kind = MessageSelectionKind::RichPage;
		result.richPagePosition = position;
		result.richPage = value;
		return result;
	}

	[[nodiscard]] bool isFlat() const {
		return (kind == MessageSelectionKind::Flat);
	}

	[[nodiscard]] bool isRichPage() const {
		return (kind == MessageSelectionKind::RichPage);
	}

	[[nodiscard]] bool valid() const {
		return isFlat()
			|| (isRichPage()
				&& richPagePosition.valid()
				&& richPage.valid());
	}
};

struct MessageSelectionRichPage {
	Iv::Markdown::MarkdownArticleSelection selection;
	Iv::Markdown::MarkdownArticleSelectionEndpoints endpoints;

	[[nodiscard]] bool empty() const {
		return selection.empty();
	}
};

[[nodiscard]] inline int CompareMessageSelectionPositions(
		Iv::Markdown::MarkdownArticleSelectionPosition a,
		Iv::Markdown::MarkdownArticleSelectionPosition b) {
	if (a.segment != b.segment) {
		return (a.segment < b.segment) ? -1 : 1;
	} else if (a.offset != b.offset) {
		return (a.offset < b.offset) ? -1 : 1;
	}
	return 0;
}

struct MessageSelection {
	MessageSelectionKind kind = MessageSelectionKind::None;
	TextSelection flat;
	MessageSelectionRichPage richPage;
	MessageSelectionEndpoint anchor;
	MessageSelectionEndpoint focus;

	[[nodiscard]] static MessageSelection Flat(
			TextSelection selection,
			MessageSelectionFlatEndpoint anchor,
			MessageSelectionFlatEndpoint focus) {
		auto result = MessageSelection();
		if (selection.empty()) {
			return result;
		}
		result.kind = MessageSelectionKind::Flat;
		result.flat = selection;
		result.anchor = MessageSelectionEndpoint::Flat(anchor);
		result.focus = MessageSelectionEndpoint::Flat(focus);
		return result;
	}

	[[nodiscard]] static MessageSelection Flat(
			MessageSelectionFlatEndpoint anchor,
			MessageSelectionFlatEndpoint focus) {
		const auto anchorOffset = anchor.offset();
		const auto focusOffset = focus.offset();
		if (anchorOffset == focusOffset) {
			return {};
		}
		return Flat(
			(anchorOffset < focusOffset)
				? TextSelection(anchorOffset, focusOffset)
				: TextSelection(focusOffset, anchorOffset),
			anchor,
			focus);
	}

	[[nodiscard]] static MessageSelection RichPage(
			Iv::Markdown::MarkdownArticleSelection selection,
			Iv::Markdown::MarkdownArticleSelectionEndpoints endpoints,
			Iv::Markdown::MarkdownArticleSelectionPosition anchorPosition,
			Iv::Markdown::MarkdownArticleSelectionPosition focusPosition,
			Iv::Markdown::MarkdownArticleSelectionEndpoint anchor,
			Iv::Markdown::MarkdownArticleSelectionEndpoint focus) {
		auto result = MessageSelection();
		if (selection.empty()
			|| !anchorPosition.valid()
			|| !focusPosition.valid()
			|| !anchor.valid()
			|| !focus.valid()) {
			return result;
		}
		if (CompareMessageSelectionPositions(selection.from, selection.to) > 0) {
			const auto position = selection.from;
			selection.from = selection.to;
			selection.to = position;
			const auto endpoint = endpoints.from;
			endpoints.from = endpoints.to;
			endpoints.to = endpoint;
		}
		result.kind = MessageSelectionKind::RichPage;
		result.richPage.selection = selection;
		result.richPage.endpoints = endpoints;
		result.anchor = MessageSelectionEndpoint::RichPage(
			anchorPosition,
			anchor);
		result.focus = MessageSelectionEndpoint::RichPage(
			focusPosition,
			focus);
		return result;
	}

	[[nodiscard]] bool empty() const {
		return (kind == MessageSelectionKind::Flat)
			? flat.empty()
			: (kind == MessageSelectionKind::RichPage)
			? richPage.empty()
			: true;
	}

	[[nodiscard]] bool isFlat() const {
		return (kind == MessageSelectionKind::Flat) && !flat.empty();
	}

	[[nodiscard]] bool isRichPage() const {
		return (kind == MessageSelectionKind::RichPage)
			&& !richPage.empty();
	}

	[[nodiscard]] TextSelection flatSelection() const {
		return isFlat() ? flat : TextSelection();
	}

	[[nodiscard]] TextSelection flatRangeForEdit() const {
		return flatSelection();
	}

	[[nodiscard]] bool contains(
			const MessageSelectionEndpoint &endpoint) const {
		return isFlat()
			&& endpoint.isFlat()
			&& (endpoint.flat.symbol >= flat.from)
			&& (endpoint.flat.symbol < flat.to);
	}
};

} // namespace HistoryView
