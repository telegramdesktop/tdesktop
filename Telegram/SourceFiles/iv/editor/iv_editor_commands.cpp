/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/editor/iv_editor_commands.h"

namespace Iv::Editor {
namespace {

using Command = bool(*)(CommandContext&);

[[nodiscard]] bool Applied(
		CommandContext &context,
		std::optional<int> target) {
	if (!target) {
		return false;
	}
	context.targetOrdinal = *target;
	return true;
}

[[nodiscard]] bool InsertLeadingParagraph(CommandContext &context) {
	const auto state = context.state;
	if (!context.caretAtStart
		|| state->previousEditableOrdinal().has_value()
		|| state->isActiveTopLevelParagraphOrHeading()
		|| state->hasActiveListItemSurface()) {
		return false;
	}
	return Applied(context, state->insertLeadingParagraphActive(false));
}

[[nodiscard]] bool ListEnter(CommandContext &context) {
	return Applied(
		context,
		context.state->handleActiveListEnter(context.enter));
}

[[nodiscard]] bool EscapeEmptyLine(CommandContext &context) {
	return Applied(context, context.state->escapeEmptyActiveBlockLine());
}

[[nodiscard]] bool HeadingEnter(CommandContext &context) {
	return Applied(
		context,
		context.state->handleActiveHeadingEnter(context.enter));
}

[[nodiscard]] bool FooterEnter(CommandContext &context) {
	return Applied(
		context,
		context.state->handleActiveFooterEnter(context.enter));
}

[[nodiscard]] bool ParagraphEnter(CommandContext &context) {
	return Applied(
		context,
		context.state->handleActiveParagraphEnter(context.enter));
}

[[nodiscard]] bool QuoteEnter(CommandContext &context) {
	return Applied(
		context,
		context.state->handleActiveQuoteEnter(context.enter));
}

[[nodiscard]] bool SubmitSingleLineField(CommandContext &context) {
	return Applied(
		context,
		context.state->submitActiveSingleLineField(context.enter));
}

const Command kEnterChain[] = {
	InsertLeadingParagraph,
	ListEnter,
	EscapeEmptyLine,
	HeadingEnter,
	FooterEnter,
	ParagraphEnter,
	QuoteEnter,
	SubmitSingleLineField,
};

[[nodiscard]] bool RunFirst(
		gsl::span<const Command> chain,
		CommandContext &context) {
	for (const auto command : chain) {
		if (command(context)) {
			return true;
		}
	}
	return false;
}

} // namespace

bool RunEnterChain(CommandContext &context) {
	return RunFirst(kEnterChain, context);
}

} // namespace Iv::Editor
