/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_prepare_inline.h"
#include "iv/markdown/iv_markdown_prepare_links.h"
#include "iv/markdown/iv_markdown_prepare_serialize.h"
#include "ui/basic_click_handlers.h"

#include <algorithm>
#include <limits>

namespace Iv::Markdown {
namespace {

struct InlineFormulaSource {
	int formulaIndex = -1;
	SourceRange range;
	QString copySource;
};

struct InlineFormulaContext {
	const std::vector<InlineFormulaSource> *formulas = nullptr;
	QString *blockAnchorId = nullptr;
	int next = 0;
	int textSize = 0;
	int renderWidthCap = 0;
	int renderHeightCap = 0;
};

enum class RawInlineTag {
	None,
	SubOpen,
	SubClose,
	SupOpen,
	SupClose,
	MarkOpen,
	MarkClose,
};

void FinalizePreparedExternalLink(
		PreparedLink *link,
		QStringView renderedText) {
	if (!link
		|| link->kind != PreparedLinkKind::External
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

[[nodiscard]] bool RangeContains(
		const SourceRange &outer,
		const SourceRange &inner) {
	return outer.available
		&& inner.available
		&& outer.startOffset <= inner.startOffset
		&& outer.endOffset >= inner.endOffset;
}

struct DecodedDisplaySpan {
	int offset = -1;
	int length = 0;
};

[[nodiscard]] int DisplayOffsetForSourceOffset(
		const MarkdownNode &node,
		const QString &value,
		int sourceOffset,
		const PrepareState *state) {
	if (!state
		|| !node.range.available
		|| sourceOffset < node.range.startOffset
		|| sourceOffset > node.range.endOffset) {
		return -1;
	}
	const auto prefixSize = sourceOffset - node.range.startOffset;
	if (!prefixSize) {
		return 0;
	}
	const auto prefix = state->sourceUtf8.mid(
		node.range.startOffset,
		prefixSize);
	const auto displayPrefix = DecodeMarkdownTextPrefix(prefix);
	return value.startsWith(displayPrefix) ? displayPrefix.size() : -1;
}

[[nodiscard]] DecodedDisplaySpan DisplaySpanForSourceRange(
		const MarkdownNode &node,
		const QString &value,
		const SourceRange &range,
		const PrepareState *state) {
	auto result = DecodedDisplaySpan();
	if (!state
		|| !node.range.available
		|| !range.available
		|| range.startOffset < node.range.startOffset
		|| range.endOffset < range.startOffset
		|| range.endOffset > node.range.endOffset) {
		return result;
	}
	const auto offset = DisplayOffsetForSourceOffset(
		node,
		value,
		range.startOffset,
		state);
	if (offset < 0) {
		return result;
	}
	const auto decoded = DecodeMarkdownTextPrefix(
		state->sourceUtf8.mid(
			range.startOffset,
			range.endOffset - range.startOffset));
	const auto length = int(decoded.size());
	if ((offset + length) > value.size()
		|| value.mid(offset, length) != decoded) {
		return result;
	}
	result.offset = offset;
	result.length = length;
	return result;
}

[[nodiscard]] std::vector<InlineFormulaSource> CollectInlineFormulas(
		const MarkdownNode &node,
		PrepareState *state) {
	auto result = std::vector<InlineFormulaSource>();
	if (!state
		|| !state->request
		|| !state->request->document
		|| !node.range.available) {
		return result;
	}
	const auto &formulas = state->request->document->formulas;
	for (auto i = 0, count = int(formulas.size()); i != count; ++i) {
		const auto &formula = formulas[i];
		if (formula.kind != MathKind::Inline
			|| !RangeContains(node.range, formula.range)) {
			continue;
		}
		auto copySource = state->formulaSourceText(i);
		if (copySource.isEmpty()) {
			copySource = u"$"_q + formula.tex + u"$"_q;
		}
		result.push_back({
			.formulaIndex = i,
			.range = formula.range,
			.copySource = std::move(copySource),
		});
	}
	return result;
}

void ReplaceInlineFormulasInAppendedText(
		const MarkdownNode &node,
		const QString &value,
		int from,
		TextWithEntities *text,
		InlineFormulaContext *inlineFormulas,
		PrepareState *state) {
	if (!text
		|| !inlineFormulas
		|| !inlineFormulas->formulas
		|| !state
		|| inlineFormulas->next >= int(inlineFormulas->formulas->size())
		|| !node.range.available) {
		return;
	}
	auto removedLength = 0;
	while (inlineFormulas->next < int(inlineFormulas->formulas->size())) {
		const auto &formula = (*inlineFormulas->formulas)[inlineFormulas->next];
		if (formula.range.endOffset <= node.range.startOffset) {
			++inlineFormulas->next;
			continue;
		} else if (!RangeContains(node.range, formula.range)) {
			break;
		}

		const auto displaySpan = DisplaySpanForSourceRange(
			node,
			value,
			formula.range,
			state);
		if (displaySpan.offset < 0 || displaySpan.length <= 0) {
			++inlineFormulas->next;
			continue;
		}
		const auto found = from + displaySpan.offset - removedLength;
		text->text.replace(
			found,
			displaySpan.length,
			QString(QChar::ObjectReplacementCharacter));
		const auto &source = state->request->document->formulas[formula.formulaIndex];
		const auto entityData = SerializeInlineTextObjectEntity({
			.kind = InlineTextObjectKind::Formula,
			.data = InlineTextObjectFormulaData{
				.copySource = formula.copySource,
				.trimmedTex = source.tex.trimmed(),
			},
		});
		if (!entityData.isEmpty()) {
			text->entities.push_back(EntityInText(
				EntityType::CustomEmoji,
				found,
				1,
				entityData));
		}
		removedLength += (displaySpan.length - 1);

		state->rememberFormula(
			formula.formulaIndex,
			MathKind::Inline,
			source.tex,
			inlineFormulas->textSize,
			inlineFormulas->renderWidthCap,
			inlineFormulas->renderHeightCap);
		++inlineFormulas->next;
	}
}

void AppendTextWithInlineFormulas(
		const MarkdownNode &node,
		const QString &value,
		TextWithEntities *text,
		InlineFormulaContext *inlineFormulas,
		PrepareState *state) {
	const auto from = text->text.size();
	text->append(value);
	ReplaceInlineFormulasInAppendedText(
		node,
		value,
		from,
		text,
		inlineFormulas,
		state);
}

[[nodiscard]] RawInlineTag ParseRawInlineTag(const MarkdownNode &node) {
	if (node.kind != NodeKind::HtmlInline) {
		return RawInlineTag::None;
	} else if (node.raw == u"<sub>"_q) {
		return RawInlineTag::SubOpen;
	} else if (node.raw == u"</sub>"_q) {
		return RawInlineTag::SubClose;
	} else if (node.raw == u"<sup>"_q) {
		return RawInlineTag::SupOpen;
	} else if (node.raw == u"</sup>"_q) {
		return RawInlineTag::SupClose;
	} else if (node.raw == u"<mark>"_q) {
		return RawInlineTag::MarkOpen;
	} else if (node.raw == u"</mark>"_q) {
		return RawInlineTag::MarkClose;
	}
	return RawInlineTag::None;
}

[[nodiscard]] bool IsSimpleRawInlineLineBreak(const MarkdownNode &node) {
	return (node.kind == NodeKind::HtmlInline) && (node.raw == u"<br>"_q);
}

[[nodiscard]] bool IsOpeningRawInlineTag(RawInlineTag tag) {
	return (tag == RawInlineTag::SubOpen)
		|| (tag == RawInlineTag::SupOpen)
		|| (tag == RawInlineTag::MarkOpen);
}

[[nodiscard]] RawInlineTag MatchingClosingRawInlineTag(RawInlineTag tag) {
	switch (tag) {
	case RawInlineTag::SubOpen: return RawInlineTag::SubClose;
	case RawInlineTag::SupOpen: return RawInlineTag::SupClose;
	case RawInlineTag::MarkOpen: return RawInlineTag::MarkClose;
	default: return RawInlineTag::None;
	}
}

[[nodiscard]] EntityType EntityTypeForRawInlineTag(RawInlineTag tag) {
	switch (tag) {
	case RawInlineTag::SubOpen: return EntityType::Subscript;
	case RawInlineTag::SupOpen: return EntityType::Superscript;
	case RawInlineTag::MarkOpen: return EntityType::Marked;
	default: return EntityType::Invalid;
	}
}

[[nodiscard]] int FindMatchingRawInlineTag(
		const std::vector<MarkdownNode> &nodes,
		int from,
		int till,
		RawInlineTag openingTag) {
	if (!IsOpeningRawInlineTag(openingTag)) {
		return -1;
	}
	auto stack = std::vector<RawInlineTag>();
	stack.push_back(openingTag);
	for (auto i = from; i != till; ++i) {
		const auto tag = ParseRawInlineTag(nodes[i]);
		if (tag == RawInlineTag::None) {
			continue;
		} else if (IsOpeningRawInlineTag(tag)) {
			stack.push_back(tag);
		} else if (stack.empty()
			|| MatchingClosingRawInlineTag(stack.back()) != tag) {
			return -1;
		} else if (stack.size() == 1) {
			return i;
		} else {
			stack.pop_back();
		}
	}
	return -1;
}

void AppendInline(
	const MarkdownNode &node,
	TextWithEntities *text,
	std::vector<PreparedLink> *links,
	InlineFormulaContext *inlineFormulas,
	PrepareState *state);

void AppendInlineRange(
		const std::vector<MarkdownNode> &nodes,
		int from,
		int till,
		TextWithEntities *text,
		std::vector<PreparedLink> *links,
		InlineFormulaContext *inlineFormulas,
		PrepareState *state) {
	for (auto i = from; i != till; ++i) {
		const auto &node = nodes[i];
		const auto tag = ParseRawInlineTag(node);
		if (IsOpeningRawInlineTag(tag)) {
			const auto closing = FindMatchingRawInlineTag(
				nodes,
				i + 1,
				till,
				tag);
			if (closing > i) {
				const auto entityFrom = text->text.size();
				AppendInlineRange(
					nodes,
					i + 1,
					closing,
					text,
					links,
					inlineFormulas,
					state);
				const auto entityLength = text->text.size() - entityFrom;
				if (entityLength > 0) {
					text->entities.push_back(EntityInText(
						EntityTypeForRawInlineTag(tag),
						entityFrom,
						entityLength));
				}
				i = closing;
				continue;
			}
		}
		if (IsSimpleRawInlineLineBreak(node)
			&& ((i + 1) < till)
			&& (nodes[i + 1].kind == NodeKind::SoftBreak)) {
			text->append(QChar('\n'));
			++i;
			continue;
		}
		AppendInline(node, text, links, inlineFormulas, state);
	}
}

void AppendInline(
		const MarkdownNode &node,
		TextWithEntities *text,
		std::vector<PreparedLink> *links,
		InlineFormulaContext *inlineFormulas,
		PrepareState *state) {
	const auto from = text->text.size();
	switch (node.kind) {
	case NodeKind::Text:
	case NodeKind::InlineMath:
		AppendTextWithInlineFormulas(
			node,
			node.text,
			text,
			inlineFormulas,
			state);
		break;
	case NodeKind::SoftBreak:
		text->append(QChar(' '));
		break;
	case NodeKind::LineBreak:
		text->append(QChar('\n'));
		break;
	case NodeKind::Emphasis:
		AppendInlineRange(
			node.children,
			0,
			int(node.children.size()),
			text,
			links,
			inlineFormulas,
			state);
		if (text->text.size() > from) {
			text->entities.push_back(
				EntityInText(
					EntityType::Italic,
					from,
					text->text.size() - from));
		}
		break;
	case NodeKind::Strong:
		AppendInlineRange(
			node.children,
			0,
			int(node.children.size()),
			text,
			links,
			inlineFormulas,
			state);
		if (text->text.size() > from) {
			text->entities.push_back(
				EntityInText(
					EntityType::Bold,
					from,
					text->text.size() - from));
		}
		break;
	case NodeKind::Strike:
		AppendInlineRange(
			node.children,
			0,
			int(node.children.size()),
			text,
			links,
			inlineFormulas,
			state);
		if (text->text.size() > from) {
			text->entities.push_back(
				EntityInText(
					EntityType::StrikeOut,
					from,
					text->text.size() - from));
		}
		break;
	case NodeKind::InlineCode:
		if (!node.text.isEmpty()) {
			text->append(node.text);
		} else {
			AppendInlineRange(
				node.children,
				0,
				int(node.children.size()),
				text,
				links,
				inlineFormulas,
				state);
		}
		if (text->text.size() > from) {
			text->entities.push_back(
				EntityInText(
					EntityType::Code,
					from,
					text->text.size() - from));
		}
		break;
	case NodeKind::Link: {
		AppendInlineRange(
			node.children,
			0,
			int(node.children.size()),
			text,
			links,
			inlineFormulas,
			state);
		if (text->text.size() == from && !node.url.isEmpty()) {
			text->append(node.url);
		}
		const auto length = text->text.size() - from;
		if (length <= 0 || node.url.isEmpty()) {
			break;
		}
		const auto index = links->size() + 1;
		if (index > std::numeric_limits<uint16>::max()) {
			break;
		}
		auto preparedLink = ClassifiedLink(uint16(index), node.url, state);
		FinalizePreparedExternalLink(
			&preparedLink,
			QStringView(text->text).mid(from, length));
		text->entities.push_back(
			EntityInText(
				EntityType::CustomUrl,
				from,
				length,
				InternalLinkData(uint16(index))));
		links->push_back(std::move(preparedLink));
	} break;
	case NodeKind::FootnoteReference: {
		if (node.footnoteOrdinal <= 0) {
			const auto fallback = !node.raw.isEmpty()
				? node.raw
				: (u"[^"_q + node.footnoteLabel + u"]"_q);
			AppendTextWithInlineFormulas(
				node,
				fallback,
				text,
				inlineFormulas,
				state);
			break;
		}
		const auto index = links->size() + 1;
		const auto label = !node.footnoteLabel.isEmpty()
			? node.footnoteLabel
			: QString::number(node.footnoteOrdinal);
		const auto display = u"["_q + label + u"]"_q;
		text->append(display);
		const auto length = display.size();
		if (length > 0) {
			text->entities.push_back(EntityInText(
				EntityType::Superscript,
				from,
				length));
		}
		if (index <= std::numeric_limits<uint16>::max()) {
			text->entities.push_back(EntityInText(
				EntityType::CustomUrl,
				from,
				length,
				InternalLinkData(uint16(index))));
			links->push_back({
				.index = uint16(index),
				.kind = PreparedLinkKind::Footnote,
				.target = label,
				.copyText = display,
			});
		}
	} break;
	case NodeKind::HtmlInline:
	case NodeKind::Unsupported:
		if (!node.raw.isEmpty()) {
			AppendTextWithInlineFormulas(
				node,
				node.raw,
				text,
				inlineFormulas,
				state);
		} else if (!node.children.empty()) {
			AppendInlineRange(
				node.children,
				0,
				int(node.children.size()),
				text,
				links,
				inlineFormulas,
				state);
		} else if (!node.text.isEmpty()) {
			AppendTextWithInlineFormulas(
				node,
				node.text,
				text,
				inlineFormulas,
				state);
		}
		break;
	default:
		if (!node.children.empty()) {
			AppendInlineRange(
				node.children,
				0,
				int(node.children.size()),
				text,
				links,
				inlineFormulas,
				state);
		} else if (!node.text.isEmpty()) {
			AppendTextWithInlineFormulas(
				node,
				node.text,
				text,
				inlineFormulas,
				state);
		} else if (!node.raw.isEmpty()) {
			AppendTextWithInlineFormulas(
				node,
				node.raw,
				text,
				inlineFormulas,
				state);
		}
		break;
	}
}

} // namespace

void PrepareInlineRichText(
		const MarkdownNode &node,
		int textSize,
		int renderWidthCap,
		int renderHeightCap,
		QString *blockAnchorId,
		TextWithEntities *text,
		std::vector<PreparedLink> *links,
		PrepareState *state) {
	auto formulas = CollectInlineFormulas(node, state);
	auto inlineFormulas = InlineFormulaContext{
		.formulas = &formulas,
		.blockAnchorId = blockAnchorId,
		.textSize = textSize,
		.renderWidthCap = renderWidthCap,
		.renderHeightCap = renderHeightCap,
	};
	if (!node.children.empty()) {
		AppendInlineRange(
			node.children,
			0,
			int(node.children.size()),
			text,
			links,
			&inlineFormulas,
			state);
	} else if (!node.text.isEmpty()) {
		AppendTextWithInlineFormulas(
			node,
			node.text,
			text,
			&inlineFormulas,
			state);
	} else if (!node.raw.isEmpty()) {
		AppendTextWithInlineFormulas(
			node,
			node.raw,
			text,
			&inlineFormulas,
			state);
	}
}

} // namespace Iv::Markdown
