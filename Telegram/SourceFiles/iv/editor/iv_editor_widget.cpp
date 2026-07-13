/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/editor/iv_editor_widget.h"

#include "base/event_filter.h"
#include "base/qthelp_url.h"
#include "base/qt/qt_common_adapters.h"
#include "base/random.h"
#include "base/weak_qptr.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "chat_helpers/message_field.h"
#include "core/mime_type.h"
#include "data/data_msg_id.h"
#include "data/data_types.h"
#include "data/stickers/data_custom_emoji.h"
#include "editor/editor_layer_widget.h"
#include "editor/photo_editor.h"
#include "editor/photo_editor_common.h"
#include "iv/editor/iv_editor_text_entities.h"
#include "iv/editor/iv_editor_window.h"
#include "iv/markdown/iv_markdown_article_paint.h"
#include "iv/markdown/iv_markdown_microtex.h"
#include "iv/markdown/iv_markdown_prepare_links.h"
#include "iv/markdown/iv_markdown_prepare_native_richtext.h"
#include "iv/iv_search_bar.h"
#include "iv/iv_search_controller.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "main/session/session_show.h"
#include "menu/menu_checked_action.h"
#include "platform/platform_file_utilities.h"
#include "spellcheck/spellcheck_highlight_syntax.h"
#include "storage/storage_media_prepare.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/click_handler.h"
#include "ui/image/image.h"
#include "ui/image/image_location.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/text/text_entity.h"
#include "ui/text/text_utilities.h"
#include "ui/ui_utility.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/menu/menu_separator.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"

#include "styles/palette.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_iv.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDate>
#include <QtCore/QEvent>
#include <QtCore/QMimeData>
#include <QtCore/QPointer>
#include <QtGui/QClipboard>
#include <QtGui/QContextMenuEvent>
#include <QtGui/QCursor>
#include <QtGui/QFocusEvent>
#include <QtGui/QInputMethodEvent>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QResizeEvent>
#include <QtGui/QTextBlock>
#include <QtGui/QTextLayout>
#include <QtGui/QTouchEvent>
#include <QtGui/QWheelEvent>
#include <QtGui/QTextCursor>
#include <QtGui/QTextDocument>
#include <QtWidgets/QApplication>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMenu>
#include <QtWidgets/QTextEdit>
#include <QShortcut>
#include <QAction>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace Iv::Editor {
namespace {

const auto kFormulaSamples = std::array{
	u"e^{i\\pi}=-1"_q,
	u"x^n+y^n=z^n"_q,
	u"\\sin^2\\alpha+\\cos^2\\alpha=1"_q,
	u"x_{1,2}=\\frac{-b\\pm\\sqrt{b^2-4ac}}{2a}"_q,
};

[[nodiscard]] std::vector<Ui::Text::SpecialColor> HighlightColors(
		not_null<const Ui::ChatStyle*> style) {
	auto result = Ui::SyntaxHighlightColors(style);

	const auto &fg = style->lightButtonFg();
	const auto &bg = style->lightButtonBgOver();
	result.push_back({ &fg->p, &fg->p, &bg->b, &bg->b });

	Ensures(result.size() == Markdown::kNativeIvLinkSpecialColorIndex);
	return result;
}

[[nodiscard]] int MaxVisualLineWidth(
		not_null<const QTextDocument*> document) {
	auto result = 0.;
	for (auto block = document->begin(); block.isValid(); block = block.next()) {
		const auto layout = block.layout();
		if (!layout) {
			continue;
		}
		for (auto i = 0, count = layout->lineCount(); i != count; ++i) {
			result = std::max(
				result,
				double(layout->lineAt(i).naturalTextWidth()));
		}
	}
	return std::max(int(std::ceil(result)), 0);
}

[[nodiscard]] int MaxVisualLineWidthForWidth(
		not_null<const QTextDocument*> document,
		int width) {
	width = std::max(width, 1);
	const auto clone = std::unique_ptr<QTextDocument>(document->clone());
	clone->setTextWidth(width);
	clone->adjustSize();
	return MaxVisualLineWidth(clone.get());
}

[[nodiscard]] std::unique_ptr<Ui::ChatTheme> CreateStandaloneChatTheme() {
	const auto palette = style::main_palette::get();
	return std::make_unique<Ui::ChatTheme>(Ui::ChatThemeDescriptor{
		.preparePalette = [=](style::palette &copy) {
			copy = *palette;
		},
		.backgroundData = {
			.colors = { palette->windowBg()->c },
		},
	});
}

[[nodiscard]] const style::margins &EditorBodyPadding() {
	return st::ivEditorBodyPadding;
}

[[nodiscard]] bool MatchesKeySequence(
		QKeyEvent *e,
		const QKeySequence &sequence) {
	const auto matches = [&](Qt::KeyboardModifiers modifiers, int key) {
		const auto searchKey = (int(modifiers) | key)
			& ~(int(Qt::KeypadModifier) | int(Qt::GroupSwitchModifier));
		return sequence.matches(QKeySequence(searchKey))
			== QKeySequence::ExactMatch;
	};
	const auto modifiers = e->modifiers();
	if (matches(modifiers, e->key())) {
		return true;
	}
	const auto cleanedModifiers = int(modifiers)
		& ~(int(Qt::KeypadModifier) | int(Qt::GroupSwitchModifier));
	return (sequence == Ui::kBlockquoteSequence)
		&& (cleanedModifiers == int(Qt::ControlModifier | Qt::ShiftModifier))
		&& (e->key() == Qt::Key_Period || e->key() == Qt::Key_Greater);
}

constexpr auto kRetainedLeafFieldLimit = 50;
thread_local Widget *PreservingExternalFieldRestore = nullptr;
using ToolbarFormatAction = Widget::ToolbarFormatAction;
using ToolbarLinkMode = Widget::ToolbarLinkMode;
using TextFormattingAction = State::TextFormattingAction;
using TextNodeSpan = State::TextNodeSpan;
using StateBlockContainerKind = State::BlockContainerKind;
using StateBlockContainerPath = State::BlockContainerPath;
using StateBlockPath = State::BlockPath;
using StateLeafKind = State::LeafKind;
using StateLeafPath = State::LeafPath;
using PreparedBlockContainerKind = Markdown::PreparedEditBlockContainerKind;
using PreparedBlockContainerPath = Markdown::PreparedEditBlockContainerPath;
using PreparedBlockContainerStep = Markdown::PreparedEditBlockContainerStep;
using PreparedBlockPath = Markdown::PreparedEditBlockPath;
using PreparedBlockRange = Markdown::PreparedEditBlockRange;
using PreparedListItemRange = Markdown::PreparedEditListItemRange;
using PreparedOrderedListType = Markdown::PreparedOrderedListType;
using PreparedSelection = Markdown::PreparedEditSelection;
using PreparedSelectionKind = Markdown::PreparedEditSelectionKind;

[[nodiscard]] const std::vector<RichPage::Block> *BlockContainer(
	const RichPage &page,
	const StateBlockContainerPath &path);
[[nodiscard]] const RichPage::Block *BlockFromPath(
	const RichPage &page,
	const StateBlockPath &path);
[[nodiscard]] const RichPage::RichText *RichTextFromPath(
	const RichPage &page,
	const StateLeafPath &path);
[[nodiscard]] StateBlockContainerPath BlockChildrenContainer(
	StateBlockPath path);
[[nodiscard]] StateBlockContainerPath ListItemChildrenContainer(
	StateBlockPath path,
	int itemIndex);

struct TextRange {
	int offset = 0;
	int length = 0;
};

struct CommittedFieldSelectionCapture {
	StateLeafPath leaf;
	TextWithEntities text;
	int anchorOffset = 0;
	int cursorOffset = 0;
};

struct CommittedFieldSelectionRestore {
	int ordinal = -1;
	int anchorOffset = 0;
	int cursorOffset = 0;
};

void RemoveBlockLevelEntities(TextWithEntities *text) {
	auto &list = text->entities;
	for (auto i = list.begin(); i != list.end();) {
		const auto type = i->type();
		if (type == EntityType::Blockquote || type == EntityType::Pre) {
			i = list.erase(i);
		} else {
			++i;
		}
	}
}

constexpr auto kMaxRichTextNodeLength = 16000;
constexpr auto kMaxCommittedFieldLength = 256 * 1024;

[[nodiscard]] std::vector<TextWithEntities> SplitCommittedFieldText(
		TextWithEntities text) {
	auto result = std::vector<TextWithEntities>();
	auto left = std::move(text);
	auto consumed = 0;
	while (!left.text.isEmpty() && consumed < kMaxCommittedFieldLength) {
		auto part = TextWithEntities();
		const auto limit = std::min(
			kMaxRichTextNodeLength,
			kMaxCommittedFieldLength - consumed);
		if (!TextUtilities::CutPart(part, left, limit)
			|| part.text.isEmpty()) {
			break;
		}
		consumed += part.text.size();
		result.push_back(std::move(part));
	}
	return result;
}

struct SplitCommittedFieldOffset {
	int chunkIndex = -1;
	int localOffset = 0;
};

[[nodiscard]] SplitCommittedFieldOffset SplitCommittedFieldOffsetAt(
		const std::vector<TextWithEntities> &chunks,
		int offset) {
	auto consumed = 0;
	for (auto i = 0, count = int(chunks.size()); i != count; ++i) {
		const auto size = int(chunks[i].text.size());
		if ((offset <= consumed + size) || (i + 1 == count)) {
			return {
				.chunkIndex = i,
				.localOffset = std::clamp(offset - consumed, 0, size),
			};
		}
		consumed += size;
	}
	return {};
}

[[nodiscard]] std::optional<StateLeafPath> SplitCommittedFieldLeafAt(
		const RichPage &page,
		const CommittedFieldSelectionCapture &capture,
		int chunkIndex) {
	if (chunkIndex < 0) {
		return std::nullopt;
	}
	if (capture.leaf.kind == StateLeafKind::BlockText) {
		const auto block = BlockFromPath(page, capture.leaf.block);
		if (!block) {
			return std::nullopt;
		} else if (block->kind == RichPage::BlockKind::Paragraph) {
			return StateLeafPath{
				.kind = StateLeafKind::BlockText,
				.block = {
					.container = capture.leaf.block.container,
					.index = capture.leaf.block.index + chunkIndex,
				},
			};
		} else if (block->kind == RichPage::BlockKind::Quote
			&& !block->pullquote) {
			return StateLeafPath{
				.kind = StateLeafKind::BlockText,
				.block = {
					.container = BlockChildrenContainer(capture.leaf.block),
					.index = chunkIndex,
				},
			};
		}
	} else if (capture.leaf.kind == StateLeafKind::ListItemText) {
		return StateLeafPath{
			.kind = StateLeafKind::BlockText,
			.block = {
				.container = ListItemChildrenContainer(
					capture.leaf.block,
					capture.leaf.listItemIndex),
				.index = chunkIndex,
			},
		};
	}
	return std::nullopt;
}

[[nodiscard]] std::optional<CommittedFieldSelectionRestore>
MapCommittedFieldSelectionAfterCommit(
		const State &state,
		const CommittedFieldSelectionCapture &capture) {
	const auto fallback = [&](int anchor, int cursor)
			-> std::optional<CommittedFieldSelectionRestore> {
		const auto ordinal = state.activeTextOrdinal();
		if (ordinal < 0 || ordinal >= state.textNodeCount()) {
			return std::nullopt;
		}
		const auto length = state.activeTextLength();
		return CommittedFieldSelectionRestore{
			.ordinal = ordinal,
			.anchorOffset = std::clamp(anchor, 0, length),
			.cursorOffset = std::clamp(cursor, 0, length),
		};
	};
	const auto fullLength = int(capture.text.text.size());
	const auto anchorOffset = std::clamp(capture.anchorOffset, 0, fullLength);
	const auto cursorOffset = std::clamp(capture.cursorOffset, 0, fullLength);
	auto chunks = SplitCommittedFieldText(capture.text);
	if (chunks.size() <= 1) {
		const auto ordinal = state.textOrdinalForLeafPath(capture.leaf);
		if (ordinal >= 0) {
			const auto rich = RichTextFromPath(state.richPage(), capture.leaf);
			const auto length = rich ? int(rich->text.text.size()) : 0;
			return CommittedFieldSelectionRestore{
				.ordinal = ordinal,
				.anchorOffset = std::clamp(anchorOffset, 0, length),
				.cursorOffset = std::clamp(cursorOffset, 0, length),
			};
		}
		return fallback(anchorOffset, cursorOffset);
	}
	const auto anchor = SplitCommittedFieldOffsetAt(chunks, anchorOffset);
	const auto cursor = SplitCommittedFieldOffsetAt(chunks, cursorOffset);
	if (anchor.chunkIndex >= 0
		&& (anchor.chunkIndex == cursor.chunkIndex)) {
		if (const auto leaf = SplitCommittedFieldLeafAt(
				state.richPage(),
				capture,
				cursor.chunkIndex)) {
			const auto ordinal = state.textOrdinalForLeafPath(*leaf);
			const auto rich = RichTextFromPath(state.richPage(), *leaf);
			if (ordinal >= 0 && rich) {
				const auto length = int(rich->text.text.size());
				return CommittedFieldSelectionRestore{
					.ordinal = ordinal,
					.anchorOffset = std::clamp(anchor.localOffset, 0, length),
					.cursorOffset = std::clamp(cursor.localOffset, 0, length),
				};
			}
		}
	}
	return fallback(anchorOffset, cursorOffset);
}

[[nodiscard]] const QString *ToolbarActionTag(ToolbarFormatAction action) {
	switch (action) {
	case ToolbarFormatAction::Bold:
		return &Ui::InputField::kTagBold;
	case ToolbarFormatAction::Italic:
		return &Ui::InputField::kTagItalic;
	case ToolbarFormatAction::Underline:
		return &Ui::InputField::kTagUnderline;
	case ToolbarFormatAction::StrikeOut:
		return &Ui::InputField::kTagStrikeOut;
	case ToolbarFormatAction::Spoiler:
		return &Ui::InputField::kTagSpoiler;
	case ToolbarFormatAction::Subscript:
		return &Ui::InputField::kTagIvSubscript;
	case ToolbarFormatAction::Superscript:
		return &Ui::InputField::kTagIvSuperscript;
	case ToolbarFormatAction::Marked:
		return &Ui::InputField::kTagIvMarked;
	case ToolbarFormatAction::Math:
	case ToolbarFormatAction::Undo:
	case ToolbarFormatAction::Redo:
	case ToolbarFormatAction::PlainText:
	case ToolbarFormatAction::Link:
	case ToolbarFormatAction::Count:
		return nullptr;
	}
	return nullptr;
}

[[nodiscard]] std::optional<TextFormattingAction> BroaderFormattingAction(
		ToolbarFormatAction action) {
	switch (action) {
	case ToolbarFormatAction::Bold:
		return TextFormattingAction::Bold;
	case ToolbarFormatAction::Italic:
		return TextFormattingAction::Italic;
	case ToolbarFormatAction::Underline:
		return TextFormattingAction::Underline;
	case ToolbarFormatAction::StrikeOut:
		return TextFormattingAction::StrikeOut;
	case ToolbarFormatAction::Spoiler:
		return TextFormattingAction::Spoiler;
	case ToolbarFormatAction::PlainText:
		return TextFormattingAction::PlainText;
	case ToolbarFormatAction::Undo:
	case ToolbarFormatAction::Redo:
	case ToolbarFormatAction::Subscript:
	case ToolbarFormatAction::Superscript:
	case ToolbarFormatAction::Marked:
	case ToolbarFormatAction::Link:
	case ToolbarFormatAction::Math:
	case ToolbarFormatAction::Count:
		return std::nullopt;
	}
	return std::nullopt;
}

[[nodiscard]] bool RangeInsideText(
		const QString &text,
		int offset,
		int length) {
	return (offset >= 0)
		&& (length >= 0)
		&& (offset <= text.size())
		&& ((offset + length) <= text.size());
}

[[nodiscard]] bool TagContains(QStringView tags, QStringView tagId) {
	return TextUtilities::SplitTags(tags).contains(tagId);
}

[[nodiscard]] bool HasFullTextTag(
		const TextWithTags &textWithTags,
		const QString &tag) {
	if (tag.isEmpty() || textWithTags.text.isEmpty()) {
		return false;
	}
	auto ranges = std::vector<TextRange>();
	ranges.reserve(textWithTags.tags.size());
	for (const auto &existing : textWithTags.tags) {
		if (existing.length <= 0
			|| !RangeInsideText(
				textWithTags.text,
				existing.offset,
				existing.length)
			|| !TagContains(existing.id, tag)) {
			continue;
		}
		ranges.push_back({
			.offset = existing.offset,
			.length = existing.length,
		});
	}
	if (ranges.empty()) {
		return false;
	}
	std::sort(ranges.begin(), ranges.end(), [](const auto &a, const auto &b) {
		if (a.offset != b.offset) {
			return a.offset < b.offset;
		}
		return a.length < b.length;
	});
	auto coveredTill = 0;
	for (const auto &range : ranges) {
		if (range.offset > coveredTill) {
			return false;
		}
		coveredTill = std::max(coveredTill, range.offset + range.length);
		if (coveredTill >= textWithTags.text.size()) {
			return true;
		}
	}
	return (coveredTill >= textWithTags.text.size());
}

[[nodiscard]] bool SplitTextSpan(
		const TextWithEntities &text,
		int from,
		int till,
		TextWithEntities *before,
		TextWithEntities *selected,
		TextWithEntities *after) {
	if (!before || !selected || !after) {
		return false;
	}
	const auto textSize = int(text.text.size());
	from = std::clamp(from, 0, textSize);
	till = std::clamp(till, from, textSize);
	if (from >= till) {
		return false;
	}
	*before = Ui::Text::Mid(text, 0, from);
	*selected = Ui::Text::Mid(text, from, till - from);
	if (selected->text.isEmpty()) {
		return false;
	}
	*after = Ui::Text::Mid(text, till);
	return true;
}

[[nodiscard]] PreparedBlockContainerPath ToPreparedBlockContainerPath(
		const StateBlockContainerPath &path) {
	auto result = PreparedBlockContainerPath();
	result.steps.reserve(path.steps.size());
	for (const auto &step : path.steps) {
		auto converted = PreparedBlockContainerStep();
		converted.blockIndex = step.blockIndex;
		converted.listItemIndex = step.listItemIndex;
		switch (step.kind) {
		case StateBlockContainerKind::Root:
			continue;
		case StateBlockContainerKind::BlockChildren:
			converted.kind = PreparedBlockContainerKind::BlockChildren;
			break;
		case StateBlockContainerKind::ListItemChildren:
			converted.kind = PreparedBlockContainerKind::ListItemChildren;
			break;
		}
		result.steps.push_back(converted);
	}
	return result;
}

[[nodiscard]] PreparedBlockPath ToPreparedBlockPath(
		const StateBlockPath &path) {
	return {
		.container = ToPreparedBlockContainerPath(path.container),
		.index = path.index,
	};
}

[[nodiscard]] StateBlockContainerPath ToStateBlockContainerPath(
		const PreparedBlockContainerPath &path) {
	auto result = StateBlockContainerPath();
	result.steps.reserve(path.steps.size());
	for (const auto &step : path.steps) {
		auto converted = State::BlockContainerStep();
		converted.blockIndex = step.blockIndex;
		converted.listItemIndex = step.listItemIndex;
		switch (step.kind) {
		case PreparedBlockContainerKind::Root:
			continue;
		case PreparedBlockContainerKind::BlockChildren:
			converted.kind = StateBlockContainerKind::BlockChildren;
			break;
		case PreparedBlockContainerKind::ListItemChildren:
			converted.kind = StateBlockContainerKind::ListItemChildren;
			break;
		}
		result.steps.push_back(converted);
	}
	return result;
}

[[nodiscard]] StateBlockPath ToStateBlockPath(
		const PreparedBlockPath &path) {
	return {
		.container = ToStateBlockContainerPath(path.container),
		.index = path.index,
	};
}

[[nodiscard]] bool PreparedContainerHasPrefix(
		const PreparedBlockContainerPath &path,
		const PreparedBlockContainerPath &prefix) {
	if (path.steps.size() < prefix.steps.size()) {
		return false;
	}
	return std::equal(
		prefix.steps.begin(),
		prefix.steps.end(),
		path.steps.begin());
}

[[nodiscard]] bool IndexInRange(int index, int from, int till) {
	return (index >= from) && (index < till);
}

[[nodiscard]] bool PreparedPathInBlockRange(
		const PreparedBlockPath &path,
		const PreparedBlockRange &range) {
	if (path.container == range.container) {
		return IndexInRange(path.index, range.from, range.till);
	}
	if (!PreparedContainerHasPrefix(path.container, range.container)
		|| (path.container.steps.size() <= range.container.steps.size())) {
		return false;
	}
	const auto &step = path.container.steps[range.container.steps.size()];
	return IndexInRange(step.blockIndex, range.from, range.till);
}

[[nodiscard]] bool PreparedPathInListItemRange(
		const PreparedBlockPath &path,
		const PreparedListItemRange &range) {
	if (!PreparedContainerHasPrefix(path.container, range.block.container)
		|| (path.container.steps.size() <= range.block.container.steps.size())) {
		return false;
	}
	const auto &step = path.container.steps[range.block.container.steps.size()];
	return (step.kind == PreparedBlockContainerKind::ListItemChildren)
		&& (step.blockIndex == range.block.index)
		&& IndexInRange(step.listItemIndex, range.from, range.till);
}

[[nodiscard]] bool PreparedContainerNestedInSelection(
		const PreparedBlockContainerPath &container,
		const PreparedSelection &selection) {
	const auto marker = PreparedBlockPath{
		.container = container,
		.index = 0,
	};
	switch (selection.kind) {
	case PreparedSelectionKind::Blocks:
		return (container.steps.size() > selection.blocks.container.steps.size())
			&& PreparedPathInBlockRange(marker, selection.blocks);
	case PreparedSelectionKind::ListItems:
		return (container.steps.size()
			> selection.listItems.block.container.steps.size())
			&& PreparedPathInListItemRange(marker, selection.listItems);
	case PreparedSelectionKind::TableRows:
	case PreparedSelectionKind::TableCells:
	case PreparedSelectionKind::None:
		return false;
	}
	return false;
}

[[nodiscard]] bool PreparedBlockPathInSelection(
		const PreparedBlockPath &path,
		const PreparedSelection &selection) {
	switch (selection.kind) {
	case PreparedSelectionKind::Blocks:
		return PreparedPathInBlockRange(path, selection.blocks);
	case PreparedSelectionKind::ListItems:
		return PreparedPathInListItemRange(path, selection.listItems);
	case PreparedSelectionKind::TableRows:
	case PreparedSelectionKind::TableCells:
	case PreparedSelectionKind::None:
		return false;
	}
	return false;
}

[[nodiscard]] const std::vector<RichPage::Block> *BlockContainer(
		const RichPage &page,
		const StateBlockContainerPath &path) {
	const auto *current = &page.blocks;
	for (const auto &step : path.steps) {
		if (!current) {
			return nullptr;
		}
		switch (step.kind) {
		case StateBlockContainerKind::Root:
			break;
		case StateBlockContainerKind::BlockChildren: {
			if (step.blockIndex < 0 || step.blockIndex >= int(current->size())) {
				return nullptr;
			}
			current = &(*current)[step.blockIndex].blocks;
		} break;
		case StateBlockContainerKind::ListItemChildren: {
			if (step.blockIndex < 0 || step.blockIndex >= int(current->size())) {
				return nullptr;
			}
			const auto &block = (*current)[step.blockIndex];
			if (step.listItemIndex < 0
				|| step.listItemIndex >= int(block.listItems.size())) {
				return nullptr;
			}
			current = &block.listItems[step.listItemIndex].blocks;
		} break;
		}
	}
	return current;
}

[[nodiscard]] const RichPage::Block *BlockFromPath(
		const RichPage &page,
		const StateBlockPath &path) {
	const auto *container = BlockContainer(page, path.container);
	if (!container || path.index < 0 || path.index >= int(container->size())) {
		return nullptr;
	}
	return &(*container)[path.index];
}

[[nodiscard]] const RichPage::RichText *RichTextFromPath(
		const RichPage &page,
		const StateLeafPath &path) {
	const auto block = BlockFromPath(page, path.block);
	if (!block) {
		return nullptr;
	}
	switch (path.kind) {
	case StateLeafKind::BlockText:
		return &block->text;
	case StateLeafKind::BlockCaption:
		return &block->caption;
	case StateLeafKind::ListItemText:
		if (path.listItemIndex < 0
			|| path.listItemIndex >= int(block->listItems.size())) {
			return nullptr;
		}
		return &block->listItems[path.listItemIndex].text;
	case StateLeafKind::TableCellText:
		if (path.tableRowIndex < 0
			|| path.tableRowIndex >= int(block->tableRows.size())) {
			return nullptr;
		}
		if (path.tableCellIndex < 0
			|| path.tableCellIndex
				>= int(block->tableRows[path.tableRowIndex].cells.size())) {
			return nullptr;
		}
		return &block->tableRows[path.tableRowIndex].cells[path.tableCellIndex]
			.text;
	case StateLeafKind::MathFormula:
		return nullptr;
	}
	return nullptr;
}

using TableGridOccupancyRow = std::vector<char>;
using TableGridOccupancy = std::vector<TableGridOccupancyRow>;

struct TableGridCellReference {
	int rowIndex = -1;
	int cellIndex = -1;
	int rowFrom = -1;
	int rowTill = -1;
	int columnFrom = -1;
	int columnTill = -1;
};

struct TableGrid {
	std::vector<TableGridCellReference> cells;
	TableGridOccupancy occupancy;
	int rowCount = 0;
	int columnCount = 0;
};

[[nodiscard]] int NormalizeTableSpan(int span) {
	return std::max(span, 1);
}

[[nodiscard]] int ClampTableRowspan(
		int rawRowspan,
		int row,
		int rowCount) {
	if ((row < 0) || (row >= rowCount) || (rowCount <= 0)) {
		return 0;
	}
	const auto remainingRows = int64(rowCount) - row;
	return int(std::min<int64>(NormalizeTableSpan(rawRowspan), remainingRows));
}

[[nodiscard]] int ClampTableColspan(
		int rawColspan,
		int column,
		int maxColumns) {
	if ((column < 0) || (column >= maxColumns) || (maxColumns <= 0)) {
		return 0;
	}
	const auto remainingColumns = int64(maxColumns) - column;
	return int(std::min<int64>(
		NormalizeTableSpan(rawColspan),
		remainingColumns));
}

[[nodiscard]] bool CanOccupyTableSlots(
		const TableGridOccupancy &occupancy,
		int row,
		int column,
		int rowspan,
		int colspan) {
	if ((row < 0)
		|| (column < 0)
		|| (rowspan <= 0)
		|| (colspan <= 0)
		|| (row >= int(occupancy.size()))) {
		return false;
	}
	const auto rowLimit = int(std::min<int64>(
		int64(row) + rowspan,
		occupancy.size()));
	const auto columnLimit64 = int64(column) + colspan;
	if (columnLimit64 <= column) {
		return false;
	}
	const auto columnLimit = int(std::min<int64>(
		columnLimit64,
		std::numeric_limits<int>::max()));
	for (auto currentRow = row; currentRow < rowLimit; ++currentRow) {
		const auto &occupied = occupancy[currentRow];
		const auto occupiedLimit = std::min(columnLimit, int(occupied.size()));
		for (auto currentColumn = column;
			currentColumn < occupiedLimit;
			++currentColumn) {
			if (occupied[currentColumn]) {
				return false;
			}
		}
	}
	return true;
}

[[nodiscard]] int FirstAvailableTableColumn(
		const TableGridOccupancy &occupancy,
		int row,
		int rowspan,
		int colspan,
		int maxColumns) {
	if ((row < 0)
		|| (row >= int(occupancy.size()))
		|| (rowspan <= 0)
		|| (colspan <= 0)
		|| (maxColumns <= 0)) {
		return -1;
	}
	for (auto column = 0; column < maxColumns; ++column) {
		const auto effectiveColspan = ClampTableColspan(
			colspan,
			column,
			maxColumns);
		if (effectiveColspan <= 0) {
			continue;
		}
		if (CanOccupyTableSlots(
				occupancy,
				row,
				column,
				rowspan,
				effectiveColspan)) {
			return column;
		}
	}
	return -1;
}

void MarkTableSlots(
		TableGridOccupancy *occupancy,
		int row,
		int column,
		int rowspan,
		int colspan) {
	if ((row < 0)
		|| (column < 0)
		|| (rowspan <= 0)
		|| (colspan <= 0)
		|| (row >= int(occupancy->size()))) {
		return;
	}
	const auto rowLimit = int(std::min<int64>(
		int64(row) + rowspan,
		occupancy->size()));
	const auto columnLimit64 = int64(column) + colspan;
	if (columnLimit64 <= column) {
		return;
	}
	const auto columnLimit = int(std::min<int64>(
		columnLimit64,
		std::numeric_limits<int>::max()));
	for (auto currentRow = row; currentRow < rowLimit; ++currentRow) {
		auto &occupied = (*occupancy)[currentRow];
		if (columnLimit > int(occupied.size())) {
			occupied.resize(columnLimit, false);
		}
		for (auto currentColumn = column;
			currentColumn < columnLimit;
			++currentColumn) {
			occupied[currentColumn] = true;
		}
	}
}

[[nodiscard]] int TableGridColumnCount(const TableGridOccupancy &occupancy) {
	auto result = 0;
	for (const auto &row : occupancy) {
		result = std::max(result, int(row.size()));
	}
	return result;
}

[[nodiscard]] int TableMaxColumns(const RichPage::Block &table) {
	auto result = 0;
	for (const auto &row : table.tableRows) {
		auto columns = 0;
		for (const auto &cell : row.cells) {
			columns += NormalizeTableSpan(cell.colspan);
		}
		result = std::max(result, columns);
	}
	return result;
}

[[nodiscard]] TableGrid BuildTableGrid(const RichPage::Block &table) {
	auto result = TableGrid();
	result.rowCount = int(table.tableRows.size());
	result.occupancy = TableGridOccupancy(result.rowCount);
	const auto maxColumns = TableMaxColumns(table);
	if (result.rowCount <= 0 || maxColumns <= 0) {
		return result;
	}
	for (auto rowIndex = 0; rowIndex != result.rowCount; ++rowIndex) {
		const auto &row = table.tableRows[rowIndex];
		for (auto cellIndex = 0, cellCount = int(row.cells.size());
				cellIndex != cellCount;
				++cellIndex) {
			const auto &cell = row.cells[cellIndex];
			const auto normalizedColspan = NormalizeTableSpan(cell.colspan);
			const auto rowspan = ClampTableRowspan(
				cell.rowspan,
				rowIndex,
				result.rowCount);
			if (rowspan <= 0) {
				continue;
			}
			const auto column = FirstAvailableTableColumn(
				result.occupancy,
				rowIndex,
				rowspan,
				normalizedColspan,
				maxColumns);
			if (column < 0) {
				continue;
			}
			const auto colspan = ClampTableColspan(
				normalizedColspan,
				column,
				maxColumns);
			if (colspan <= 0) {
				continue;
			}
			result.cells.push_back({
				.rowIndex = rowIndex,
				.cellIndex = cellIndex,
				.rowFrom = rowIndex,
				.rowTill = rowIndex + rowspan,
				.columnFrom = column,
				.columnTill = column + colspan,
			});
			MarkTableSlots(
				&result.occupancy,
				rowIndex,
				column,
				rowspan,
				colspan);
		}
	}
	result.columnCount = TableGridColumnCount(result.occupancy);
	return result;
}

template <typename Range>
[[nodiscard]] bool TableGridCellIntersectsRange(
		const TableGridCellReference &cell,
		const Range &range) {
	return (cell.rowFrom < range.rowTill)
		&& (cell.rowTill > range.rowFrom)
		&& (cell.columnFrom < range.columnTill)
		&& (cell.columnTill > range.columnFrom);
}

template <typename Range>
[[nodiscard]] std::vector<TableGridCellReference> SelectedTableGridCells(
		const TableGrid &grid,
		const Range &range) {
	auto result = std::vector<TableGridCellReference>();
	result.reserve(grid.cells.size());
	for (const auto &cell : grid.cells) {
		if (TableGridCellIntersectsRange(cell, range)) {
			result.push_back(cell);
		}
	}
	return result;
}

[[nodiscard]] bool TableGridCellMatchesLeaf(
		const TableGridCellReference &cell,
		const StateLeafPath &leaf,
		const StateBlockPath &block) {
	return (leaf.block == block)
		&& (leaf.kind == StateLeafKind::TableCellText)
		&& (leaf.tableRowIndex == cell.rowIndex)
		&& (leaf.tableCellIndex == cell.cellIndex);
}

[[nodiscard]] bool LeafSelectedStructurally(
		const RichPage &page,
		const StateLeafPath &leaf,
		const PreparedSelection &selection) {
	const auto path = ToPreparedBlockPath(leaf.block);
	switch (selection.kind) {
	case PreparedSelectionKind::Blocks:
		return PreparedPathInBlockRange(path, selection.blocks);
	case PreparedSelectionKind::ListItems:
		if (leaf.kind == StateLeafKind::ListItemText
			&& (path == selection.listItems.block)
			&& IndexInRange(
				leaf.listItemIndex,
				selection.listItems.from,
				selection.listItems.till)) {
			return true;
		}
		return PreparedPathInListItemRange(path, selection.listItems);
	case PreparedSelectionKind::TableRows:
		return (leaf.kind == StateLeafKind::TableCellText)
			&& (path == selection.tableRows.block)
			&& IndexInRange(
				leaf.tableRowIndex,
				selection.tableRows.from,
				selection.tableRows.till);
	case PreparedSelectionKind::TableCells: {
		if (leaf.kind != StateLeafKind::TableCellText
			|| (path != selection.tableCells.block)) {
			return false;
		}
		const auto owner = BlockFromPath(page, leaf.block);
		if (!owner || owner->kind != RichPage::BlockKind::Table) {
			return false;
		}
		for (const auto &reference : SelectedTableGridCells(
				BuildTableGrid(*owner),
				selection.tableCells)) {
			if (TableGridCellMatchesLeaf(reference, leaf, leaf.block)) {
				return true;
			}
		}
		return false;
	}
	case PreparedSelectionKind::None:
		return false;
	}
	return false;
}

[[nodiscard]] bool BlockSelectedStructurally(
		const StateBlockPath &path,
		const PreparedSelection &selection) {
	const auto prepared = ToPreparedBlockPath(path);
	switch (selection.kind) {
	case PreparedSelectionKind::Blocks:
		return PreparedPathInBlockRange(prepared, selection.blocks);
	case PreparedSelectionKind::ListItems:
		return PreparedPathInListItemRange(prepared, selection.listItems);
	case PreparedSelectionKind::TableRows:
	case PreparedSelectionKind::TableCells:
	case PreparedSelectionKind::None:
		return false;
	}
	return false;
}

[[nodiscard]] bool MediaBlockSupportsSpoiler(
		const RichPage::Block &block) {
	switch (block.kind) {
	case RichPage::BlockKind::Photo:
	case RichPage::BlockKind::Video:
	case RichPage::BlockKind::Audio:
	case RichPage::BlockKind::Map:
		return true;
	case RichPage::BlockKind::GroupedMedia:
		return ranges::any_of(
			block.mediaItems,
			[](const RichPage::GroupedMediaItem &item) {
				return (item.kind == RichPage::BlockKind::Photo)
					|| (item.kind == RichPage::BlockKind::Video)
					|| (item.kind == RichPage::BlockKind::Audio)
					|| (item.kind == RichPage::BlockKind::Map);
			});
	default:
		return false;
	}
}

[[nodiscard]] bool IsSimpleMediaBlockKind(RichPage::BlockKind kind) {
	switch (kind) {
	case RichPage::BlockKind::Photo:
	case RichPage::BlockKind::Video:
	case RichPage::BlockKind::Audio:
		return true;
	default:
		return false;
	}
}

[[nodiscard]] uint64 MediaIdForBlock(const RichPage::Block &block) {
	switch (block.kind) {
	case RichPage::BlockKind::Photo:
		return block.photoId;
	case RichPage::BlockKind::Video:
	case RichPage::BlockKind::Audio:
		return block.documentId;
	default:
		return uint64(0);
	}
}

[[nodiscard]] uint64 MediaIdForGroupedItem(
		const RichPage::GroupedMediaItem &item) {
	switch (item.kind) {
	case RichPage::BlockKind::Photo:
		return item.photoId;
	case RichPage::BlockKind::Video:
	case RichPage::BlockKind::Audio:
		return item.documentId;
	default:
		return uint64(0);
	}
}

[[nodiscard]] bool IsPhotoVideoBlockKind(RichPage::BlockKind kind) {
	return (kind == RichPage::BlockKind::Photo)
		|| (kind == RichPage::BlockKind::Video);
}

[[nodiscard]] bool GroupedMediaHasPhotoVideoItems(
		const RichPage::Block &block) {
	return (block.kind == RichPage::BlockKind::GroupedMedia)
		&& ranges::any_of(
			block.mediaItems,
			[](const RichPage::GroupedMediaItem &item) {
				return IsPhotoVideoBlockKind(item.kind);
			});
}

[[nodiscard]] bool GroupedPhotoVideoItemsHaveSpoiler(
		const RichPage::Block &block) {
	auto any = false;
	for (const auto &item : block.mediaItems) {
		if (!IsPhotoVideoBlockKind(item.kind)) {
			continue;
		}
		any = true;
		if (!item.spoiler) {
			return false;
		}
	}
	return any;
}

[[nodiscard]] bool MediaBlockHasSpoiler(
		const RichPage::Block &block) {
	if (block.kind == RichPage::BlockKind::GroupedMedia) {
		auto any = false;
		for (const auto &item : block.mediaItems) {
			if ((item.kind != RichPage::BlockKind::Photo)
				&& (item.kind != RichPage::BlockKind::Video)
				&& (item.kind != RichPage::BlockKind::Audio)
				&& (item.kind != RichPage::BlockKind::Map)) {
				continue;
			}
			any = true;
			if (!item.spoiler) {
				return false;
			}
		}
		return any;
	}
	return block.spoiler;
}

[[nodiscard]] StateBlockContainerPath BlockChildrenContainer(
		StateBlockPath path) {
	auto result = std::move(path.container);
	result.steps.push_back({
		.kind = StateBlockContainerKind::BlockChildren,
		.blockIndex = path.index,
	});
	return result;
}

[[nodiscard]] StateBlockContainerPath ListItemChildrenContainer(
		StateBlockPath path,
		int itemIndex) {
	auto result = std::move(path.container);
	result.steps.push_back({
		.kind = StateBlockContainerKind::ListItemChildren,
		.blockIndex = path.index,
		.listItemIndex = itemIndex,
	});
	return result;
}

template <typename Callback>
void EnumerateBlockPaths(
		const RichPage &page,
		const StateBlockContainerPath &container,
		Callback &&callback) {
	const auto *blocks = BlockContainer(page, container);
	if (!blocks) {
		return;
	}
	for (auto index = 0, count = int(blocks->size()); index != count; ++index) {
		const auto path = StateBlockPath{
			.container = container,
			.index = index,
		};
		const auto &block = (*blocks)[index];
		callback(path, block);
		EnumerateBlockPaths(page, BlockChildrenContainer(path), callback);
		for (auto itemIndex = 0, itemCount = int(block.listItems.size());
			itemIndex != itemCount;
			++itemIndex) {
			EnumerateBlockPaths(
				page,
				ListItemChildrenContainer(path, itemIndex),
				callback);
		}
	}
}

void EnableQTextEditLineMetrics(style::TextStyle &style) {
	style.qtextEditLineMetrics = true;
}

void EnableQTextEditLineMetrics(style::Markdown &style) {
	EnableQTextEditLineMetrics(style.body);
	EnableQTextEditLineMetrics(style.heading1);
	EnableQTextEditLineMetrics(style.heading2);
	EnableQTextEditLineMetrics(style.heading3);
	EnableQTextEditLineMetrics(style.heading4);
	EnableQTextEditLineMetrics(style.heading5);
	EnableQTextEditLineMetrics(style.heading6);
	EnableQTextEditLineMetrics(style.quoteAuthorStyle);
	EnableQTextEditLineMetrics(style.code);
	EnableQTextEditLineMetrics(style.displayMath.fallbackStyle);
	EnableQTextEditLineMetrics(style.table.headerStyle);
	EnableQTextEditLineMetrics(style.table.bodyStyle);
	EnableQTextEditLineMetrics(style.details.summaryStyle);
	EnableQTextEditLineMetrics(style.embedPost.authorStyle);
	EnableQTextEditLineMetrics(style.embedPost.dateStyle);
	EnableQTextEditLineMetrics(style.placeholder.labelStyle);
	EnableQTextEditLineMetrics(style.audio.titleStyle);
	EnableQTextEditLineMetrics(style.audio.subtitleStyle);
	EnableQTextEditLineMetrics(style.channel.titleStyle);
	EnableQTextEditLineMetrics(style.channel.subtitleStyle);
	EnableQTextEditLineMetrics(style.channel.button.textStyle);
	EnableQTextEditLineMetrics(style.relatedArticle.titleStyle);
	EnableQTextEditLineMetrics(style.relatedArticle.subtitleStyle);
	EnableQTextEditLineMetrics(style.relatedArticle.footerStyle);
}

[[nodiscard]] style::Markdown CreateEditorMarkdownStyle() {
	auto result = st::messageMarkdown;
	EnableQTextEditLineMetrics(result);
	return result;
}

[[nodiscard]] int CompareSelectionPositions(
		Markdown::MarkdownArticleSelectionPosition a,
		Markdown::MarkdownArticleSelectionPosition b) {
	if (a.segment != b.segment) {
		return (a.segment < b.segment) ? -1 : 1;
	}
	if (a.offset != b.offset) {
		return (a.offset < b.offset) ? -1 : 1;
	}
	return 0;
}

[[nodiscard]] Markdown::MarkdownArticleSelection NormalizeSelection(
		Markdown::MarkdownArticleSelection selection) {
	if (selection.empty()) {
		return {};
	}
	if (CompareSelectionPositions(selection.from, selection.to) > 0) {
		std::swap(selection.from, selection.to);
	}
	return selection;
}

[[nodiscard]] Markdown::MarkdownArticleSelectionEndpoint MakeSelectionEndpoint(
		const Markdown::MarkdownArticleHitTestResult &hit) {
	return {
		.segment = hit.segmentIndex,
		.direct = hit.direct,
	};
}

[[nodiscard]] bool RedirectTextToField(const QString &text) {
	for (const auto &ch : text) {
		if (ch.unicode() >= 32) {
			return true;
		}
	}
	return false;
}

struct InlineFieldTrimResult {
	TextWithTags text;
	int left = 0;
};

[[nodiscard]] InlineFieldTrimResult TrimInlineFieldText(
		TextWithTags text,
		bool trimLeft) {
	auto from = 0;
	auto till = int(text.text.size());
	if (trimLeft) {
		while (from < till && text.text[from].isSpace()) {
			++from;
		}
	}
	while (till > from && text.text[till - 1].isSpace()) {
		--till;
	}
	if (from == 0 && till == text.text.size()) {
		return { std::move(text), 0 };
	}
	text.text = text.text.mid(from, till - from);
	for (auto i = text.tags.begin(); i != text.tags.end();) {
		const auto tagFrom = i->offset;
		const auto tagTill = i->offset + i->length;
		const auto clippedFrom = std::max(tagFrom, from);
		const auto clippedTill = std::min(tagTill, till);
		if (clippedTill <= clippedFrom || i->length <= 0) {
			i = text.tags.erase(i);
		} else {
			i->offset = clippedFrom - from;
			i->length = clippedTill - clippedFrom;
			++i;
		}
	}
	return { std::move(text), from };
}

[[nodiscard]] int MapEditorOffsetToRichOffset(
		const std::vector<RichTextEditorOffsetReplacement> &replacements,
		int offset) {
	auto delta = 0;
	for (const auto &replacement : replacements) {
		if (replacement.richLength <= 0) {
			continue;
		}
		const auto richStart = replacement.richOffset;
		const auto editorStart = richStart + delta;
		const auto editorEnd = editorStart + replacement.editorLength;
		if (offset < editorStart) {
			break;
		} else if (offset <= editorEnd) {
			return richStart
				+ ((offset == editorEnd) ? replacement.richLength : 0);
		}
		delta += replacement.editorLength - replacement.richLength;
	}
	return offset - delta;
}

[[nodiscard]] auto ClipboardPasteInsertContext(
		std::optional<State::ActiveTextInsertContext> context)
-> std::optional<State::ActiveTextInsertContext> {
	if (context) {
		context->selected = TextWithEntities();
	}
	return context;
}

[[nodiscard]] std::optional<Ui::PreparedList> PreparedMediaFromClipboard(
		not_null<const QMimeData*> data,
		bool premium) {
	const auto hasImage = data->hasImage();
	const auto urls = Core::ReadMimeUrls(data);
	if (!urls.empty()) {
		auto list = Storage::PrepareMediaList(
			urls,
			st::sendMediaPreviewSize,
			premium);
		if (list.error != Ui::PreparedList::Error::NonLocalUrl) {
			return list;
		} else if (!hasImage) {
			return std::nullopt;
		}
	}
	if (auto read = Core::ReadMimeImage(data)) {
		return Storage::PrepareMediaFromImage(
			std::move(read.image),
			std::move(read.content),
			st::sendMediaPreviewSize);
	}
	return std::nullopt;
}

[[nodiscard]] bool IsAcceptableDropMedia(not_null<const QMimeData*> data) {
	if (data->hasFormat(u"application/x-td-forward"_q)) {
		return false;
	} else if (data->hasImage()) {
		return true;
	}
	const auto urls = Core::ReadMimeUrls(data);
	if (urls.isEmpty()) {
		return false;
	}
	for (const auto &url : urls) {
		if (!url.isLocalFile()) {
			return false;
		}
		const auto type = Core::DetectNameType(Platform::File::UrlToLocal(url));
		if (type != Core::NameType::Image
			&& type != Core::NameType::Video
			&& type != Core::NameType::Audio) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] bool CanPrepareMediaFromClipboard(
		not_null<const QMimeData*> data) {
	return IsAcceptableDropMedia(data);
}

[[nodiscard]] QString ValidateInstantViewEditorLink(QString link) {
	const auto normal = qthelp::validate_url(link);
	if (!normal.isEmpty()) {
		return normal;
	}
	link = link.trimmed();
	const auto hasPayload = [&](const QString &prefix) {
		return link.startsWith(prefix)
			&& !link.mid(prefix.size()).trimmed().isEmpty();
	};
	if (hasPayload(u"mailto:"_q)
		|| hasPayload(u"tel:"_q)
		|| (link.startsWith(u"#"_q)
			&& !Markdown::NormalizeFragmentId(link).isEmpty())) {
		return link;
	}
	return QString();
}

class MathPreview final : public Ui::RpWidget {
public:
	MathPreview(QWidget *parent);

	void setSource(QString source);
	void setColor(const style::color &color);
	void setCardWidth(int cardWidth);

	rpl::producer<int> desiredHeightValue() const override;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void rerender();
	void updateDesiredHeight();
	void relayout();

	QString _source;
	style::color _color = st::ivFormulaPreviewFg;
	QImage _image;
	QSize _logicalSize;
	int _cardWidth = 0;
	rpl::variable<int> _desiredHeight = 0;

};

MathPreview::MathPreview(QWidget *parent) : RpWidget(parent) {
	_desiredHeight = st::ivFormulaPreviewMinHeight;
}

void MathPreview::setSource(QString source) {
	source = source.trimmed();
	source.replace('\r', ' ');
	source.replace('\n', ' ');
	if (source == _source) {
		return;
	}
	_source = source;
	rerender();
}

void MathPreview::setColor(const style::color &color) {
	if (_color == color) {
		return;
	}
	_color = color;
	rerender();
}

void MathPreview::setCardWidth(int cardWidth) {
	if (_cardWidth == cardWidth) {
		return;
	}
	_cardWidth = cardWidth;
	relayout();
}

rpl::producer<int> MathPreview::desiredHeightValue() const {
	return _desiredHeight.value();
}

void MathPreview::rerender() {
	if (_source.isEmpty()) {
		_image = QImage();
		_logicalSize = QSize();
		updateDesiredHeight();
		relayout();
		update();
		return;
	}
	const auto ratio = std::max(style::DevicePixelRatio(), 1);
	const auto &math = st::defaultMarkdownDisplayMath;
	auto rendered = Markdown::RenderWithMicrotex({
		.trimmedTex = _source,
		.kind = Markdown::MathKind::Display,
		.textSize = math.textSize,
		.renderWidthCap = math.maxRenderWidth,
		.renderHeightCap = math.maxRenderHeight,
		.devicePixelRatio = ratio,
	});
	if (!rendered.measured.success || rendered.image.isNull()) {
		_image = QImage();
		_logicalSize = QSize();
		updateDesiredHeight();
		relayout();
		update();
		return;
	}
	const auto &white = rendered.image;
	auto colorized = QImage(
		white.size(),
		QImage::Format_ARGB32_Premultiplied);
	style::colorizeImage(
		white,
		_color->c,
		&colorized,
		QRect(),
		QPoint(),
		true);
	colorized.setDevicePixelRatio(white.devicePixelRatio());
	_image = std::move(colorized);
	_logicalSize = rendered.measured.logicalSize;
	updateDesiredHeight();
	relayout();
	update();
}

void MathPreview::updateDesiredHeight() {
	const auto padded = _logicalSize.height()
		+ st::ivFormulaPreviewPadding.top()
		+ st::ivFormulaPreviewPadding.bottom();
	_desiredHeight = std::max(st::ivFormulaPreviewMinHeight, padded);
}

void MathPreview::relayout() {
	const auto &padding = st::ivFormulaPreviewPadding;
	const auto width = std::max(
		_cardWidth,
		_logicalSize.width() + padding.left() + padding.right());
	resize(width, _desiredHeight.current());
}

void MathPreview::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st::ivFormulaPreviewBg);
	const auto radius = st::ivFormulaPreviewRadius;
	p.drawRoundedRect(rect(), radius, radius);
	if (_image.isNull()) {
		return;
	}
	const auto x = (width() - _logicalSize.width()) / 2;
	const auto y = (height() - _logicalSize.height()) / 2;
	p.drawImage(QPoint(x, y), _image);
}

void EditMathBox(
		not_null<Ui::GenericBox*> box,
		QString startSource,
		bool editingExisting,
		std::optional<bool> separateLine,
		Fn<void(QString, bool)> callback,
		Fn<void(bool)> setExternalInteractionActive,
		Fn<void()> restoreFocus) {
	Expects(callback != nullptr);
	Expects(setExternalInteractionActive != nullptr);

	setExternalInteractionActive(true);
	box->boxClosing() | rpl::on_next([=] {
		setExternalInteractionActive(false);
		if (restoreFocus) {
			restoreFocus();
		}
	}, box->lifetime());

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_formatting_math_source_label(),
			st::ivFormulaSectionTitle),
		st::ivFormulaPreviewLabelMargin);
	const auto source = box->addRow(
		object_ptr<Ui::InputField>(
			box,
			st::ivFormulaSourceField,
			Ui::InputField::Mode::MultiLine,
			tr::lng_formatting_math_source_placeholder(),
			startSource),
		st::markdownLinkFieldPadding);
	source->setSubmitSettings(Ui::InputField::SubmitSettings::Enter);
	source->setMinHeight(source->st().heightMin);
	const auto separateLineField = separateLine
		? box->addRow(
			object_ptr<Ui::Checkbox>(
				box,
				tr::lng_formatting_math_separate_line(tr::now),
				*separateLine,
				st::defaultBoxCheckbox),
			st::markdownMathCheckboxMargin)
		: nullptr;
	auto checkboxHeight = separateLineField
		? separateLineField->heightValue()
		: rpl::single(0);
	rpl::combine(
		source->topValue(),
		box->getDelegate()->contentHeightMaxValue(),
		std::move(checkboxHeight)
	) | rpl::on_next([=](int top, int contentHeight, int checkboxHeight) {
		const auto checkboxBlock = separateLineField
			? (checkboxHeight
				+ st::markdownMathCheckboxMargin.top()
				+ st::markdownMathCheckboxMargin.bottom())
			: 0;
		source->setMaxHeight(std::max(
			source->st().heightMin,
			std::min(
				st::markdownMathFieldMaxHeight,
				contentHeight
					- top
					- st::markdownLinkFieldPadding.bottom()
					- checkboxBlock)));
	}, source->lifetime());

	const auto submit = [=] {
		auto sourceText = source->getLastText().trimmed();
		sourceText.replace('\r', ' ');
		sourceText.replace('\n', ' ');
		if (sourceText.isEmpty()) {
			source->showError();
			return;
		}
		const auto weak = base::make_weak(box);
		callback(
			sourceText,
			separateLineField && separateLineField->checked());
		if (weak) {
			box->closeBox();
		}
	};
	source->submits(
	) | rpl::on_next(submit, source->lifetime());

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_formatting_math_result_label(),
			st::ivFormulaSectionTitle),
		st::ivFormulaPreviewLabelMargin);
	const auto host = box->addRow(
		object_ptr<Ui::RpWidget>(box),
		st::ivFormulaPreviewMargin);
	const auto scroll = Ui::CreateChild<Ui::ScrollArea>(
		host,
		st::ivFormulaPreviewScroll,
		true);
	const auto preview = scroll->setOwnedWidget(
		object_ptr<MathPreview>(scroll));
	host->sizeValue(
	) | rpl::on_next([=](QSize size) {
		scroll->setGeometry(QRect(QPoint(), size));
	}, host->lifetime());
	const auto cardWidth = st::boxWidth
		- st::ivFormulaPreviewMargin.left()
		- st::ivFormulaPreviewMargin.right();
	preview->setCardWidth(cardWidth);
	preview->desiredHeightValue(
	) | rpl::on_next([=](int desiredHeight) {
		host->resize(cardWidth, desiredHeight);
	}, host->lifetime());
	preview->setSource(startSource);
	const auto applyRandomSample = [=] {
		const auto &sample = kFormulaSamples[
			base::RandomIndex(int(kFormulaSamples.size()))];
		source->setPlaceholder(rpl::single(sample));
		preview->setColor(st::windowSubTextFg);
		preview->setSource(sample);
	};
	const auto wasEmpty = box->lifetime().make_state<bool>(
		startSource.isEmpty());
	if (startSource.isEmpty()) {
		applyRandomSample();
	}
	source->changes(
	) | rpl::on_next([=] {
		const auto text = source->getLastText();
		if (text.isEmpty()) {
			if (!*wasEmpty) {
				*wasEmpty = true;
				applyRandomSample();
			}
		} else {
			*wasEmpty = false;
			preview->setColor(st::ivFormulaPreviewFg);
			preview->setSource(text);
		}
	}, source->lifetime());

	box->setTitle(editingExisting
		? tr::lng_formatting_math_edit_title()
		: tr::lng_formatting_math_create_title());
	box->addButton(tr::lng_settings_save(), submit);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });

	box->verticalLayout()->resizeToWidth(st::boxWidth);
	box->verticalLayout()->moveToLeft(0, 0);
	box->setWidth(st::boxWidth);

	box->setFocusCallback([=] {
		if (!startSource.isEmpty()) {
			source->selectAll();
		}
		source->setFocusFast();
	});
}

[[nodiscard]] bool ImeEventProducesInput(
		const QInputMethodEvent &e,
		const QTextCursor &cursor) {
	return !e.commitString().isEmpty()
		|| e.preeditString() != cursor.block().layout()->preeditAreaText()
		|| e.replacementLength() > 0;
}

using PreparedEditBlockContainerPath
	= Markdown::PreparedEditBlockContainerPath;
using PreparedEditBlockContainerStep
	= Markdown::PreparedEditBlockContainerStep;
using PreparedEditBlockContainerKind
	= Markdown::PreparedEditBlockContainerKind;
using PreparedEditBlockPath = Markdown::PreparedEditBlockPath;
using PreparedEditBlockSource = Markdown::PreparedEditBlockSource;
using PreparedEditHit = Markdown::PreparedEditHit;
using PreparedEditHitKind = Markdown::PreparedEditHitKind;
using PreparedEditLeafKind = Markdown::PreparedEditLeafKind;
using PreparedEditDropTarget = Markdown::PreparedEditDropTarget;
using PreparedEditBlockDropTarget = Markdown::PreparedEditBlockDropTarget;
using PreparedEditLeafSource = Markdown::PreparedEditLeafSource;
using PreparedEditListItemDropTarget = Markdown::PreparedEditListItemDropTarget;
using PreparedEditListItemSource = Markdown::PreparedEditListItemSource;
using PreparedEditSelection = Markdown::PreparedEditSelection;
using PreparedEditSelectionKind = Markdown::PreparedEditSelectionKind;
using PreparedEditTableCellRange = Markdown::PreparedEditTableCellRange;
using PreparedEditTableCellSource = Markdown::PreparedEditTableCellSource;
using PreparedEditTableRowSource = Markdown::PreparedEditTableRowSource;
using PreparedEditTextDropTarget = Markdown::PreparedEditTextDropTarget;
using ApplyResult = State::ApplyResult;
using PreparedMutationKind = State::PreparedMutationKind;

[[nodiscard]] bool SnapshotEquals(
		const State::Snapshot &a,
		const State::Snapshot &b) {
	return RichPagesEqual(a.richPage, b.richPage)
		&& (a.activeLeaf == b.activeLeaf)
		&& (a.temporaryDownParagraph == b.temporaryDownParagraph);
}

struct NormalizedIntegerRange {
	int from = -1;
	int till = -1;

	[[nodiscard]] bool empty() const {
		return (from < 0) || (till <= from);
	}
};

[[nodiscard]] NormalizedIntegerRange NormalizeIntegerRange(int a, int b) {
	if (a < 0 || b < 0) {
		return {};
	}
	return {
		.from = std::min(a, b),
		.till = std::max(a, b) + 1,
	};
}

[[nodiscard]] PreparedEditSelection BlockSelectionFromIndexes(
		PreparedEditBlockContainerPath container,
		int first,
		int second) {
	const auto range = NormalizeIntegerRange(first, second);
	if (range.empty()) {
		return {};
	}
	return {
		.kind = PreparedEditSelectionKind::Blocks,
		.blocks = {
			.container = std::move(container),
			.from = range.from,
			.till = range.till,
		},
	};
}

[[nodiscard]] bool SingleRootPlainTextFieldSelectAllPassthrough(
		const RichPage &page,
		const std::optional<StateLeafPath> &leaf,
		bool fieldHidden) {
	if (fieldHidden
		|| !leaf
		|| (page.blocks.size() != 1)
		|| (leaf->kind != StateLeafKind::BlockText)
		|| !leaf->block.container.steps.empty()
		|| (leaf->block.index != 0)) {
		return false;
	}
	switch (page.blocks[0].kind) {
	case RichPage::BlockKind::Paragraph:
	case RichPage::BlockKind::Heading:
		return true;
	default:
		return false;
	}
}

[[nodiscard]] int CompareIntegers(int a, int b) {
	return (a < b) ? -1 : (a > b) ? 1 : 0;
}

[[nodiscard]] int ComparePreparedEditBlockContainerSteps(
		const PreparedEditBlockContainerStep &a,
		const PreparedEditBlockContainerStep &b) {
	if (const auto result = CompareIntegers(
			static_cast<int>(a.kind),
			static_cast<int>(b.kind))) {
		return result;
	} else if (const auto result = CompareIntegers(
			a.blockIndex,
			b.blockIndex)) {
		return result;
	}
	return CompareIntegers(a.listItemIndex, b.listItemIndex);
}

[[nodiscard]] int ComparePreparedEditBlockContainerPaths(
		const PreparedEditBlockContainerPath &a,
		const PreparedEditBlockContainerPath &b) {
	const auto common = std::min(a.steps.size(), b.steps.size());
	for (auto i = size_t(); i != common; ++i) {
		if (const auto result = ComparePreparedEditBlockContainerSteps(
				a.steps[i],
				b.steps[i])) {
			return result;
		}
	}
	return CompareIntegers(int(a.steps.size()), int(b.steps.size()));
}

[[nodiscard]] int ComparePreparedEditBlockPaths(
		const PreparedEditBlockPath &a,
		const PreparedEditBlockPath &b) {
	if (const auto result = ComparePreparedEditBlockContainerPaths(
			a.container,
			b.container)) {
		return result;
	}
	return CompareIntegers(a.index, b.index);
}

[[nodiscard]] bool SamePreparedEditBlockPath(
		const PreparedEditBlockPath &a,
		const PreparedEditBlockPath &b) {
	return (ComparePreparedEditBlockPaths(a, b) == 0);
}

[[nodiscard]] bool ValidPreparedEditBlockPath(
		const PreparedEditBlockPath &path) {
	return (path.index >= 0);
}

[[nodiscard]] PreparedEditBlockSource PreparedEditBlockSourceFromPath(
		PreparedEditBlockPath path) {
	return { .path = std::move(path) };
}

enum class StructuralOwnerKind {
	None,
	Block,
	ListItem,
	TableRow,
	TableCell,
};

struct StructuralOwner {
	StructuralOwnerKind kind = StructuralOwnerKind::None;
	std::optional<PreparedEditBlockSource> block;
	std::optional<PreparedEditListItemSource> listItem;
	std::optional<PreparedEditTableRowSource> tableRow;
	std::optional<PreparedEditTableCellSource> tableCell;

	[[nodiscard]] bool valid() const {
		return (kind != StructuralOwnerKind::None);
	}
};

[[nodiscard]] StructuralOwner StructuralOwnerFromBlock(
		const PreparedEditBlockSource &source) {
	if (!ValidPreparedEditBlockPath(source.path)) {
		return {};
	}
	return {
		.kind = StructuralOwnerKind::Block,
		.block = source,
	};
}

[[nodiscard]] StructuralOwner StructuralOwnerFromListItem(
		const PreparedEditListItemSource &source) {
	if (!ValidPreparedEditBlockPath(source.block)
		|| source.listItemIndex < 0) {
		return {};
	}
	return {
		.kind = StructuralOwnerKind::ListItem,
		.block = PreparedEditBlockSourceFromPath(source.block),
		.listItem = source,
	};
}

[[nodiscard]] StructuralOwner StructuralOwnerFromTableRow(
		const PreparedEditTableRowSource &source) {
	if (!ValidPreparedEditBlockPath(source.block)
		|| source.tableRowIndex < 0) {
		return {};
	}
	return {
		.kind = StructuralOwnerKind::TableRow,
		.block = PreparedEditBlockSourceFromPath(source.block),
		.tableRow = source,
	};
}

[[nodiscard]] PreparedEditTableRowSource PreparedEditTableRowFromCell(
		const PreparedEditTableCellSource &source) {
	return {
		.block = source.block,
		.tableRowIndex = source.tableRowIndex,
	};
}

[[nodiscard]] StructuralOwner StructuralOwnerFromTableCell(
		const PreparedEditTableCellSource &source) {
	if (!ValidPreparedEditBlockPath(source.block)
		|| source.tableRowIndex < 0
		|| source.tableCellIndex < 0
		|| source.column < 0
		|| source.colspan <= 0
		|| source.rowspan <= 0) {
		return {};
	}
	return {
		.kind = StructuralOwnerKind::TableCell,
		.block = PreparedEditBlockSourceFromPath(source.block),
		.tableRow = PreparedEditTableRowFromCell(source),
		.tableCell = source,
	};
}

[[nodiscard]] StructuralOwner StructuralOwnerFromLeaf(
		const PreparedEditLeafSource &source) {
	if (!ValidPreparedEditBlockPath(source.block)) {
		return {};
	}
	switch (source.kind) {
	case PreparedEditLeafKind::ListItemText:
		return StructuralOwnerFromListItem({
			.block = source.block,
			.listItemIndex = source.listItemIndex,
		});
	case PreparedEditLeafKind::TableCellText:
		return {};
	case PreparedEditLeafKind::BlockText:
	case PreparedEditLeafKind::BlockCaption:
	case PreparedEditLeafKind::MathFormula:
		return StructuralOwnerFromBlock(
			PreparedEditBlockSourceFromPath(source.block));
	}
	return {};
}

[[nodiscard]] StructuralOwner StructuralOwnerFromHit(
		const PreparedEditHit &hit) {
	if (!hit.valid()) {
		return {};
	}
	switch (hit.kind) {
	case PreparedEditHitKind::Block:
		if (hit.block) {
			return StructuralOwnerFromBlock(*hit.block);
		}
		break;
	case PreparedEditHitKind::ListItem:
		if (hit.listItem) {
			return StructuralOwnerFromListItem(*hit.listItem);
		}
		break;
	case PreparedEditHitKind::TableRow:
		if (hit.tableRow) {
			return StructuralOwnerFromTableRow(*hit.tableRow);
		}
		break;
	case PreparedEditHitKind::TableCell:
		if (hit.tableCell) {
			return StructuralOwnerFromTableCell(*hit.tableCell);
		}
		break;
	case PreparedEditHitKind::Leaf:
		if (hit.leaf) {
			return StructuralOwnerFromLeaf(*hit.leaf);
		}
		break;
	case PreparedEditHitKind::None:
		break;
	}
	return hit.leaf ? StructuralOwnerFromLeaf(*hit.leaf) : StructuralOwner();
}

[[nodiscard]] std::optional<PreparedEditTableCellSource> TableCellFromOwner(
		const StructuralOwner &owner) {
	return owner.tableCell;
}

[[nodiscard]] PreparedEditTableCellRange TableRangeFromCell(
		const PreparedEditTableCellSource &source) {
	if (source.tableRowIndex < 0
		|| source.column < 0
		|| source.rowspan <= 0
		|| source.colspan <= 0) {
		return {};
	}
	return {
		.block = source.block,
		.rowFrom = source.tableRowIndex,
		.rowTill = source.tableRowIndex + source.rowspan,
		.columnFrom = source.column,
		.columnTill = source.column + source.colspan,
	};
}

[[nodiscard]] bool SameTableRangeBlock(
		const PreparedEditTableCellRange &a,
		const PreparedEditTableCellRange &b) {
	return !a.empty()
		&& !b.empty()
		&& SamePreparedEditBlockPath(a.block, b.block);
}

[[nodiscard]] bool TableRangeContainsCell(
		const PreparedEditTableCellRange &range,
		const PreparedEditTableCellSource &source) {
	const auto cell = TableRangeFromCell(source);
	return SameTableRangeBlock(range, cell)
		&& (range.rowFrom <= cell.rowFrom)
		&& (range.rowTill >= cell.rowTill)
		&& (range.columnFrom <= cell.columnFrom)
		&& (range.columnTill >= cell.columnTill);
}

[[nodiscard]] PreparedEditTableCellRange TableRangesUnion(
		const PreparedEditTableCellRange &a,
		const PreparedEditTableCellRange &b) {
	if (!SameTableRangeBlock(a, b)) {
		return {};
	}
	return {
		.block = a.block,
		.rowFrom = std::min(a.rowFrom, b.rowFrom),
		.rowTill = std::max(a.rowTill, b.rowTill),
		.columnFrom = std::min(a.columnFrom, b.columnFrom),
		.columnTill = std::max(a.columnTill, b.columnTill),
	};
}

[[nodiscard]] std::optional<PreparedEditTableRowSource> TableRowFromOwner(
		const StructuralOwner &owner) {
	return owner.tableRow;
}

[[nodiscard]] std::optional<PreparedEditListItemSource> ListItemFromOwner(
		const StructuralOwner &owner) {
	return owner.listItem;
}

[[nodiscard]] std::optional<PreparedEditListItemSource> ListItemSourceFromLeaf(
		const PreparedEditLeafSource &source) {
	if (source.kind != PreparedEditLeafKind::ListItemText
		|| !ValidPreparedEditBlockPath(source.block)
		|| source.listItemIndex < 0) {
		return std::nullopt;
	}
	return PreparedEditListItemSource{
		.block = source.block,
		.listItemIndex = source.listItemIndex,
	};
}

[[nodiscard]] PreparedListItemRange ListRangeFromItem(
		const PreparedEditListItemSource &source) {
	if (!ValidPreparedEditBlockPath(source.block) || source.listItemIndex < 0) {
		return {};
	}
	return {
		.block = source.block,
		.from = source.listItemIndex,
		.till = source.listItemIndex + 1,
	};
}

[[nodiscard]] bool IsBlockOwner(const StructuralOwner &owner) {
	return (owner.kind == StructuralOwnerKind::Block);
}

[[nodiscard]] std::optional<PreparedEditBlockPath> BlockPathFromOwner(
		const StructuralOwner &owner) {
	if (owner.kind == StructuralOwnerKind::Block && owner.block) {
		return owner.block->path;
	} else if (owner.kind == StructuralOwnerKind::ListItem
		&& owner.listItem) {
		return owner.listItem->block;
	} else if (owner.kind == StructuralOwnerKind::TableRow
		&& owner.tableRow) {
		return owner.tableRow->block;
	} else if (owner.kind == StructuralOwnerKind::TableCell
		&& owner.tableCell) {
		return owner.tableCell->block;
	}
	return std::nullopt;
}

struct LiftedPreparedEditBlocks {
	PreparedEditBlockContainerPath container;
	int first = -1;
	int second = -1;
};

[[nodiscard]] PreparedEditBlockContainerPath PreparedEditBlockContainerPrefix(
		const PreparedEditBlockContainerPath &path,
		int count) {
	auto result = PreparedEditBlockContainerPath();
	const auto till = std::clamp(count, 0, int(path.steps.size()));
	result.steps.insert(
		result.steps.end(),
		path.steps.begin(),
		path.steps.begin() + till);
	return result;
}

[[nodiscard]] int CommonPreparedEditBlockContainerSize(
		const PreparedEditBlockContainerPath &a,
		const PreparedEditBlockContainerPath &b) {
	const auto common = std::min(a.steps.size(), b.steps.size());
	for (auto i = size_t(); i != common; ++i) {
		if (ComparePreparedEditBlockContainerSteps(
				a.steps[i],
				b.steps[i]) != 0) {
			return int(i);
		}
	}
	return int(common);
}

[[nodiscard]] int LiftedPreparedEditBlockIndex(
		const PreparedEditBlockPath &path,
		int commonContainerSize) {
	if (commonContainerSize == int(path.container.steps.size())) {
		return path.index;
	} else if (commonContainerSize >= 0
		&& commonContainerSize < int(path.container.steps.size())) {
		return path.container.steps[commonContainerSize].blockIndex;
	}
	return -1;
}

[[nodiscard]] std::optional<LiftedPreparedEditBlocks>
LiftPreparedEditBlocksToCommonContainer(
		const PreparedEditBlockPath &a,
		const PreparedEditBlockPath &b) {
	if (!ValidPreparedEditBlockPath(a) || !ValidPreparedEditBlockPath(b)) {
		return std::nullopt;
	}
	const auto common = CommonPreparedEditBlockContainerSize(
		a.container,
		b.container);
	auto result = LiftedPreparedEditBlocks{
		.container = PreparedEditBlockContainerPrefix(a.container, common),
		.first = LiftedPreparedEditBlockIndex(a, common),
		.second = LiftedPreparedEditBlockIndex(b, common),
	};
	if (result.first < 0 || result.second < 0) {
		return std::nullopt;
	}
	return result;
}

[[nodiscard]] PreparedEditSelection LiftedBlockSelection(
		const PreparedEditBlockPath &anchor,
		const PreparedEditBlockPath &focus) {
	const auto lifted = LiftPreparedEditBlocksToCommonContainer(anchor, focus);
	if (!lifted) {
		return {};
	}
	return BlockSelectionFromIndexes(
		lifted->container,
		lifted->first,
		lifted->second);
}

[[nodiscard]] auto ListItemSourcesFromBlockPath(
		const PreparedEditBlockPath &path)
-> std::vector<PreparedEditListItemSource> {
	auto result = std::vector<PreparedEditListItemSource>();
	for (auto i = int(path.container.steps.size()); i != 0; --i) {
		const auto stepIndex = i - 1;
		const auto &step = path.container.steps[stepIndex];
		if (step.kind != PreparedEditBlockContainerKind::ListItemChildren
			|| step.blockIndex < 0
			|| step.listItemIndex < 0) {
			continue;
		}
		result.push_back({
			.block = {
				.container = PreparedEditBlockContainerPrefix(
					path.container,
					stepIndex),
				.index = step.blockIndex,
			},
			.listItemIndex = step.listItemIndex,
		});
	}
	return result;
}

[[nodiscard]] auto ListItemSourcesFromOwner(
		const StructuralOwner &owner,
		const std::optional<PreparedEditBlockPath> &block)
-> std::vector<PreparedEditListItemSource> {
	auto result = std::vector<PreparedEditListItemSource>();
	if (const auto listItem = ListItemFromOwner(owner)) {
		result.push_back(*listItem);
	}
	if (!block) {
		return result;
	}
	for (const auto &source : ListItemSourcesFromBlockPath(*block)) {
		if (std::find(result.begin(), result.end(), source) == result.end()) {
			result.push_back(source);
		}
	}
	return result;
}

[[nodiscard]] auto ListContextSources(
		const std::optional<PreparedEditListItemSource> &source,
		const std::optional<PreparedEditBlockPath> &block)
-> std::vector<PreparedEditListItemSource> {
	auto result = std::vector<PreparedEditListItemSource>();
	if (source) {
		result.push_back(*source);
	}
	if (!block) {
		return result;
	}
	for (const auto &candidate : ListItemSourcesFromBlockPath(*block)) {
		if (std::find(result.begin(), result.end(), candidate) == result.end()) {
			result.push_back(candidate);
		}
	}
	return result;
}

[[nodiscard]] QString OrderedListTypeText(PreparedOrderedListType type) {
	switch (type) {
	case PreparedOrderedListType::LowerAlpha:
		return tr::lng_article_list_lowercase_letters(tr::now);
	case PreparedOrderedListType::UpperAlpha:
		return tr::lng_article_list_uppercase_letters(tr::now);
	case PreparedOrderedListType::LowerRoman:
		return tr::lng_article_list_lowercase_roman(tr::now);
	case PreparedOrderedListType::UpperRoman:
		return tr::lng_article_list_uppercase_roman(tr::now);
	case PreparedOrderedListType::Decimal:
		return tr::lng_article_list_numbers(tr::now);
	}
	return QString();
}

[[nodiscard]] const style::icon *OrderedListTypeIcon(
		PreparedOrderedListType type) {
	switch (type) {
	case PreparedOrderedListType::LowerAlpha:
		return &st::ivEditorToolbarOrderedListAlphaLowerIcon;
	case PreparedOrderedListType::UpperAlpha:
		return &st::ivEditorToolbarOrderedListAlphaUpperIcon;
	case PreparedOrderedListType::LowerRoman:
		return &st::ivEditorToolbarOrderedListRomanLowerIcon;
	case PreparedOrderedListType::UpperRoman:
		return &st::ivEditorToolbarOrderedListRomanUpperIcon;
	case PreparedOrderedListType::Decimal:
		return &st::ivEditorToolbarOrderedListIcon;
	}
	return nullptr;
}

[[nodiscard]] bool OrderedListTypeChecked(
		const State::ListSelectionInfo &info,
		PreparedOrderedListType type) {
	switch (type) {
	case PreparedOrderedListType::LowerAlpha:
		return info.allOrderedLowerAlpha;
	case PreparedOrderedListType::UpperAlpha:
		return info.allOrderedUpperAlpha;
	case PreparedOrderedListType::LowerRoman:
		return info.allOrderedLowerRoman;
	case PreparedOrderedListType::UpperRoman:
		return info.allOrderedUpperRoman;
	case PreparedOrderedListType::Decimal:
		return info.allOrderedDecimal;
	}
	return false;
}

[[nodiscard]] PreparedEditSelection ListItemSelectionFromSources(
		const std::vector<PreparedEditListItemSource> &anchorSources,
		const std::vector<PreparedEditListItemSource> &focusSources) {
	for (const auto &anchorListItem : anchorSources) {
		for (const auto &focusListItem : focusSources) {
			if (!SamePreparedEditBlockPath(
					anchorListItem.block,
					focusListItem.block)) {
				continue;
			}
			const auto range = NormalizeIntegerRange(
				anchorListItem.listItemIndex,
				focusListItem.listItemIndex);
			if (!range.empty()) {
				return {
					.kind = PreparedEditSelectionKind::ListItems,
					.listItems = {
						.block = anchorListItem.block,
						.from = range.from,
						.till = range.till,
					},
				};
			}
		}
	}
	return {};
}

[[nodiscard]] PreparedEditHit PreparedEditHitFromBlockSelection(
		const PreparedBlockRange &range,
		bool forward) {
	if (range.empty()) {
		return {};
	}
	const auto index = forward ? (range.till - 1) : range.from;
	if (index < 0) {
		return {};
	}
	return {
		.kind = PreparedEditHitKind::Block,
		.block = PreparedEditBlockSource{
			.path = {
				.container = range.container,
				.index = index,
			},
		},
	};
}

[[nodiscard]] PreparedEditHit PreparedEditHitFromListItemSelection(
		const PreparedListItemRange &range,
		bool forward) {
	if (range.empty()) {
		return {};
	}
	const auto index = forward ? (range.till - 1) : range.from;
	if (index < 0) {
		return {};
	}
	return {
		.kind = PreparedEditHitKind::ListItem,
		.listItem = PreparedEditListItemSource{
			.block = range.block,
			.listItemIndex = index,
		},
	};
}

[[nodiscard]] PreparedEditSelection SelectionFromStructuralOwner(
		const StructuralOwner &owner) {
	switch (owner.kind) {
	case StructuralOwnerKind::Block:
		return owner.block
			? BlockSelectionFromIndexes(
				owner.block->path.container,
				owner.block->path.index,
				owner.block->path.index)
			: PreparedEditSelection();
	case StructuralOwnerKind::ListItem:
		return owner.listItem
			? PreparedEditSelection{
				.kind = PreparedEditSelectionKind::ListItems,
				.listItems = {
					.block = owner.listItem->block,
					.from = owner.listItem->listItemIndex,
					.till = owner.listItem->listItemIndex + 1,
				},
			}
			: PreparedEditSelection();
	case StructuralOwnerKind::TableRow:
		return owner.tableRow
			? PreparedEditSelection{
				.kind = PreparedEditSelectionKind::TableRows,
				.tableRows = {
					.block = owner.tableRow->block,
					.from = owner.tableRow->tableRowIndex,
					.till = owner.tableRow->tableRowIndex + 1,
				},
			}
			: PreparedEditSelection();
	case StructuralOwnerKind::TableCell:
		return owner.tableCell
			? PreparedEditSelection{
				.kind = PreparedEditSelectionKind::TableCells,
				.tableCells = TableRangeFromCell(*owner.tableCell),
			}
			: PreparedEditSelection();
	case StructuralOwnerKind::None:
		break;
	}
	return {};
}

[[nodiscard]] PreparedEditSelection EdgeSelection(
		const PreparedEditSelection &selection,
		bool forward) {
	switch (selection.kind) {
	case PreparedEditSelectionKind::Blocks:
		if (selection.blocks.empty()) {
			return {};
		}
		return {
			.kind = PreparedEditSelectionKind::Blocks,
			.blocks = {
				.container = selection.blocks.container,
				.from = forward
					? (selection.blocks.till - 1)
					: selection.blocks.from,
				.till = forward
					? selection.blocks.till
					: (selection.blocks.from + 1),
			},
		};
	case PreparedEditSelectionKind::ListItems:
		if (selection.listItems.empty()) {
			return {};
		}
		return {
			.kind = PreparedEditSelectionKind::ListItems,
			.listItems = {
				.block = selection.listItems.block,
				.from = forward
					? (selection.listItems.till - 1)
					: selection.listItems.from,
				.till = forward
					? selection.listItems.till
					: (selection.listItems.from + 1),
			},
		};
	case PreparedEditSelectionKind::TableRows:
		if (selection.tableRows.empty()) {
			return {};
		}
		return {
			.kind = PreparedEditSelectionKind::TableRows,
			.tableRows = {
				.block = selection.tableRows.block,
				.from = forward
					? (selection.tableRows.till - 1)
					: selection.tableRows.from,
				.till = forward
					? selection.tableRows.till
					: (selection.tableRows.from + 1),
			},
		};
	case PreparedEditSelectionKind::TableCells:
		if (selection.tableCells.empty()) {
			return {};
		}
		return {
			.kind = PreparedEditSelectionKind::TableCells,
			.tableCells = {
				.block = selection.tableCells.block,
				.rowFrom = forward
					? (selection.tableCells.rowTill - 1)
					: selection.tableCells.rowFrom,
				.rowTill = forward
					? selection.tableCells.rowTill
					: (selection.tableCells.rowFrom + 1),
				.columnFrom = forward
					? (selection.tableCells.columnTill - 1)
					: selection.tableCells.columnFrom,
				.columnTill = forward
					? selection.tableCells.columnTill
					: (selection.tableCells.columnFrom + 1),
			},
		};
	case PreparedEditSelectionKind::None:
		break;
	}
	return {};
}

[[nodiscard]] std::optional<int> StructuralSelectionEdgeTextOrdinal(
		const State &state,
		const PreparedEditSelection &selection,
		bool forward) {
	const auto edge = EdgeSelection(selection, forward);
	if (edge.empty()) {
		return std::nullopt;
	}
	const auto &nodes = state.textNodes();
	const auto &page = state.richPage();
	if (forward) {
		for (auto i = int(nodes.size()); i != 0; --i) {
			const auto ordinal = i - 1;
			const auto &descriptor = nodes[ordinal];
			if (LeafSelectedStructurally(page, descriptor.leaf, edge)) {
				return ordinal;
			}
		}
	} else {
		for (auto ordinal = 0, count = int(nodes.size()); ordinal != count;
				++ordinal) {
			const auto &descriptor = nodes[ordinal];
			if (LeafSelectedStructurally(page, descriptor.leaf, edge)) {
				return ordinal;
			}
		}
	}
	return std::nullopt;
}

[[nodiscard]] bool IsMultiListItemSelection(
		const PreparedEditSelection &selection) {
	return !selection.empty()
		&& (selection.kind == PreparedEditSelectionKind::ListItems)
		&& (selection.listItems.till > selection.listItems.from + 1);
}

[[nodiscard]] int FieldNaturalHeight(not_null<Ui::InputField*> field) {
	const auto margins = field->fullTextMargins();
	return std::max(
		int(std::ceil(field->document()->size().height()))
			+ margins.top()
			+ margins.bottom(),
		1);
}

[[nodiscard]] QPoint LocalPosition(QWheelEvent *e) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	return e->position().toPoint();
#else // Qt >= 6.0
	return e->pos();
#endif // Qt >= 6.0
}

[[nodiscard]] QPoint GlobalPosition(QWheelEvent *e) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	return e->globalPosition().toPoint();
#else // Qt >= 6.0
	return e->globalPos();
#endif // Qt >= 6.0
}

} // namespace

Widget::Widget(
	QWidget *parent,
	WidgetServices services,
	not_null<PeerData*> peer,
	std::shared_ptr<State> state,
	Fn<void(RichMessageLimitError)> showLimitToast)
: Ui::RpWidget(parent)
, _session(services.session)
, _show(std::move(services.show))
, _outer(services.outer)
, _customEmojiPaused(std::move(services.customEmojiPaused))
, _requestMedia(std::move(services.requestMedia))
, _applyPreparedMedia(std::move(services.applyPreparedMedia))
, _requestPhotoEditSource(std::move(services.requestPhotoEditSource))
, _replacePhotoWithList(std::move(services.replacePhotoWithList))
, _mediaUploadState(std::move(services.mediaUploadState))
, _cancelMediaUpload(std::move(services.cancelMediaUpload))
, _addMediaAndGroupWithBlock(std::move(services.addMediaAndGroupWithBlock))
, _peer(peer)
, _state(std::move(state))
, _showLimitToast(std::move(showLimitToast))
, _articleStyle(std::make_shared<style::Markdown>(
	CreateEditorMarkdownStyle()))
, _article(std::make_shared<Markdown::MarkdownArticle>(*_articleStyle))
, _theme(CreateStandaloneChatTheme())
, _style(std::make_unique<Ui::ChatStyle>(style::main_palette::get())) {
	_style->apply(_theme.get());
	_highlightColors = HighlightColors(_style.get());

	setMouseTracking(true);
	setAttribute(Qt::WA_AcceptTouchEvents);
	setFocusPolicy(Qt::StrongFocus);
	setAttribute(Qt::WA_InputMethodEnabled);
	setAcceptDrops(true);

	std::move(services.imeCompositionStarts) | rpl::filter([=] {
		return redirectImeToField();
	}) | rpl::on_next([=] {
		if (prepareFieldForInput()) {
			_field->setFocusFast();
		}
	}, lifetime());

	Spellchecker::HighlightReady(
	) | rpl::on_next([=](Spellchecker::HighlightProcessId processId) {
		if (_article && _article->highlightProcessDone(processId)) {
			update();
		}
	}, _highlightReadyLifetime);

	style::PaletteChanged() | rpl::on_next([=] {
		refreshPalette();
	}, lifetime());

	const auto weak = QPointer<Widget>(this);
	_article->setTextRepaintCallbacks(
		[=] {
			if (weak) {
				weak->update();
			}
		},
		[=](QRect rect) {
			if (!weak) {
				return;
			} else if (rect.isEmpty()) {
				weak->update();
			} else {
				weak->update(rect.translated(weak->articleTopLeft()));
			}
		});
	_article->setMediaBlockHost(this);

	_selectScroll.scrolls(
	) | rpl::on_next([=](int delta) {
		const auto scroll = selectionScrollArea();
		if (!scroll) {
			_selectScroll.cancel();
			return;
		}
		scroll->scrollToY(scroll->scrollTop() + delta);
		updateArticleSelectionDragFromCursor();
	}, lifetime());

	const auto &fieldStyle = inlineFieldStyleFor(
		Markdown::MarkdownArticleTextLeafStyle());
	_activeFieldStyleKey = fieldStyle.key;
	_field = base::make_unique_q<Ui::InputField>(
		this,
		*fieldStyle.style,
		Ui::InputField::Mode::MultiLine,
		rpl::single(QString()));
	setupInlineField();
	refreshPreparedContent();
	_history.push_back(captureHistoryEntry());
	_historyIndex = 0;

	base::install_event_filter(this, qApp, [=](not_null<QEvent*> e) {
		if (e->type() != QEvent::ShortcutOverride) {
			return base::EventFilterResult::Continue;
		}
		const auto top = window();
		if (!top || !top->isActiveWindow()) {
			return base::EventFilterResult::Continue;
		}
		const auto event = static_cast<QKeyEvent*>(e.get());
		if (event->isAccepted()) {
			return base::EventFilterResult::Continue;
		} else if ((event->modifiers() & Qt::ControlModifier)
			&& (event->key() == Qt::Key_F)
			&& !searchBlockedByLayer()) {
			event->accept();
			toggleSearch();
			return base::EventFilterResult::Cancel;
		} else if (handleUndoRedoShortcutOverride(event)) {
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	});
}

Widget::~Widget() {
	if (_keyboardStructuralSelectionActive) {
		releaseKeyboard();
	}
	if (_article) {
		_article->setTextRepaintCallbacks(nullptr, nullptr);
		_article->setMediaBlockHost(nullptr);
	}
}

void Widget::activateInitialNode() {
	const auto ordinal = (_activeOrdinal >= 0)
		? _activeOrdinal
		: _state->activeTextOrdinal();
	if (ordinal < 0) {
		const auto first = _article->firstEditableSegmentIndex();
		const auto fallback = editableOrdinalForSegment(first);
		if (fallback < 0) {
			return;
		}
		activateTextOrdinal(fallback, 0);
		return;
	}
	activateTextOrdinal(ordinal, 0);
}

void Widget::activateSegment(int segmentIndex, int cursorOffset) {
	const auto ordinal = editableOrdinalForSegment(segmentIndex);
	if (ordinal < 0) {
		return;
	}
	activateTextOrdinal(ordinal, cursorOffset);
}

bool Widget::prepareFieldForInput() {
	if (hasStructuralSelection()) {
		if (const auto target = removeCurrentStructuralSelection(true)) {
			activateTextOrdinal(*target, 0);
		} else {
			activateInitialNode();
		}
	} else if (_field->isHidden()) {
		activateInitialNode();
	}
	return !_field->isHidden();
}

bool Widget::replayKeyIntoField(QKeyEvent *e) {
	if (!RedirectTextToField(e->text()) || !prepareFieldForInput()) {
		return false;
	}
	_field->setFocusFast();
	QCoreApplication::sendEvent(_field->rawTextEdit(), e);
	return true;
}

bool Widget::replayImeIntoField(QInputMethodEvent *e) {
	const auto cursor = _field->rawTextEdit()->textCursor();
	if (!ImeEventProducesInput(*e, cursor) || !prepareFieldForInput()) {
		return false;
	}
	_field->setFocusFast();
	QCoreApplication::sendEvent(_field->rawTextEdit(), e);
	return true;
}

ApplyResult Widget::commitInlineField() {
	const auto result = applyFieldTextToState();
	if (result != ApplyResult::Failed) {
		return result;
	}
	revertInlineFieldToState();
	showLastLimitToast();
	return result;
}

ApplyResult Widget::commitInlineFieldForClose() {
	const auto result = recordMutationTransaction([&] {
		return applyFieldTextToState();
	});
	if (result == ApplyResult::Failed) {
		showLastLimitToast();
	} else if (result == ApplyResult::Changed) {
		refreshAfterInlineFieldCommit(result);
	}
	return result;
}

void Widget::hideInlineFieldAndRefresh() {
	if (_field->isHidden()) {
		return;
	}
	beginArticleRelayoutDeferral();
	const auto relayoutGuard = gsl::finally([&] {
		endArticleRelayoutDeferral();
	});
	const auto committed = recordMutationTransaction([&] {
		const auto committed = commitInlineField();
		_pendingOrdinal = -1;
		_pendingCursorOffset = 0;
		hideInlineField();
		clearInlineFieldEditSession();
		return committed;
	});
	refreshAfterInlineFieldCommit(committed);
}

bool Widget::commitAndActivateTextOrdinal(
		int ordinal,
		int selectionFrom,
		int selectionTo,
		ActivateReveal revealAfterRestore) {
	const auto restoreScroll = captureScrollTopRestorer();
	auto source = std::optional<Markdown::PreparedEditLeafSource>();
	auto committed = ApplyResult::Unchanged;
	beginArticleRelayoutDeferral();
	if (!_field->isHidden()) {
		source = _state->activePreparedLeafSource();
		committed = recordMutationTransaction([&] {
			const auto committed = commitInlineField();
			if (committed != ApplyResult::Failed) {
				_pendingOrdinal = -1;
				_pendingCursorOffset = 0;
				hideInlineField();
				clearInlineFieldEditSession();
			}
			return committed;
		});
		if (committed == ApplyResult::Failed) {
			endArticleRelayoutDeferral();
			return false;
		}
	}
	finishArticleSelection();
	beginInlineFieldRevealSuppression();
	{
		const auto revealGuard = gsl::finally([&] {
			endInlineFieldRevealSuppression();
		});
		activateTextOrdinal(
			ordinal,
			selectionFrom,
			selectionTo,
			ActivateReveal::Skip);
		refreshAfterInlineFieldCommit(committed, std::move(source));
	}
	endArticleRelayoutDeferral();
	if (restoreScroll) {
		restoreScroll();
	}
	if (revealAfterRestore == ActivateReveal::Reveal) {
		revealActiveInlineField();
	}
	return true;
}

void Widget::acceptInlineField() {
	hideInlineFieldAndRefresh();
}

void Widget::toggleSearch() {
	if (_search && _search->shown()) {
		_search->hide();
		return;
	}
	hideInlineFieldAndRefresh();
	clearStructuralSelection();
	if (!_search) {
		createSearchController();
	}
	_search->toggle();
}

bool Widget::closeSearch() {
	if (!_search || !_search->shown()) {
		return false;
	}
	_search->hide();
	return true;
}

void Widget::createSearchController() {
	auto host = SearchHost{
		.ready = [=] { return _article != nullptr; },
		.sources = [=] { return _article->searchSources(); },
		.applyMatches = [=](
				std::vector<Markdown::MarkdownArticleSearchMatch> matches,
				int current) {
			_article->setSearchMatches(std::move(matches), current);
			update();
		},
		.scrollToSegment = [=](int segmentIndex) {
			scrollToSearchSegment(segmentIndex);
		},
		.expandDetails = [](const QString &) { return false; },
		.focusContent = [=] { setFocus(); },
		.fieldFocused = [=] { hideInlineFieldAndRefresh(); },
	};
	_search = std::make_unique<SearchController>(
		_outer,
		widthValue() | rpl::map([=](int outerWidth) {
			return searchBarColumn(outerWidth).width;
		}),
		std::move(host),
		SearchBarMode::EditorPill);
	_searchSlideHeight = _search->barHeightValue();
	_searchSlideHeight.changes() | rpl::on_next([=] {
		resizeToWidth(width());
		update();
	}, lifetime());
	widthValue() | rpl::on_next([=] {
		updateSearchBarGeometry();
	}, lifetime());
	_search->raiseBar();
}

void Widget::scrollToSearchSegment(int segmentIndex) {
	const auto scroll = selectionScrollArea();
	if (!scroll || !_article) {
		return;
	}
	const auto rect = _article->segmentRect(
		segmentIndex
	).translated(articleTopLeft());
	if (rect.isEmpty()) {
		return;
	}
	const auto topMargin = _topContentPadding
		+ st::ivEditorToolbarPadding.top()
		+ st::ivEditorToolbarButtonSize
		+ 2 * st::ivEditorPillPadding;
	const auto current = scroll->scrollTop();
	const auto height = scroll->height();
	const auto from = rect.y() - topMargin;
	const auto till = rect.y() + rect.height() + _bottomContentPadding;
	auto target = current;
	if (from < current) {
		target = from;
	} else if (till > current + height) {
		target = std::min(till - height, from);
	}
	scroll->scrollToY(target);
}

void Widget::updateSearchBarGeometry() {
	if (!_search) {
		return;
	}
	_search->moveBar(searchBarColumn(width()).left, searchBarTop());
}

Widget::ArticleColumn Widget::searchBarColumn(int outerWidth) const {
	const auto column = articleColumnForWidth(outerWidth);
	return (column.width >= _contentMaxWidth)
		? column
		: ArticleColumn{ 0, outerWidth };
}

int Widget::searchBarTop() const {
	return st::ivEditorToolbarPadding.top()
		+ st::ivEditorToolbarButtonSize
		+ 2 * st::ivEditorPillPadding;
}

void Widget::refreshPreparedContent() {
	setDocument(_state->prepared());
	relayoutCurrentContent();
	update();
	if (_search) {
		if (articleRelayoutDeferralActive()) {
			_searchRefreshDeferred = true;
		} else {
			_search->refresh();
		}
	}
}

void Widget::refreshPreparedLeafAtActiveSource() {
	if (const auto source = _state->activePreparedLeafSource()) {
		refreshPreparedLeafAtSource(*source);
	} else {
		refreshPreparedContent();
	}
}

void Widget::refreshPreparedLeafAtSource(
		const Markdown::PreparedEditLeafSource &source) {
	_article->updatePreparedLeaf(source, _state->prepared());
	relayoutCurrentContent();
	if (_search) {
		if (articleRelayoutDeferralActive()) {
			_searchRefreshDeferred = true;
		} else {
			_search->refresh();
		}
	}
}

void Widget::applyExternalRichPageMutation(Fn<bool(RichPage&)> mutation) {
	if (!mutation) {
		return;
	}
	auto savedActiveIndexes = std::vector<std::pair<State::BlockPath, int>>();
	if (_article) {
		for (const auto &geo : _article->mediaBlockGeometries()) {
			if (!geo.grouped) {
				continue;
			}
			if (const auto path = _state->convertBlockPath(geo.block)) {
				savedActiveIndexes.emplace_back(*path, geo.activeItemIndex);
			}
		}
	}
	auto live = captureHistoryEntry();
	for (auto &entry : _history) {
		mutation(entry.snapshot.richPage);
	}
	mutation(live.snapshot.richPage);
	const auto wasPreservingExternalFieldRestore
		= PreservingExternalFieldRestore;
	PreservingExternalFieldRestore = this;
	const auto preserveExternalFieldRestore = gsl::finally([&] {
		PreservingExternalFieldRestore = wasPreservingExternalFieldRestore;
	});
	restoreHistoryEntry(live);
	for (const auto &[path, activeIndex] : savedActiveIndexes) {
		restoreGroupedActiveIndexForPath(path, activeIndex);
	}
	_fieldUndoAvailable = !_field->isHidden()
		? _field->isUndoAvailable()
		: false;
	_fieldRedoAvailable = !_field->isHidden()
		? _field->isRedoAvailable()
		: false;
}

void Widget::beginArticleRelayoutDeferral() {
	++_articleRelayoutDeferralDepth;
}

void Widget::endArticleRelayoutDeferral() {
	if (_articleRelayoutDeferralDepth <= 0) {
		return;
	}
	--_articleRelayoutDeferralDepth;
	if (_articleRelayoutDeferralDepth > 0) {
		return;
	}
	flushArticleRelayoutDeferral();
}

bool Widget::articleRelayoutDeferralActive() const {
	return (_articleRelayoutDeferralDepth > 0);
}

void Widget::requestDeferredArticleRelayout() {
	_articleRelayoutDeferred = true;
}

void Widget::requestDeferredInlineFieldGeometry() {
	_inlineFieldGeometryDeferred = true;
}

void Widget::requestDeferredInlineFieldHeightOverride() {
	_inlineFieldHeightOverrideDeferred = true;
}

void Widget::clearArticleEditableHeightOverride() {
	if (!_article) {
		return;
	} else if (articleRelayoutDeferralActive()) {
		_articleEditableHeightOverrideClearDeferred = true;
		requestDeferredArticleRelayout();
		return;
	}
	_article->clearEditableHeightOverride();
}

void Widget::flushArticleRelayoutDeferral() {
	if (articleRelayoutDeferralActive()) {
		return;
	}
	const auto clearHeightOverride
		= _articleEditableHeightOverrideClearDeferred;
	const auto relayout = _articleRelayoutDeferred || clearHeightOverride;
	const auto geometry = _inlineFieldGeometryDeferred;
	const auto heightOverride = _inlineFieldHeightOverrideDeferred;
	const auto searchRefresh = _searchRefreshDeferred;
	_articleEditableHeightOverrideClearDeferred = false;
	_articleRelayoutDeferred = false;
	_inlineFieldGeometryDeferred = false;
	_inlineFieldHeightOverrideDeferred = false;
	_searchRefreshDeferred = false;
	if (!relayout && !geometry && !heightOverride && !searchRefresh) {
		return;
	}
	if (clearHeightOverride && _article) {
		_article->clearEditableHeightOverride();
	}
	if (relayout) {
		relayoutCurrentContent();
		ensurePendingActivation();
	}
	if (geometry) {
		syncInlineFieldGeometry();
	}
	if (heightOverride) {
		updateInlineFieldHeightOverride();
	}
	if (searchRefresh && _search) {
		_search->refresh();
	}
	syncArticleVisibleTopBottom();
}

void Widget::beginInlineFieldRevealSuppression() {
	++_inlineFieldRevealSuppressionDepth;
}

void Widget::endInlineFieldRevealSuppression() {
	if (_inlineFieldRevealSuppressionDepth > 0) {
		--_inlineFieldRevealSuppressionDepth;
	}
}

bool Widget::inlineFieldRevealSuppressed() const {
	return (_inlineFieldRevealSuppressionDepth > 0);
}

void Widget::resizeCurrentContentToWidth(int width) {
	if (articleRelayoutDeferralActive()) {
		requestDeferredArticleRelayout();
		return;
	}
	if (width > 0) {
		resizeToWidth(width);
	} else {
		update();
	}
}

void Widget::relayoutCurrentContent() {
	const auto width = std::max(
		widthNoMargins(),
		parentWidget() ? parentWidget()->width() : 0);
	resizeCurrentContentToWidth(width);
}

void Widget::syncInlineFieldGeometry() {
	syncInlineFieldGeometry(widthNoMargins());
}

void Widget::insertBlock(State::InsertAction action) {
	recordMutationTransaction([&] {
		const auto context = activeTextInsertContext();
		const auto restoreField = context.has_value();
		const auto restoreLeaf = restoreField
			? _fieldLeaf
			: std::optional<State::LeafPath>();
		const auto restoreStyleKey = restoreField
			? _activeFieldStyleKey
			: std::optional<InlineFieldStyleKey>();
		const auto restoreMode = _fieldMode;
		const auto restoreSelection = restoreField
			? captureHistoryViewState().leafSelection
			: std::optional<HistoryLeafSelection>();
		auto committed = ApplyResult::Unchanged;
		if (!context && !_field->isHidden()) {
			committed = commitInlineField();
			if (committed == ApplyResult::Failed) {
				return MutationTransactionResult{
					.committed = committed,
					.failed = true,
				};
			}
		}
		const auto hadStructuralSelection = hasStructuralSelection();
		auto destination = State::BoundaryTarget();
		if (hadStructuralSelection || restoreField) {
			_pendingOrdinal = -1;
			_pendingCursorOffset = 0;
			hideInlineField();
			clearInlineFieldEditSession(restoreField);
		}
		auto restore = restoreField;
		const auto restoreInlineField = gsl::finally([&] {
			if (!restore) {
				return;
			}
			if (restoreLeaf && restoreStyleKey) {
				if (auto revived = reviveRetainedLeafField(
						_historyIndex,
						*restoreLeaf,
						restoreMode,
						*restoreStyleKey)) {
					_field = std::move(revived);
					_activeFieldStyleKey = restoreStyleKey;
					_fieldMode = restoreMode;
					_fieldLeaf = *restoreLeaf;
					refreshInlineFieldPlaceholder();
					_fieldUndoAvailable = _field->isUndoAvailable();
					_fieldRedoAvailable = _field->isRedoAvailable();
					clearFieldUndoRedoNoopState();
				}
			}
			if (!_fieldLeaf && restoreSelection) {
				const auto ordinal = _state->textOrdinalForLeafPath(
					restoreSelection->leaf);
				if (ordinal >= 0) {
					activateTextOrdinal(
						ordinal,
						restoreSelection->anchorOffset,
						restoreSelection->cursorOffset);
					return;
				}
			}
			_field->show();
			syncInlineFieldGeometry();
			updateInlineFieldHeightOverride();
			syncArticleVisibleTopBottom();
			revealActiveInlineField();
			_field->raise();
			_field->setFocusFast();
			notifyToolbarStateChanged();
		});
		auto activeBlockResult
			= std::optional<State::ActiveTextBlockActionResult>();
		auto applied = false;
		if (hadStructuralSelection) {
			applied = _state->replaceStructuralSelectionWithBlock(
				_structuralSelection,
				action,
				context,
				&destination);
		} else if (restoreField
			&& context
			&& (action.type == State::InsertBlockType::Blockquote
				|| action.type == State::InsertBlockType::Code)) {
			activeBlockResult = _state->applyActiveTextBlockAction(
				action,
				*context);
			applied = (activeBlockResult->result == ApplyResult::Changed);
		} else {
			applied = _state->insertBlockAfterActive(action, context);
		}
		if (!applied) {
			showLastLimitToast();
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		restore = false;
		if (hadStructuralSelection) {
			refreshPreparedContent();
			switch (destination.action) {
			case State::BoundaryTarget::Action::StructuralSelection:
				_boundarySelectionOrigin = std::nullopt;
				_selection = {};
				_selectionEndpoints = {};
				finishArticleSelection();
				setStructuralSelection(destination.structuralSelection);
				update();
				break;
			case State::BoundaryTarget::Action::Text:
				clearSelection();
				activateTextOrdinal(destination.textOrdinal, 0);
				break;
			case State::BoundaryTarget::Action::None:
			case State::BoundaryTarget::Action::RemoveActiveOwner: {
				clearSelection();
				const auto ordinal = _state->activeTextOrdinal();
				if (ordinal >= 0 && ordinal < _state->textNodeCount()) {
					activateTextOrdinal(ordinal, 0);
				} else {
					activateInitialNode();
				}
			} break;
			}
		} else {
			refreshPreparedContent();
			auto restoredActiveBlock = false;
			if (activeBlockResult && activeBlockResult->destinationLeaf) {
				const auto ordinal = _state->textOrdinalForLeafPath(
					*activeBlockResult->destinationLeaf);
				if (ordinal >= 0) {
					activateTextOrdinal(
						ordinal,
						activeBlockResult->selectionFrom,
						activeBlockResult->selectionTo);
					restoredActiveBlock = true;
				}
			}
			if (!restoredActiveBlock) {
				const auto ordinal = _state->activeTextOrdinal();
				if (ordinal >= 0 && ordinal < _state->textNodeCount()) {
					activateTextOrdinal(ordinal, 0);
				} else {
					activateInitialNode();
				}
			}
		}
		return MutationTransactionResult{
			.committed = committed,
			.changed = true,
		};
	});
}

void Widget::insertPreparedBlock(RichPage::Block block) {
	auto blocks = std::vector<RichPage::Block>();
	blocks.push_back(std::move(block));
	insertPreparedBlocks(std::move(blocks));
}

void Widget::requestMedia(
		std::optional<State::ReplaceTarget> replaceTarget,
		RequestMediaType type) {
	if (_requestMedia) {
		_requestMedia(
			not_null<Widget*>(this),
			QPointer<QWidget>(_outer.get()),
			std::move(replaceTarget),
			type);
	}
}

void Widget::replacePreparedBlock(
		State::ReplaceTarget target,
		RichPage::Block block) {
	const auto savedActiveIndex = (target.itemIndex >= 0)
		? groupedActiveIndexForPath(target.path)
		: -1;
	recordMutationTransaction([&] {
		auto committed = ApplyResult::Unchanged;
		if (!_field->isHidden()) {
			committed = commitInlineField();
			if (committed == ApplyResult::Failed) {
				return MutationTransactionResult{
					.committed = committed,
					.failed = true,
				};
			}
		}
		if (!_state->replaceBlockWithPreparedBlock(target, std::move(block))) {
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		_pendingOrdinal = -1;
		_pendingCursorOffset = 0;
		hideInlineField();
		clearInlineFieldEditSession();
		clearTextSelection();
		clearStructuralSelection();
		refreshPreparedContent();
		restoreGroupedActiveIndexForPath(target.path, savedActiveIndex);
		const auto ordinal = _state->activeTextOrdinal();
		if (ordinal >= 0 && ordinal < _state->textNodeCount()) {
			activateTextOrdinal(ordinal, 0);
		}
		return MutationTransactionResult{
			.committed = committed,
			.changed = true,
		};
	});
}

void Widget::insertPreparedBlocks(std::vector<RichPage::Block> blocks) {
	insertPreparedBlocks(std::move(blocks), activeTextInsertContext());
}

void Widget::pastePreparedBlock(
		RichPage::Block block,
		PreparedMediaPasteTarget target) {
	auto blocks = std::vector<RichPage::Block>();
	blocks.push_back(std::move(block));
	pastePreparedBlocks(std::move(blocks), std::move(target));
}

void Widget::pastePreparedBlocks(
		std::vector<RichPage::Block> blocks,
		PreparedMediaPasteTarget target) {
	if (target.blockDrop) {
		pasteBlocksAtDropTarget(std::move(blocks), *target.blockDrop);
		return;
	}
	auto activation = activatePreparedMediaPasteTarget(std::move(target));
	if (activation.resolved) {
		insertPreparedBlocks(
			std::move(blocks),
			std::move(activation.context),
			false);
	} else {
		insertPreparedBlocks(
			std::move(blocks),
			activeTextInsertContext(),
			false);
	}
}

void Widget::pasteBlocksAtDropTarget(
		std::vector<RichPage::Block> blocks,
		const Markdown::PreparedEditBlockDropTarget &target) {
	recordMutationTransaction([&] {
		auto committed = ApplyResult::Unchanged;
		if (!_field->isHidden()) {
			committed = commitInlineField();
			if (committed == ApplyResult::Failed) {
				return MutationTransactionResult{
					.committed = committed,
					.failed = true,
				};
			}
		}
		if (!_state->insertPreparedBlocksAtDropTarget(
				std::move(blocks),
				target)) {
			showLastLimitToast();
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		_pendingOrdinal = -1;
		_pendingCursorOffset = 0;
		hideInlineField();
		clearInlineFieldEditSession();
		clearTextSelection();
		clearStructuralSelection();
		refreshPreparedContent();
		const auto ordinal = _state->activeTextOrdinal();
		if (ordinal >= 0 && ordinal < _state->textNodeCount()) {
			activateTextOrdinal(ordinal, 0);
		}
		return MutationTransactionResult{
			.committed = committed,
			.changed = true,
		};
	});
}

void Widget::insertPreparedBlocks(
		std::vector<RichPage::Block> blocks,
		std::optional<State::ActiveTextInsertContext> context,
		bool useStructuralSelection) {
	if (blocks.empty()) {
		return;
	}
	recordMutationTransaction([&] {
		const auto restoreField = context.has_value();
		const auto restoreLeaf = restoreField
			? _fieldLeaf
			: std::optional<State::LeafPath>();
		const auto restoreStyleKey = restoreField
			? _activeFieldStyleKey
			: std::optional<InlineFieldStyleKey>();
		const auto restoreMode = _fieldMode;
		const auto restoreSelection = restoreField
			? captureHistoryViewState().leafSelection
			: std::optional<HistoryLeafSelection>();
		auto committed = ApplyResult::Unchanged;
		if (!context && !_field->isHidden()) {
			committed = commitInlineField();
			if (committed == ApplyResult::Failed) {
				return MutationTransactionResult{
					.committed = committed,
					.failed = true,
				};
			}
		}
		const auto ignoredStructuralSelection = !useStructuralSelection
			&& hasStructuralSelection();
		const auto hadStructuralSelection = useStructuralSelection
			&& hasStructuralSelection();
		if (hadStructuralSelection || restoreField) {
			_pendingOrdinal = -1;
			_pendingCursorOffset = 0;
			hideInlineField();
			clearInlineFieldEditSession(restoreField);
		}
		auto restore = restoreField;
		const auto restoreInlineField = gsl::finally([&] {
			if (!restore) {
				return;
			}
			if (restoreLeaf && restoreStyleKey) {
				if (auto revived = reviveRetainedLeafField(
						_historyIndex,
						*restoreLeaf,
						restoreMode,
						*restoreStyleKey)) {
					_field = std::move(revived);
					_activeFieldStyleKey = restoreStyleKey;
					_fieldMode = restoreMode;
					_fieldLeaf = *restoreLeaf;
					refreshInlineFieldPlaceholder();
					_fieldUndoAvailable = _field->isUndoAvailable();
					_fieldRedoAvailable = _field->isRedoAvailable();
					clearFieldUndoRedoNoopState();
				}
			}
			if (!_fieldLeaf && restoreSelection) {
				const auto ordinal = _state->textOrdinalForLeafPath(
					restoreSelection->leaf);
				if (ordinal >= 0) {
					activateTextOrdinal(
						ordinal,
						restoreSelection->anchorOffset,
						restoreSelection->cursorOffset);
					return;
				}
			}
			_field->show();
			syncInlineFieldGeometry();
			updateInlineFieldHeightOverride();
			syncArticleVisibleTopBottom();
			revealActiveInlineField();
			_field->raise();
			_field->setFocusFast();
			notifyToolbarStateChanged();
		});
		const auto applied = hadStructuralSelection
			? _state->replaceStructuralSelectionWithPreparedBlocks(
				_structuralSelection,
				std::move(blocks),
				context)
			: _state->insertPreparedBlocksAfterActive(
				std::move(blocks),
				context);
		if (!applied) {
			showLastLimitToast();
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		restore = false;
		if (hadStructuralSelection || ignoredStructuralSelection) {
			clearSelection();
		}
		refreshPreparedContent();
		activateTextOrdinal(_state->activeTextOrdinal(), 0);
		return MutationTransactionResult{
			.committed = committed,
			.changed = true,
		};
	});
}

TextForMimeData Widget::currentSelectionTextForClipboard() const {
	return _article
		? _article->textForSelection(
			_selection,
			&_selectionEndpoints,
			hasStructuralSelection() ? &_structuralSelection : nullptr)
		: TextForMimeData();
}

void Widget::copyCurrentSelectionToClipboard() {
	auto structured = std::optional<ClipboardData>();
	if (hasStructuralSelection()) {
		structured = _state->structuredClipboardDataForSelection(
			_structuralSelection);
	}
	const auto text = currentSelectionTextForClipboard();
	auto mimeData = structured
		? MimeDataFromClipboardData(*structured)
		: TextUtilities::MimeDataFromText(text);
	if (!mimeData) {
		return;
	}
	if (structured) {
		if (const auto textMimeData = TextUtilities::MimeDataFromText(text)) {
			for (const auto &format : textMimeData->formats()) {
				mimeData->setData(format, textMimeData->data(format));
			}
		}
	}
	QApplication::clipboard()->setMimeData(mimeData.release());
}

void Widget::pasteStructuredClipboardData(const ClipboardData &data) {
	const auto blocks = std::get_if<ClipboardBlockData>(&data);
	const auto items = std::get_if<ClipboardListItemsData>(&data);
	if (blocks) {
		if (blocks->blocks.empty()) {
			return;
		}
	} else if (!items || items->items.empty()) {
		return;
	}
	recordMutationTransaction([&] {
		const auto context = ClipboardPasteInsertContext(
			activeTextInsertContext());
		const auto restoreField = context.has_value();
		const auto restoreLeaf = restoreField
			? _fieldLeaf
			: std::optional<State::LeafPath>();
		const auto restoreStyleKey = restoreField
			? _activeFieldStyleKey
			: std::optional<InlineFieldStyleKey>();
		const auto restoreMode = _fieldMode;
		const auto restoreSelection = restoreField
			? captureHistoryViewState().leafSelection
			: std::optional<HistoryLeafSelection>();
		auto committed = ApplyResult::Unchanged;
		if (!context && !_field->isHidden()) {
			committed = commitInlineField();
			if (committed == ApplyResult::Failed) {
				return MutationTransactionResult{
					.committed = committed,
					.failed = true,
				};
			}
		}
		const auto hadStructuralSelection = hasStructuralSelection();
		if (hadStructuralSelection || restoreField) {
			_pendingOrdinal = -1;
			_pendingCursorOffset = 0;
			hideInlineField();
			clearInlineFieldEditSession(restoreField);
		}
		auto restore = restoreField;
		const auto restoreInlineField = gsl::finally([&] {
			if (!restore) {
				return;
			}
			if (restoreLeaf && restoreStyleKey) {
				if (auto revived = reviveRetainedLeafField(
						_historyIndex,
						*restoreLeaf,
						restoreMode,
						*restoreStyleKey)) {
					_field = std::move(revived);
					_activeFieldStyleKey = restoreStyleKey;
					_fieldMode = restoreMode;
					_fieldLeaf = *restoreLeaf;
					refreshInlineFieldPlaceholder();
					_fieldUndoAvailable = _field->isUndoAvailable();
					_fieldRedoAvailable = _field->isRedoAvailable();
					clearFieldUndoRedoNoopState();
				}
			}
			if (!_fieldLeaf && restoreSelection) {
				const auto ordinal = _state->textOrdinalForLeafPath(
					restoreSelection->leaf);
				if (ordinal >= 0) {
					activateTextOrdinal(
						ordinal,
						restoreSelection->anchorOffset,
						restoreSelection->cursorOffset);
					return;
				}
			}
			_field->show();
			syncInlineFieldGeometry();
			updateInlineFieldHeightOverride();
			syncArticleVisibleTopBottom();
			revealActiveInlineField();
			_field->raise();
			_field->setFocusFast();
			notifyToolbarStateChanged();
		});
		const auto applied = hadStructuralSelection
			? (blocks
				? _state->replaceStructuralSelectionWithPreparedBlocks(
					_structuralSelection,
					blocks->blocks,
					context)
				: _state->replaceStructuralSelectionWithClipboardListItems(
					_structuralSelection,
					*items,
					context))
			: (blocks
				? _state->insertPreparedBlocksAfterActive(
					blocks->blocks,
					context)
				: _state->pasteClipboardListItemsAfterActive(
					*items,
					context));
		if (!applied) {
			showLastLimitToast();
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		restore = false;
		if (hadStructuralSelection) {
			clearSelection();
		}
		refreshPreparedContent();
		activateTextOrdinal(_state->activeTextOrdinal(), 0);
		return MutationTransactionResult{
			.committed = committed,
			.changed = true,
		};
	});
}

bool Widget::hasFieldTextSpanSelection() const {
	return !_settingField
		&& !_field->isHidden()
		&& (_activeSegmentIndex >= 0)
		&& (_state->activeFieldMode() != State::FieldMode::Raw)
		&& _field->textCursor().hasSelection();
}

bool Widget::hasActiveSelection() const {
	return hasStructuralSelection()
		|| !_selection.empty()
		|| hasFieldTextSpanSelection();
}

TextWithEntities Widget::textSpanForCurrentSelection() {
	if (hasStructuralSelection()) {
		return {};
	}
	if (hasFieldTextSpanSelection()) {
		if (const auto context = activeTextInsertContext()) {
			return context->selected;
		}
		return {};
	}
	const auto selection = _selection;
	const auto sameSegmentSelection = !selection.empty()
		&& _article
		&& (selection.from.segment == selection.to.segment)
		&& _article->segmentIsText(selection.from.segment);
	const auto ordinal = sameSegmentSelection
		? editableOrdinalForSegment(selection.from.segment)
		: -1;
	if (ordinal < 0) {
		return {};
	}
	const auto selectionFrom = selection.from.offset;
	const auto selectionTo = selection.to.offset;
	clearTextSelection();
	if (!commitAndActivateTextOrdinal(
			ordinal,
			selectionFrom,
			selectionTo)) {
		return {};
	}
	if (const auto context = activeTextInsertContext()) {
		return context->selected;
	}
	return {};
}

std::shared_ptr<const RichPage> Widget::richPageForCurrentSelection() const {
	if (hasStructuralSelection()) {
		const auto kind = _structuralSelection.kind;
		if (kind == PreparedEditSelectionKind::TableRows
			|| kind == PreparedEditSelectionKind::TableCells) {
			return _state->richPageForTableSelection(_structuralSelection);
		}
		const auto data = _state->structuredClipboardDataForSelection(
			_structuralSelection);
		if (!data) {
			return nullptr;
		}
		auto page = std::make_shared<RichPage>();
		if (const auto blocks = std::get_if<ClipboardBlockData>(&*data)) {
			page->blocks = blocks->blocks;
		} else if (const auto items
				= std::get_if<ClipboardListItemsData>(&*data)) {
			auto list = RichPage::Block();
			list.kind = RichPage::BlockKind::List;
			list.listKind = items->listKind;
			list.orderedList = items->orderedList;
			list.listItems = items->items;
			page->blocks.push_back(std::move(list));
		}
		if (page->blocks.empty()) {
			return nullptr;
		}
		return page;
	}
	return nullptr;
}

void Widget::replaceCurrentSelectionWithRichPage(
		std::shared_ptr<const RichPage> page) {
	if (!page || page->blocks.empty()) {
		return;
	}
	if (hasStructuralSelection()) {
		const auto kind = _structuralSelection.kind;
		if (kind == PreparedEditSelectionKind::TableRows
			|| kind == PreparedEditSelectionKind::TableCells) {
			auto blocks = page->blocks;
			recordMutationTransaction([&] {
				auto committed = ApplyResult::Unchanged;
				if (!_field->isHidden()) {
					committed = commitInlineField();
					if (committed == ApplyResult::Failed) {
						return MutationTransactionResult{
							.committed = committed,
							.failed = true,
						};
					}
				}
				using InPlace = State::TableInPlaceApplyResult;
				const auto inPlace
					= _state->replaceTableSelectionCellsInPlace(
						_structuralSelection,
						*page);
				if (inPlace == InPlace::Failed
					|| inPlace == InPlace::Unchanged) {
					if (inPlace == InPlace::Failed) {
						showLastLimitToast();
					}
					return MutationTransactionResult{
						.committed = committed,
						.changed = (committed == ApplyResult::Changed),
					};
				}
				if (inPlace == InPlace::StructureMismatch
					&& !_state->insertPreparedBlocksAfterTableSelection(
						_structuralSelection,
						std::move(blocks))) {
					showLastLimitToast();
					return MutationTransactionResult{
						.committed = committed,
						.changed = (committed == ApplyResult::Changed),
					};
				}
				_pendingOrdinal = -1;
				_pendingCursorOffset = 0;
				hideInlineField();
				clearInlineFieldEditSession();
				clearTextSelection();
				clearStructuralSelection();
				refreshPreparedContent();
				const auto ordinal = _state->activeTextOrdinal();
				if (ordinal >= 0 && ordinal < _state->textNodeCount()) {
					activateTextOrdinal(ordinal, 0);
				}
				return MutationTransactionResult{
					.committed = committed,
					.changed = true,
				};
			});
			return;
		}
		if (kind == PreparedEditSelectionKind::ListItems
			&& page->blocks.size() == 1
			&& page->blocks.front().kind == RichPage::BlockKind::List) {
			const auto &list = page->blocks.front();
			auto data = ClipboardListItemsData();
			data.listKind = list.listKind;
			data.orderedList = list.orderedList;
			data.items = list.listItems;
			data.taskList = !data.items.empty()
				&& (data.items.front().taskState
					!= RichPage::TaskState::None);
			if (!data.items.empty()) {
				pasteStructuredClipboardData(ClipboardData(std::move(data)));
				return;
			}
		}
	}
	auto data = ClipboardBlockData();
	data.blocks = page->blocks;
	pasteStructuredClipboardData(ClipboardData(std::move(data)));
}

void Widget::replaceCurrentSelectionWithText(TextWithEntities text) {
	RemoveBlockLevelEntities(&text);
	if (text.text.isEmpty()) {
		return;
	}
	auto context = activeTextInsertContext();
	if (!context || context->selected.text.isEmpty()) {
		return;
	}
	recordMutationTransaction([&] {
		const auto restoreLeaf = _fieldLeaf;
		const auto restoreStyleKey = _activeFieldStyleKey;
		const auto restoreMode = _fieldMode;
		const auto restoreSelection
			= captureHistoryViewState().leafSelection;
		_pendingOrdinal = -1;
		_pendingCursorOffset = 0;
		hideInlineField();
		clearInlineFieldEditSession(true);
		auto restore = true;
		const auto restoreInlineField = gsl::finally([&] {
			if (!restore) {
				return;
			}
			if (restoreLeaf && restoreStyleKey) {
				if (auto revived = reviveRetainedLeafField(
						_historyIndex,
						*restoreLeaf,
						restoreMode,
						*restoreStyleKey)) {
					_field = std::move(revived);
					_activeFieldStyleKey = restoreStyleKey;
					_fieldMode = restoreMode;
					_fieldLeaf = *restoreLeaf;
					refreshInlineFieldPlaceholder();
					_fieldUndoAvailable = _field->isUndoAvailable();
					_fieldRedoAvailable = _field->isRedoAvailable();
					clearFieldUndoRedoNoopState();
				}
			}
			if (!_fieldLeaf && restoreSelection) {
				const auto ordinal = _state->textOrdinalForLeafPath(
					restoreSelection->leaf);
				if (ordinal >= 0) {
					activateTextOrdinal(
						ordinal,
						restoreSelection->anchorOffset,
						restoreSelection->cursorOffset);
					return;
				}
			}
			_field->show();
			syncInlineFieldGeometry();
			updateInlineFieldHeightOverride();
			syncArticleVisibleTopBottom();
			revealActiveInlineField();
			_field->raise();
			_field->setFocusFast();
			notifyToolbarStateChanged();
		});
		const auto applied = _state->replaceActiveTextSelectionWithText(
			std::move(text),
			*context);
		if (applied.result != ApplyResult::Changed) {
			showLastLimitToast();
			return MutationTransactionResult{
				.committed = ApplyResult::Unchanged,
			};
		}
		restore = false;
		refreshPreparedContent();
		auto restored = false;
		if (applied.destinationLeaf) {
			const auto ordinal = _state->textOrdinalForLeafPath(
				*applied.destinationLeaf);
			if (ordinal >= 0) {
				activateTextOrdinal(
					ordinal,
					applied.selectionFrom,
					applied.selectionTo);
				restored = true;
			}
		}
		if (!restored) {
			const auto ordinal = _state->activeTextOrdinal();
			if (ordinal >= 0 && ordinal < _state->textNodeCount()) {
				activateTextOrdinal(ordinal, 0);
			} else {
				activateInitialNode();
			}
		}
		return MutationTransactionResult{
			.committed = ApplyResult::Unchanged,
			.changed = true,
		};
	});
}

bool Widget::handleClipboardKey(QKeyEvent *e) {
	if (e == QKeySequence::Copy) {
		if (_selection.empty() && !hasStructuralSelection()) {
			return false;
		}
		copyCurrentSelectionToClipboard();
		e->accept();
		return true;
	} else if (e == QKeySequence::Cut) {
		if (!hasStructuralSelection()
			|| !_state->canRemoveStructuralSelection(_structuralSelection)) {
			return false;
		}
		copyCurrentSelectionToClipboard();
		removeStructuralSelectionAndReposition(true);
		e->accept();
		return true;
	} else if ((e == QKeySequence::Paste) && _field->isHidden()) {
		const auto mimeData = QApplication::clipboard()->mimeData();
		if (const auto data = ClipboardDataFromMimeData(mimeData)) {
			pasteStructuredClipboardData(*data);
			e->accept();
			return true;
		}
		if (mimeData && _applyPreparedMedia) {
			if (auto list = PreparedMediaFromClipboard(
					not_null<const QMimeData*>(mimeData),
					_session->premium())) {
				_applyPreparedMedia(
					not_null<Widget*>(this),
					std::move(*list),
					preparedMediaPasteTarget());
				e->accept();
				return true;
			}
		}
		if (prepareFieldForInput()) {
			_field->setFocusFast();
			QCoreApplication::sendEvent(_field->rawTextEdit(), e);
			e->accept();
			return true;
		}
	}
	return false;
}

bool Widget::handleHardcodedBlockShortcut(QKeyEvent *e) {
	const auto type = e->type();
	if (type != QEvent::ShortcutOverride && type != QEvent::KeyPress) {
		return false;
	}
	const auto perform = (type == QEvent::KeyPress);
	if (MatchesKeySequence(e, kEditorHeading1Sequence)) {
		if (perform) {
			insertBlock({
				.type = State::InsertBlockType::Heading,
				.headingLevel = 1,
			});
		}
	} else if (MatchesKeySequence(e, kEditorHeading2Sequence)) {
		if (perform) {
			insertBlock({
				.type = State::InsertBlockType::Heading,
				.headingLevel = 2,
			});
		}
	} else if (MatchesKeySequence(e, kEditorTableSequence)) {
		if (perform) {
			insertBlock({ .type = State::InsertBlockType::Table });
		}
	} else if (MatchesKeySequence(e, kEditorBodyTextSequence)) {
		if (perform) {
			applyToolbarFormatAction(ToolbarFormatAction::PlainText);
		}
	} else {
		return false;
	}
	e->accept();
	return true;
}

bool Widget::handleFieldBlockInsertShortcut(QKeyEvent *e) {
	if (_fieldMode != State::FieldMode::Rich || _field->isHidden()) {
		return false;
	}
	if (handleHardcodedBlockShortcut(e)) {
		return true;
	}
	const auto type = e->type();
	if (type != QEvent::ShortcutOverride && type != QEvent::KeyPress) {
		return false;
	}
	const auto blockquote = MatchesKeySequence(e, Ui::kBlockquoteSequence);
	const auto monospace = MatchesKeySequence(e, Ui::kMonospaceSequence);
	if (!blockquote && !monospace) {
		return false;
	}
	if (blockquote) {
		if (type == QEvent::KeyPress) {
			insertBlockquote();
		}
		e->accept();
		return true;
	}
	if (type == QEvent::KeyPress) {
		applyFieldMonospaceAction();
	}
	e->accept();
	return true;
}

bool Widget::handleStructuralBlockInsertShortcut(QKeyEvent *e) {
	if (!hasStructuralSelection()) {
		return false;
	}
	if (handleHardcodedBlockShortcut(e)) {
		return true;
	}
	const auto type = e->type();
	if (type != QEvent::ShortcutOverride && type != QEvent::KeyPress) {
		return false;
	}
	const auto blockquote = MatchesKeySequence(e, Ui::kBlockquoteSequence);
	const auto monospace = MatchesKeySequence(e, Ui::kMonospaceSequence);
	if (!blockquote && !monospace) {
		return false;
	}
	if (blockquote) {
		if (type == QEvent::KeyPress) {
			insertBlockquote();
		}
		e->accept();
		return true;
	}
	if (type == QEvent::KeyPress) {
		applyStructuralMonospaceAction();
	}
	e->accept();
	return true;
}

bool Widget::fieldMonospaceShortcutUsesCodeBlock() const {
	return (_fieldMode == State::FieldMode::Rich)
		&& _field
		&& _field->isVisible()
		&& (_field->selectionMarkdownTagForToggle(
			Ui::InputField::kTagCode) == Ui::InputField::kTagPre);
}

bool Widget::structuralMonospaceShortcutTargetsCodeBlock() const {
	if (_structuralSelection.kind != PreparedSelectionKind::Blocks) {
		return false;
	}
	const auto &range = _structuralSelection.blocks;
	if (range.from + 1 != range.till) {
		return false;
	}
	const auto block = BlockFromPath(_state->richPage(), ToStateBlockPath({
		.container = range.container,
		.index = range.from,
	}));
	return block
		&& ((block->kind == RichPage::BlockKind::Paragraph)
			|| (block->kind == RichPage::BlockKind::Code));
}

void Widget::applyFieldMonospaceAction() {
	if (!_field) {
		return;
	} else if (fieldMonospaceShortcutUsesCodeBlock()) {
		insertCodeBlock();
	} else {
		_field->toggleCurrentMarkdownTag(Ui::InputField::kTagCode);
		notifyToolbarStateChanged();
	}
}

void Widget::applyStructuralMonospaceAction() {
	if (!structuralMonospaceShortcutTargetsCodeBlock()) {
		return;
	}
	recordMutationTransaction([&] {
		const auto committed = commitInlineField();
		if (committed == ApplyResult::Failed) {
			return MutationTransactionResult{
				.committed = committed,
				.failed = true,
			};
		}
		_pendingOrdinal = -1;
		_pendingCursorOffset = 0;
		hideInlineField();
		clearInlineFieldEditSession();
		if (!_state->toggleCodeBlockForStructuralSelection(
				_structuralSelection)) {
			showLastLimitToast();
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		clearSelection();
		refreshPreparedContent();
		activateTextOrdinal(_state->activeTextOrdinal(), 0);
		return MutationTransactionResult{
			.committed = committed,
			.changed = true,
		};
	});
}

void Widget::truncateHistoryRedo() {
	if ((_historyIndex < 0) || (_historyIndex >= int(_history.size()))) {
		return;
	}
	const auto next = _history.begin() + _historyIndex + 1;
	if (next != _history.end()) {
		_history.erase(next, _history.end());
		removeRetainedLeafFieldsAfter(_historyIndex);
		notifyToolbarStateChanged();
	}
}

bool Widget::canPerformFieldUndoRedo(bool redo) const {
	if (_field->isHidden()) {
		return false;
	}
	const auto document = _field->rawTextEdit()->document();
	const auto steps = redo
		? document->availableRedoSteps()
		: document->availableUndoSteps();
	const auto localRedoAvailable = (document->availableRedoSteps() > 0)
		|| _fieldRedoAvailable
		|| _field->isRedoAvailable();
	if (!redo
		&& localRedoAvailable
		&& activeInlineFieldTextMatchesState()) {
		return false;
	}
	const auto available = (steps > 0)
		|| (redo
			? (_fieldRedoAvailable || _field->isRedoAvailable())
			: (_fieldUndoAvailable || _field->isUndoAvailable()));
	if (!available) {
		return false;
	}
	const auto &noopState = redo
		? _fieldRedoNoopState
		: _fieldUndoNoopState;
	return !noopState || (_field->getTextWithTags() != *noopState);
}

bool Widget::activeInlineFieldTextMatchesState() const {
	if (_field->isHidden() || !_fieldLeaf) {
		return false;
	}
	const auto activeLeaf = _state->activeLeafPath();
	if (!activeLeaf || (*activeLeaf != *_fieldLeaf)) {
		return false;
	}
	const auto trimLeft = !_state->codeBlockLanguage(
		_state->activeTextOrdinal()).has_value();
	if (_state->activeFieldMode() == State::FieldMode::Raw) {
		return _field->getTextWithTags() == TrimInlineFieldText(
			{ _state->activeRawText(), {} },
			trimLeft).text;
	}
	const auto activeText = ConvertRichTextToEditorTags(_state->activeText());
	return _field->getTextWithTags() == TrimInlineFieldText(
		activeText.text,
		trimLeft).text;
}

bool Widget::canPerformHistoryUndoRedo(bool redo) const {
	if ((_historyIndex < 0) || (_historyIndex >= int(_history.size()))) {
		return false;
	}
	return redo
		? (_historyIndex + 1 < int(_history.size()))
		: (_historyIndex > 0);
}

bool Widget::canPerformUndoRedo(bool redo) const {
	return canPerformFieldUndoRedo(redo) || canPerformHistoryUndoRedo(redo);
}

bool Widget::handleUndoRedoShortcut(QKeyEvent *e) {
	auto redo = std::optional<bool>();
	if (e == QKeySequence::Undo) {
		redo = false;
	} else if (e == QKeySequence::Redo) {
		redo = true;
	}
	if (!redo) {
		return false;
	}
	const auto redoValue = *redo;
	if (canPerformFieldUndoRedo(redoValue)) {
		if (performFieldUndoRedo(redoValue)) {
			e->accept();
			return true;
		}
	}
	if (canPerformHistoryUndoRedo(redoValue)) {
		crl::on_main(this, [=] {
			performUndoRedo(redoValue, false);
		});
	}
	e->accept();
	return true;
}

bool Widget::handleUndoRedoShortcutOverride(QKeyEvent *e) {
	auto redo = std::optional<bool>();
	if (e == QKeySequence::Undo) {
		redo = false;
	} else if (e == QKeySequence::Redo) {
		redo = true;
	}
	if (!redo || searchBlockedByLayer()) {
		return false;
	}
	const auto focused = QApplication::focusWidget();
	if (qobject_cast<QTextEdit*>(focused)
		|| qobject_cast<QLineEdit*>(focused)) {
		return false;
	} else if (hasFocus()) {
		e->accept();
		return false;
	}
	const auto redoValue = *redo;
	if (!canPerformUndoRedo(redoValue)) {
		return false;
	}
	e->accept();
	crl::on_main(this, [=] {
		performToolbarUndoRedo(redoValue);
	});
	return true;
}

bool Widget::handleSelectAllShortcut(QKeyEvent *e) {
	if (e != QKeySequence::SelectAll) {
		return false;
	}
	if (SingleRootPlainTextFieldSelectAllPassthrough(
			_state->richPage(),
			_state->activeLeafPath(),
			_field->isHidden())) {
		return false;
	}
	selectWholeDocument();
	e->accept();
	return true;
}

void Widget::selectWholeDocument() {
	if (!_field->isHidden()) {
		const auto committed = recordMutationTransaction([&] {
			const auto committed = commitInlineField();
			if (committed != ApplyResult::Failed) {
				_pendingOrdinal = -1;
				_pendingCursorOffset = 0;
				hideInlineField();
				clearInlineFieldEditSession();
			}
			return committed;
		});
		if (committed == ApplyResult::Failed) {
			return;
		}
		refreshAfterInlineFieldCommit(committed);
	}
	const auto blockCount = int(_state->richPage().blocks.size());
	_selection = {};
	_selectionEndpoints = {};
	finishArticleSelection();
	setStructuralSelection(blockCount > 0
		? BlockSelectionFromIndexes(
			PreparedEditBlockContainerPath(),
			0,
			blockCount - 1)
		: PreparedEditSelection());
	setFocus();
	update();
}

bool Widget::performFieldUndoRedo(bool redo) {
	if (!canPerformFieldUndoRedo(redo)) {
		return false;
	}
	const auto before = _field->getTextWithTags();
	const auto wasPerformingUndoRedo = _performingUndoRedo;
	_performingUndoRedo = true;
	const auto guard = gsl::finally([&] {
		_performingUndoRedo = wasPerformingUndoRedo;
	});
	if (redo) {
		_field->redo();
	} else {
		_field->undo();
	}
	if (_field->isHidden()) {
		return false;
	}
	const auto document = _field->rawTextEdit()->document();
	_fieldUndoAvailable = (document->availableUndoSteps() > 0)
		|| _field->isUndoAvailable();
	_fieldRedoAvailable = (document->availableRedoSteps() > 0)
		|| _field->isRedoAvailable();
	const auto after = _field->getTextWithTags();
	if (after != before) {
		clearFieldUndoRedoNoopState();
		notifyToolbarStateChanged();
		return true;
	}
	if (redo) {
		_fieldRedoNoopState = after;
	} else {
		_fieldUndoNoopState = after;
	}
	notifyToolbarStateChanged();
	return false;
}

void Widget::performUndoRedo(bool redo, bool allowFieldLocal) {
	if (allowFieldLocal && performFieldUndoRedo(redo)) {
		return;
	}
	if (!canPerformHistoryUndoRedo(redo)) {
		return;
	}
	const auto nextIndex = _historyIndex + (redo ? 1 : -1);
	if ((nextIndex < 0) || (nextIndex >= int(_history.size()))) {
		return;
	}
	const auto previousIndex = _historyIndex;
	const auto wasPerformingUndoRedo = _performingUndoRedo;
	_performingUndoRedo = true;
	const auto guard = gsl::finally([&] {
		_performingUndoRedo = wasPerformingUndoRedo;
	});
	const auto wasRetainingFieldHistoryIndexOverride
		= _retainingFieldHistoryIndexOverride;
	_retainingFieldHistoryIndexOverride = previousIndex;
	const auto retainingFieldHistoryIndexOverride = gsl::finally([&] {
		_retainingFieldHistoryIndexOverride
			= wasRetainingFieldHistoryIndexOverride;
	});
	retainActiveLeafField();
	_historyIndex = nextIndex;
	const auto wasRestoringHistoryRedo = _restoringHistoryRedo;
	_restoringHistoryRedo = redo;
	const auto restoringHistoryRedo = gsl::finally([&] {
		_restoringHistoryRedo = wasRestoringHistoryRedo;
	});
	restoreHistoryEntry(_history[_historyIndex]);
	_fieldUndoAvailable = !_field->isHidden()
		? _field->isUndoAvailable()
		: false;
	_fieldRedoAvailable = !_field->isHidden()
		? _field->isRedoAvailable()
		: false;
	clearFieldUndoRedoNoopState();
	notifyToolbarStateChanged();
}

void Widget::notifyToolbarStateChanged() {
	_toolbarStateChanges.fire_copy(toolbarStateValue());
}

bool Widget::inlineToolbarModeActive() const {
	return !_field->isHidden()
		&& (_state->activeFieldMode() == State::FieldMode::Rich);
}

Widget::ToolbarLinkMode Widget::toolbarLinkMode() const {
	return inlineToolbarModeActive() && _field->hasCurrentMarkdownLink()
		? ToolbarLinkMode::Edit
		: ToolbarLinkMode::Create;
}

Widget::ToolbarActionState Widget::toolbarActionState(
		ToolbarFormatAction action) const {
	const auto inlineActive = inlineToolbarModeActive();
	const auto activeDisplayMath = !inlineActive
		&& [&] {
			const auto leaf = _state->activeLeafPath();
			return leaf && (leaf->kind == StateLeafKind::MathFormula);
		}();
	const auto broaderTextSelected = !inlineActive
		&& broaderSelectionHasSelectedText();
	const auto broaderMediaSelected = !inlineActive
		&& (action == ToolbarFormatAction::Spoiler)
		&& !broaderSelectionMediaBlocks().empty();
	switch (action) {
	case ToolbarFormatAction::Undo:
		return {
			.shown = true,
			.enabled = canPerformFieldUndoRedo(false)
				|| canPerformHistoryUndoRedo(false),
		};
	case ToolbarFormatAction::Redo: {
		const auto enabled = canPerformFieldUndoRedo(true)
			|| canPerformHistoryUndoRedo(true);
		return {
			.shown = enabled,
			.enabled = enabled,
		};
	}
	case ToolbarFormatAction::Link:
		return {
			.shown = true,
			.enabled = inlineActive,
		};
	case ToolbarFormatAction::Count:
		return {};
	case ToolbarFormatAction::Bold:
	case ToolbarFormatAction::Italic:
	case ToolbarFormatAction::Underline:
	case ToolbarFormatAction::StrikeOut:
	case ToolbarFormatAction::PlainText:
		return {
			.shown = true,
			.enabled = inlineActive || broaderTextSelected,
			.active = inlineActive
				&& (action != ToolbarFormatAction::PlainText)
				&& ToolbarActionTag(action)
				&& _field->isMarkdownTagActive(*ToolbarActionTag(action)),
		};
	case ToolbarFormatAction::Spoiler:
		return {
			.shown = true,
			.enabled = inlineActive
				|| broaderTextSelected
				|| broaderMediaSelected,
			.active = inlineActive
				&& _field->isMarkdownTagActive(Ui::InputField::kTagSpoiler),
		};
	case ToolbarFormatAction::Subscript:
	case ToolbarFormatAction::Superscript:
	case ToolbarFormatAction::Marked:
		return {
			.shown = true,
			.enabled = inlineActive,
			.active = inlineActive
				&& ToolbarActionTag(action)
				&& _field->isMarkdownTagActive(*ToolbarActionTag(action)),
		};
	case ToolbarFormatAction::Math:
		return {
			.shown = true,
			.enabled = inlineActive || activeDisplayMath,
			.active = activeDisplayMath || (inlineActive
				&& _field->isMarkdownTagActive(Ui::InputField::kTagIvMath)),
		};
	}
	return {};
}

void Widget::clearFieldUndoRedoNoopState() {
	_fieldUndoNoopState = std::nullopt;
	_fieldRedoNoopState = std::nullopt;
}

bool Widget::escapeActiveBlockBodyFromToolbar() {
	if (_field->isHidden()
		|| _field->textCursor().hasSelection()
		|| !_state->activeBlockBodyCanEscape()) {
		return false;
	}
	auto handled = false;
	recordMutationTransaction([&] {
		const auto committed = commitInlineField();
		if (committed == ApplyResult::Failed) {
			handled = true;
			return MutationTransactionResult{
				.committed = committed,
				.failed = true,
			};
		} else if (const auto target = _state->escapeActiveBlockBody()) {
			refreshPreparedContent();
			activateTextOrdinal(*target, 0);
			handled = true;
			return MutationTransactionResult{
				.committed = committed,
				.changed = true,
			};
		} else if (_state->lastLimitError()) {
			showLastLimitToast();
			handled = true;
		}
		return MutationTransactionResult{
			.committed = committed,
			.changed = (committed == ApplyResult::Changed),
		};
	});
	return handled;
}

Fn<void()> Widget::captureScrollTopRestorer() const {
	for (auto parent = parentWidget(); parent; parent = parent->parentWidget()) {
		if (const auto scroll = dynamic_cast<Ui::ScrollArea*>(parent)) {
			const auto weak = QPointer<Ui::ScrollArea>(scroll);
			const auto top = scroll->scrollTop();
			return [=] {
				if (weak) {
					weak->scrollToY(top);
				}
			};
		}
		if (const auto scroll = dynamic_cast<Ui::ElasticScroll*>(parent)) {
			const auto weak = QPointer<Ui::ElasticScroll>(scroll);
			const auto top = scroll->scrollTop();
			return [=] {
				if (weak) {
					weak->scrollToY(top);
				}
			};
		}
	}
	return nullptr;
}

void Widget::insertHeading1() {
	insertBlock({
		.type = State::InsertBlockType::Heading,
		.headingLevel = 1,
	});
}

void Widget::insertBlockquote() {
	insertBlock({ .type = State::InsertBlockType::Blockquote });
}

void Widget::insertCodeBlock() {
	insertBlock({ .type = State::InsertBlockType::Code });
}

void Widget::insertEmoji(EmojiPtr emoji) {
	if (!emoji || !prepareFieldForInput()) {
		return;
	}
	_field->setFocusFast();
	Ui::InsertEmojiAtCursor(_field->textCursor(), emoji);
}

void Widget::insertCustomEmoji(not_null<DocumentData*> document) {
	if (!prepareFieldForInput()) {
		return;
	}
	_field->setFocusFast();
	Data::InsertCustomEmoji(_field.get(), document);
}

Widget::ToolbarState Widget::toolbarStateValue() const {
	auto result = ToolbarState();
	result.linkMode = toolbarLinkMode();
	for (auto i = 0; i != int(ToolbarFormatAction::Count); ++i) {
		const auto action = ToolbarFormatAction(i);
		result[action] = toolbarActionState(action);
	}
	return result;
}

rpl::producer<Widget::ToolbarState> Widget::toolbarStateChanges() const {
	return _toolbarStateChanges.events_starting_with(toolbarStateValue());
}

rpl::producer<Widget::AutosaveEvent> Widget::autosaveEvents() const {
	return _autosaveEvents.events();
}

void Widget::performToolbarUndoRedo(bool redo) {
	if (!canPerformFieldUndoRedo(redo) && !canPerformHistoryUndoRedo(redo)) {
		return;
	}
	performUndoRedo(redo);
}

void Widget::applyToolbarFormatAction(ToolbarFormatAction action) {
	switch (action) {
	case ToolbarFormatAction::Undo:
		performToolbarUndoRedo(false);
		return;
	case ToolbarFormatAction::Redo:
		performToolbarUndoRedo(true);
		return;
	case ToolbarFormatAction::Link:
		editLinkFromToolbar();
		return;
	case ToolbarFormatAction::Math:
		editMathFromToolbar();
		return;
	case ToolbarFormatAction::Count:
		return;
	case ToolbarFormatAction::Bold:
	case ToolbarFormatAction::Italic:
	case ToolbarFormatAction::Underline:
	case ToolbarFormatAction::StrikeOut:
	case ToolbarFormatAction::Spoiler:
	case ToolbarFormatAction::Subscript:
	case ToolbarFormatAction::Superscript:
	case ToolbarFormatAction::Marked:
	case ToolbarFormatAction::PlainText:
		break;
	}
	if (action == ToolbarFormatAction::PlainText) {
		if (inlineToolbarModeActive() && escapeActiveBlockBodyFromToolbar()) {
			return;
		}
		if (const auto fullHeadingSpan = visibleFullHeadingFieldTextSpan()) {
			const auto full = ConvertEditorTagsToRichText(
				_field->getTextWithAppliedMarkdown());
			const auto cursor = _field->textCursor();
			const auto length = int(full.text.size());
			const auto restoreLeaf = fullHeadingSpan->leaf;
			const auto restoreAnchorOffset = std::clamp(
				richOffsetForFieldOffset(full, cursor.anchor()),
				0,
				length);
			const auto restoreCursorOffset = std::clamp(
				richOffsetForFieldOffset(full, cursor.position()),
				0,
				length);
			recordMutationTransaction([&] {
				const auto committed = commitInlineField();
				if (committed == ApplyResult::Failed) {
					return MutationTransactionResult{
						.committed = committed,
						.failed = true,
					};
				}
				_pendingOrdinal = -1;
				_pendingCursorOffset = 0;
				hideInlineField();
				clearInlineFieldEditSession();
				const auto result = _state->applyFormattingToTextSpans(
					{ *fullHeadingSpan },
					TextFormattingAction::PlainText);
				if (result == ApplyResult::Failed) {
					return MutationTransactionResult{
						.committed = committed,
						.failed = true,
					};
				}
				refreshPreparedContent();
				const auto ordinal = _state->textOrdinalForLeafPath(restoreLeaf);
				if (ordinal >= 0) {
					activateTextOrdinal(
						ordinal,
						restoreAnchorOffset,
						restoreCursorOffset);
				} else {
					setFocus();
					notifyToolbarStateChanged();
				}
				return MutationTransactionResult{
					.committed = committed,
					.changed = (result == ApplyResult::Changed)
						|| (committed == ApplyResult::Changed),
				};
			});
			return;
		}
	}
	if (inlineToolbarModeActive()) {
		if (action == ToolbarFormatAction::PlainText) {
			_field->clearCurrentMarkdown();
			notifyToolbarStateChanged();
			return;
		}
		if (const auto tag = ToolbarActionTag(action)) {
			_field->toggleCurrentMarkdownTag(*tag);
			notifyToolbarStateChanged();
		}
		return;
	}
	if (action == ToolbarFormatAction::Marked
		|| action == ToolbarFormatAction::Subscript
		|| action == ToolbarFormatAction::Superscript) {
		return;
	}
	const auto textSpans = broaderSelectionTextSpans();
	const auto mediaBlocks = (action == ToolbarFormatAction::Spoiler)
		? broaderSelectionMediaBlocks()
		: std::vector<State::BlockPath>();
	const auto broaderAction = BroaderFormattingAction(action);
	if ((!broaderAction || textSpans.empty())
		&& mediaBlocks.empty()) {
		return;
	}
	recordMutationTransaction([&] {
		const auto hadVisibleField = !_field->isHidden();
		const auto committed = commitInlineField();
		if (committed == ApplyResult::Failed) {
			return MutationTransactionResult{
				.committed = committed,
				.failed = true,
			};
		}
		if (hadVisibleField) {
			_pendingOrdinal = -1;
			_pendingCursorOffset = 0;
			hideInlineField();
			clearInlineFieldEditSession();
		}
		auto changed = false;
		if (action == ToolbarFormatAction::Spoiler) {
			const auto &page = _state->richPage();
			const auto allTextSpoilered = textSpans.empty()
				|| ranges::all_of(textSpans, [&](const TextNodeSpan &span) {
					const auto current = RichTextFromPath(page, span.leaf);
					if (!current) {
						return true;
					}
					auto before = TextWithEntities();
					auto selected = TextWithEntities();
					auto after = TextWithEntities();
					if (!SplitTextSpan(
							current->text,
							span.from,
							span.till,
							&before,
							&selected,
							&after)) {
						return true;
					}
					return HasFullTextTag(
						ConvertRichTextToEditorTags(std::move(selected)).text,
						Ui::InputField::kTagSpoiler);
				});
			const auto allMediaSpoilered = mediaBlocks.empty()
				|| ranges::all_of(
					mediaBlocks,
					[&](const State::BlockPath &path) {
						const auto block = BlockFromPath(page, path);
						return block && MediaBlockHasSpoiler(*block);
					});
			const auto enableSpoiler = !(allTextSpoilered && allMediaSpoilered);
			if (!textSpans.empty()) {
				const auto result = _state->applyFormattingToTextSpans(
					textSpans,
					TextFormattingAction::Spoiler,
					enableSpoiler);
				if (result == ApplyResult::Failed) {
					return MutationTransactionResult{
						.committed = committed,
						.failed = true,
					};
				}
				changed |= (result == ApplyResult::Changed);
			}
			if (!mediaBlocks.empty()) {
				changed |= _state->toggleSpoilerOnBlocks(
					mediaBlocks,
					enableSpoiler);
			}
		} else if (broaderAction) {
			const auto result = _state->applyFormattingToTextSpans(
				textSpans,
				*broaderAction);
			if (result == ApplyResult::Failed) {
				return MutationTransactionResult{
					.committed = committed,
					.failed = true,
				};
			}
			changed = (result == ApplyResult::Changed);
		}
		if (changed || hadVisibleField || (committed == ApplyResult::Changed)) {
			refreshPreparedContent();
		}
		setFocus();
		notifyToolbarStateChanged();
		return MutationTransactionResult{
			.committed = committed,
			.changed = changed
				|| hadVisibleField
				|| (committed == ApplyResult::Changed),
		};
	});
}

void Widget::editLinkFromToolbar() {
	if (!inlineToolbarModeActive()) {
		return;
	}
	_field->editCurrentMarkdownLink();
}

void Widget::editMathFromToolbar() {
	if (!_field->isHidden()
		&& _state->activeFieldMode() == State::FieldMode::Raw) {
		hideInlineField();
		clearInlineFieldEditSession();
	}
	if (const auto request = activeMathEditRequest()) {
		showMathEditBox(*request);
	} else {
		showMathEditBox(newDisplayMathRequest());
	}
}

void Widget::setInlineFieldExternalInteractionActive(bool active) {
	_inlineFieldExternalInteractionActive = active;
}

void Widget::setTopContentPadding(int value) {
	if (_topContentPadding == value) {
		return;
	}
	_topContentPadding = value;
	updateSearchBarGeometry();
	resizeToWidth(width());
	update();
}

void Widget::setBottomContentPadding(int value) {
	if (_bottomContentPadding == value) {
		return;
	}
	_bottomContentPadding = value;
	resizeToWidth(width());
	update();
}

void Widget::setContentMaxWidth(int value) {
	if (_contentMaxWidth == value) {
		return;
	}
	_contentMaxWidth = value;
	update();
}

rpl::producer<int> Widget::searchSlideHeightValue() const {
	return _searchSlideHeight.value();
}

int Widget::resizeGetHeight(int newWidth) {
	if (!_article) {
		return 1;
	}
	const auto width = std::max(newWidth, 1);
	const auto padding = effectiveBodyPadding();
	if (articleRelayoutDeferralActive()) {
		requestDeferredArticleRelayout();
		if (!_field->isHidden()) {
			requestDeferredInlineFieldGeometry();
			requestDeferredInlineFieldHeightOverride();
		}
		const auto fieldBottom = !_field->isHidden()
			? (_field->y() + _field->height())
			: 0;
		return std::max(
			std::max(
				_articleHeight + padding.top() + padding.bottom(),
				fieldBottom),
			st::ivEditorMinHeight);
	}
	_articleHeight = _article->resizeGetHeight(articleWidth(width));
	syncArticleVisibleTopBottom();
	ensurePendingActivation();
	syncInlineFieldGeometry(width);
	const auto fieldBottom = !_field->isHidden()
		? (_field->y() + _field->height())
		: 0;
	return std::max(
		std::max(
			_articleHeight + padding.top() + padding.bottom(),
			fieldBottom),
		st::ivEditorMinHeight);
}

void Widget::visibleTopBottomUpdated(int visibleTop, int visibleBottom) {
	_visibleRange = Ui::VisibleRange{
		.top = visibleTop,
		.bottom = visibleBottom,
	};
	syncArticleVisibleTopBottom();
}

bool Widget::eventFilter(QObject *object, QEvent *event) {
	if (_field) {
		const auto raw = _field->rawTextEdit();
		const auto fieldObject = (object == _field.get());
		const auto rawObject = (object == raw.get())
			|| (object == raw->viewport());
		if (fieldObject || rawObject) {
			const auto type = event->type();
			if (type == QEvent::ShortcutOverride || type == QEvent::KeyPress) {
				const auto keyEvent = static_cast<QKeyEvent*>(event);
				if (handleFieldBlockInsertShortcut(keyEvent)
					|| handleStructuralBlockInsertShortcut(keyEvent)) {
					return true;
				} else if (type == QEvent::KeyPress
					&& (handleUndoRedoShortcut(keyEvent)
						|| handleSelectAllShortcut(keyEvent)
						|| handleTabNavigation(keyEvent)
						|| handleStructuralSelectionKey(keyEvent)
						|| handleFieldKey(keyEvent))) {
					return true;
				}
			} else if (rawObject && type == QEvent::Wheel) {
				if (_article && _activeSegmentIndex >= 0) {
					const auto wheel = static_cast<QWheelEvent*>(event);
					auto articlePoint = std::optional<QPoint>();
					if (const auto widget = qobject_cast<QWidget*>(object)) {
						articlePoint = widget->mapTo(this, LocalPosition(wheel))
							- articleTopLeft();
					} else {
						articlePoint = mapFromGlobal(GlobalPosition(wheel))
							- articleTopLeft();
					}
					if (!articlePoint) {
						const auto segmentRect = _article->segmentRect(
							_activeSegmentIndex);
						if (!segmentRect.isEmpty()) {
							articlePoint = segmentRect.center();
						}
					}
					if (articlePoint
						&& handleHorizontalScrollWheel(wheel, *articlePoint)) {
						return true;
					}
				}
			} else if (rawObject
				&& (type == QEvent::MouseButtonPress
				|| type == QEvent::MouseMove
				|| type == QEvent::MouseButtonRelease)
				&& handleFieldMouseEvent(event)) {
				return true;
			}
		}
	}
	return Ui::RpWidget::eventFilter(object, event);
}

bool Widget::eventHook(QEvent *e) {
	if (e->type() == QEvent::ShortcutOverride) {
		if (handleFieldBlockInsertShortcut(
				static_cast<QKeyEvent*>(e))
			|| handleStructuralBlockInsertShortcut(
				static_cast<QKeyEvent*>(e))) {
			return true;
		}
	}
	if (e->type() == QEvent::TouchBegin
		|| e->type() == QEvent::TouchUpdate
		|| e->type() == QEvent::TouchEnd
		|| e->type() == QEvent::TouchCancel) {
		auto *ev = static_cast<QTouchEvent*>(e);
		if (ev->device()->type() == base::TouchDevice::TouchScreen) {
			const auto active = (_horizontalScrollDrag
				== HorizontalScrollDrag::Touch);
			touchEvent(ev);
			if (active
				|| (_horizontalScrollDrag == HorizontalScrollDrag::Touch)) {
				return true;
			}
		}
	}
	return Ui::RpWidget::eventHook(e);
}

void Widget::contextMenuEvent(QContextMenuEvent *e) {
	if (!_article) {
		Ui::RpWidget::contextMenuEvent(e);
		return;
	}
	const auto articlePoint = e->pos() - articleTopLeft();
	const auto hit = _article->hitTest(
		articlePoint,
		Ui::Text::StateRequest::Flag::LookupSymbol);
	const auto editHit = _article->editHitTest(articlePoint);
	if (showMediaMenuFromHit(
			editHit,
			hit,
			e->globalPos(),
			MediaClickKind::ContextMenu)) {
		e->accept();
		return;
	}
	const auto owner = StructuralOwnerFromHit(editHit);
	const auto cell = TableCellFromOwner(owner);
	if (cell) {
		const auto range = effectiveTableRangeForCell(*cell);
		if (range.empty()) {
			Ui::RpWidget::contextMenuEvent(e);
			return;
		}
		showTableContextMenu(range, e->globalPos());
		e->accept();
		return;
	}
	const auto listSources = ListContextSources(
		ListItemFromOwner(owner),
		BlockPathFromOwner(owner));
	if (!listSources.empty()) {
		if (const auto range = fullListRangeForSource(listSources.front())) {
			if (_state->listSelectionInfo(*range).valid) {
				showListContextMenu(*range, e->globalPos());
				e->accept();
				return;
			}
		}
	}
	Ui::RpWidget::contextMenuEvent(e);
}

void Widget::focusInEvent(QFocusEvent *e) {
	Ui::RpWidget::focusInEvent(e);
	if (!_settingField && !_field->isHidden()) {
		_field->setFocusFast();
	}
}

bool Widget::focusNextPrevChild(bool next) {
	if (hasFocus() && _field->isHidden() && moveTabBoundary(next)) {
		return true;
	}
	return Ui::RpWidget::focusNextPrevChild(next);
}

void Widget::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape && closeSearch()) {
		e->accept();
		return;
	} else if (handleUndoRedoShortcut(e)) {
		return;
	} else if (handleSelectAllShortcut(e)) {
		return;
	} else if (handleClipboardKey(e)) {
		return;
	} else if (handleFieldBlockInsertShortcut(e)) {
		return;
	} else if (handleStructuralBlockInsertShortcut(e)) {
		return;
	} else if (handleStructuralSelectionKey(e)) {
		return;
	} else if (_field->isHidden() && handleTabNavigation(e)) {
		return;
	} else if (redirectKeyToField(e) && replayKeyIntoField(e)) {
		e->accept();
		return;
	}
	Ui::RpWidget::keyPressEvent(e);
}

bool Widget::handleHorizontalScrollWheel(
		QWheelEvent *e,
		QPoint articlePoint) {
	const auto phase = e->phase();
	if (phase == Qt::NoScrollPhase) {
		_horizontalScrollLock = std::nullopt;
	} else if (phase == Qt::ScrollBegin) {
		_horizontalScrollLock = std::nullopt;
	}
	if (!_article) {
		return false;
	}
	const auto delta = Ui::ScrollDeltaF(e);
	const auto horizontal = (std::abs(delta.x()) > std::abs(delta.y()));
	if (phase != Qt::NoScrollPhase
		&& phase != Qt::ScrollBegin
		&& !_horizontalScrollLock) {
		_horizontalScrollLock = horizontal ? Qt::Horizontal : Qt::Vertical;
	}
	if (!_article->horizontalScrollHit(articlePoint).scrollable) {
		return false;
	}
	if (horizontal) {
		if (_horizontalScrollLock == Qt::Vertical) {
			return false;
		}
		if (_article->consumeHorizontalScroll(
				articlePoint,
				int(std::round(delta.x())))) {
			syncInlineFieldGeometry();
		}
		e->accept();
		return true;
	}
	if (_horizontalScrollLock == Qt::Horizontal) {
		e->accept();
		return true;
	}
	return false;
}

std::optional<PreparedEditTableCellSource> Widget::activeTableCellSourceAt(
		QObject *object,
		const QContextMenuEvent &e) const {
	if (!_article || _activeSegmentIndex < 0) {
		return std::nullopt;
	}
	const auto cellAt = [&](QPoint articlePoint) {
		const auto owner = StructuralOwnerFromHit(
			_article->editHitTest(articlePoint));
		return TableCellFromOwner(owner);
	};
	if (const auto widget = qobject_cast<QWidget*>(object)) {
		if (const auto cell = cellAt(
				widget->mapTo(this, e.pos()) - articleTopLeft())) {
			return cell;
		}
	}
	const auto segmentRect = _article->segmentRect(_activeSegmentIndex);
	return !segmentRect.isEmpty()
		? cellAt(segmentRect.center())
		: std::optional<PreparedEditTableCellSource>();
}

void Widget::addFieldBlockFormatActions(not_null<QMenu*> menu) {
	const auto formattingText = tr::lng_menu_formatting(tr::now);
	const auto baseText = [](const QString &text) {
		const auto tab = text.indexOf(QChar('\t'));
		return (tab >= 0) ? text.left(tab) : text;
	};
	auto submenu = (QMenu*)nullptr;
	for (const auto action : menu->actions()) {
		if (const auto candidate = action->menu()) {
			if (baseText(action->text()) == formattingText) {
				submenu = candidate;
				break;
			}
		}
	}
	if (!submenu) {
		return;
	}
	const auto textWithShortcut = [](QString text, const QKeySequence &sequence) {
		const auto shortcut = sequence.toString(QKeySequence::NativeText);
		return shortcut.isEmpty()
			? text
			: text + QChar('\t') + shortcut;
	};
	const auto shortcutText = [](const QString &text) {
		const auto tab = text.indexOf(QChar('\t'));
		return (tab >= 0) ? text.mid(tab + 1) : QString();
	};
	const auto monospaceText = tr::lng_menu_formatting_monospace(tr::now);
	const auto monospaceShortcut = Ui::kMonospaceSequence.toString(
		QKeySequence::NativeText);
	for (const auto action : submenu->actions()) {
		if (!action->isSeparator()
			&& baseText(action->text()) == monospaceText
			&& shortcutText(action->text()) == monospaceShortcut) {
			submenu->removeAction(action);
			delete action;
			break;
		}
	}
	const auto before = [&] {
		for (const auto action : submenu->actions()) {
			if (action->isSeparator()) {
				return action;
			}
		}
		return (QAction*)nullptr;
	}();
	const auto add = [&](QAction *action) {
		if (before) {
			submenu->insertAction(before, action);
		} else {
			submenu->addAction(action);
		}
	};
	const auto blockquote = new QAction(
		textWithShortcut(
			tr::lng_menu_formatting_blockquote(tr::now),
			Ui::kBlockquoteSequence),
		submenu);
	connect(blockquote, &QAction::triggered, this, [=] {
		insertBlockquote();
	});
	add(blockquote);

	const auto monospace = new QAction(
		textWithShortcut(monospaceText, Ui::kMonospaceSequence),
		submenu);
	connect(monospace, &QAction::triggered, this, [=] {
		applyFieldMonospaceAction();
	});
	add(monospace);
}

void Widget::handleFieldContextMenuRequest(
		Ui::InputField::ContextMenuRequest request) {
	addFieldBlockFormatActions(request.menu);
	if (!SingleRootPlainTextFieldSelectAllPassthrough(
			_state->richPage(),
			_state->activeLeafPath(),
			_field->isHidden())) {
		const auto selectAllShortcut = QKeySequence(
			QKeySequence::SelectAll).toString(QKeySequence::NativeText);
		const auto shortcutText = [](const QString &text) {
			const auto tab = text.indexOf(QChar('\t'));
			return (tab >= 0) ? text.mid(tab + 1) : QString();
		};
		for (const auto action : request.menu->actions()) {
			if (!action->isSeparator()
				&& !selectAllShortcut.isEmpty()
				&& shortcutText(action->text()) == selectAllShortcut) {
				QObject::disconnect(
					action,
					&QAction::triggered,
					nullptr,
					nullptr);
				connect(action, &QAction::triggered, this, [this] {
					selectWholeDocument();
				});
				break;
			}
		}
	}
	const auto activeLeaf = _state->activePreparedLeafSource();
	const auto listSource = activeLeaf
		? ListItemSourceFromLeaf(*activeLeaf)
		: std::optional<PreparedEditListItemSource>();
	const auto listBlock = activeLeaf
		? std::make_optional(activeLeaf->block)
		: std::optional<PreparedEditBlockPath>();
	const auto listRange = effectiveListRangeForSource(listSource, listBlock);
	const auto listSources = ListContextSources(listSource, listBlock);
	const auto listMenuRange = !listSources.empty()
		? fullListRangeForSource(listSources.front())
		: std::optional<PreparedListItemRange>();
	const auto cell = activeTableCellSourceAt(
		_field->rawTextEdit().get(),
		*request.event);
	const auto tableRange = cell
		? effectiveTableRangeForCell(*cell)
		: PreparedEditTableCellRange();
	const auto listInfo = listMenuRange
		? _state->listSelectionInfo(*listMenuRange)
		: State::ListSelectionInfo();
	const auto listItemInfo = listRange
		? _state->listSelectionInfo(*listRange)
		: State::ListSelectionInfo();
	const auto tableInfo = !tableRange.empty()
		? _state->tableSelectionInfo(tableRange)
		: State::TableSelectionInfo();
	const auto hasListMenu = listMenuRange && listInfo.valid;
	const auto hasListItemMenu = listRange
		&& listItemInfo.valid
		&& (listItemInfo.listKind == RichPage::ListKind::Ordered)
		&& !listItemInfo.taskList;
	const auto hasTableMenu = !tableRange.empty() && tableInfo.valid;
	if (!hasListMenu && !hasListItemMenu && !hasTableMenu) {
		return;
	}
	request.customizePopupMenu([=](not_null<Ui::PopupMenu*> popup) {
		const auto popupMenu = popup->menu();
		auto position = 0;
		const auto addSubmenu = [&](const QString &text, const auto &fill) {
			const auto action = new QAction(text, popupMenu.get());
			action->setMenu(new QMenu(popupMenu.get()));
			popup->insertAction(
				position++,
				base::make_unique_q<Ui::Menu::Action>(
					popupMenu,
					popupMenu->st(),
					action,
					nullptr,
					nullptr));
			const auto submenu = popup->ensureSubmenu(
				action,
				st::popupMenuWithIcons);
			fill(submenu);
		};
		if (hasListMenu) {
			addSubmenu(
				tr::lng_article_list_change(tr::now),
				[=](not_null<Ui::PopupMenu*> submenu) {
					fillListChangeMenu(submenu, *listMenuRange);
				});
		}
		if (hasListItemMenu) {
			addSubmenu(
				tr::lng_article_list_item_change(tr::now),
				[=](not_null<Ui::PopupMenu*> submenu) {
					fillListItemChangeMenu(submenu, *listRange);
				});
		}
		if (hasTableMenu) {
			addSubmenu(
				tr::lng_article_table_change(tr::now),
				[=](not_null<Ui::PopupMenu*> submenu) {
					fillTableChangeMenu(submenu, tableRange);
				});
		}
		if (position > 0) {
			const auto separator = new QAction(popupMenu.get());
			separator->setSeparator(true);
			popup->insertAction(
				position,
				base::make_unique_q<Ui::Menu::Separator>(
					popupMenu,
					popupMenu->st(),
					popupMenu->st().separator,
					separator));
		}
	});
}

std::optional<PreparedListItemRange> Widget::effectiveListRangeForSource(
		const std::optional<PreparedEditListItemSource> &source,
		const std::optional<PreparedEditBlockPath> &block) {
	auto fallback = std::optional<PreparedListItemRange>();
	for (const auto &candidate : ListContextSources(source, block)) {
		const auto single = ListRangeFromItem(candidate);
		if (single.empty()) {
			continue;
		}
		if (!fallback) {
			fallback = single;
		}
		if (const auto selected = _state->listContextRangeForSelection(
				_structuralSelection,
				candidate)) {
			return *selected;
		}
	}
	return fallback;
}

std::optional<PreparedListItemRange> Widget::fullListRangeForSource(
		const PreparedEditListItemSource &source) const {
	const auto path = _state->convertBlockPath(source.block);
	const auto block = path ? BlockFromPath(_state->richPage(), *path) : nullptr;
	if (!path
		|| !block
		|| block->kind != RichPage::BlockKind::List
		|| source.listItemIndex < 0
		|| source.listItemIndex >= int(block->listItems.size())) {
		return std::nullopt;
	}
	return PreparedListItemRange{
		.block = source.block,
		.from = 0,
		.till = int(block->listItems.size()),
	};
}

std::optional<Markdown::PreparedEditBlockPath> Widget::selectedBlockPath() const {
	switch (_structuralSelection.kind) {
	case PreparedEditSelectionKind::Blocks: {
		const auto &range = _structuralSelection.blocks;
		if (range.empty() || (range.till - range.from != 1)) {
			return std::nullopt;
		}
		return PreparedEditBlockPath{
			.container = range.container,
			.index = range.from,
		};
	}
	case PreparedEditSelectionKind::ListItems:
		return _structuralSelection.listItems.empty()
			? std::nullopt
			: std::make_optional(_structuralSelection.listItems.block);
	case PreparedEditSelectionKind::TableRows:
		return _structuralSelection.tableRows.empty()
			? std::nullopt
			: std::make_optional(_structuralSelection.tableRows.block);
	case PreparedEditSelectionKind::TableCells:
		return _structuralSelection.tableCells.empty()
			? std::nullopt
			: std::make_optional(_structuralSelection.tableCells.block);
	case PreparedEditSelectionKind::None:
		return std::nullopt;
	}
	return std::nullopt;
}

Widget::ActiveBlockInfo Widget::activeBlockInfo() const {
	if (const auto path = selectedBlockPath()) {
		const auto statePath = _state->convertBlockPath(*path);
		const auto block = statePath
			? BlockFromPath(_state->richPage(), *statePath)
			: nullptr;
		if (block) {
			return {
				.kind = block->kind,
				.pullquote = block->pullquote,
				.headingLevel = block->headingLevel,
			};
		}
	}
	const auto leaf = _state->activeLeafPath();
	if (!leaf) {
		return {};
	}
	const auto block = BlockFromPath(_state->richPage(), leaf->block);
	if (!block) {
		return {};
	}
	return {
		.kind = block->kind,
		.pullquote = block->pullquote,
		.headingLevel = block->headingLevel,
	};
}

std::optional<PreparedListItemRange> Widget::currentListRangeAtCaret() const {
	if (_structuralSelection.kind == PreparedEditSelectionKind::ListItems
		&& !_structuralSelection.listItems.empty()
		&& _state->listSelectionInfo(_structuralSelection.listItems).valid) {
		const auto source = PreparedEditListItemSource{
			.block = _structuralSelection.listItems.block,
			.listItemIndex = _structuralSelection.listItems.from,
		};
		if (const auto selected = _state->listContextRangeForSelection(
				_structuralSelection,
				source)) {
			if (!selected->empty()
				&& _state->listSelectionInfo(*selected).valid) {
				return *selected;
			}
		}
	}
	const auto activeLeaf = _state->activePreparedLeafSource();
	const auto listSource = activeLeaf
		? ListItemSourceFromLeaf(*activeLeaf)
		: std::optional<PreparedEditListItemSource>();
	const auto listBlock = activeLeaf
		? std::make_optional(activeLeaf->block)
		: std::optional<PreparedEditBlockPath>();
	const auto sources = ListContextSources(listSource, listBlock);
	if (sources.empty()) {
		return std::nullopt;
	}
	const auto range = fullListRangeForSource(sources.front());
	if (!range || !_state->listSelectionInfo(*range).valid) {
		return std::nullopt;
	}
	return range;
}

std::optional<PreparedListItemRange> Widget::currentListItemRangeAtCaret() {
	const auto activeLeaf = _state->activePreparedLeafSource();
	const auto listSource = activeLeaf
		? ListItemSourceFromLeaf(*activeLeaf)
		: std::optional<PreparedEditListItemSource>();
	const auto listBlock = activeLeaf
		? std::make_optional(activeLeaf->block)
		: std::optional<PreparedEditBlockPath>();
	const auto range = effectiveListRangeForSource(listSource, listBlock);
	if (!range || !_state->listSelectionInfo(*range).valid) {
		return std::nullopt;
	}
	return range;
}

State::ListSelectionInfo Widget::listSelectionInfo(
		const PreparedListItemRange &range) const {
	return _state->listSelectionInfo(range);
}

std::optional<PreparedEditTableCellRange>
Widget::currentTableRangeAtCaret() const {
	if (_structuralSelection.kind == PreparedEditSelectionKind::TableCells
		&& !_structuralSelection.tableCells.empty()
		&& _state->tableSelectionInfo(_structuralSelection.tableCells).valid) {
		return _structuralSelection.tableCells;
	}
	const auto activeLeaf = _state->activePreparedLeafSource();
	if (!activeLeaf
		|| activeLeaf->kind != PreparedEditLeafKind::TableCellText
		|| activeLeaf->tableRowIndex < 0
		|| activeLeaf->tableCellIndex < 0) {
		return std::nullopt;
	}
	const auto path = _state->convertBlockPath(activeLeaf->block);
	const auto block = path
		? BlockFromPath(_state->richPage(), *path)
		: nullptr;
	if (!block || block->kind != RichPage::BlockKind::Table) {
		return std::nullopt;
	}
	const auto grid = BuildTableGrid(*block);
	for (const auto &cell : grid.cells) {
		if (cell.rowIndex != activeLeaf->tableRowIndex
			|| cell.cellIndex != activeLeaf->tableCellIndex) {
			continue;
		}
		auto range = PreparedEditTableCellRange{
			.block = activeLeaf->block,
			.rowFrom = cell.rowFrom,
			.rowTill = cell.rowTill,
			.columnFrom = cell.columnFrom,
			.columnTill = cell.columnTill,
		};
		if (range.empty() || !_state->tableSelectionInfo(range).valid) {
			return std::nullopt;
		}
		const auto source = Markdown::PreparedEditTableCellSource{
			.block = activeLeaf->block,
			.tableRowIndex = cell.rowFrom,
			.tableCellIndex = cell.cellIndex,
			.column = cell.columnFrom,
			.colspan = cell.columnTill - cell.columnFrom,
			.rowspan = cell.rowTill - cell.rowFrom,
		};
		if (const auto selected = _state->tableContextRangeForSelection(
				_structuralSelection,
				source)) {
			if (!selected->empty()
				&& _state->tableSelectionInfo(*selected).valid) {
				return *selected;
			}
		}
		return range;
	}
	return std::nullopt;
}

void Widget::showListContextMenu(
		const PreparedListItemRange &range,
		QPoint globalPos) {
	const auto menu = Ui::CreateChild<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	fillListChangeMenu(menu, range);
	if (menu->empty()) {
		menu->deleteLater();
		return;
	}
	menu->popup(globalPos);
}

void Widget::fillListChangeMenu(
		not_null<Ui::PopupMenu*> menu,
		const PreparedListItemRange &range) {
	const auto info = _state->listSelectionInfo(range);
	if (!info.valid) {
		return;
	}
	const auto ordered = (info.listKind == RichPage::ListKind::Ordered);
	const auto task = !ordered && info.taskList;
	const auto bullet = !ordered && !task;
	Menu::AddCheckedAction(
		menu,
		tr::lng_article_insert_ordered_list(tr::now),
		[=] {
			applyListChange([=] {
				return _state->setListStyle(range, State::ListStyle::Ordered);
			});
		},
		&st::ivEditorToolbarOrderedListIcon,
		ordered);
	Menu::AddCheckedAction(
		menu,
		tr::lng_article_insert_bullet_list(tr::now),
		[=] {
			applyListChange([=] {
				return _state->setListStyle(range, State::ListStyle::Bullet);
			});
		},
		&st::ivEditorToolbarBulletListIcon,
		bullet);
	Menu::AddCheckedAction(
		menu,
		tr::lng_article_insert_task_list(tr::now),
		[=] {
			applyListChange([=] {
				return _state->setListStyle(range, State::ListStyle::Task);
			});
		},
		&st::ivEditorToolbarTaskListIcon,
		task);
	if (!ordered) {
		return;
	}
	const auto addOrderedTypeAction = [&](PreparedOrderedListType type) {
		Menu::AddCheckedAction(
			menu,
			OrderedListTypeText(type),
			[=] {
				applyListChange([=] {
					return _state->setListOrderedType(range, type);
				});
			},
			OrderedListTypeIcon(type),
			OrderedListTypeChecked(info, type));
	};
	menu->addSeparator();
	addOrderedTypeAction(PreparedOrderedListType::Decimal);
	addOrderedTypeAction(PreparedOrderedListType::LowerAlpha);
	addOrderedTypeAction(PreparedOrderedListType::UpperAlpha);
	addOrderedTypeAction(PreparedOrderedListType::LowerRoman);
	addOrderedTypeAction(PreparedOrderedListType::UpperRoman);
	menu->addSeparator();
	Menu::AddCheckedAction(
		menu,
		tr::lng_article_list_reversed(tr::now),
		[=] {
			applyListChange([=] {
				return _state->setListOrderedReversed(range, !info.reversed);
			});
		},
		&st::menuIconChangeOrder,
		info.reversed);
}

void Widget::fillListItemChangeMenu(
		not_null<Ui::PopupMenu*> menu,
		const PreparedListItemRange &range) {
	const auto info = _state->listSelectionInfo(range);
	if (!info.valid
		|| info.taskList
		|| info.listKind != RichPage::ListKind::Ordered) {
		return;
	}
	const auto addOrderedTypeAction = [&](PreparedOrderedListType type) {
		Menu::AddCheckedAction(
			menu,
			OrderedListTypeText(type),
			[=] {
				applyListChange([=] {
					return _state->setListItemOrderedType(range, type);
				});
			},
			OrderedListTypeIcon(type),
			OrderedListTypeChecked(info, type));
	};
	addOrderedTypeAction(PreparedOrderedListType::Decimal);
	addOrderedTypeAction(PreparedOrderedListType::LowerAlpha);
	addOrderedTypeAction(PreparedOrderedListType::UpperAlpha);
	addOrderedTypeAction(PreparedOrderedListType::LowerRoman);
	addOrderedTypeAction(PreparedOrderedListType::UpperRoman);
}

PreparedEditTableCellRange Widget::effectiveTableRangeForCell(
		const PreparedEditTableCellSource &source) {
	const auto single = TableRangeFromCell(source);
	if (single.empty()) {
		return {};
	}
	if (const auto selected = _state->tableContextRangeForSelection(
			_structuralSelection,
			source)) {
		return *selected;
	}
	clearSelection();
	return single;
}

void Widget::applyListChange(Fn<bool()> change) {
	recordMutationTransaction([&] {
		const auto committed = commitInlineField();
		if (committed == ApplyResult::Failed) {
			return MutationTransactionResult{
				.committed = committed,
				.failed = true,
			};
		}
		_pendingOrdinal = -1;
		_pendingCursorOffset = 0;
		hideInlineField();
		if (_article) {
			_article->clearTextLeafHeightOverride();
			_article->clearEditableTextEmptyOverride();
			_article->clearEditableMaxLineWidthOverride();
		}
		clearSelection();
		setFocus();
		if (!change()) {
			refreshAfterInlineFieldCommit(committed);
			showLastLimitToast();
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		refreshPreparedContent();
		return MutationTransactionResult{
			.committed = committed,
			.changed = true,
		};
	});
}

void Widget::showTableContextMenu(
		const PreparedEditTableCellRange &range,
		QPoint globalPos) {
	const auto menu = Ui::CreateChild<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	fillTableChangeMenu(menu, range);
	if (menu->empty()) {
		menu->deleteLater();
		return;
	}
	menu->popup(globalPos);
}

void Widget::fillTableChangeMenu(
		not_null<Ui::PopupMenu*> menu,
		const PreparedEditTableCellRange &range) {
	const auto info = _state->tableSelectionInfo(range);
	if (!info.valid) {
		return;
	}
	menu->addAction(
		tr::lng_article_table_add_row(tr::now),
		[=] {
			applyTableChange([=] {
				return _state->addTableRow(range, false);
			});
		},
		&st::ivEditorTableAddRowAboveIcon);
	menu->addAction(
		tr::lng_article_table_add_row(tr::now),
		[=] {
			applyTableChange([=] {
				return _state->addTableRow(range, true);
			});
		},
		&st::ivEditorTableAddRowBelowIcon);
	menu->addAction(
		tr::lng_article_table_add_column(tr::now),
		[=] {
			applyTableChange([=] {
				return _state->addTableColumn(range, false);
			});
		},
		&st::ivEditorTableAddColumnLeftIcon);
	menu->addAction(
		tr::lng_article_table_add_column(tr::now),
		[=] {
			applyTableChange([=] {
				return _state->addTableColumn(range, true);
			});
		},
		&st::ivEditorTableAddColumnRightIcon);
	menu->addSeparator();
	Menu::AddCheckedAction(
		menu,
		tr::lng_article_table_header(tr::now),
		[=] {
			applyTableChange([=] {
				return _state->setTableHeader(range, !info.allHeader);
			});
		},
		info.allHeader
			? &st::ivEditorTableHeaderOffIcon
			: &st::ivEditorTableHeaderIcon,
		info.allHeader);
	menu->addSeparator();
	Menu::AddCheckedAction(
		menu,
		tr::lng_article_table_align_left(tr::now),
		[=] {
			applyTableChange([=] {
				return _state->setTableAlignment(
					range,
					RichPage::TableAlignment::Left);
			});
		},
		&st::ivEditorTableAlignLeftIcon,
		info.allAlignLeft);
	Menu::AddCheckedAction(
		menu,
		tr::lng_article_table_align_center(tr::now),
		[=] {
			applyTableChange([=] {
				return _state->setTableAlignment(
					range,
					RichPage::TableAlignment::Center);
			});
		},
		&st::ivEditorTableAlignCenterIcon,
		info.allAlignCenter);
	Menu::AddCheckedAction(
		menu,
		tr::lng_article_table_align_right(tr::now),
		[=] {
			applyTableChange([=] {
				return _state->setTableAlignment(
					range,
					RichPage::TableAlignment::Right);
			});
		},
		&st::ivEditorTableAlignRightIcon,
		info.allAlignRight);
	menu->addSeparator();
	Menu::AddCheckedAction(
		menu,
		tr::lng_article_table_align_top(tr::now),
		[=] {
			applyTableChange([=] {
				return _state->setTableVerticalAlignment(
					range,
					RichPage::TableVerticalAlignment::Top);
			});
		},
		&st::ivEditorTableAlignTopIcon,
		info.allAlignTop);
	Menu::AddCheckedAction(
		menu,
		tr::lng_article_table_align_middle(tr::now),
		[=] {
			applyTableChange([=] {
				return _state->setTableVerticalAlignment(
					range,
					RichPage::TableVerticalAlignment::Middle);
			});
		},
		&st::ivEditorTableAlignMiddleIcon,
		info.allAlignMiddle);
	Menu::AddCheckedAction(
		menu,
		tr::lng_article_table_align_bottom(tr::now),
		[=] {
			applyTableChange([=] {
				return _state->setTableVerticalAlignment(
					range,
					RichPage::TableVerticalAlignment::Bottom);
			});
		},
		&st::ivEditorTableAlignBottomIcon,
		info.allAlignBottom);
	if (info.canSplitCell) {
		menu->addSeparator();
		menu->addAction(
			tr::lng_article_table_split_cell(tr::now),
			[=] {
				applyTableChange([=] {
					return _state->splitTableCell(range);
				});
			},
			&st::ivEditorTableSplitIcon);
	} else if (info.canUniteCells) {
		menu->addSeparator();
		menu->addAction(
			tr::lng_article_table_unite_cells(tr::now),
			[=] {
				applyTableChange([=] {
					return _state->uniteTableCells(range);
				});
			},
			&st::ivEditorTableMergeIcon);
	}
	const auto hasDeleteAction = info.canDeleteTable
		|| info.canDeleteRows
		|| info.canDeleteColumns;
	if (hasDeleteAction) {
		menu->addSeparator();
		if (info.canDeleteTable) {
			menu->addAction(
				tr::lng_article_table_delete_table(tr::now),
				[=] {
					applyTableChange([=] {
						return _state->removeTable(range);
					});
				},
				&st::menuIconTableSubmenuDelete);
		} else {
			if (info.canDeleteRows) {
				menu->addAction(
					(info.selectedRows == 1)
						? tr::lng_article_table_delete_row(tr::now)
						: tr::lng_article_table_delete_rows(tr::now),
					[=] {
						applyTableChange([=] {
							return _state->removeTableRows(range);
						});
					},
					&st::menuIconTableSubmenuDelete);
			}
			if (info.canDeleteColumns) {
				menu->addAction(
					(info.selectedColumns == 1)
						? tr::lng_article_table_delete_column(tr::now)
						: tr::lng_article_table_delete_columns(tr::now),
					[=] {
						applyTableChange([=] {
							return _state->removeTableColumns(range);
						});
					},
					&st::menuIconTableSubmenuDelete);
			}
		}
	}
	menu->addSeparator();
	Menu::AddCheckedAction(
		menu,
		tr::lng_article_table_borderless(tr::now),
		[=] {
			applyTableChange([=] {
				return _state->setTableBordered(range, !info.bordered);
			});
		},
		&st::ivEditorTableBorderlessIcon,
		!info.bordered);
	Menu::AddCheckedAction(
		menu,
		tr::lng_article_table_striped(tr::now),
		[=] {
			applyTableChange([=] {
				return _state->setTableStriped(range, !info.striped);
			});
		},
		&st::ivEditorTableStripedIcon,
		info.striped);
}

void Widget::applyTableChange(Fn<bool()> change) {
	recordMutationTransaction([&] {
		const auto committed = commitInlineField();
		if (committed == ApplyResult::Failed) {
			return MutationTransactionResult{
				.committed = committed,
				.failed = true,
			};
		}
		_pendingOrdinal = -1;
		_pendingCursorOffset = 0;
		hideInlineField();
		if (_article) {
			_article->clearTextLeafHeightOverride();
		}
		clearSelection();
		setFocus();
		if (!change()) {
			refreshAfterInlineFieldCommit(committed);
			showLastLimitToast();
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		refreshPreparedContent();
		return MutationTransactionResult{
			.committed = committed,
			.changed = true,
		};
	});
}

std::optional<State::BlockPath> Widget::simpleMediaBlockPathFromHit(
		const PreparedEditHit &hit) const {
	if (hit.kind != PreparedEditHitKind::Block || !hit.block) {
		return std::nullopt;
	}
	const auto path = _state->convertBlockPath(*hit.block);
	if (!path) {
		return std::nullopt;
	}
	const auto block = BlockFromPath(_state->richPage(), *path);
	if (!block || !IsSimpleMediaBlockKind(block->kind)) {
		return std::nullopt;
	}
	return path;
}

std::optional<State::BlockPath> Widget::groupedMediaBlockPathFromHit(
		const PreparedEditHit &hit) const {
	if (hit.kind != PreparedEditHitKind::Block || !hit.block) {
		return std::nullopt;
	}
	const auto path = _state->convertBlockPath(*hit.block);
	if (!path) {
		return std::nullopt;
	}
	const auto block = BlockFromPath(_state->richPage(), *path);
	if (!block || block->kind != RichPage::BlockKind::GroupedMedia) {
		return std::nullopt;
	}
	return path;
}

bool Widget::structuralPhotoVideoSelectionAvailable() const {
	return _state->canGroupPhotoVideoBlocks(_structuralSelection);
}

bool Widget::clickHitsStructuralPhotoVideoSelection(
		const PreparedEditHit &hit) const {
	if (!structuralPhotoVideoSelectionAvailable()
		|| _structuralSelection.kind != PreparedEditSelectionKind::Blocks
		|| hit.kind != PreparedEditHitKind::Block
		|| !hit.block) {
		return false;
	}
	const auto path = _state->convertBlockPath(*hit.block);
	if (!path || !BlockFromPath(_state->richPage(), *path)) {
		return false;
	}
	return PreparedPathInBlockRange(
		hit.block->path,
		_structuralSelection.blocks);
}

void Widget::showSimpleMediaMenu(
		const State::BlockPath &path,
		QPoint globalPos) {
	const auto block = BlockFromPath(_state->richPage(), path);
	if (!block || !IsSimpleMediaBlockKind(block->kind)) {
		return;
	}
	const auto menu = Ui::CreateChild<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	menu->addAction(
		tr::lng_attach_replace(tr::now),
		[=] {
			requestReplaceMedia(path);
		},
		&st::menuIconReplace);
	if (block->kind == RichPage::BlockKind::Photo) {
		menu->addAction(
			tr::lng_context_draw(tr::now),
			[=] {
				editPhotoBlock(path);
			},
			&st::menuIconPalette);
	}
	if (IsPhotoVideoBlockKind(block->kind)) {
		const auto currentSpoiler = block->spoiler;
		Menu::AddCheckedAction(
			menu,
			tr::lng_context_spoiler_effect(tr::now),
			[=] {
				[[maybe_unused]] const auto changed = applyMediaBlockChange([=] {
					const auto current = BlockFromPath(
						_state->richPage(),
						path);
					if (!current || !IsPhotoVideoBlockKind(current->kind)) {
						return false;
					}
					return _state->toggleSpoilerOnBlocks(
						std::vector<State::BlockPath>{ path },
						!currentSpoiler);
				});
			},
			&st::menuIconSpoiler,
			currentSpoiler);
	}
	Ui::Menu::CreateAddActionCallback(menu)({
		.text = tr::lng_box_remove(tr::now),
		.handler = [=] {
			auto target = std::optional<int>();
			const auto changed = applyMediaBlockChange([=, &target] {
				const auto current = BlockFromPath(
					_state->richPage(),
					path);
				if (!current || !IsSimpleMediaBlockKind(current->kind)) {
					return false;
				}
				target = _state->removeBlock(path, true);
				return true;
			});
			if (!changed) {
				return;
			} else if (target) {
				activateTextOrdinal(*target, 0);
			} else {
				activateInitialNode();
			}
		},
		.icon = &st::menuIconDeleteAttention,
		.isAttention = true,
	});
	if (menu->empty()) {
		menu->deleteLater();
		return;
	}
	menu->popup(globalPos);
}

void Widget::showGroupedMediaMenu(
		const State::BlockPath &path,
		int itemIndex,
		QPoint globalPos) {
	const auto block = BlockFromPath(_state->richPage(), path);
	if (!block || block->kind != RichPage::BlockKind::GroupedMedia) {
		return;
	}
	const auto menu = Ui::CreateChild<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	const auto hasItem = (itemIndex >= 0)
		&& (itemIndex < int(block->mediaItems.size()))
		&& IsPhotoVideoBlockKind(block->mediaItems[itemIndex].kind);
	const auto currentSpoiler = hasItem
		? block->mediaItems[itemIndex].spoiler
		: GroupedPhotoVideoItemsHaveSpoiler(*block);
	menu->addAction(
		tr::lng_article_media_ungroup(tr::now),
		[=] {
			[[maybe_unused]] const auto changed = applyMediaBlockChange([=] {
				const auto current = BlockFromPath(
					_state->richPage(),
					path);
				if (!current
					|| current->kind != RichPage::BlockKind::GroupedMedia) {
					return false;
				}
				return _state->ungroupGroupedMediaBlock(path);
			});
		},
		&st::menuIconExpand);
	if (GroupedMediaHasPhotoVideoItems(*block)) {
		Menu::AddCheckedAction(
			menu,
			tr::lng_context_spoiler_effect(tr::now),
			[=] {
				[[maybe_unused]] const auto changed
					= applyGroupedMediaChangePreservingActiveIndex(path, [=] {
						const auto current = BlockFromPath(
							_state->richPage(),
							path);
						if (!current
							|| !GroupedMediaHasPhotoVideoItems(*current)) {
							return false;
						}
						return hasItem
							? _state->toggleSpoilerOnGroupedItem(
								path,
								itemIndex,
								!currentSpoiler)
							: _state->toggleSpoilerOnBlocks(
								std::vector<State::BlockPath>{ path },
								!currentSpoiler);
					});
			},
			&st::menuIconSpoiler,
			currentSpoiler);
	}
	Ui::Menu::CreateAddActionCallback(menu)({
		.text = tr::lng_box_remove(tr::now),
		.handler = [=] {
			auto target = std::optional<int>();
			const auto changed = applyGroupedMediaChangePreservingActiveIndex(
				path,
				[=, &target] {
					const auto current = BlockFromPath(
						_state->richPage(),
						path);
					if (!current
						|| current->kind != RichPage::BlockKind::GroupedMedia) {
						return false;
					}
					if (hasItem) {
						return _state->removeGroupedItem(path, itemIndex);
					}
					target = _state->removeBlock(path, true);
					return true;
				});
			if (!changed) {
				return;
			} else if (target) {
				activateTextOrdinal(*target, 0);
			} else {
				activateInitialNode();
			}
		},
		.icon = &st::menuIconDeleteAttention,
		.isAttention = true,
	});
	if (menu->empty()) {
		menu->deleteLater();
		return;
	}
	menu->popup(globalPos);
}

void Widget::showStructuralPhotoVideoMenu(QPoint globalPos) {
	if (!structuralPhotoVideoSelectionAvailable()) {
		return;
	}
	const auto selection = _structuralSelection;
	const auto menu = Ui::CreateChild<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	menu->addAction(
		tr::lng_article_media_collage(tr::now),
		[=] {
			[[maybe_unused]] const auto changed = applyMediaBlockChange([=] {
				return _state->groupPhotoVideoBlocks(
					selection,
					RichPage::GroupedMediaIntent::Collage);
			});
		},
		&st::menuIconShowAll);
	menu->addAction(
		tr::lng_article_media_slideshow(tr::now),
		[=] {
			[[maybe_unused]] const auto changed = applyMediaBlockChange([=] {
				return _state->groupPhotoVideoBlocks(
					selection,
					RichPage::GroupedMediaIntent::Slideshow);
			});
		},
		&st::menuIconPhotoSet);
	Ui::Menu::CreateAddActionCallback(menu)({
		.text = tr::lng_box_remove(tr::now),
		.handler = [=] {
			if (structuralPhotoVideoSelectionAvailable()) {
				removeStructuralSelectionAndReposition(true);
			}
		},
		.icon = &st::menuIconDeleteAttention,
		.isAttention = true,
	});
	if (menu->empty()) {
		menu->deleteLater();
		return;
	}
	menu->popup(globalPos);
}

bool Widget::showMediaMenuFromHit(
		const PreparedEditHit &hit,
		const Markdown::MarkdownArticleHitTestResult &articleHit,
		QPoint globalPos,
		MediaClickKind clickKind) {
	if (clickHitsStructuralPhotoVideoSelection(hit)) {
		showStructuralPhotoVideoMenu(globalPos);
		return true;
	} else if (const auto path = simpleMediaBlockPathFromHit(hit)) {
		if (articleHit.mediaActivation.kind
			== Markdown::MediaActivationKind::None) {
			return false;
		}
		showSimpleMediaMenu(*path, globalPos);
		return true;
	} else if (const auto path = groupedMediaBlockPathFromHit(hit)) {
		if (articleHit.mediaActivation.kind
			== Markdown::MediaActivationKind::None) {
			return false;
		}
		const auto itemIndex = articleHit.mediaActivation.itemIndex;
		if (clickKind == MediaClickKind::Left) {
			const auto block = BlockFromPath(_state->richPage(), *path);
			const auto photoItem = block
				&& (itemIndex >= 0)
				&& (itemIndex < int(block->mediaItems.size()))
				&& (block->mediaItems[itemIndex].kind
					== RichPage::BlockKind::Photo);
			if (photoItem) {
				editGroupedItemPhoto(*path, itemIndex);
				return true;
			}
		}
		showGroupedMediaMenu(*path, itemIndex, globalPos);
		return true;
	}
	return false;
}

bool Widget::activateGroupedMediaLinkFromHit(
		const PreparedEditHit &hit,
		const Markdown::MarkdownArticleHitTestResult &articleHit,
		Qt::MouseButton button) {
	if (!groupedMediaBlockPathFromHit(hit)
		|| !articleHit.state.link
		|| articleHit.mediaActivation.kind
			!= Markdown::MediaActivationKind::None) {
		return false;
	}
	ActivateClickHandler(this, articleHit.state.link, button);
	return true;
}

int Widget::groupedActiveIndexForPath(const State::BlockPath &path) const {
	for (const auto &geo : _article->mediaBlockGeometries()) {
		if (!geo.grouped) {
			continue;
		}
		const auto geoPath = _state->convertBlockPath(geo.block);
		if (geoPath && (*geoPath == path)) {
			return geo.activeItemIndex;
		}
	}
	return -1;
}

void Widget::restoreGroupedActiveIndexForPath(
		const State::BlockPath &path,
		int activeIndex) {
	if (activeIndex < 0) {
		return;
	}
	for (const auto &geo : _article->mediaBlockGeometries()) {
		if (!geo.grouped) {
			continue;
		}
		const auto geoPath = _state->convertBlockPath(geo.block);
		if (geoPath && (*geoPath == path)) {
			_article->setGroupedActiveIndex(geo.block, activeIndex);
			return;
		}
	}
}

bool Widget::applyGroupedMediaChangePreservingActiveIndex(
		const State::BlockPath &path,
		Fn<bool()> change) {
	const auto savedActiveIndex = groupedActiveIndexForPath(path);
	const auto changed = applyMediaBlockChange(std::move(change));
	if (changed) {
		restoreGroupedActiveIndexForPath(path, savedActiveIndex);
	}
	return changed;
}

bool Widget::applyMediaBlockChange(Fn<bool()> change) {
	const auto hadVisibleField = !_field->isHidden();
	auto changed = false;
	const auto result = recordMutationTransaction([&] {
		const auto committed = commitInlineField();
		if (committed == ApplyResult::Failed) {
			return MutationTransactionResult{
				.committed = committed,
				.failed = true,
			};
		}
		_pendingOrdinal = -1;
		_pendingCursorOffset = 0;
		hideInlineField();
		clearInlineFieldEditSession();
		changed = change();
		if (changed) {
			refreshPreparedContent();
		} else if (hadVisibleField) {
			refreshAfterInlineFieldCommit(committed);
		}
		clearTextSelection();
		clearStructuralSelection();
		setFocus();
		notifyToolbarStateChanged();
		return MutationTransactionResult{
			.committed = committed,
			.changed = (committed == ApplyResult::Changed) || changed,
		};
	});
	return !result.failed && changed;
}

void Widget::requestReplaceMedia(State::BlockPath path) {
	const auto target = _state->replaceTargetForBlock(path);
	if (!target) {
		return;
	}
	requestMedia(std::move(target));
}

void Widget::editPhotoBlock(State::BlockPath path) {
	const auto block = BlockFromPath(_state->richPage(), path);
	if (!block || block->kind != RichPage::BlockKind::Photo) {
		return;
	}
	auto target = _state->replaceTargetForBlock(path);
	if (!target) {
		return;
	}
	openPhotoEditor(block->photoId, block->spoiler, std::move(*target));
}

void Widget::openPhotoEditor(
		uint64 photoId,
		bool spoiler,
		State::ReplaceTarget target) {
	if (!_requestPhotoEditSource) {
		return;
	}
	const auto weak = base::make_weak(this);
	_requestPhotoEditSource(photoId, [=, target = std::move(target)](
			QImage source) {
		const auto strong = weak.get();
		if (!strong || source.isNull()) {
			return;
		}
		strong->showPhotoEditor(std::move(source), spoiler, target);
	});
}

void Widget::showPhotoEditor(
		QImage source,
		bool spoiler,
		State::ReplaceTarget target) {
	const auto previewWidth = st::sendMediaPreviewSize;
	const auto sourceShared = std::make_shared<QImage>(std::move(source));
	const auto replaceTarget = std::make_shared<State::ReplaceTarget>(
		std::move(target));
	auto fileImage = std::make_shared<Image>(QImage(*sourceShared));
	auto editor = base::make_unique_q<::Editor::PhotoEditor>(
		_outer,
		_show,
		nullptr,
		std::move(fileImage),
		::Editor::PhotoModifications());
	const auto raw = editor.get();
	auto layer = std::make_unique<::Editor::LayerWidget>(
		_outer,
		std::move(editor));
	const auto weak = base::make_weak(this);
	::Editor::InitEditorLayer(layer.get(), raw, [=](
			::Editor::PhotoModifications mods) {
		const auto strong = weak.get();
		if (!strong || !mods || !strong->_replacePhotoWithList) {
			return;
		}
		auto copy = QImage(*sourceShared);
		auto list = Storage::PrepareMediaFromImage(
			std::move(copy),
			QByteArray(),
			previewWidth);
		if (list.files.empty()) {
			return;
		}
		using ImageInfo = Ui::PreparedFileInformation::Image;
		auto &file = list.files.front();
		file.spoiler = spoiler;
		if (const auto image = std::get_if<ImageInfo>(
				&file.information->media)) {
			image->modifications = std::move(mods);
		}
		Storage::ApplyModifications(list);
		strong->_replacePhotoWithList(
			not_null<Widget*>(strong),
			std::move(list),
			*replaceTarget);
	});
	_show->showLayer(std::move(layer), Ui::LayerOption::KeepOther);
}

void Widget::editGroupedItemPhoto(State::BlockPath path, int itemIndex) {
	const auto block = BlockFromPath(_state->richPage(), path);
	if (!block
		|| block->kind != RichPage::BlockKind::GroupedMedia
		|| itemIndex < 0
		|| itemIndex >= int(block->mediaItems.size())) {
		return;
	}
	const auto &item = block->mediaItems[itemIndex];
	if (item.kind != RichPage::BlockKind::Photo
		|| mediaUploadStateForGroupedItem(path, itemIndex).uploading) {
		return;
	}
	auto target = _state->replaceTargetForGroupedItem(path, itemIndex);
	if (!target) {
		return;
	}
	openPhotoEditor(item.photoId, item.spoiler, std::move(*target));
}

MediaUploadState Widget::mediaUploadStateForBlock(
		const State::BlockPath &path) const {
	const auto block = BlockFromPath(_state->richPage(), path);
	if (!block) {
		return {};
	}
	const auto mediaId = MediaIdForBlock(*block);
	return _mediaUploadState ? _mediaUploadState(mediaId) : MediaUploadState();
}

MediaUploadState Widget::mediaUploadStateForGroupedItem(
		const State::BlockPath &path,
		int itemIndex) const {
	const auto block = BlockFromPath(_state->richPage(), path);
	if (!block
		|| block->kind != RichPage::BlockKind::GroupedMedia
		|| itemIndex < 0
		|| itemIndex >= int(block->mediaItems.size())) {
		return {};
	}
	const auto mediaId = MediaIdForGroupedItem(block->mediaItems[itemIndex]);
	return _mediaUploadState ? _mediaUploadState(mediaId) : MediaUploadState();
}

Widget::MediaControlLayout Widget::mediaControlLayout(
		QRect mediaRect) const {
	const auto d = st::ivEditorMediaCornerSize;
	const auto skip = st::ivEditorMediaCornerSkip;
	const auto &r = mediaRect;
	const auto threeDots = QRect(r.left() + skip, r.top() + skip, d, d);
	const auto plus = QRect(r.right() - skip - d + 1, r.top() + skip, d, d);
	const auto layoutSwitch = plus.translated(-(d + skip), 0);
	const auto rs = st::ivEditorMediaUploadRadialSize;
	const auto radial = QRect(
		r.center().x() - rs / 2,
		r.center().y() - rs / 2,
		rs,
		rs);
	return { threeDots, plus, radial, layoutSwitch };
}

void Widget::paintMediaControls(Painter &p, QPoint topLeft) {
	for (const auto &geo : _article->mediaBlockGeometries()) {
		if (geo.visibleMediaRect.isEmpty()) {
			continue;
		}
		const auto path = _state->convertBlockPath(geo.block);
		if (!path) {
			continue;
		}
		const auto block = BlockFromPath(_state->richPage(), *path);
		if (!block) {
			continue;
		}
		const auto paintCircleIcon = [&](QRect circle, const style::icon &icon) {
			const auto target = circle.translated(topLeft);
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st::roundedBg);
			p.drawEllipse(target);
			icon.paintInCenter(p, target);
		};
		if (block->kind == RichPage::BlockKind::GroupedMedia) {
			const auto active = geo.activeItemIndex;
			for (auto i = 0, count = int(geo.itemRects.size())
				; i != count
				; ++i) {
				const auto &itemRect = geo.itemRects[i];
				if (itemRect.isEmpty()) {
					continue;
				}
				const auto itemIndex = (active >= 0) ? active : i;
				const auto layout = mediaControlLayout(itemRect);
				if (!mediaUploadStateForGroupedItem(*path, itemIndex).uploading) {
					paintCircleIcon(
						layout.threeDots,
						st::sendBoxAlbumButtonMediaMore);
				}
			}
			const auto group = mediaControlLayout(geo.visibleMediaRect);
			const auto switchIcon = (block->mediaIntent
					== RichPage::GroupedMediaIntent::Slideshow)
				? &st::ivEditorMediaToCollageIcon
				: &st::ivEditorMediaToSlideshowIcon;
			paintCircleIcon(group.layoutSwitch, *switchIcon);
			paintCircleIcon(group.plus, st::ivEditorMediaAddIcon);
			continue;
		}
		if (!IsSimpleMediaBlockKind(block->kind)) {
			continue;
		}
		const auto layout = mediaControlLayout(geo.visibleMediaRect);
		if (!mediaUploadStateForBlock(*path).uploading) {
			paintCircleIcon(layout.threeDots, st::sendBoxAlbumButtonMediaMore);
			paintCircleIcon(layout.plus, st::ivEditorMediaAddIcon);
		}
	}
}

Widget::PressedMediaControl Widget::mediaControlHitTest(
		QPoint articlePoint) const {
	for (const auto &geo : _article->mediaBlockGeometries()) {
		if (geo.visibleMediaRect.isEmpty()) {
			continue;
		}
		const auto path = _state->convertBlockPath(geo.block);
		if (!path) {
			continue;
		}
		const auto block = BlockFromPath(_state->richPage(), *path);
		if (!block) {
			continue;
		}
		if (block->kind == RichPage::BlockKind::GroupedMedia) {
			const auto active = geo.activeItemIndex;
			for (auto i = 0, count = int(geo.itemRects.size())
				; i != count
				; ++i) {
				const auto &itemRect = geo.itemRects[i];
				if (itemRect.isEmpty()) {
					continue;
				}
				const auto itemIndex = (active >= 0) ? active : i;
				const auto layout = mediaControlLayout(itemRect);
				if (mediaUploadStateForGroupedItem(*path, itemIndex).uploading) {
					if (layout.radial.contains(articlePoint)) {
						return { MediaControl::UploadRadial, *path, itemIndex };
					}
				} else if (layout.threeDots.contains(articlePoint)) {
					return { MediaControl::ThreeDots, *path, itemIndex };
				}
			}
			const auto group = mediaControlLayout(geo.visibleMediaRect);
			if (group.layoutSwitch.contains(articlePoint)) {
				return { MediaControl::LayoutSwitch, *path };
			} else if (group.plus.contains(articlePoint)) {
				return { MediaControl::Plus, *path };
			}
			continue;
		}
		if (!IsSimpleMediaBlockKind(block->kind)) {
			continue;
		}
		const auto layout = mediaControlLayout(geo.visibleMediaRect);
		if (mediaUploadStateForBlock(*path).uploading) {
			if (layout.radial.contains(articlePoint)) {
				return { MediaControl::UploadRadial, *path };
			}
		} else if (layout.threeDots.contains(articlePoint)) {
			return { MediaControl::ThreeDots, *path };
		} else if (layout.plus.contains(articlePoint)) {
			return { MediaControl::Plus, *path };
		} else if ((block->kind == RichPage::BlockKind::Photo)
			&& geo.visibleMediaRect.contains(articlePoint)) {
			return { MediaControl::MediaPixels, *path };
		}
	}
	return {};
}

void Widget::addToCollageFromBlock(const State::BlockPath &path) {
	if (_addMediaAndGroupWithBlock) {
		_addMediaAndGroupWithBlock(
			this,
			path,
			QPointer<QWidget>(_outer.get()));
	}
}

void Widget::toggleGroupedMediaIntent(const State::BlockPath &path) {
	[[maybe_unused]] const auto changed = applyMediaBlockChange([=] {
		const auto current = BlockFromPath(_state->richPage(), path);
		if (!current
			|| current->kind != RichPage::BlockKind::GroupedMedia) {
			return false;
		}
		const auto next = (current->mediaIntent
				== RichPage::GroupedMediaIntent::Slideshow)
			? RichPage::GroupedMediaIntent::Collage
			: RichPage::GroupedMediaIntent::Slideshow;
		return _state->setGroupedMediaIntent(path, next);
	});
}

void Widget::groupBlocksIntoGroup(
		State::BlockPath anchor,
		int insertedCount) {
	if (insertedCount < 1) {
		return;
	}
	const auto block = BlockFromPath(_state->richPage(), anchor);
	if (block && block->kind == RichPage::BlockKind::GroupedMedia) {
		[[maybe_unused]] const auto changed
			= applyGroupedMediaChangePreservingActiveIndex(anchor, [&] {
				return _state->addItemsToGroupedMedia(anchor, insertedCount);
			});
		return;
	}
	auto selection = _state->preparedSelectionForBlock(anchor);
	selection.blocks.till += insertedCount;
	if (!_state->canGroupPhotoVideoBlocks(selection)) {
		return;
	}
	[[maybe_unused]] const auto changed = applyMediaBlockChange([&] {
		return _state->groupPhotoVideoBlocks(
			selection,
			RichPage::GroupedMediaIntent::Collage);
	});
}

void Widget::cancelMediaUploadForBlock(const State::BlockPath &path) {
	const auto block = BlockFromPath(_state->richPage(), path);
	if (!block) {
		return;
	}
	const auto mediaId = MediaIdForBlock(*block);
	if (_cancelMediaUpload) {
		_cancelMediaUpload(this, mediaId);
	}
	auto target = std::optional<int>();
	const auto changed = applyMediaBlockChange([=, &target] {
		const auto current = BlockFromPath(_state->richPage(), path);
		if (!current || !IsSimpleMediaBlockKind(current->kind)) {
			return false;
		}
		target = _state->removeBlock(path, true);
		return true;
	});
	if (!changed) {
		return;
	} else if (target) {
		activateTextOrdinal(*target, 0);
	} else {
		activateInitialNode();
	}
}

void Widget::cancelMediaUploadForGroupedItem(
		const State::BlockPath &path,
		int itemIndex) {
	const auto block = BlockFromPath(_state->richPage(), path);
	if (!block
		|| block->kind != RichPage::BlockKind::GroupedMedia
		|| itemIndex < 0
		|| itemIndex >= int(block->mediaItems.size())) {
		return;
	}
	const auto mediaId = MediaIdForGroupedItem(block->mediaItems[itemIndex]);
	if (_cancelMediaUpload) {
		_cancelMediaUpload(this, mediaId);
	}
	[[maybe_unused]] const auto changed = applyMediaBlockChange([=] {
		const auto current = BlockFromPath(_state->richPage(), path);
		if (!current
			|| current->kind != RichPage::BlockKind::GroupedMedia) {
			return false;
		}
		return _state->removeGroupedItem(path, itemIndex);
	});
}

void Widget::touchEvent(QTouchEvent *e) {
	if (e->type() == QEvent::TouchCancel) {
		_pendingTouchHorizontalScrollPoint = std::nullopt;
		if (_horizontalScrollDrag != HorizontalScrollDrag::Touch) {
			return;
		}
		_horizontalScrollDrag = HorizontalScrollDrag::None;
		if (_article) {
			_article->endHorizontalScroll();
		}
		e->accept();
		return;
	}
	if (!_article || e->touchPoints().isEmpty()) {
		return;
	}
	const auto articlePoint = mapFromGlobal(
		e->touchPoints().cbegin()->screenPos().toPoint()) - articleTopLeft();
	switch (e->type()) {
	case QEvent::TouchBegin: {
		_pendingTouchHorizontalScrollPoint = std::nullopt;
		const auto hit = _article->horizontalScrollHit(articlePoint);
		if (hit.overScrollbar
			&& _article->beginHorizontalScroll(articlePoint, false)) {
			_horizontalScrollDrag = HorizontalScrollDrag::Touch;
			syncInlineFieldGeometry();
			e->accept();
		} else if (hit.overViewport) {
			_pendingTouchHorizontalScrollPoint = articlePoint;
		}
	} break;
	case QEvent::TouchUpdate:
		if (_horizontalScrollDrag == HorizontalScrollDrag::Touch) {
			if (_article->updateHorizontalScroll(articlePoint)) {
				syncInlineFieldGeometry();
			}
			e->accept();
		} else if (_pendingTouchHorizontalScrollPoint) {
			const auto delta = articlePoint - *_pendingTouchHorizontalScrollPoint;
			if (delta.manhattanLength() < QApplication::startDragDistance()) {
				break;
			}
			const auto horizontal = (std::abs(delta.x()) > std::abs(delta.y()));
			if (!horizontal) {
				_pendingTouchHorizontalScrollPoint = std::nullopt;
				break;
			}
			if (_article->beginHorizontalScroll(
					*_pendingTouchHorizontalScrollPoint,
					true)) {
				_horizontalScrollDrag = HorizontalScrollDrag::Touch;
				if (_article->updateHorizontalScroll(articlePoint)) {
					syncInlineFieldGeometry();
				}
				e->accept();
			}
			_pendingTouchHorizontalScrollPoint = std::nullopt;
		}
		break;
	case QEvent::TouchEnd:
		_pendingTouchHorizontalScrollPoint = std::nullopt;
		if (_horizontalScrollDrag == HorizontalScrollDrag::Touch) {
			_horizontalScrollDrag = HorizontalScrollDrag::None;
			_article->endHorizontalScroll();
			e->accept();
		}
		break;
	default:
		break;
	}
}

void Widget::wheelEvent(QWheelEvent *e) {
	if (handleHorizontalScrollWheel(
			e,
			LocalPosition(e) - articleTopLeft())) {
		return;
	}
	e->ignore();
}

bool Widget::redirectKeyToField(QKeyEvent *e) const {
	if (!hasFocus()) {
		return false;
	}
	const auto modifiers = e->modifiers()
		& ~(Qt::KeypadModifier | Qt::GroupSwitchModifier);
	return (modifiers == Qt::NoModifier || modifiers == Qt::ShiftModifier)
		&& (e->key() != Qt::Key_Shift)
		&& RedirectTextToField(e->text());
}

void Widget::inputMethodEvent(QInputMethodEvent *e) {
	if (!_field) {
		Ui::RpWidget::inputMethodEvent(e);
		return;
	}
	const auto cursor = _field->rawTextEdit()->textCursor();
	if (!ImeEventProducesInput(*e, cursor) || !redirectImeToField()) {
		Ui::RpWidget::inputMethodEvent(e);
		return;
	}
	if (!replayImeIntoField(e)) {
		Ui::RpWidget::inputMethodEvent(e);
		return;
	}
	e->accept();
	return;
}

QVariant Widget::inputMethodQuery(Qt::InputMethodQuery query) const {
	if (!_field) {
		return Ui::RpWidget::inputMethodQuery(query);
	}
	return _field->rawTextEdit()->inputMethodQuery(query);
}

bool Widget::redirectImeToField() const {
	return hasFocus()
		&& (hasStructuralSelection() || _field->isHidden());
}

void Widget::mouseMoveEvent(QMouseEvent *e) {
	const auto articlePoint = e->pos() - articleTopLeft();
	if (_horizontalScrollDrag == HorizontalScrollDrag::Mouse) {
		if (_article->updateHorizontalScroll(articlePoint)) {
			syncInlineFieldGeometry();
		}
		e->accept();
		return;
	}
	if (_articleSelectionDrag.active && !(e->buttons() & Qt::LeftButton)) {
		finishArticleSelection();
	}
	if (!_articleSelectionDrag.active) {
		auto cursor = style::cur_default;
		const auto controlHit = _article->editControlHitTest(articlePoint);
		if (controlHit.valid()) {
			cursor = style::cur_pointer;
		} else {
			const auto editHit = _article->editHitTest(articlePoint);
			if (simpleMediaBlockPathFromHit(editHit)
				|| groupedMediaBlockPathFromHit(editHit)
				|| clickHitsStructuralPhotoVideoSelection(editHit)) {
				cursor = style::cur_pointer;
			} else {
				const auto hit = _article->hitTest(
					articlePoint,
					Ui::Text::StateRequest::Flag::LookupSymbol);
				if (hit.valid() && hit.codeHeaderCopy) {
					cursor = style::cur_pointer;
				} else if (hit.valid()
					&& hit.direct
					&& _article->segmentIsText(hit.segmentIndex)) {
					cursor = style::cur_text;
				}
			}
		}
		setCursor(cursor);
		Ui::RpWidget::mouseMoveEvent(e);
		return;
	}
	const auto hit = _article->hitTest(
		articlePoint,
		Ui::Text::StateRequest::Flag::LookupSymbol);
	const auto editHit = _article->editHitTest(articlePoint);
	const auto movedFarEnough = (e->globalPos()
		- _articleSelectionDrag.globalPressPoint).manhattanLength()
		>= QApplication::startDragDistance();
	if (!_articleSelectionDrag.dragStarted) {
		if (!movedFarEnough) {
			_selectScroll.cancel();
			e->accept();
			return;
		}
		_articleSelectionDrag.dragStarted = true;
	}
	updateArticleSelectionDragAtArticlePoint(articlePoint, hit, editHit);
	updateArticleSelectionAutoScroll(e->pos());
	e->accept();
}

void Widget::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		Ui::RpWidget::mousePressEvent(e);
		return;
	}
	if (_articleSelectionDrag.active) {
		finishArticleSelection();
	}
	_trackingPointerPress = true;
	_selectScroll.cancel();
	_pressedControl = {};
	_pressedControlPoint = std::nullopt;
	_pressedMediaControl = {};
	_pressedMediaControlPoint = std::nullopt;
	auto articlePoint = e->pos() - articleTopLeft();
	const auto horizontalScrollHit = _article->horizontalScrollHit(
		articlePoint);
	if (horizontalScrollHit.overScrollbar
		&& _article->beginHorizontalScroll(articlePoint, false)) {
		_horizontalScrollDrag = HorizontalScrollDrag::Mouse;
		syncInlineFieldGeometry();
		e->accept();
		return;
	}
	const auto controlHit = _article->editControlHitTest(articlePoint);
	if (controlHit.valid()) {
		_pressedControl = controlHit;
		_pressedControlPoint = articlePoint;
		e->accept();
		return;
	}
	const auto mediaControl = mediaControlHitTest(articlePoint);
	if (mediaControl.valid()) {
		_pressedMediaControl = mediaControl;
		_pressedMediaControlPoint = articlePoint;
		e->accept();
		return;
	}
	auto hit = _article->hitTest(
		articlePoint,
		Ui::Text::StateRequest::Flag::LookupSymbol);
	const auto editHit = _article->editHitTest(articlePoint);
	const auto startedBelow = (articlePoint.y() >= _articleHeight);
	const auto pressedSelectedText = _article->selectionContains(
		_selection,
		&_selectionEndpoints,
		hit);
	const auto pressedSelectedStructuralOwner = [&] {
		if (_structuralSelection.empty()) {
			return false;
		}
		const auto owner = StructuralOwnerFromHit(editHit);
		if (!owner.valid()) {
			return false;
		}
		switch (_structuralSelection.kind) {
		case PreparedEditSelectionKind::Blocks:
			if (const auto path = BlockPathFromOwner(owner)) {
				return PreparedPathInBlockRange(
					*path,
					_structuralSelection.blocks);
			}
			return false;
		case PreparedEditSelectionKind::ListItems:
			if (const auto listItem = ListItemFromOwner(owner)) {
				return SamePreparedEditBlockPath(
					listItem->block,
					_structuralSelection.listItems.block)
					&& IndexInRange(
						listItem->listItemIndex,
						_structuralSelection.listItems.from,
						_structuralSelection.listItems.till);
			}
			if (const auto path = BlockPathFromOwner(owner)) {
				return PreparedPathInListItemRange(
					*path,
					_structuralSelection.listItems);
			}
			return false;
		case PreparedEditSelectionKind::None:
		case PreparedEditSelectionKind::TableRows:
		case PreparedEditSelectionKind::TableCells:
			return false;
		}
		return false;
	}();
	if ((pressedSelectedText || pressedSelectedStructuralOwner)
		&& startSelectionDragFromExistingState(
			articlePoint,
			e->globalPos(),
			editHit)) {
		e->accept();
		return;
	}
	if (hit.codeHeaderCopy) {
		startArticleSelection(articlePoint, e->globalPos(), hit, editHit);
		e->accept();
		return;
	}
	if (hit.valid() && hit.direct && _article->segmentIsText(hit.segmentIndex)) {
		startArticleSelection(articlePoint, e->globalPos(), hit, editHit);
		e->accept();
		return;
	}
	if (startedBelow) {
		if (editHit.valid()) {
			startArticleSelection(
				articlePoint,
				e->globalPos(),
				hit,
				editHit,
				false,
				true);
		} else {
			clearSelection();
		}
		e->accept();
		return;
	}
	if (editHit.valid()) {
		startArticleSelection(articlePoint, e->globalPos(), hit, editHit);
		e->accept();
		return;
	}
	clearSelection();
	e->accept();
}

void Widget::mouseReleaseEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		Ui::RpWidget::mouseReleaseEvent(e);
		return;
	}
	const auto guard = gsl::finally([&] {
		_trackingPointerPress = false;
	});
	const auto finishDrag = gsl::finally([&] {
		finishArticleSelection();
	});
	const auto articlePoint = e->pos() - articleTopLeft();
	if (_horizontalScrollDrag == HorizontalScrollDrag::Mouse) {
		const auto changed = _article->updateHorizontalScroll(articlePoint);
		_article->endHorizontalScroll();
		_horizontalScrollDrag = HorizontalScrollDrag::None;
		if (changed) {
			syncInlineFieldGeometry();
		}
		e->accept();
		return;
	}
	const auto controlHit = _article->editControlHitTest(articlePoint);
	const auto applyControlToggle = [&](auto &&toggle, auto &&afterRefresh) {
		const auto hadVisibleField = !_field->isHidden();
		auto toggled = false;
		const auto result = recordMutationTransaction([&] {
			const auto committed = commitInlineField();
			if (committed == ApplyResult::Failed) {
				return MutationTransactionResult{
					.committed = committed,
					.failed = true,
				};
			}
			_pendingOrdinal = -1;
			_pendingCursorOffset = 0;
			hideInlineField();
			clearInlineFieldEditSession();
			toggled = toggle();
			if (toggled) {
				refreshPreparedContent();
			} else if (hadVisibleField) {
				refreshAfterInlineFieldCommit(committed);
			}
			clearTextSelection();
			clearStructuralSelection();
			setFocus();
			if (toggled) {
				afterRefresh();
			}
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed) || toggled,
			};
		});
		return !result.failed && toggled;
	};
	if (_pressedControl.valid()) {
		const auto pressedControl = _pressedControl;
		const auto pressedControlPoint = _pressedControlPoint;
		_pressedControl = {};
		_pressedControlPoint = std::nullopt;
		const auto matchedControl = pressedControlPoint
			&& ((articlePoint - *pressedControlPoint).manhattanLength()
				< QApplication::startDragDistance())
			&& (controlHit == pressedControl);
		if (matchedControl) {
			switch (pressedControl.kind) {
			case Markdown::MarkdownArticleEditControlHitKind::TaskMarker:
				if (pressedControl.listItem) {
					applyControlToggle([&] {
						return _state->toggleTaskState(*pressedControl.listItem);
					}, [&] {
						_article->addTaskMarkerRipple(
							*pressedControl.listItem,
							articlePoint);
					});
				}
				break;
			case Markdown::MarkdownArticleEditControlHitKind::DetailsToggle:
				if (pressedControl.block) {
					applyControlToggle([&] {
						return _state->toggleDetailsOpen(*pressedControl.block);
					}, [] {
					});
				}
				break;
			case Markdown::MarkdownArticleEditControlHitKind::None:
				break;
			}
		}
		e->accept();
		return;
	}
	if (_pressedMediaControl.valid()) {
		const auto pressed = _pressedMediaControl;
		const auto pressedPoint = _pressedMediaControlPoint;
		_pressedMediaControl = {};
		_pressedMediaControlPoint = std::nullopt;
		const auto current = mediaControlHitTest(articlePoint);
		const auto matched = pressedPoint
			&& ((articlePoint - *pressedPoint).manhattanLength()
				< QApplication::startDragDistance())
			&& (current.control == pressed.control)
			&& (current.path == pressed.path)
			&& (current.itemIndex == pressed.itemIndex);
		if (matched) {
			switch (pressed.control) {
			case MediaControl::ThreeDots:
				if (pressed.itemIndex >= 0) {
					showGroupedMediaMenu(
						pressed.path,
						pressed.itemIndex,
						e->globalPos());
				} else {
					showSimpleMediaMenu(pressed.path, e->globalPos());
				}
				break;
			case MediaControl::Plus:
				addToCollageFromBlock(pressed.path);
				break;
			case MediaControl::MediaPixels:
				editPhotoBlock(pressed.path);
				break;
			case MediaControl::UploadRadial:
				if (pressed.itemIndex >= 0) {
					cancelMediaUploadForGroupedItem(
						pressed.path,
						pressed.itemIndex);
				} else {
					cancelMediaUploadForBlock(pressed.path);
				}
				break;
			case MediaControl::LayoutSwitch:
				toggleGroupedMediaIntent(pressed.path);
				break;
			case MediaControl::None:
				break;
			}
		}
		e->accept();
		return;
	}
	const auto hit = _article->hitTest(
		articlePoint,
		Ui::Text::StateRequest::Flag::LookupSymbol);
	const auto editHit = _article->editHitTest(articlePoint);
	const auto formulaOrdinalFromEditHit = [&] {
		return editHit.leaf
			&& (editHit.leaf->kind
				== Markdown::PreparedEditLeafKind::MathFormula)
			? _state->textOrdinalForLeaf(*editHit.leaf)
			: -1;
	};
	const auto directEditableHit = [&] {
		return (hit.valid()
			&& hit.direct
			&& _article->segmentIsEditable(hit.segmentIndex))
			|| (formulaOrdinalFromEditHit() >= 0);
	};
	const auto commitVisibleInlineField = [&] {
		if (_field->isHidden()) {
			return false;
		}
		beginArticleRelayoutDeferral();
		const auto relayoutGuard = gsl::finally([&] {
			endArticleRelayoutDeferral();
		});
		const auto source = _state->activePreparedLeafSource();
		const auto committed = recordMutationTransaction([&] {
			const auto committed = commitInlineField();
			if (committed != ApplyResult::Failed) {
				_pendingOrdinal = -1;
				_pendingCursorOffset = 0;
				hideInlineField();
				clearInlineFieldEditSession();
			}
			return committed;
		});
		if (committed == ApplyResult::Failed) {
			return false;
		}
		refreshAfterInlineFieldCommit(committed, source);
		return true;
	};
	const auto focusOrActivateInitial = [&] {
		if (_field->isHidden()) {
			activateInitialNode();
		} else {
			_field->setFocusFast();
		}
	};
	const auto editCodeBlockLanguage = [&] {
		if (!hit.codeHeaderCopy) {
			return false;
		}
		auto languageHit = hit;
		if (!_field->isHidden()) {
			if (!commitVisibleInlineField()) {
				return true;
			}
			languageHit = _article->hitTest(
				articlePoint,
				Ui::Text::StateRequest::Flag::LookupSymbol);
		}
		const auto ordinal = languageHit.codeHeaderCopy
			? editableOrdinalForSegment(languageHit.segmentIndex)
			: -1;
		if (const auto now = _state->codeBlockLanguage(ordinal)) {
			const auto weak = QPointer<Widget>(this);
			DefaultEditLanguageCallback(_show)(
				*now,
				[=](QString language) {
					if (!weak) {
						return;
					}
					weak->recordMutationTransaction([&] {
						const auto changed = weak->_state->setCodeBlockLanguage(
							ordinal,
							language);
						if (changed) {
							weak->refreshPreparedContent();
							weak->update();
						}
						return changed;
					});
				});
		}
		return true;
	};
	if (_articleSelectionDrag.active) {
		const auto fromField = _articleSelectionDrag.fromField;
		const auto pendingCodeHeader = _articleSelectionDrag.codeHeader;
		const auto startedBelow = _articleSelectionDrag.startedBelow;
		const auto operation = _articleSelectionDrag.operation;
		const auto clickLike = !_articleSelectionDrag.dragStarted
			&& ((e->globalPos()
				- _articleSelectionDrag.globalPressPoint).manhattanLength()
				< QApplication::startDragDistance());
		const auto updateOnRelease
			= !clickLike
			&& ((_articleSelectionDrag.mode != DragSelectionMode::None)
				|| (!pendingCodeHeader
					&& (!startedBelow || articlePoint.y() < _articleHeight)));
		if (updateOnRelease) {
			if (operation == ArticleSelectionOperation::DragSelection) {
				updateArticleDropTarget(articlePoint);
			} else {
				updateArticleSelection(articlePoint, hit, editHit);
			}
		}
		if (clickLike) {
			if (activateGroupedMediaLinkFromHit(editHit, hit, e->button())) {
				e->accept();
				return;
			}
			if (showMediaMenuFromHit(
					editHit,
					hit,
					e->globalPos(),
					MediaClickKind::Left)) {
				e->accept();
				return;
			}
			const auto changed = !_selection.empty()
				|| _selectionEndpoints.from.valid()
				|| _selectionEndpoints.to.valid()
				|| hasStructuralSelection();
			_selection = {};
			_selectionEndpoints = {};
			setStructuralSelection({});
			if (changed) {
				update();
			}
		}
		if (!clickLike
			&& (operation == ArticleSelectionOperation::DragSelection)) {
			if (_articleSelectionDrag.dropTarget) {
				if (_articleSelectionDrag.mode == DragSelectionMode::Structural) {
					static_cast<void>(applyStructuralSelectionDrop());
				} else if (_articleSelectionDrag.mode
					== DragSelectionMode::Text) {
					static_cast<void>(applyInlineSelectionDrop());
				}
			}
			clearArticleDropTarget();
			e->accept();
			return;
		}
		if (!clickLike && hasStructuralSelection()) {
			commitVisibleInlineField();
			e->accept();
			return;
		}
		if (_articleSelectionDrag.mode == DragSelectionMode::Text) {
			const auto selection = _selection;
			const auto sameSegmentSelection = !selection.empty()
				&& (selection.from.segment == selection.to.segment)
				&& _article->segmentIsText(selection.from.segment);
			const auto selectionOrdinal = sameSegmentSelection
				? editableOrdinalForSegment(selection.from.segment)
				: -1;
			if (!fromField && selectionOrdinal >= 0) {
				const auto selectionFrom = selection.from.offset;
				const auto selectionTo = selection.to.offset;
				clearTextSelection();
				static_cast<void>(commitAndActivateTextOrdinal(
					selectionOrdinal,
					selectionFrom,
					selectionTo));
				e->accept();
				return;
			} else if (fromField) {
				e->accept();
				return;
			}
		}
		if (pendingCodeHeader
			&& _articleSelectionDrag.mode == DragSelectionMode::None
			&& editCodeBlockLanguage()) {
			e->accept();
			return;
		}
		const auto changed = !_selection.empty()
			|| _selectionEndpoints.from.valid()
			|| _selectionEndpoints.to.valid()
			|| hasStructuralSelection();
		_selection = {};
		_selectionEndpoints = {};
		setStructuralSelection({});
		if (changed) {
			update();
		}
	} else if (hit.codeHeaderCopy && editCodeBlockLanguage()) {
		e->accept();
		return;
	}
	if (directEditableHit()) {
		const auto formulaOrdinal = formulaOrdinalFromEditHit();
		if (formulaOrdinal >= 0) {
			const auto activeDisplayMath = !_field->isHidden()
				&& (_state->activeTextOrdinal() == formulaOrdinal)
				&& (_state->activeFieldMode() == State::FieldMode::Raw);
			if (!activeDisplayMath) {
				if (!_field->isHidden() && !commitVisibleInlineField()) {
					e->accept();
					return;
				}
				activateTextOrdinal(formulaOrdinal, 0);
			}
			editMathFromToolbar();
			e->accept();
			return;
		}
		const auto segmentHit = hit.valid()
			&& hit.direct
			&& _article->segmentIsEditable(hit.segmentIndex);
		const auto targetOrdinal = segmentHit
			? editableOrdinalForSegment(hit.segmentIndex)
			: formulaOrdinal;
		const auto offset = segmentHit
			? _article->selectionOffsetFromHit(
				hit,
				TextSelectType::Letters)
			: 0;
		if (targetOrdinal >= 0
			&& !_field->isHidden()
			&& hit.segmentIndex == _activeSegmentIndex) {
			auto cursor = _field->textCursor();
			cursor.setPosition(std::clamp(
				offset,
				0,
				int(_field->getLastText().size())));
			_field->setTextCursor(cursor);
			_field->setFocusFast();
		} else if (targetOrdinal >= 0) {
			static_cast<void>(commitAndActivateTextOrdinal(
				targetOrdinal,
				offset,
				offset));
		}
	} else if (articlePoint.y() >= _articleHeight) {
		activateTrailingParagraph();
	} else if (activateGroupedMediaLinkFromHit(editHit, hit, e->button())) {
		e->accept();
		return;
	} else if (!showMediaMenuFromHit(
			editHit,
			hit,
			e->globalPos(),
			MediaClickKind::Left)) {
		focusOrActivateInitial();
	}
	e->accept();
}

void Widget::paintEvent(QPaintEvent *e) {
	if (!_article) {
		return;
	}
	auto p = Painter(this);
	p.setTextPalette(st::inTextPalette);
	const auto topLeft = articleTopLeft();
	p.save();
	p.translate(topLeft);
	_article->paint(
		p,
		textPaintContext(e->rect().translated(-topLeft.x(), -topLeft.y())));
	p.restore();
	paintMediaControls(p, topLeft);
	if (!_articleSelectionDrag.indicatorRect.isEmpty()) {
		auto color = st::windowActiveTextFg->c;
		color.setAlphaF(color.alphaF() * 0.7);
		auto rect = _articleSelectionDrag.indicatorRect.translated(topLeft);
		rect.setHeight(std::max(rect.height(), st::lineWidth));
		p.fillRect(rect, color);
	}
	if (!_externalMediaDrag.indicatorRect.isEmpty()) {
		auto color = st::windowActiveTextFg->c;
		color.setAlphaF(color.alphaF() * 0.7);
		auto rect = _externalMediaDrag.indicatorRect.translated(topLeft);
		rect.setHeight(std::max(rect.height(), st::lineWidth));
		p.fillRect(rect, color);
	}
}

void Widget::resizeEvent(QResizeEvent *e) {
	Ui::RpWidget::resizeEvent(e);
	syncInlineFieldGeometry();
}

void Widget::requestRepaint(QRect articleRect) {
	crl::on_main(this, [=] {
		if (!_article) {
			return;
		} else if (articleRect.isEmpty()) {
			update();
		} else {
			update(articleRect.translated(articleTopLeft()));
		}
	});
}

void Widget::requestRelayout(QRect articleRect) {
	crl::on_main(this, [=] {
		if (!_article) {
			return;
		}
		relayoutCurrentContent();
		if (articleRect.isEmpty()) {
			update();
		} else {
			update(articleRect.translated(articleTopLeft()));
		}
	});
}

void Widget::setDocument(const Markdown::MarkdownArticleContent &prepared) {
	_article->setContent(prepared);
}

Markdown::MarkdownArticleTextLeafStyle Widget::inlineFieldStyleForSegment(
		int segmentIndex) const {
	return _article
		? _article->editableStyleForSegment(segmentIndex)
		: Markdown::MarkdownArticleTextLeafStyle();
}

const Widget::CachedInlineFieldStyle &Widget::inlineFieldStyleFor(
		const Markdown::MarkdownArticleTextLeafStyle &leafStyle) {
	return inlineFieldStyleFor(normalizedInlineFieldStyle(leafStyle));
}

const Widget::CachedInlineFieldStyle &Widget::inlineFieldStyleFor(
		const InlineFieldStyleData &data) {
	auto key = inlineFieldStyleKey(data);
	auto textFg = data.textFg;
	auto ownedTextFg = std::shared_ptr<style::owned_color>();
	auto ownedTextMarkBg = std::make_shared<style::owned_color>(
		data.textMarkBg);
	auto textMarkBg = ownedTextMarkBg->color();
	if (_inlineFieldTextColorOverride
		&& data.textFg.get() == _inlineFieldTextColorOverride->color().get()) {
		ownedTextFg = std::make_shared<style::owned_color>(data.textFg->c);
		textFg = ownedTextFg->color();
		key.textFg = textFg;
	}
	for (const auto &cached : _fieldStyles) {
		if (cached.key == key) {
			cached.ownedTextMarkBg->update(data.textMarkBg);
			return cached;
		}
	}
	auto fieldStyle = std::make_shared<style::InputField>(
		st::ivEditorInputField);
	fieldStyle->style = *data.textStyle;
	fieldStyle->style.font = data.italic
		? data.textStyle->font->italic()
		: data.textStyle->font;
	fieldStyle->style.lineHeight = data.lineHeight;
	fieldStyle->textFg = textFg;
	fieldStyle->textMarkBg = textMarkBg;
	fieldStyle->textAlign = data.align;
	fieldStyle->placeholderFont = data.quoteCaptionPlaceholder
		? fieldStyle->style.font->bold(false)
		: fieldStyle->style.font;
	fieldStyle->placeholderAlign = data.align;
	_fieldStyles.push_back({
		.key = key,
		.style = std::move(fieldStyle),
		.ownedTextFg = std::move(ownedTextFg),
		.ownedTextMarkBg = std::move(ownedTextMarkBg),
	});
	return _fieldStyles.back();
}

std::optional<QColor> Widget::activeQuoteCaptionColor() {
	if (!_state->activeLeafUsesQuoteCaptionColor()) {
		return std::nullopt;
	}
	return Markdown::NonPullquoteQuoteCaptionColor(
		textPaintContext(QRect()),
		*_articleStyle);
}

std::optional<QColor> Widget::activeQuotePlaceholderColor() {
	if (!_state->activeLeafUsesQuotePlaceholderColor()) {
		return std::nullopt;
	}
	return Markdown::NonPullquoteQuoteCaptionColor(
		textPaintContext(QRect()),
		*_articleStyle);
}

void Widget::refreshInlineFieldTextColorOverride() {
	const auto color = activeQuoteCaptionColor();
	if (!color) {
		if (_inlineFieldTextColorOverride) {
			_activeFieldStyleKey = std::nullopt;
			_inlineFieldTextColorOverride.reset();
		}
		return;
	}
	if (_inlineFieldTextColorOverride) {
		_inlineFieldTextColorOverride->update(*color);
	} else {
		_inlineFieldTextColorOverride.emplace(*color);
	}
}

Widget::InlineFieldStyleData Widget::normalizedInlineFieldStyle(
		const Markdown::MarkdownArticleTextLeafStyle &leafStyle) const {
	const auto valid = leafStyle.valid();
	const auto textStyle = valid
		? leafStyle.textStyle
		: &_articleStyle->body;
	const auto lineHeight = (valid && leafStyle.lineHeight > 0)
		? leafStyle.lineHeight
		: std::max(textStyle->lineHeight, textStyle->font->height);
	return {
		.textStyle = textStyle,
		.lineHeight = lineHeight,
		.textFg = _inlineFieldTextColorOverride
			? _inlineFieldTextColorOverride->color()
			: (valid ? leafStyle.textColor : _articleStyle->textColor),
		.textMarkBg = valid
			? leafStyle.markBg
			: _articleStyle->textPalette.markBg->c,
		.align = valid ? leafStyle.align : style::al_left,
		.italic = valid ? leafStyle.italic : false,
		.quoteCaptionPlaceholder = _state->activeLeafUsesQuoteCaptionColor(),
	};
}

Widget::InlineFieldStyleKey Widget::inlineFieldStyleKey(
		const InlineFieldStyleData &data) const {
	const auto textStyle = data.textStyle
		? data.textStyle
		: &_articleStyle->body;
	return {
		.font = data.italic
			? textStyle->font->italic()
			: textStyle->font,
		.lineHeight = data.lineHeight,
		.textFg = data.textFg,
		.textMarkBg = data.textMarkBg,
		.align = data.align,
		.quoteCaptionPlaceholder = data.quoteCaptionPlaceholder,
	};
}

void Widget::ensureInlineFieldForSegment(int segmentIndex) {
	_revivedRetainedField = false;
	refreshInlineFieldTextColorOverride();
	const auto data = normalizedInlineFieldStyle(
		inlineFieldStyleForSegment(segmentIndex));
	const auto key = inlineFieldStyleKey(data);
	const auto mode = _state->activeFieldMode();
	const auto leaf = _state->activeLeafPath();
	const auto fieldLeafMismatch = leaf
		&& _fieldLeaf
		&& (*_fieldLeaf != *leaf);
	if (_activeFieldStyleKey
		&& leaf
		&& _fieldLeaf
		&& (*_fieldLeaf == *leaf)
		&& *_activeFieldStyleKey == key
		&& _fieldMode == mode) {
		return;
	}
	if (leaf) {
		if (auto revived = reviveRetainedLeafField(
				_historyIndex,
				*leaf,
				mode,
				key)) {
			_field = std::move(revived);
			_activeFieldStyleKey = key;
			_fieldMode = mode;
			_fieldLeaf = *leaf;
			refreshInlineFieldPlaceholderColor();
			_fieldUndoAvailable = _field->isUndoAvailable();
			_fieldRedoAvailable = _field->isRedoAvailable();
			_revivedRetainedField = true;
			clearFieldUndoRedoNoopState();
			return;
		}
	}
	const auto needsRecreate = !_activeFieldStyleKey
		|| (*_activeFieldStyleKey != key)
		|| (_fieldMode != mode)
		|| fieldLeafMismatch;
	if (!needsRecreate) {
		_activeFieldStyleKey = key;
		_fieldMode = mode;
		return;
	}
	const auto &cached = inlineFieldStyleFor(data);
	_activeFieldStyleKey = cached.key;
	_fieldMode = mode;
	recreateInlineField(*cached.style);
}

void Widget::setupInlineField() {
	if (_fieldMode == State::FieldMode::Rich) {
		const auto allowPremiumEmoji = [peer = _peer](
				not_null<DocumentData*> emoji) {
			return Data::AllowEmojiWithoutPremium(peer, emoji);
		};
		_field->setInstantViewEditorTagsEnabled(true);
		InitMessageFieldHandlers({
			.session = _session,
			.show = _show,
			.field = _field.get(),
			.customEmojiPaused = _customEmojiPaused,
			.allowPremiumEmoji = allowPremiumEmoji,
			.fieldStyle = &_field->st(),
			.linkValidator = ValidateInstantViewEditorLink,
			.allowMarkdownTags = {
				Ui::InputField::kTagBold,
				Ui::InputField::kTagItalic,
				Ui::InputField::kTagUnderline,
				Ui::InputField::kTagStrikeOut,
				Ui::InputField::kTagCode,
				Ui::InputField::kTagPre,
				Ui::InputField::kTagSpoiler,
				Ui::InputField::kTagIvMarked,
				Ui::InputField::kTagIvSubscript,
				Ui::InputField::kTagIvSuperscript,
				Ui::InputField::kTagIvMath,
			},
		});
		if (_show) {
			const auto weak = QPointer<Widget>(this);
			_field->setEditLinkCallback(DefaultEditLinkCallback(
				_show,
				_field.get(),
				nullptr,
				ValidateInstantViewEditorLink,
				[=](bool active) {
					if (weak) {
						weak->setInlineFieldExternalInteractionActive(active);
						weak->notifyToolbarStateChanged();
					}
				},
				[=] {
					if (weak && !weak->_field->isHidden()) {
						weak->_field->setFocusFast();
						weak->notifyToolbarStateChanged();
					}
				}));
		}
		_fieldSuggestions = Ui::Emoji::SuggestionsController::Init(
			_outer,
			_field.get(),
			_session,
			{
				.suggestCustomEmoji = true,
				.allowCustomWithoutPremium = allowPremiumEmoji,
			});
		auto messageFieldMimeHook = WrappedMessageFieldMimeHook(
			Ui::InputField::MimeDataHook(),
			_field.get());
		_field->setMimeDataHook([=,
				messageFieldMimeHook = std::move(messageFieldMimeHook)](
				not_null<const QMimeData*> data,
				Ui::InputField::MimeAction action) {
			return handleIvClipboardMime(data, action)
				|| (messageFieldMimeHook
					? messageFieldMimeHook(data, action)
					: false);
		});
	} else {
		_fieldSuggestions = nullptr;
		_field->setInstantViewEditorTagsEnabled(false);
		_field->setInstantReplacesEnabled(
			rpl::single(false),
			rpl::single(false));
		_field->setMarkdownReplacesEnabled(
			rpl::single(Ui::MarkdownEnabledState{
				Ui::MarkdownDisabled()
			}));
	}
	_field->setDocumentMargin(0.);
	_field->setAdditionalMargins({});
	_field->setSubmitSettings(Ui::InputField::SubmitSettings::None);
	_field->setMaxHeight(std::numeric_limits<int>::max());
	refreshInlineFieldPlaceholderColor();
	const auto raw = _field->rawTextEdit();
	const auto disableFieldShortcut = [&](const QKeySequence &sequence) {
		for (const auto shortcut : raw->findChildren<QShortcut*>()) {
			if (shortcut->key().matches(sequence)
				== QKeySequence::ExactMatch) {
				shortcut->setEnabled(false);
			}
		}
	};
	disableFieldShortcut(Ui::kBlockquoteSequence);
	disableFieldShortcut(Ui::kMonospaceSequence);
	_field->customUpDown(true);
	_field->installEventFilter(this);
	raw->installEventFilter(this);
	raw->viewport()->installEventFilter(this);
	_field->addContextMenuHook([this](
			Ui::InputField::ContextMenuRequest request) {
		handleFieldContextMenuRequest(std::move(request));
	});

	const auto field = QPointer<Ui::InputField>(_field.get());
	const auto revealActiveField = [=] {
		if (!field || (_field.get() != field.data())) {
			return;
		}
		revealActiveInlineField();
	};
	_field->heightChanges(
	) | rpl::on_next([=] {
		updateInlineFieldHeightOverride();
		revealActiveField();
	}, _field->lifetime());
	_field->focusedChanges(
	) | rpl::on_next([=](bool focused) {
		if (!focused
			&& !_settingField
			&& !_trackingPointerPress
			&& !_inlineFieldExternalInteractionActive) {
			const auto committed = recordMutationTransaction([=] {
				return commitInlineField();
			});
			if (committed == ApplyResult::Changed) {
				refreshAfterInlineFieldCommit(committed);
			}
		}
	}, _field->lifetime());
	QObject::connect(
		raw->document(),
		&QTextDocument::contentsChange,
		_field.get(),
		[this, field](int, int, int) {
			if (!field || (_field.get() != field.data())) {
				return;
			}
			const auto hadRedo = _fieldRedoAvailable;
			const auto hadHistoryRedo
				= (_historyIndex + 1 < int(_history.size()));
			if (!_restoringHistory
				&& !_performingUndoRedo
				&& !_settingField
				&& !_suppressHistoryRedoInvalidation
				&& (hadRedo || hadHistoryRedo)) {
				truncateHistoryRedo();
			}
			if (!_restoringHistory && !_performingUndoRedo && !_settingField) {
				refreshInlineFieldTextEmptyOverride();
				clearFieldUndoRedoNoopState();
				_autosaveEvents.fire({
					.type = AutosaveEventType::TextIdle,
				});
			}
			crl::on_main(this, [=] {
				if (!field || (_field.get() != field.data())) {
					return;
				}
				refreshInlineFieldMaxLineWidthOverride();
				_fieldUndoAvailable = field->isUndoAvailable();
				_fieldRedoAvailable = field->isRedoAvailable();
				notifyToolbarStateChanged();
			});
		});
	QObject::connect(
		raw,
		&QTextEdit::cursorPositionChanged,
		_field.get(),
		[this, revealActiveField] {
			revealActiveField();
			notifyToolbarStateChanged();
		});
	_fieldUndoAvailable = _field->isUndoAvailable();
	_fieldRedoAvailable = _field->isRedoAvailable();

	hideInlineField();
}

void Widget::recreateInlineField(const style::InputField &st) {
	const auto text = _field->getTextWithTags();
	const auto cursor = _field->textCursor();
	const auto position = cursor.position();
	const auto anchor = cursor.anchor();
	const auto wasHidden = _field->isHidden();
	const auto hadFocus = _field->hasFocus();

	_settingField = true;
	_field = base::make_unique_q<Ui::InputField>(
		this,
		st,
		Ui::InputField::Mode::MultiLine,
		rpl::single(QString()));
	setupInlineField();
	refreshInlineFieldPlaceholder();
	const auto wasSuppressingHistoryRedoInvalidation
		= _suppressHistoryRedoInvalidation;
	_suppressHistoryRedoInvalidation = true;
	const auto suppressRedoInvalidation = gsl::finally([&] {
		_suppressHistoryRedoInvalidation
			= wasSuppressingHistoryRedoInvalidation;
	});
	_field->setTextWithTags(text, Ui::InputField::HistoryAction::Clear);
	auto restored = _field->textCursor();
	const auto size = int(_field->getLastText().size());
	const auto restoredAnchor = std::clamp(anchor, 0, size);
	const auto restoredPosition = std::clamp(position, 0, size);
	restored.setPosition(restoredAnchor);
	if (restoredPosition != restoredAnchor) {
		restored.setPosition(restoredPosition, QTextCursor::KeepAnchor);
	}
	_field->setTextCursor(restored);
	if (!wasHidden) {
		_field->show();
		_field->raise();
		if (hadFocus) {
			_field->setFocusFast();
		}
	}
	_fieldLeaf = std::nullopt;
	_settingField = false;
	_fieldUndoAvailable = _field->isUndoAvailable();
	_fieldRedoAvailable = _field->isRedoAvailable();
	clearFieldUndoRedoNoopState();
}

void Widget::refreshPalette() {
	_theme = CreateStandaloneChatTheme();
	_style->apply(_theme.get());
	_highlightColors = HighlightColors(_style.get());
	*_articleStyle = CreateEditorMarkdownStyle();
	if (_article) {
		_article->invalidatePaletteCache();
	}
	_fieldStyles.clear();
	_retainedLeafFields.clear();
	_activeFieldStyleKey = std::nullopt;
	_inlineFieldTextColorOverride.reset();
	_inlineFieldPlaceholderColorOverride.reset();
	if (_field && !_field->isHidden()) {
		refreshInlineFieldTextColorOverride();
		const auto &cached = inlineFieldStyleFor(
			inlineFieldStyleForSegment(_activeSegmentIndex));
		_activeFieldStyleKey = cached.key;
		recreateInlineField(*cached.style);
	}
	relayoutCurrentContent();
	update();
}

void Widget::ensureInlineFieldCreated() {
	if (_field) {
		return;
	}
	const auto &fieldStyle = inlineFieldStyleFor(
		Markdown::MarkdownArticleTextLeafStyle());
	_activeFieldStyleKey = fieldStyle.key;
	_fieldMode = State::FieldMode::Rich;
	_field = base::make_unique_q<Ui::InputField>(
		this,
		*fieldStyle.style,
		Ui::InputField::Mode::MultiLine,
		rpl::single(QString()));
	setupInlineField();
	clearFieldUndoRedoNoopState();
}

void Widget::refreshInlineFieldPlaceholder() {
	_field->setPlaceholder(rpl::single(_state->activePlaceholderText()));
	refreshInlineFieldPlaceholderColor();
}

void Widget::refreshInlineFieldPlaceholderColor() {
	auto color = activeQuotePlaceholderColor().value_or(
		_articleStyle->supplementaryTextColor->c);
	color.setAlphaF(color.alphaF() * 0.5);
	if (_inlineFieldPlaceholderColorOverride) {
		_inlineFieldPlaceholderColorOverride->update(color);
	} else {
		_inlineFieldPlaceholderColorOverride.emplace(color);
	}
	_field->setPlaceholderColorOverride(
		_inlineFieldPlaceholderColorOverride->color());
}

int Widget::inlineFieldMaxVisualLineWidth() const {
	if (_field->isHidden()) {
		return 0;
	}
	return MaxVisualLineWidth(_field->rawTextEdit()->document());
}

void Widget::refreshInlineFieldTextEmptyOverride() {
	if (!_article) {
		return;
	}
	if (!_field || _field->isHidden() || _settingField) {
		_article->clearEditableTextEmptyOverride();
		return;
	}
	const auto source = _state->activePreparedLeafSource();
	if (source) {
		_article->setEditableTextEmptyOverride(
			*source,
			_field->getLastText().isEmpty());
	} else {
		_article->clearEditableTextEmptyOverride();
	}
}

void Widget::refreshInlineFieldMaxLineWidthOverride() {
	if (!_article || _refreshingInlineFieldMaxLineWidthOverride) {
		return;
	}
	_refreshingInlineFieldMaxLineWidthOverride = true;
	const auto guard = gsl::finally([&] {
		_refreshingInlineFieldMaxLineWidthOverride = false;
	});
	for (auto pass = 0; pass != 2; ++pass) {
		refreshInlineFieldTextEmptyOverride();
		const auto livePullquoteWidthRelevant = !_field->isHidden()
			&& (_activeSegmentIndex >= 0)
			&& !_settingField
			&& (inlineFieldStyleForSegment(_activeSegmentIndex).italic
				|| _state->activeLeafUsesQuoteCaptionColor());
		auto source = livePullquoteWidthRelevant
			? _state->activePreparedLeafSource()
			: std::optional<Markdown::PreparedEditLeafSource>();
		if (source) {
			const auto maxWidth = _article
				->pullquoteAvailableTextWidthForEditableLeaf(*source);
			const auto width = (maxWidth > 0)
				? MaxVisualLineWidthForWidth(
					_field->rawTextEdit()->document(),
					maxWidth)
				: inlineFieldMaxVisualLineWidth();
			if (width > 0) {
				_article->setEditableMaxLineWidthOverride(*source, width);
			} else {
				_article->clearEditableMaxLineWidthOverride();
			}
		} else {
			_article->clearEditableMaxLineWidthOverride();
		}
		relayoutCurrentContent();
		if (_field->isHidden()) {
			break;
		}
		syncInlineFieldGeometry();
	}
	if (!_field->isHidden()) {
		updateInlineFieldHeightOverride();
	}
}

void Widget::setInlineFieldFromActiveState(int selectionFrom, int selectionTo) {
	ensureInlineFieldForSegment(_activeSegmentIndex);
	const auto revivedRetainedField = _revivedRetainedField;
	_revivedRetainedField = false;
	refreshInlineFieldPlaceholder();
	_settingField = true;
	const auto activeLeaf = _state->activeLeafPath();
	const auto preserveRetainedFieldSession = _restoringHistory
		&& (PreservingExternalFieldRestore == this)
		&& activeLeaf
		&& _fieldLeaf
		&& (*_fieldLeaf == *activeLeaf);
	auto cursorSelectionFrom = selectionFrom;
	auto cursorSelectionTo = selectionTo;
	auto trimmedLeft = 0;
	const auto trimLeft = !_state->codeBlockLanguage(
		_state->activeTextOrdinal()).has_value();
	const auto wasSuppressingHistoryRedoInvalidation
		= _suppressHistoryRedoInvalidation;
	_suppressHistoryRedoInvalidation = true;
	const auto suppressRedoInvalidation = gsl::finally([&] {
		_suppressHistoryRedoInvalidation
			= wasSuppressingHistoryRedoInvalidation;
	});
	if (preserveRetainedFieldSession) {
		_fieldLeaf = activeLeaf;
		_settingField = false;
		_fieldUndoAvailable = _field->isUndoAvailable();
		_fieldRedoAvailable = _field->isRedoAvailable();
		notifyToolbarStateChanged();
		return;
	}
	const auto preserveRestoredRetainedField = [&](const TextWithTags &text) {
		const auto document = _field->rawTextEdit()->document();
		const auto matchingHistoryDirection = _restoringHistoryRedo
			&& (*_restoringHistoryRedo
				? (document->availableRedoSteps() > 0
					|| _field->isRedoAvailable())
				: (document->availableUndoSteps() > 0
					|| _field->isUndoAvailable()));
		return revivedRetainedField
			&& _restoringHistory
			&& activeLeaf
			&& _fieldLeaf
			&& (*_fieldLeaf == *activeLeaf)
			&& (matchingHistoryDirection || (_field->getTextWithTags() == text));
	};
	const auto finishWithRetainedField = [&] {
		_fieldLeaf = activeLeaf;
		_settingField = false;
		_fieldUndoAvailable = _field->isUndoAvailable();
		_fieldRedoAvailable = _field->isRedoAvailable();
	};
	const auto resetFieldHistory = !activeLeaf
		|| !_fieldLeaf
		|| (*_fieldLeaf != *activeLeaf);
	if (_state->activeFieldMode() == State::FieldMode::Raw) {
		const auto trimmed = TrimInlineFieldText(
			{ _state->activeRawText(), {} },
			trimLeft);
		if (preserveRestoredRetainedField(trimmed.text)) {
			clearArticleEditableHeightOverride();
			finishWithRetainedField();
			notifyToolbarStateChanged();
			return;
		}
		if (resetFieldHistory || (_field->getTextWithTags() != trimmed.text)) {
			_field->setTextWithTags(
				trimmed.text,
				Ui::InputField::HistoryAction::Clear);
		}
		trimmedLeft = trimmed.left;
		clearArticleEditableHeightOverride();
	} else {
		const auto activeText = ConvertRichTextToEditorTags(
			_state->activeText());
		const auto trimmed = TrimInlineFieldText(activeText.text, trimLeft);
		if (preserveRestoredRetainedField(trimmed.text)) {
			finishWithRetainedField();
			notifyToolbarStateChanged();
			return;
		}
		if (resetFieldHistory || (_field->getTextWithTags() != trimmed.text)) {
			_field->setTextWithTags(
				trimmed.text,
				Ui::InputField::HistoryAction::Clear);
		}
		cursorSelectionFrom = MapRichTextOffsetToEditorOffset(
			activeText.replacements,
			selectionFrom);
		cursorSelectionTo = MapRichTextOffsetToEditorOffset(
			activeText.replacements,
			selectionTo);
		trimmedLeft = trimmed.left;
	}
	cursorSelectionFrom -= trimmedLeft;
	cursorSelectionTo -= trimmedLeft;
	auto cursor = _field->textCursor();
	const auto size = int(_field->getLastText().size());
	const auto from = std::clamp(cursorSelectionFrom, 0, size);
	const auto to = std::clamp(cursorSelectionTo, 0, size);
	cursor.setPosition(from);
	if (to != from) {
		cursor.setPosition(to, QTextCursor::KeepAnchor);
	}
	_field->setTextCursor(cursor);
	_fieldLeaf = _state->activeLeafPath();
	_settingField = false;
	_fieldUndoAvailable = _field->isUndoAvailable();
	_fieldRedoAvailable = _field->isRedoAvailable();
	clearFieldUndoRedoNoopState();
	notifyToolbarStateChanged();
}

void Widget::activateTextOrdinal(
		int ordinal,
		int cursorOffset,
		ActivateReveal reveal) {
	activateTextOrdinal(ordinal, cursorOffset, cursorOffset, reveal);
}

void Widget::activateTextOrdinal(
		int ordinal,
		int selectionFrom,
		int selectionTo,
		ActivateReveal reveal) {
	const auto targetLeaf = [&]() -> std::optional<State::LeafPath> {
		const auto &nodes = _state->textNodes();
		return (ordinal >= 0 && ordinal < int(nodes.size()))
			? std::make_optional(nodes[ordinal].leaf)
			: std::nullopt;
	}();
	if (targetLeaf
		&& _fieldLeaf
		&& (*_fieldLeaf != *targetLeaf)) {
		retainActiveLeafField();
	}
	if (!_state->setActiveTextByOrdinal(ordinal)) {
		return;
	}
	_boundarySelectionOrigin = std::nullopt;
	_activeOrdinal = ordinal;
	_pendingOrdinal = -1;
	_pendingCursorOffset = 0;

	const auto previousSegmentIndex = _activeSegmentIndex;
	const auto segmentIndex = segmentIndexForEditableOrdinal(ordinal);
	if (segmentIndex < 0) {
		_activeSegmentIndex = -1;
		_pendingOrdinal = ordinal;
		_pendingCursorOffset = selectionTo;
		hideInlineField();
		notifyToolbarStateChanged();
		return;
	}

	if (_article && previousSegmentIndex != segmentIndex) {
		clearArticleEditableHeightOverride();
	}
	if (previousSegmentIndex != segmentIndex) {
		clearDisplayMathEditSession();
	}
	_activeSegmentIndex = segmentIndex;
	if (_article->segmentIsDisplayMath(_activeSegmentIndex)) {
		clearDisplayMathEditSession();
		clearArticleEditableHeightOverride();
		hideInlineField();
		_selection = {};
		_selectionEndpoints = {};
		setStructuralSelection({});
		update();
		notifyToolbarStateChanged();
		return;
	} else {
		clearDisplayMathEditSession();
	}
	const auto hadArticleSelection = !_selection.empty()
		|| _selectionEndpoints.from.valid()
		|| _selectionEndpoints.to.valid()
		|| hasStructuralSelection();
	_selection = {};
	_selectionEndpoints = {};
	setStructuralSelection({});
	if (hadArticleSelection) {
		update();
	}
	setInlineFieldFromActiveState(selectionFrom, selectionTo);
	_field->show();
	syncInlineFieldGeometry();
	updateInlineFieldHeightOverride();
	syncArticleVisibleTopBottom();
	if (reveal == ActivateReveal::Reveal) {
		revealActiveInlineField();
	}
	_field->raise();
	_field->setFocusFast();
	notifyToolbarStateChanged();
}

QRect Widget::activeInlineFieldRevealRect() const {
	const auto raw = _field->rawTextEdit();
	const auto cursor = _field->textCursor();
	auto positionCursor = cursor;
	positionCursor.setPosition(cursor.position());
	auto revealRect = raw->cursorRect(positionCursor);
	if (cursor.hasSelection()) {
		auto anchorCursor = cursor;
		anchorCursor.setPosition(cursor.anchor());
		revealRect = revealRect.united(raw->cursorRect(anchorCursor));
	}
	if (!revealRect.isValid() || revealRect.isEmpty()) {
		return _field->rect();
	}
	revealRect.moveTopLeft(
		raw->viewport()->mapTo(_field, revealRect.topLeft()));
	return revealRect;
}

QRect Widget::mapFieldLocalRectToScrollContent(
		QWidget *inner,
		QRect rect) const {
	rect.moveTopLeft(_field->mapTo(inner, rect.topLeft()));
	return rect;
}

void Widget::revealActiveInlineField() {
	if (inlineFieldRevealSuppressed()
		|| _field->isHidden()
		|| _activeSegmentIndex < 0) {
		return;
	}
	if (_article->revealSegment(_activeSegmentIndex)) {
		syncInlineFieldGeometry();
		if (_field->isHidden()) {
			return;
		}
	}
	const auto scrollIn = [&](auto &&scroll) {
		if (const auto inner = scroll->widget()) {
			const auto localRect = mapFieldLocalRectToScrollContent(
				inner,
				activeInlineFieldRevealRect());
			scrollRangeToMakeVisible(
				scroll,
				localRect.y(),
				localRect.y() + localRect.height());
		}
	};
	for (auto parent = parentWidget(); parent; parent = parent->parentWidget()) {
		if (const auto scroll = dynamic_cast<Ui::ScrollArea*>(parent)) {
			scrollIn(scroll);
			return;
		}
		if (const auto scroll = dynamic_cast<Ui::ElasticScroll*>(parent)) {
			scrollIn(scroll);
			return;
		}
	}
}

void Widget::activateTrailingParagraph() {
	recordMutationTransaction([&] {
		const auto committed = commitInlineField();
		if (committed == ApplyResult::Failed) {
			return MutationTransactionResult{
				.committed = committed,
				.failed = true,
			};
		}
		const auto ordinal = _state->ensureTrailingParagraphActive();
		if (!ordinal) {
			showLastLimitToast();
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		refreshPreparedContent();
		activateTextOrdinal(*ordinal, _state->activeText().text.size());
		return MutationTransactionResult{
			.committed = committed,
			.changed = true,
		};
	});
}

void Widget::revertInlineFieldToState() {
	if (_field->isHidden() || _activeSegmentIndex < 0) {
		return;
	}
	const auto cursor = _field->textCursor();
	setInlineFieldFromActiveState(cursor.anchor(), cursor.position());
	syncInlineFieldGeometry();
	updateInlineFieldHeightOverride();
}

std::optional<State::ActiveTextInsertContext>
Widget::activeTextInsertContext() const {
	if (_settingField
		|| _field->isHidden()
		|| (_activeSegmentIndex < 0)
		|| (_state->activeFieldMode() == State::FieldMode::Raw)) {
		return std::nullopt;
	}
	auto full = ConvertEditorTagsToRichText(_field->getTextWithAppliedMarkdown());
	const auto cursor = _field->textCursor();
	auto from = richOffsetForFieldOffset(full, cursor.selectionStart());
	auto till = richOffsetForFieldOffset(full, cursor.selectionEnd());
	const auto textSize = int(full.text.size());
	from = std::clamp(from, 0, textSize);
	till = std::clamp(till, from, textSize);
	auto before = (from > 0)
		? Ui::Text::Mid(full, 0, from)
		: TextWithEntities();
	auto selected = (till > from)
		? Ui::Text::Mid(full, from, till - from)
		: TextWithEntities();
	auto after = (till < textSize)
		? Ui::Text::Mid(full, till)
		: TextWithEntities();
	return State::ActiveTextInsertContext{
		.before = std::move(before),
		.selected = std::move(selected),
		.after = std::move(after),
	};
}

PreparedMediaPasteTarget Widget::preparedMediaPasteTarget() const {
	auto result = PreparedMediaPasteTarget();
	const auto leaf = _state->activeLeafPath();
	if (!leaf) {
		return result;
	}
	result.leaf = leaf;
	const auto ordinal = _state->textOrdinalForLeafPath(*leaf);
	if (ordinal >= 0) {
		result.anchor = _state->textNodes()[ordinal].insertionAnchor;
	}
	switch (leaf->kind) {
	case StateLeafKind::BlockText:
	case StateLeafKind::ListItemText:
		result.context = ClipboardPasteInsertContext(
			activeTextInsertContext());
		break;
	case StateLeafKind::BlockCaption:
	case StateLeafKind::TableCellText:
	case StateLeafKind::MathFormula:
		break;
	}
	return result;
}

Widget::PreparedMediaPasteActivation
Widget::activatePreparedMediaPasteTarget(PreparedMediaPasteTarget target) {
	if (!target.leaf) {
		return {};
	}
	const auto ordinal = _state->textOrdinalForLeafPath(*target.leaf);
	if (ordinal < 0) {
		return {};
	}
	const auto &nodes = _state->textNodes();
	if (ordinal >= int(nodes.size())) {
		return {};
	}
	if (target.anchor
		&& ((nodes[ordinal].insertionAnchor.blockIndex
				!= target.anchor->blockIndex)
			|| !(nodes[ordinal].insertionAnchor.container
				== target.anchor->container))) {
		return {};
	}
	activateTextOrdinal(ordinal, 0);
	return {
		.resolved = true,
		.context = std::move(target.context),
	};
}

std::optional<State::TextNodeSpan>
Widget::visibleFullHeadingFieldTextSpan() const {
	if (_settingField
		|| _field->isHidden()
		|| (_activeSegmentIndex < 0)
		|| (_state->activeFieldMode() == State::FieldMode::Raw)) {
		return std::nullopt;
	}
	const auto leaf = _state->activeLeafPath();
	if (!leaf || (leaf->kind != StateLeafKind::BlockText)) {
		return std::nullopt;
	}
	const auto owner = BlockFromPath(_state->richPage(), leaf->block);
	if (!owner || (owner->kind != RichPage::BlockKind::Heading)) {
		return std::nullopt;
	}
	const auto full = ConvertEditorTagsToRichText(
		_field->getTextWithAppliedMarkdown());
	if (full.text.isEmpty()) {
		return TextNodeSpan{
			.leaf = *leaf,
			.from = 0,
			.till = 0,
		};
	}
	const auto cursor = _field->textCursor();
	if (!cursor.hasSelection()) {
		return std::nullopt;
	}
	const auto length = int(full.text.size());
	auto from = richOffsetForFieldOffset(full, cursor.selectionStart());
	auto till = richOffsetForFieldOffset(full, cursor.selectionEnd());
	from = std::clamp(from, 0, length);
	till = std::clamp(till, from, length);
	return (from == 0) && (till == length)
		? std::make_optional(TextNodeSpan{
			.leaf = *leaf,
			.from = 0,
			.till = length,
		})
		: std::nullopt;
}

std::optional<Widget::MathEditRequest> Widget::activeMathEditRequest() const {
	if (_settingField
		|| (_activeSegmentIndex < 0)) {
		return std::nullopt;
	}
	if (_state->activeFieldMode() == State::FieldMode::Raw) {
		const auto leaf = _state->activeLeafPath();
		if (!leaf || leaf->kind != StateLeafKind::MathFormula) {
			return std::nullopt;
		}
		return MathEditRequest{
			.source = _state->activeRawText(),
			.displayMathOrdinal = _state->activeTextOrdinal(),
			.editingExisting = true,
			.allowSeparateLine = true,
			.separateLine = true,
		};
	}
	if (_field->isHidden()) {
		return std::nullopt;
	}
	if (_state->activeFieldMode() != State::FieldMode::Rich) {
		return std::nullopt;
	}
	const auto cursor = _field->textCursor();
	const auto selection = Ui::InputFieldTextRange{
		.from = cursor.selectionStart(),
		.till = cursor.selectionEnd(),
	};
	auto request = MathEditRequest{
		.range = selection,
		.allowSeparateLine = _state->activeSurfaceAllowsSeparateLineFormula(),
	};
	if (!selection.empty()) {
		request.source = _field->getTextWithTagsPart(
			selection.from,
			selection.till).text;
		return request;
	}
	request.range = _field->selectionEditMarkdownTagRange(
		selection,
		Ui::InputField::kTagIvMath);
	if (!request.range.empty()) {
		request.source = _field->getTextWithTagsPart(
			request.range.from,
			request.range.till).text;
		request.editingExisting = true;
	}
	return request;
}

Widget::MathEditRequest Widget::newDisplayMathRequest() const {
	return MathEditRequest{
		.allowSeparateLine = true,
		.separateLine = true,
		.insertNewDisplayBlock = true,
	};
}

bool Widget::handleIvClipboardMime(
		not_null<const QMimeData*> data,
		Ui::InputField::MimeAction action) {
	const auto modifiers = QApplication::keyboardModifiers();
	if ((modifiers & Qt::ControlModifier)
		&& (modifiers & Qt::ShiftModifier)) {
		return false;
	}
	const auto clipboardData = ClipboardDataFromMimeData(data.get());
	if (clipboardData
		&& ClipboardPasteInsertContext(activeTextInsertContext())) {
		if (action == Ui::InputField::MimeAction::Check) {
			return true;
		}
		crl::on_main(this, [=, clipboardData = *clipboardData] {
			pasteStructuredClipboardData(clipboardData);
		});
		return true;
	}
	if (action == Ui::InputField::MimeAction::Check) {
		return CanPrepareMediaFromClipboard(data);
	} else if (auto list = PreparedMediaFromClipboard(
			data,
			_session->premium())) {
		if (_applyPreparedMedia) {
			auto target = preparedMediaPasteTarget();
			crl::on_main(this, [=, list = std::move(*list)]() mutable {
				_applyPreparedMedia(
					not_null<Widget*>(this),
					std::move(list),
					std::move(target));
			});
			return true;
		}
	}
	return false;
}

int Widget::richOffsetForFieldOffset(
		const TextWithEntities &text,
		int offset) const {
	const auto replacements = ConvertRichTextToEditorTags(text).replacements;
	return std::clamp(
		MapEditorOffsetToRichOffset(replacements, offset),
		0,
		int(text.text.size()));
}

ApplyResult Widget::applyFieldTextToState() {
	if (_settingField || _field->isHidden()) {
		return ApplyResult::Unchanged;
	}
	if (_state->activeFieldMode() == State::FieldMode::Raw) {
		return _state->applyActiveRawText(_field->getLastText());
	}
	const auto text = _field->getTextWithAppliedMarkdown();
	return _state->applyActiveText(ConvertEditorTagsToRichText(text));
}

ApplyResult Widget::applyMathEditResult(
		const MathEditRequest &request,
		MathEditResult result) {
	const auto source = result.source.trimmed();
	if (source.isEmpty()) {
		return ApplyResult::Unchanged;
	}
	if (_settingField) {
		return ApplyResult::Unchanged;
	}
	if (request.insertNewDisplayBlock) {
		auto block = RichPage::Block();
		block.kind = RichPage::BlockKind::Math;
		block.formula = source;
		insertPreparedBlock(std::move(block));
		return ApplyResult::Changed;
	}
	if (request.displayMathOrdinal >= 0) {
		if (!_state->setActiveTextByOrdinal(request.displayMathOrdinal)) {
			return ApplyResult::Failed;
		}
		_activeOrdinal = request.displayMathOrdinal;
		_activeSegmentIndex = segmentIndexForEditableOrdinal(_activeOrdinal);
	}
	const auto displayMathEdit
		= (_state->activeFieldMode() == State::FieldMode::Raw);
	if (!displayMathEdit && _field->isHidden()) {
		return ApplyResult::Unchanged;
	}
	if (displayMathEdit) {
		auto displayMathResult = State::DisplayMathEditResult();
		const auto committed = recordMutationTransaction([&] {
			displayMathResult = _state->editActiveDisplayMath(
				source,
				result.separateLine);
			return displayMathResult.result;
		});
		if (committed == ApplyResult::Failed) {
			showLastLimitToast();
			return committed;
		}
		if (committed != ApplyResult::Changed) {
			return committed;
		}
		refreshPreparedContent();
		if (displayMathResult.inlineLeaf) {
			const auto ordinal = _state->textOrdinalForLeafPath(
				*displayMathResult.inlineLeaf);
			activateTextOrdinal(
				(ordinal >= 0) ? ordinal : _state->activeTextOrdinal(),
				displayMathResult.selectionFrom,
				displayMathResult.selectionTo);
		} else {
			activateTextOrdinal(_state->activeTextOrdinal(), 0);
		}
		return committed;
	}
	if (_state->activeFieldMode() != State::FieldMode::Rich) {
		return ApplyResult::Unchanged;
	}
	if (result.separateLine) {
		auto cursor = _field->textCursor();
		cursor.setPosition(request.range.from);
		cursor.setPosition(request.range.till, QTextCursor::KeepAnchor);
		_field->setTextCursor(cursor);
		auto block = RichPage::Block();
		block.kind = RichPage::BlockKind::Math;
		block.formula = source;
		insertPreparedBlock(std::move(block));
		return ApplyResult::Changed;
	}
	const auto committed = recordMutationTransaction([&] {
		_field->commitMarkdownTagEdit(
			request.range,
			Ui::InputField::kTagIvMath,
			source);
		const auto committed = commitInlineField();
		if (committed != ApplyResult::Failed) {
			_pendingOrdinal = -1;
			_pendingCursorOffset = 0;
			hideInlineField();
			clearInlineFieldEditSession();
		}
		return committed;
	});
	if (committed != ApplyResult::Failed) {
		refreshAfterInlineFieldCommit(committed);
	}
	return committed;
}

bool Widget::showLastLimitToast() {
	if (_showLimitToast) {
		if (const auto error = _state->lastLimitError()) {
			_showLimitToast(*error);
			return true;
		}
	}
	return false;
}

void Widget::showMathEditBox(MathEditRequest request) {
	if (!_show) {
		return;
	}
	const auto weak = QPointer<Widget>(this);
	_show->showBox(Box(
		EditMathBox,
		request.source,
		request.editingExisting,
		request.allowSeparateLine
			? std::make_optional(request.separateLine)
			: std::nullopt,
		[=](QString source, bool separateLine) {
			if (!weak) {
				return;
			}
			const auto result = weak->applyMathEditResult(request, {
				.source = std::move(source),
				.separateLine = separateLine,
			});
			if (result != ApplyResult::Changed) {
				return;
			}
			weak->syncInlineFieldGeometry();
			weak->updateInlineFieldHeightOverride();
			weak->revealActiveInlineField();
			weak->notifyToolbarStateChanged();
		},
		[=](bool active) {
			if (weak) {
				weak->setInlineFieldExternalInteractionActive(active);
				weak->notifyToolbarStateChanged();
			}
		},
		[=] {
			if (weak && !weak->_field->isHidden()) {
				weak->_field->setFocusFast();
				weak->notifyToolbarStateChanged();
			}
		}));
}

void Widget::hideInlineField() {
	if (_field->isHidden()) {
		return;
	}
	const auto wasSettingField = _settingField;
	_settingField = true;
	const auto guard = gsl::finally([&] {
		_settingField = wasSettingField;
	});
	_field->hide();
}

void Widget::activateTextOrdinalAtEnd(int ordinal) {
	if (!_state->setActiveTextByOrdinal(ordinal)) {
		return;
	}
	activateTextOrdinal(ordinal, _state->activeTextLength());
}

void Widget::setActiveFieldCursorOffset(int offset) {
	auto cursor = _field->textCursor();
	cursor.setPosition(std::clamp(
		offset,
		0,
		int(_field->getLastText().size())));
	_field->setTextCursor(cursor);
	_field->setFocusFast();
	revealActiveInlineField();
}

std::optional<int> Widget::activeFieldPageCursorOffset(bool down) const {
	if (_field->isHidden()) {
		return std::nullopt;
	}
	const auto pageHeight = _visibleRange.bottom - _visibleRange.top;
	if (pageHeight <= 0) {
		return std::nullopt;
	}
	const auto raw = _field->rawTextEdit();
	const auto cursor = _field->textCursor();
	const auto rect = raw->cursorRect(cursor);
	if (!rect.isValid() || rect.isEmpty()) {
		return std::nullopt;
	}
	const auto point = rect.center()
		+ QPoint(0, down ? pageHeight : -pageHeight);
	if (!raw->viewport()->rect().contains(point)) {
		return std::nullopt;
	}
	return std::clamp(
		raw->cursorForPosition(point).position(),
		0,
		int(_field->getLastText().size()));
}

std::optional<QPoint> Widget::activeFieldCursorArticlePoint() const {
	if (_field->isHidden()) {
		return std::nullopt;
	}
	const auto raw = _field->rawTextEdit();
	auto cursor = _field->textCursor();
	cursor.setPosition(cursor.position());
	const auto rect = raw->cursorRect(cursor);
	return (!rect.isValid() || rect.isEmpty())
		? std::nullopt
		: std::make_optional(
			raw->viewport()->mapTo(this, rect.center()) - articleTopLeft());
}

bool Widget::fieldCursorLeavesVisibleRow(bool down) const {
	if (_field->isHidden()) {
		return false;
	}
	const auto raw = _field->rawTextEdit();
	const auto viewport = raw->viewport()->rect();
	const auto cursor = _field->textCursor();
	auto next = cursor;
	const auto moved = next.movePosition(
		down ? QTextCursor::Down : QTextCursor::Up,
		QTextCursor::MoveAnchor);
	if (!moved || (next.position() == cursor.position())) {
		return true;
	}
	const auto nextRect = raw->cursorRect(next);
	return !nextRect.isValid()
		|| nextRect.isEmpty()
		|| (nextRect.top() < viewport.top())
		|| (nextRect.bottom() > viewport.bottom());
}

int Widget::textEditableSegmentIndex(int ordinal) const {
	if (!_article) {
		return -1;
	}
	const auto &nodes = _state->textNodes();
	if (ordinal < 0 || ordinal >= int(nodes.size())) {
		return -1;
	}
	const auto segmentIndex = segmentIndexForEditableOrdinal(ordinal);
	return (segmentIndex >= 0)
		&& _article->segmentIsText(segmentIndex)
		&& _article->segmentIsEditable(segmentIndex)
		&& (editableOrdinalForSegment(segmentIndex) == ordinal)
		? segmentIndex
		: -1;
}

std::optional<int> Widget::adjacentTextEditableOrdinal(bool down) const {
	const auto &nodes = _state->textNodes();
	const auto count = int(nodes.size());
	if (_activeOrdinal < 0 || _activeOrdinal >= count) {
		return std::nullopt;
	}
	for (auto ordinal = _activeOrdinal + (down ? 1 : -1);
		ordinal >= 0 && ordinal < count;
		ordinal += down ? 1 : -1) {
		if (textEditableSegmentIndex(ordinal) >= 0) {
			return ordinal;
		}
	}
	return std::nullopt;
}

std::optional<int> Widget::textEditableOrdinalFromSegment(
		int segmentIndex,
		bool down) const {
	if (segmentIndex < 0) {
		return std::nullopt;
	}
	const auto &nodes = _state->textNodes();
	const auto count = int(nodes.size());
	if (down) {
		for (auto ordinal = 0; ordinal != count; ++ordinal) {
			const auto candidateSegmentIndex = textEditableSegmentIndex(ordinal);
			if (candidateSegmentIndex >= segmentIndex) {
				return ordinal;
			}
		}
	} else {
		for (auto ordinal = count - 1; ordinal >= 0; --ordinal) {
			const auto candidateSegmentIndex = textEditableSegmentIndex(ordinal);
			if (candidateSegmentIndex >= 0
				&& candidateSegmentIndex <= segmentIndex) {
				return ordinal;
			}
		}
	}
	return std::nullopt;
}

std::optional<Widget::VerticalNavigationTarget> Widget::adjacentRowTarget(
		int ordinal,
		QPoint articlePoint,
		bool down) {
	if (!_article) {
		return std::nullopt;
	}
	const auto segmentIndex = textEditableSegmentIndex(ordinal);
	if (segmentIndex < 0) {
		return std::nullopt;
	}
	const auto segmentRect = _article->segmentRect(segmentIndex);
	if (!segmentRect.isValid() || segmentRect.isEmpty()) {
		return std::nullopt;
	}
	const auto clampedY = down
		? std::max(articlePoint.y(), segmentRect.top())
		: std::min(articlePoint.y(), segmentRect.bottom());
	articlePoint.setY(std::clamp(
		clampedY,
		segmentRect.top(),
		segmentRect.bottom()));
	syncArticleVisibleTopBottom();
	const auto hit = _article->hitTest(
		articlePoint,
		Ui::Text::StateRequest::Flag::LookupSymbol);
	if (!hit.valid()
		|| !_article->segmentIsText(hit.segmentIndex)
		|| !_article->segmentIsEditable(hit.segmentIndex)
		|| (editableOrdinalForSegment(hit.segmentIndex) != ordinal)) {
		return std::nullopt;
	}
	return VerticalNavigationTarget{
		.ordinal = ordinal,
		.offset = _article->selectionOffsetFromHit(
			hit,
			TextSelectType::Letters),
	};
}

std::optional<Widget::VerticalNavigationTarget> Widget::pageNavigationTarget(
		bool down) {
	if (_field->isHidden()
		|| !_article
		|| (_activeOrdinal < 0)
		|| (_activeSegmentIndex < 0)) {
		return std::nullopt;
	}
	const auto activeSegmentIndex = textEditableSegmentIndex(_activeOrdinal);
	if (activeSegmentIndex < 0 || activeSegmentIndex != _activeSegmentIndex) {
		return std::nullopt;
	}
	const auto pageHeight = _visibleRange.bottom - _visibleRange.top;
	if (pageHeight <= 0) {
		return std::nullopt;
	}
	const auto articlePoint = activeFieldCursorArticlePoint();
	if (!articlePoint) {
		return std::nullopt;
	}
	const auto shiftedPoint = *articlePoint
		+ QPoint(0, down ? pageHeight : -pageHeight);
	syncArticleVisibleTopBottom();
	const auto hit = _article->hitTest(
		shiftedPoint,
		Ui::Text::StateRequest::Flag::LookupSymbol);
	if (!hit.valid()) {
		return std::nullopt;
	}
	if (_article->segmentIsText(hit.segmentIndex)
		&& _article->segmentIsEditable(hit.segmentIndex)) {
		const auto ordinal = editableOrdinalForSegment(hit.segmentIndex);
		if (ordinal < 0
			|| ordinal == _activeOrdinal
			|| (segmentIndexForEditableOrdinal(ordinal) != hit.segmentIndex)) {
			return std::nullopt;
		}
		return VerticalNavigationTarget{
			.ordinal = ordinal,
			.offset = _article->selectionOffsetFromHit(
				hit,
				TextSelectType::Letters),
		};
	}
	const auto ordinal = textEditableOrdinalFromSegment(hit.segmentIndex, down);
	return (ordinal && *ordinal != _activeOrdinal)
		? adjacentRowTarget(*ordinal, shiftedPoint, down)
		: std::nullopt;
}

std::optional<Widget::BoundarySelectionOrigin>
Widget::currentBoundarySelectionOrigin(bool forward) const {
	if (_field->isHidden()
		|| !_article
		|| (_activeOrdinal < 0)
		|| (_state->activeFieldMode() != State::FieldMode::Rich)) {
		return std::nullopt;
	}
	const auto viewState = captureHistoryViewState();
	if (!viewState.leafSelection) {
		return std::nullopt;
	}
	const auto activeLeaf = _state->activePreparedLeafSource();
	auto hit = PreparedEditHit();
	if (const auto articlePoint = activeFieldCursorArticlePoint()) {
		const auto current = _article->editHitTest(*articlePoint);
		if (current.valid()
			&& activeLeaf
			&& current.leaf
			&& (*current.leaf == *activeLeaf)
			&& StructuralOwnerFromHit(current).valid()) {
			hit = current;
		}
	}
	if (!hit.valid()) {
		const auto segmentIndex = textEditableSegmentIndex(_activeOrdinal);
		if (segmentIndex >= 0) {
			const auto rect = _article->segmentRect(segmentIndex);
			if (rect.isValid() && !rect.isEmpty()) {
				hit = _article->editHitTest(rect.center());
			}
		}
	}
	if (!StructuralOwnerFromHit(hit).valid()) {
		return std::nullopt;
	}
	return BoundarySelectionOrigin{
		.leafSelection = *viewState.leafSelection,
		.anchorHit = hit,
		.forward = forward,
	};
}

std::optional<PreparedEditHit> Widget::boundaryHitFromTarget(
		const State::BoundaryTarget &target) const {
	switch (target.action) {
	case State::BoundaryTarget::Action::Text: {
		if (!_article || target.textOrdinal < 0) {
			return std::nullopt;
		}
		const auto segmentIndex = segmentIndexForEditableOrdinal(
			target.textOrdinal);
		if (segmentIndex < 0
			|| (editableOrdinalForSegment(segmentIndex)
				!= target.textOrdinal)) {
			return std::nullopt;
		}
		const auto rect = _article->segmentRect(segmentIndex);
		if (!rect.isValid() || rect.isEmpty()) {
			return std::nullopt;
		}
		const auto hit = _article->editHitTest(rect.center());
		return StructuralOwnerFromHit(hit).valid()
			? std::make_optional(hit)
			: std::nullopt;
	}
	case State::BoundaryTarget::Action::StructuralSelection:
		switch (target.structuralSelection.kind) {
		case PreparedEditSelectionKind::Blocks: {
			if (target.structuralSelection.blocks.from + 1
				!= target.structuralSelection.blocks.till) {
				return std::nullopt;
			}
			const auto hit = PreparedEditHitFromBlockSelection(
				target.structuralSelection.blocks,
				false);
			return StructuralOwnerFromHit(hit).valid()
				? std::make_optional(hit)
				: std::nullopt;
		}
		case PreparedEditSelectionKind::ListItems: {
			if (target.structuralSelection.listItems.from + 1
				!= target.structuralSelection.listItems.till) {
				return std::nullopt;
			}
			const auto hit = PreparedEditHitFromListItemSelection(
				target.structuralSelection.listItems,
				false);
			return StructuralOwnerFromHit(hit).valid()
				? std::make_optional(hit)
				: std::nullopt;
		}
		case PreparedEditSelectionKind::None:
		case PreparedEditSelectionKind::TableRows:
		case PreparedEditSelectionKind::TableCells:
			return std::nullopt;
		}
		return std::nullopt;
	case State::BoundaryTarget::Action::None:
	case State::BoundaryTarget::Action::RemoveActiveOwner:
		return std::nullopt;
	}
	return std::nullopt;
}

bool Widget::enterStructuralSelectionFromField(bool forward, bool page) {
	const auto committedSelection = [&]() {
		if (_field->isHidden()
			|| (_state->activeFieldMode() != State::FieldMode::Rich)) {
			return std::optional<CommittedFieldSelectionCapture>();
		}
		const auto leaf = _state->activeLeafPath();
		if (!leaf) {
			return std::optional<CommittedFieldSelectionCapture>();
		}
		const auto text = ConvertEditorTagsToRichText(
			_field->getTextWithAppliedMarkdown());
		const auto cursor = _field->textCursor();
		const auto length = int(text.text.size());
		return std::make_optional(CommittedFieldSelectionCapture{
			.leaf = *leaf,
			.text = text,
			.anchorOffset = std::clamp(
				richOffsetForFieldOffset(text, cursor.anchor()),
				0,
				length),
			.cursorOffset = std::clamp(
				richOffsetForFieldOffset(text, cursor.position()),
				0,
				length),
		});
	}();
	const auto committed = commitInlineFieldForClose();
	if (committed == ApplyResult::Failed) {
		return false;
	}
	if (committed == ApplyResult::Changed) {
		const auto mapped = committedSelection
			? MapCommittedFieldSelectionAfterCommit(*_state, *committedSelection)
			: std::nullopt;
		if (!mapped) {
			return false;
		}
		activateTextOrdinal(
			mapped->ordinal,
			mapped->anchorOffset,
			mapped->cursorOffset,
			ActivateReveal::Skip);
		const auto activeLeaf = _state->activeLeafPath();
		if (_field->isHidden()
			|| !activeLeaf
			|| !_fieldLeaf
			|| (*_fieldLeaf != *activeLeaf)) {
			return false;
		}
	}
	const auto origin = currentBoundarySelectionOrigin(forward);
	if (!origin) {
		return false;
	}
	const auto owner = StructuralOwnerFromHit(origin->anchorHit);
	if (!owner.valid()) {
		return false;
	}
	const auto initialSelection = SelectionFromStructuralOwner(owner);
	if (initialSelection.empty()) {
		return false;
	}
	_selection = {};
	_selectionEndpoints = {};
	finishArticleSelection();
	_pendingOrdinal = -1;
	_pendingCursorOffset = 0;
	hideInlineField();
	clearInlineFieldEditSession();
	relayoutCurrentContent();
	setFocus();
	setStructuralSelection(initialSelection, origin);
	if (page) {
		static_cast<void>(adjustStructuralSelectionFromKeyboard(
			forward,
			true));
	}
	update();
	return true;
}

bool Widget::adjustStructuralSelectionFromKeyboard(bool forward, bool page) {
	if (!_article
		|| !_boundarySelectionOrigin
		|| _structuralSelection.empty()) {
		return false;
	}
	const auto origin = *_boundarySelectionOrigin;
	const auto originSelection = SelectionFromStructuralOwner(
		StructuralOwnerFromHit(origin.anchorHit));
	if (originSelection.empty()) {
		return false;
	}
	const auto edgeY = [&](const PreparedEditSelection &selection) {
		const auto ordinal = StructuralSelectionEdgeTextOrdinal(
			*_state,
			selection,
			origin.forward);
		if (!ordinal) {
			return std::optional<int>();
		}
		const auto segmentIndex = segmentIndexForEditableOrdinal(*ordinal);
		if (segmentIndex < 0
			|| (editableOrdinalForSegment(segmentIndex) != *ordinal)) {
			return std::optional<int>();
		}
		const auto rect = _article->segmentRect(segmentIndex);
		if (!rect.isValid() || rect.isEmpty()) {
			return std::optional<int>();
		}
		return std::make_optional(origin.forward ? rect.bottom() : rect.top());
	};
	const auto advance = [&]() {
		if (!_boundarySelectionOrigin || _structuralSelection.empty()) {
			return false;
		}
		if ((forward != origin.forward)
			&& (_structuralSelection == originSelection)) {
			return restoreFieldFromBoundaryOrigin();
		}
		const auto focusEdge = EdgeSelection(
			_structuralSelection,
			origin.forward);
		if (focusEdge.empty()) {
			return false;
		}
		const auto withinFocusEdge = [&](const PreparedEditHit &hit) {
			const auto owner = StructuralOwnerFromHit(hit);
			const auto selection = SelectionFromStructuralOwner(owner);
			if (selection.empty()) {
				return false;
			} else if (selection == focusEdge) {
				return true;
			}
			const auto block = BlockPathFromOwner(owner);
			if (!block) {
				return false;
			} else if (focusEdge.kind == PreparedEditSelectionKind::Blocks) {
				return PreparedPathInBlockRange(*block, focusEdge.blocks);
			} else if (focusEdge.kind == PreparedEditSelectionKind::ListItems) {
				return PreparedPathInListItemRange(
					*block,
					focusEdge.listItems);
			}
			return false;
		};
		const auto steps = _state->boundarySteps(true);
		auto firstFocus = -1;
		auto lastFocus = -1;
		for (auto i = 0, count = int(steps.size()); i != count; ++i) {
			const auto hit = boundaryHitFromTarget(steps[i]);
			if (!hit || !withinFocusEdge(*hit)) {
				continue;
			}
			if (firstFocus < 0) {
				firstFocus = i;
			}
			lastFocus = i;
		}
		if (firstFocus < 0 || lastFocus < firstFocus) {
			return false;
		}
		for (auto i = forward ? (lastFocus + 1) : (firstFocus - 1);
				i >= 0 && i < int(steps.size());
				i += forward ? 1 : -1) {
			const auto hit = boundaryHitFromTarget(steps[i]);
			if (!hit || withinFocusEdge(*hit)) {
				continue;
			}
			const auto selection = SelectionFromStructuralOwner(
				StructuralOwnerFromHit(*hit));
			if (selection.empty()) {
				continue;
			}
			if ((forward != origin.forward)
				&& (selection == originSelection)) {
				return restoreFieldFromBoundaryOrigin();
			}
			const auto next = structuralSelectionFromHits(
				origin.anchorHit,
				*hit);
			if (next.empty() || next == _structuralSelection) {
				continue;
			}
			setStructuralSelection(next, _boundarySelectionOrigin);
			revealStructuralSelectionEdge(origin.forward);
			update();
			return true;
		}
		return false;
	};
	const auto start = page ? edgeY(_structuralSelection) : std::optional<int>();
	if (!advance()) {
		return false;
	}
	if (!page || !_boundarySelectionOrigin || _structuralSelection.empty()) {
		return true;
	}
	const auto pageHeight = _visibleRange.bottom - _visibleRange.top;
	if (pageHeight <= 0 || !start) {
		return true;
	}
	while (_boundarySelectionOrigin && !_structuralSelection.empty()) {
		const auto current = edgeY(_structuralSelection);
		if (!current) {
			break;
		}
		if (std::abs(*current - *start) >= pageHeight) {
			break;
		}
		if (!advance()) {
			break;
		}
		if (!_boundarySelectionOrigin || _structuralSelection.empty()) {
			break;
		}
	}
	return true;
}

bool Widget::handleFieldKey(QKeyEvent *e) {
	if (_field->isHidden()) {
		return false;
	}
	const auto key = e->key();
	if (key == Qt::Key_Escape) {
		hideInlineFieldAndRefresh();
		e->accept();
		return true;
	}
	const auto modifiers = e->modifiers()
		& ~(Qt::KeypadModifier | Qt::GroupSwitchModifier);
	const auto cursor = _field->textCursor();
	const auto vertical = (key == Qt::Key_Up)
		|| (key == Qt::Key_Down)
		|| (key == Qt::Key_PageUp)
		|| (key == Qt::Key_PageDown);
	const auto down = (key == Qt::Key_Down) || (key == Qt::Key_PageDown);
	const auto page = (key == Qt::Key_PageUp) || (key == Qt::Key_PageDown);
	const auto atStart = cursor.atStart();
	const auto atEnd = cursor.atEnd();
	auto handled = false;
	const auto applyFieldCursor = [&](QTextCursor next) {
		if ((next.position() == cursor.position())
			&& (next.anchor() == cursor.anchor())) {
			return false;
		}
		_field->setTextCursor(next);
		_field->setFocusFast();
		revealActiveInlineField();
		handled = true;
		return true;
	};
	const auto moveFieldCursor = [&](
			QTextCursor::MoveOperation operation,
			QTextCursor::MoveMode mode) {
		auto next = _field->textCursor();
		static_cast<void>(next.movePosition(operation, mode));
		return applyFieldCursor(next);
	};
	const auto activateVerticalTarget = [&](
			const VerticalNavigationTarget &target) {
		if (target.ordinal == _activeOrdinal) {
			setActiveFieldCursorOffset(target.offset);
		} else {
			static_cast<void>(commitAndActivateTextOrdinal(
				target.ordinal,
				target.offset,
				target.offset,
				ActivateReveal::Reveal));
		}
		handled = true;
	};
	const auto refreshPreparedContentAndActivate = [&](
			int ordinal,
			int cursorOffset) {
		beginInlineFieldRevealSuppression();
		{
			const auto revealGuard = gsl::finally([&] {
				endInlineFieldRevealSuppression();
			});
			refreshPreparedContent();
			activateTextOrdinal(ordinal, cursorOffset, ActivateReveal::Skip);
		}
		revealActiveInlineField();
	};
	if (vertical) {
		if (modifiers == Qt::NoModifier) {
			if (page) {
				if (const auto offset = activeFieldPageCursorOffset(down)) {
					auto next = cursor;
					next.setPosition(*offset);
					handled = applyFieldCursor(next);
				}
				if (!handled) {
					if (const auto target = pageNavigationTarget(down)) {
						activateVerticalTarget(*target);
					} else if (down) {
						handled = moveVerticalDownBoundary();
					} else {
						handled = moveBoundary(false, false)
							|| insertLeadingParagraphFromField(true);
					}
				}
			} else if (!fieldCursorLeavesVisibleRow(down)) {
				handled = moveFieldCursor(
					down ? QTextCursor::Down : QTextCursor::Up,
					QTextCursor::MoveAnchor);
			}
			if (!handled) {
				if (const auto articlePoint = activeFieldCursorArticlePoint()) {
					if (const auto ordinal = adjacentTextEditableOrdinal(down)) {
						if (const auto target = adjacentRowTarget(
								*ordinal,
								*articlePoint,
								down)) {
							activateVerticalTarget(*target);
						}
					}
				}
				if (!handled && down) {
					if (const auto ordinal
						= _state->firstTableCellOrdinalFromActiveTitle()) {
						refreshPreparedContentAndActivate(*ordinal, 0);
						handled = true;
					}
				}
				if (!handled) {
					handled = down
						? moveVerticalDownBoundary()
						: (moveBoundary(false, false)
							|| insertLeadingParagraphFromField(true));
				}
			}
		} else if (modifiers == Qt::ShiftModifier) {
			if (page) {
				if (const auto offset = activeFieldPageCursorOffset(down)) {
					auto next = cursor;
					next.setPosition(*offset, QTextCursor::KeepAnchor);
					handled = applyFieldCursor(next);
				}
				if (!handled) {
					handled = enterStructuralSelectionFromField(down, true);
				}
			} else if (!fieldCursorLeavesVisibleRow(down)) {
				handled = moveFieldCursor(
					down ? QTextCursor::Down : QTextCursor::Up,
					QTextCursor::KeepAnchor);
			}
			if (!handled) {
				handled = enterStructuralSelectionFromField(down, false);
			}
		}
		if (handled) {
			e->accept();
		}
		return handled;
	}
	if (modifiers != Qt::NoModifier) {
		return false;
	}
	if (cursor.hasSelection()) {
		return false;
	}
	if (atStart
		&& key == Qt::Key_Left) {
		handled = moveBoundary(false, false);
	} else if (atEnd
		&& key == Qt::Key_Right) {
		handled = moveBoundary(true, true);
	} else if (key == Qt::Key_Return || key == Qt::Key_Enter) {
		if (_fieldSuggestions && _fieldSuggestions->shown()) {
			return false;
		}
		recordMutationTransaction([&] {
			const auto committed = commitInlineField();
			// At the very start of the very first text node of a block that
			// is not a top-level paragraph or heading (a table, a list, ...)
			// Enter inserts a paragraph above everything, so content can
			// always be added at the very top of the article. The focus
			// stays in the initially edited node.
			const auto insertLeading = (committed != ApplyResult::Failed)
				&& atStart
				&& !_state->previousEditableOrdinal().has_value()
				&& !_state->isActiveTopLevelParagraphOrHeading();
			const auto leadingTarget = insertLeading
				? _state->insertLeadingParagraphActive(false)
				: std::optional<int>();
			if (committed == ApplyResult::Failed) {
				handled = true;
				return MutationTransactionResult{
					.committed = committed,
					.failed = true,
				};
			} else if (leadingTarget) {
				refreshPreparedContentAndActivate(*leadingTarget, 0);
				handled = true;
				return MutationTransactionResult{
					.committed = committed,
					.changed = true,
				};
			} else if (const auto target = _state->handleActiveListEnter()) {
				refreshPreparedContentAndActivate(*target, 0);
				handled = true;
				return MutationTransactionResult{
					.committed = committed,
					.changed = true,
				};
			} else if (const auto target = _state->handleActiveHeadingEnter()) {
				refreshPreparedContentAndActivate(*target, 0);
				handled = true;
				return MutationTransactionResult{
					.committed = committed,
					.changed = true,
				};
			} else if (const auto target
				= _state->submitActiveSingleLineField()) {
				refreshPreparedContentAndActivate(*target, 0);
				handled = true;
				return MutationTransactionResult{
					.committed = committed,
					.changed = true,
				};
			} else if (_state->lastLimitError()) {
				showLastLimitToast();
				handled = true;
			}
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		});
	} else if (atStart && key == Qt::Key_Backspace) {
		handled = removeBoundaryOwner(false);
	} else if (atEnd && key == Qt::Key_Delete) {
		handled = removeBoundaryOwner(true);
	}
	if (handled) {
		e->accept();
	}
	return handled;
}

bool Widget::handleTabNavigation(QKeyEvent *e) {
	const auto key = e->key();
	if (key != Qt::Key_Tab && key != Qt::Key_Backtab) {
		return false;
	}
	const auto modifiers = e->modifiers()
		& ~(Qt::KeypadModifier | Qt::GroupSwitchModifier);
	if (modifiers != Qt::NoModifier && modifiers != Qt::ShiftModifier) {
		return false;
	}
	const auto forward = (key != Qt::Key_Backtab)
		&& (modifiers != Qt::ShiftModifier);
	if (!moveTabBoundary(forward)) {
		return false;
	}
	e->accept();
	return true;
}

bool Widget::moveBoundary(bool forward, bool allowTrailing) {
	const auto target = forward
		? _state->nextEditableOrdinal()
		: _state->previousEditableOrdinal();
	const auto addTrailing = forward
		&& allowTrailing
		&& !target
		&& !_state->isActiveTopLevelParagraph();
	if (!target && !addTrailing) {
		return false;
	}
	auto handled = false;
	beginArticleRelayoutDeferral();
	const auto relayoutGuard = gsl::finally([&] {
		endArticleRelayoutDeferral();
	});
	recordMutationTransaction([&] {
		const auto committed = commitInlineField();
		if (committed == ApplyResult::Failed) {
			handled = true;
			return MutationTransactionResult{
				.committed = committed,
				.failed = true,
			};
		}
		if (target) {
			refreshPreparedContent();
			if (forward) {
				activateTextOrdinal(*target, 0);
			} else {
				activateTextOrdinalAtEnd(*target);
			}
			handled = true;
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		const auto ordinal = _state->ensureTrailingParagraphActive();
		if (!ordinal) {
			handled = forward
				&& allowTrailing
				&& _state->lastLimitError().has_value();
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		refreshPreparedContent();
		activateTextOrdinal(*ordinal, 0);
		handled = true;
		return MutationTransactionResult{
			.committed = committed,
			.changed = true,
		};
	});
	return handled;
}

bool Widget::insertLeadingParagraphFromField(bool focusInserted) {
	if (_state->previousEditableOrdinal().has_value()
		|| _state->isActiveTopLevelParagraphOrHeading()) {
		return false;
	}
	auto handled = false;
	beginArticleRelayoutDeferral();
	const auto relayoutGuard = gsl::finally([&] {
		endArticleRelayoutDeferral();
	});
	recordMutationTransaction([&] {
		const auto committed = commitInlineField();
		if (committed == ApplyResult::Failed) {
			handled = true;
			return MutationTransactionResult{
				.committed = committed,
				.failed = true,
			};
		}
		const auto ordinal = _state->insertLeadingParagraphActive(
			focusInserted);
		if (!ordinal) {
			if (_state->lastLimitError()) {
				showLastLimitToast();
				handled = true;
			}
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		refreshPreparedContent();
		activateTextOrdinal(*ordinal, 0);
		handled = true;
		return MutationTransactionResult{
			.committed = committed,
			.changed = true,
		};
	});
	return handled;
}

bool Widget::moveBoundaryAfterCommit(
		ApplyResult committed,
		bool forward,
		bool allowTrailing,
		bool *mutated) {
	if (mutated) {
		*mutated = false;
	}
	const auto target = forward
		? _state->nextEditableOrdinal()
		: _state->previousEditableOrdinal();
	if (target) {
		refreshPreparedContent();
		if (forward) {
			activateTextOrdinal(*target, 0);
		} else {
			activateTextOrdinalAtEnd(*target);
		}
		return true;
	}
	if (forward && allowTrailing && !_state->isActiveTopLevelParagraph()) {
		const auto ordinal = _state->ensureTrailingParagraphActive();
		if (!ordinal) {
			return _state->lastLimitError().has_value();
		}
		if (mutated) {
			*mutated = true;
		}
		refreshPreparedContent();
		activateTextOrdinal(*ordinal, 0);
		return true;
	}
	return false;
}

bool Widget::moveVerticalDownBoundary() {
	auto handled = false;
	const auto refreshPreparedContentAndActivate = [&](
			int ordinal,
			int cursorOffset) {
		beginInlineFieldRevealSuppression();
		{
			const auto revealGuard = gsl::finally([&] {
				endInlineFieldRevealSuppression();
			});
			refreshPreparedContent();
			activateTextOrdinal(ordinal, cursorOffset, ActivateReveal::Skip);
		}
		revealActiveInlineField();
	};
	recordMutationTransaction([&] {
		const auto committed = commitInlineField();
		if (committed == ApplyResult::Failed) {
			handled = true;
			return MutationTransactionResult{
				.committed = committed,
				.failed = true,
			};
		} else if (const auto target
			= _state->removeTemporaryDownParagraphAndMove();
			target.action != State::BoundaryTarget::Action::None) {
			switch (target.action) {
			case State::BoundaryTarget::Action::Text:
				refreshPreparedContentAndActivate(target.textOrdinal, 0);
				break;
			case State::BoundaryTarget::Action::StructuralSelection:
				beginInlineFieldRevealSuppression();
				{
					const auto revealGuard = gsl::finally([&] {
						endInlineFieldRevealSuppression();
					});
					refreshPreparedContent();
				}
				_boundarySelectionOrigin = std::nullopt;
				_selection = {};
				_selectionEndpoints = {};
				finishArticleSelection();
				setStructuralSelection(target.structuralSelection);
				_pendingOrdinal = -1;
				_pendingCursorOffset = 0;
				hideInlineField();
				clearInlineFieldEditSession();
				relayoutCurrentContent();
				setFocus();
				update();
				break;
			case State::BoundaryTarget::Action::None:
			case State::BoundaryTarget::Action::RemoveActiveOwner:
				break;
			}
			handled = true;
			return MutationTransactionResult{
				.committed = committed,
				.changed = true,
			};
		} else if (const auto target = _state->moveActiveSpecialBlockDown()) {
			refreshPreparedContentAndActivate(*target, 0);
			handled = true;
			return MutationTransactionResult{
				.committed = committed,
				.changed = true,
			};
		}
		auto mutated = false;
		if (_state->lastLimitError()) {
			handled = moveBoundaryAfterCommit(
				committed,
				true,
				false,
				&mutated);
			if (!handled) {
				handled = true;
			}
		} else {
			handled = moveBoundaryAfterCommit(
				committed,
				true,
				true,
				&mutated);
		}
		return MutationTransactionResult{
			.committed = committed,
			.changed = mutated || (committed == ApplyResult::Changed),
		};
	});
	return handled;
}

bool Widget::moveTabBoundary(bool forward) {
	auto handled = false;
	const auto target = forward
		? _state->nextEditableOrdinal()
		: _state->previousEditableOrdinal();
	if (!target && (!forward || _state->isActiveTopLevelParagraph())) {
		return false;
	}
	beginArticleRelayoutDeferral();
	const auto relayoutGuard = gsl::finally([&] {
		endArticleRelayoutDeferral();
	});
	recordMutationTransaction([&] {
		auto committed = ApplyResult::Unchanged;
		if (!_field->isHidden()) {
			committed = commitInlineField();
			if (committed == ApplyResult::Failed) {
				handled = true;
				return MutationTransactionResult{
					.committed = committed,
					.failed = true,
				};
			}
		}
		if (target) {
			clearSelection();
			if (committed == ApplyResult::Changed) {
				refreshAfterInlineFieldCommit(committed);
			}
			activateTextOrdinalAtEnd(*target);
			handled = true;
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		clearSelection();
		const auto ordinal = _state->ensureTrailingParagraphActive();
		if (!ordinal) {
			handled = _state->lastLimitError().has_value();
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		refreshPreparedContent();
		activateTextOrdinalAtEnd(*ordinal);
		handled = true;
		return MutationTransactionResult{
			.committed = committed,
			.changed = true,
		};
	});
	return handled;
}

bool Widget::removeBoundaryOwner(bool forward) {
	auto handled = false;
	const auto committedSelection = [&]() {
		if (_field->isHidden()
			|| (_state->activeFieldMode() != State::FieldMode::Rich)) {
			return std::optional<CommittedFieldSelectionCapture>();
		}
		const auto leaf = _state->activeLeafPath();
		if (!leaf) {
			return std::optional<CommittedFieldSelectionCapture>();
		}
		const auto text = ConvertEditorTagsToRichText(
			_field->getTextWithAppliedMarkdown());
		const auto cursor = _field->textCursor();
		const auto length = int(text.text.size());
		return std::make_optional(CommittedFieldSelectionCapture{
			.leaf = *leaf,
			.text = text,
			.anchorOffset = std::clamp(
				richOffsetForFieldOffset(text, cursor.anchor()),
				0,
				length),
			.cursorOffset = std::clamp(
				richOffsetForFieldOffset(text, cursor.position()),
				0,
				length),
		});
	}();
	beginArticleRelayoutDeferral();
	const auto relayoutGuard = gsl::finally([&] {
		endArticleRelayoutDeferral();
	});
	recordMutationTransaction([&] {
		const auto committed = commitInlineField();
		if (committed == ApplyResult::Failed) {
			handled = true;
			return MutationTransactionResult{
				.committed = committed,
				.failed = true,
			};
		}
		if (committed == ApplyResult::Changed) {
			refreshAfterInlineFieldCommit(committed);
			if (committedSelection) {
				const auto mapped = MapCommittedFieldSelectionAfterCommit(
					*_state,
					*committedSelection);
				if (!mapped) {
					handled = true;
					return MutationTransactionResult{
						.committed = committed,
						.changed = true,
					};
				}
				activateTextOrdinal(
					mapped->ordinal,
					mapped->anchorOffset,
					mapped->cursorOffset,
					ActivateReveal::Skip);
			}
		}
		const auto joined = _state->joinActiveParagraphBoundary(forward);
		if (joined.result == ApplyResult::Changed) {
			_boundarySelectionOrigin = std::nullopt;
			hideInlineField();
			clearInlineFieldEditSession();
			refreshPreparedContent();
			auto ordinal = joined.destinationLeaf
				? _state->textOrdinalForLeafPath(*joined.destinationLeaf)
				: -1;
			if (ordinal < 0) {
				ordinal = _state->activeTextOrdinal();
			}
			if (ordinal >= 0 && ordinal < _state->textNodeCount()) {
				activateTextOrdinal(
					ordinal,
					joined.selectionFrom,
					joined.selectionTo);
			} else {
				setFocus();
				notifyToolbarStateChanged();
			}
			handled = true;
			return MutationTransactionResult{
				.committed = committed,
				.changed = true,
			};
		} else if (joined.result == ApplyResult::Failed) {
			if (_state->lastLimitError()) {
				showLastLimitToast();
			}
			handled = true;
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		const auto target = _state->activeBoundaryTarget(forward);
		using BoundaryAction = State::BoundaryTarget::Action;
		switch (target.action) {
		case BoundaryAction::RemoveActiveOwner: {
			_boundarySelectionOrigin = std::nullopt;
			const auto adjacent = _state->removeActiveOwnerAndSelectAdjacent(
				forward);
			hideInlineField();
			clearInlineFieldEditSession();
			refreshPreparedContent();
			if (adjacent) {
				if (forward) {
					activateTextOrdinal(*adjacent, 0);
				} else {
					activateTextOrdinalAtEnd(*adjacent);
				}
			} else {
				activateInitialNode();
			}
			handled = true;
			return MutationTransactionResult{
				.committed = committed,
				.changed = true,
			};
		}
		case BoundaryAction::Text:
			_boundarySelectionOrigin = std::nullopt;
			if (committed == ApplyResult::Changed) {
				refreshAfterInlineFieldCommit(committed);
			}
			if (forward) {
				activateTextOrdinal(target.textOrdinal, 0);
			} else {
				activateTextOrdinalAtEnd(target.textOrdinal);
			}
			handled = true;
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		case BoundaryAction::StructuralSelection:
			setStructuralSelection(
				target.structuralSelection,
				currentBoundarySelectionOrigin(forward));
			_selection = {};
			_selectionEndpoints = {};
			finishArticleSelection();
			_pendingOrdinal = -1;
			_pendingCursorOffset = 0;
			hideInlineField();
			clearInlineFieldEditSession();
			refreshAfterInlineFieldCommit(committed);
			update();
			handled = true;
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		case BoundaryAction::None: {
			auto mutated = false;
			handled = moveBoundaryAfterCommit(
				committed,
				forward,
				forward,
				&mutated);
			return MutationTransactionResult{
				.committed = committed,
				.changed = mutated || (committed == ApplyResult::Changed),
			};
		}
		}
		Unexpected("Boundary action.");
	});
	return handled;
}

void Widget::ensurePendingActivation() {
	if (_pendingOrdinal < 0) {
		_activeSegmentIndex = (_activeOrdinal >= 0)
			? segmentIndexForEditableOrdinal(_activeOrdinal)
			: _article->firstEditableSegmentIndex();
		return;
	}
	const auto ordinal = _pendingOrdinal;
	const auto cursorOffset = _pendingCursorOffset;
	_pendingOrdinal = -1;
	_pendingCursorOffset = 0;
	activateTextOrdinal(ordinal, cursorOffset);
}

void Widget::updateInlineFieldHeightOverride() {
	if (_settingField
		|| _field->isHidden()
		|| _activeOrdinal < 0
		|| !_article) {
		return;
	} else if (_syncingInlineFieldGeometry) {
		_pendingHeightOverrideUpdate = true;
		return;
	} else if (articleRelayoutDeferralActive()) {
		requestDeferredInlineFieldGeometry();
		requestDeferredInlineFieldHeightOverride();
		return;
	}
	if (_article->editableIndexForSegment(_activeSegmentIndex) < 0) {
		clearArticleEditableHeightOverride();
		return;
	}
	const auto segmentRect = fieldOuterRectForSegment(_activeSegmentIndex);
	auto height = segmentRect.isEmpty()
		? _field->height()
		: std::max(_field->geometry().bottom() + 1 - segmentRect.y(), 1);
	if (_activeSegmentIsDisplayMath) {
		const auto blockRect = _article->displayMathBlockRect(
			_activeSegmentIndex).translated(articleTopLeft());
		if (!blockRect.isEmpty()) {
			height = std::max(
				_field->geometry().bottom() + 1 - blockRect.y(),
				1);
		}
		height = std::max(height, _activeDisplayMathBaselineHeight);
	}
	_article->setEditableHeightOverrideForSegment(_activeSegmentIndex, height);
	relayoutCurrentContent();
	update();
}

void Widget::clearDisplayMathEditSession() {
	_activeSegmentIsDisplayMath = false;
	_activeDisplayMathBaselineHeight = 0;
}

void Widget::clearInlineFieldEditSession(
		bool keepRetainedFieldOnCurrentHistoryEntry) {
	clearDisplayMathEditSession();
	if (_article) {
		clearArticleEditableHeightOverride();
		_article->clearEditableMaxLineWidthOverride();
	}
	if (!_field->isHidden()
		|| !_fieldLeaf) {
		return;
	}
	const auto activeLeaf = _state->activeLeafPath();
	if (!activeLeaf || (*activeLeaf != *_fieldLeaf)) {
		const auto &fieldStyle = inlineFieldStyleFor(
			Markdown::MarkdownArticleTextLeafStyle());
		_activeFieldStyleKey = fieldStyle.key;
		_fieldMode = State::FieldMode::Rich;
		recreateInlineField(*fieldStyle.style);
		return;
	}
	retainActiveLeafField(keepRetainedFieldOnCurrentHistoryEntry);
	const auto &fieldStyle = inlineFieldStyleFor(
		Markdown::MarkdownArticleTextLeafStyle());
	_activeFieldStyleKey = fieldStyle.key;
	_fieldMode = State::FieldMode::Rich;
	recreateInlineField(*fieldStyle.style);
}

Widget::HistoryViewState Widget::captureHistoryViewState() const {
	auto result = HistoryViewState();
	if (!_field->isHidden()) {
		const auto leaf = _state->activeLeafPath();
		if (!leaf) {
			return result;
		}
		const auto cursor = _field->textCursor();
		const auto trimLeft = !_state->codeBlockLanguage(
			_state->activeTextOrdinal()).has_value();
		auto anchorOffset = 0;
		auto cursorOffset = 0;
		if (_state->activeFieldMode() == State::FieldMode::Raw) {
			const auto trimmed = TrimInlineFieldText(
				{ _state->activeRawText(), {} },
				trimLeft);
			const auto size = int(_state->activeRawText().size());
			anchorOffset = std::clamp(cursor.anchor() + trimmed.left, 0, size);
			cursorOffset = std::clamp(
				cursor.position() + trimmed.left,
				0,
				size);
		} else {
			const auto activeText = ConvertRichTextToEditorTags(
				_state->activeText());
			const auto trimmed = TrimInlineFieldText(activeText.text, trimLeft);
			const auto size = int(_state->activeText().text.size());
			anchorOffset = std::clamp(
				MapEditorOffsetToRichOffset(
					activeText.replacements,
					cursor.anchor() + trimmed.left),
				0,
				size);
			cursorOffset = std::clamp(
				MapEditorOffsetToRichOffset(
					activeText.replacements,
					cursor.position() + trimmed.left),
				0,
				size);
		}
		result.leafSelection = HistoryLeafSelection{
			.leaf = *leaf,
			.anchorOffset = anchorOffset,
			.cursorOffset = cursorOffset,
		};
	} else if (hasStructuralSelection()) {
		result.structuralSelection = _structuralSelection;
		result.boundarySelectionOrigin = _boundarySelectionOrigin;
	}
	return result;
}

Widget::HistoryEntry Widget::captureHistoryEntry() const {
	return {
		.snapshot = _state->snapshot(),
		.viewState = captureHistoryViewState(),
	};
}

void Widget::restoreHistoryEntry(const HistoryEntry &entry) {
	hideInlineField();
	clearInlineFieldEditSession();
	if (_article && (_horizontalScrollDrag != HorizontalScrollDrag::None)) {
		_article->endHorizontalScroll();
	}
	_selection = {};
	_selectionEndpoints = {};
	setStructuralSelection({});
	finishArticleSelection();
	_pendingOrdinal = -1;
	_pendingCursorOffset = 0;
	_trackingPointerPress = false;
	_horizontalScrollLock = std::nullopt;
	_pressedControl = {};
	_pressedControlPoint = std::nullopt;
	_horizontalScrollDrag = HorizontalScrollDrag::None;
	_pendingTouchHorizontalScrollPoint = std::nullopt;

	const auto wasRestoring = _restoringHistory;
	_restoringHistory = true;
	const auto guard = gsl::finally([&] {
		_restoringHistory = wasRestoring;
	});

	_state->restoreSnapshot(entry.snapshot);
	refreshPreparedContent();

	if (const auto &selection = entry.viewState.structuralSelection) {
		_activeOrdinal = _state->activeTextOrdinal();
		_activeSegmentIndex = -1;
		_fieldLeaf = std::nullopt;
		clearDisplayMathEditSession();
		setStructuralSelection(
			*selection,
			entry.viewState.boundarySelectionOrigin);
		hideInlineField();
		update();
		return;
	}
	if (const auto &leafSelection = entry.viewState.leafSelection) {
		const auto ordinal = _state->textOrdinalForLeafPath(leafSelection->leaf);
		if (ordinal >= 0) {
			activateTextOrdinal(
				ordinal,
				leafSelection->anchorOffset,
				leafSelection->cursorOffset);
			return;
		}
	}
	_activeOrdinal = _state->activeTextOrdinal();
	_activeSegmentIndex = -1;
	_fieldLeaf = std::nullopt;
	clearDisplayMathEditSession();
	hideInlineField();
	update();
}

bool Widget::mutationTransactionChanged(bool changed) {
	return changed;
}

bool Widget::mutationTransactionChanged(ApplyResult result) {
	return (result == ApplyResult::Changed);
}

bool Widget::mutationTransactionChanged(
		const MutationTransactionResult &result) {
	return result.changed;
}

void Widget::finishMutationTransaction(
		const HistoryEntry &before,
		bool changed,
		int beforeHistoryIndex,
		uint64 beforeRetainToken) {
	if (!changed) {
		return;
	}
	const auto after = captureHistoryEntry();
	const auto snapshotChanged = !SnapshotEquals(before.snapshot, after.snapshot);
	if (!snapshotChanged && (before.viewState == after.viewState)) {
		return;
	}
	truncateHistoryRedo();
	_history.push_back(after);
	_historyIndex = int(_history.size()) - 1;
	moveRetainedLeafFields(
		beforeHistoryIndex,
		_historyIndex,
		beforeRetainToken);
	notifyToolbarStateChanged();
	if (snapshotChanged
		&& (_state->lastPreparedMutationKind()
			!= PreparedMutationKind::LeafOnly)) {
		_autosaveEvents.fire({
			.type = AutosaveEventType::StructuralMutation,
		});
	}
}

void Widget::retainActiveLeafField(
		bool keepRetainedFieldOnCurrentHistoryEntry) {
	if (!_field) {
		ensureInlineFieldCreated();
		return;
	} else if (!_fieldLeaf
		|| !_activeFieldStyleKey) {
		return;
	}
	const auto leaf = *_fieldLeaf;
	if (_state->textOrdinalForLeafPath(leaf) < 0) {
		return;
	}
	const auto wasSettingField = _settingField;
	_settingField = true;
	_field->hide();
	_settingField = wasSettingField;
	const auto historyIndex = _retainingFieldHistoryIndexOverride.value_or(
		_historyIndex);
	const auto &fieldStyle = inlineFieldStyleFor(
		Markdown::MarkdownArticleTextLeafStyle());
	auto replacement = base::make_unique_q<Ui::InputField>(
		this,
		*fieldStyle.style,
		Ui::InputField::Mode::MultiLine,
		rpl::single(QString()));
	auto retained = RetainedLeafField{
		.historyIndex = historyIndex,
		.retainToken = keepRetainedFieldOnCurrentHistoryEntry
			? _retainedLeafFieldToken
			: ++_retainedLeafFieldToken,
		.leaf = leaf,
		.mode = _fieldMode,
		.styleKey = _activeFieldStyleKey,
	};
	retained.field = std::move(_field);
	retained.suggestions = _fieldSuggestions;
	_field = std::move(replacement);
	_activeFieldStyleKey = std::nullopt;
	_fieldMode = State::FieldMode::Rich;
	_fieldLeaf = std::nullopt;
	setupInlineField();
	clearFieldUndoRedoNoopState();
	for (auto i = _retainedLeafFields.begin(); i != _retainedLeafFields.end();) {
		if ((i->historyIndex == retained.historyIndex)
			&& (i->leaf == retained.leaf)
			&& (i->mode == retained.mode)
			&& (i->styleKey == retained.styleKey)) {
			i = _retainedLeafFields.erase(i);
		} else {
			++i;
		}
	}
	_retainedLeafFields.push_back(std::move(retained));
	pruneRetainedLeafFields();
}

base::unique_qptr<Ui::InputField> Widget::reviveRetainedLeafField(
		int historyIndex,
		const State::LeafPath &leaf,
		State::FieldMode mode,
		const InlineFieldStyleKey &styleKey) {
	for (auto i = int(_retainedLeafFields.size()) - 1; i >= 0; --i) {
		if ((_retainedLeafFields[i].historyIndex == historyIndex)
			&& (_retainedLeafFields[i].leaf == leaf)
			&& (_retainedLeafFields[i].mode == mode)
			&& _retainedLeafFields[i].styleKey
			&& (*_retainedLeafFields[i].styleKey == styleKey)) {
			auto result = std::move(_retainedLeafFields[i].field);
			_fieldSuggestions = _retainedLeafFields[i].suggestions;
			_retainedLeafFields.erase(_retainedLeafFields.begin() + i);
			return result;
		}
	}
	return {};
}

void Widget::pruneRetainedLeafFields() {
	for (auto i = _retainedLeafFields.begin(); i != _retainedLeafFields.end();) {
		if (!i->field) {
			i = _retainedLeafFields.erase(i);
		} else {
			++i;
		}
	}
	while (int(_retainedLeafFields.size()) > kRetainedLeafFieldLimit) {
		_retainedLeafFields.erase(_retainedLeafFields.begin());
	}
}

void Widget::removeRetainedLeafFieldsAfter(int historyIndex) {
	for (auto i = _retainedLeafFields.begin(); i != _retainedLeafFields.end();) {
		if (i->historyIndex > historyIndex) {
			i = _retainedLeafFields.erase(i);
		} else {
			++i;
		}
	}
}

void Widget::moveRetainedLeafFields(
		int fromHistoryIndex,
		int toHistoryIndex,
		uint64 afterRetainToken) {
	if (fromHistoryIndex == toHistoryIndex) {
		return;
	}
	for (auto &retained : _retainedLeafFields) {
		if ((retained.historyIndex == fromHistoryIndex)
			&& (retained.retainToken > afterRetainToken)) {
			retained.historyIndex = toHistoryIndex;
		}
	}
}

void Widget::refreshAfterInlineFieldCommit(ApplyResult committed) {
	refreshAfterInlineFieldCommit(
		committed,
		_state->activePreparedLeafSource());
}

void Widget::refreshAfterInlineFieldCommit(
		ApplyResult committed,
		std::optional<Markdown::PreparedEditLeafSource> source) {
	switch ((committed == ApplyResult::Changed)
		? _state->lastPreparedMutationKind()
		: PreparedMutationKind::None) {
	case PreparedMutationKind::LeafOnly:
		if (source) {
			refreshPreparedLeafAtSource(*source);
		} else {
			refreshPreparedContent();
		}
		break;
	case PreparedMutationKind::FullRebuild:
		refreshPreparedContent();
		break;
	case PreparedMutationKind::None:
		relayoutCurrentContent();
		break;
	}
	notifyToolbarStateChanged();
}

void Widget::ensureArticleLayoutForInlineField(int width) {
	if (!_article || width <= 0) {
		return;
	} else if (articleRelayoutDeferralActive()) {
		requestDeferredArticleRelayout();
		requestDeferredInlineFieldGeometry();
		return;
	}
	_articleHeight = _article->resizeGetHeight(articleWidth(width));
}

void Widget::syncArticleVisibleTopBottom() {
	if (!_article) {
		return;
	}
	const auto articleTop = articleTopLeft().y();
	_article->setVisibleTopBottom(
		_visibleRange.top - articleTop,
		_visibleRange.bottom - articleTop);
}

void Widget::syncInlineFieldGeometry(int width) {
	if (_field->isHidden() || width <= 0) {
		return;
	} else if (articleRelayoutDeferralActive()) {
		requestDeferredInlineFieldGeometry();
		return;
	}
	ensureArticleLayoutForInlineField(width);
	if (_activeSegmentIndex >= 0) {
		ensureInlineFieldForSegment(_activeSegmentIndex);
	}
	const auto segmentRect = fieldOuterRectForSegment(_activeSegmentIndex);
	if (segmentRect.isEmpty()) {
		_pendingOrdinal = _activeOrdinal;
		_pendingCursorOffset = _field->textCursor().position();
		hideInlineField();
		clearArticleEditableHeightOverride();
		_article->clearEditableMaxLineWidthOverride();
		const auto pendingOrdinal = _pendingOrdinal;
		ensureArticleLayoutForInlineField(width);
		if (_pendingOrdinal == pendingOrdinal && pendingOrdinal >= 0) {
			const auto segmentIndex = segmentIndexForEditableOrdinal(
				pendingOrdinal);
			if (segmentIndex >= 0
				&& !_article->logicalSegmentRect(segmentIndex).isEmpty()) {
				ensurePendingActivation();
			}
		}
		return;
	}
	const auto margins = _field->fullTextMargins();
	const auto left = segmentRect.x() - margins.left();
	const auto top = segmentRect.y() - margins.top();
	const auto fieldWidth = std::max(
		segmentRect.width() + margins.left() + margins.right(),
		1);
	_syncingInlineFieldGeometry = true;
	_field->resizeToWidth(fieldWidth);
	const auto fieldHeight = FieldNaturalHeight(_field.get());
	_field->setGeometryToLeft(left, top, fieldWidth, fieldHeight, width);
	_field->raise();
	_syncingInlineFieldGeometry = false;
	if (_pendingHeightOverrideUpdate) {
		_pendingHeightOverrideUpdate = false;
		updateInlineFieldHeightOverride();
	}
	refreshInlineFieldMaxLineWidthOverride();
}

void Widget::setStructuralSelection(
		Markdown::PreparedEditSelection selection,
		std::optional<BoundarySelectionOrigin> origin) {
	if (_structuralSelection.empty()
		&& !selection.empty()
		&& !_field->isHidden()) {
		const auto committed = recordMutationTransaction([&] {
			return commitInlineField();
		});
		if (committed != ApplyResult::Failed) {
			refreshAfterInlineFieldCommit(committed);
		}
	}
	_structuralSelection = std::move(selection);
	_boundarySelectionOrigin = std::move(origin);
	const auto keyboardSelection = !_structuralSelection.empty()
		&& _boundarySelectionOrigin.has_value();
	if (keyboardSelection && !_keyboardStructuralSelectionActive) {
		grabKeyboard();
		_keyboardStructuralSelectionActive = true;
	} else if (!keyboardSelection && _keyboardStructuralSelectionActive) {
		releaseKeyboard();
		_keyboardStructuralSelectionActive = false;
	}
	notifyToolbarStateChanged();
}

bool Widget::broaderSelectionHasSelectedText() const {
	const auto &nodes = _state->textNodes();
	const auto &page = _state->richPage();
	const auto hasTextSelection = !_selection.empty()
		&& _selectionEndpoints.from.valid()
		&& _selectionEndpoints.to.valid();
	const auto normalizedSelection = hasTextSelection
		? NormalizeSelection(_selection)
		: Markdown::MarkdownArticleSelection();
	for (auto ordinal = 0, count = int(nodes.size()); ordinal != count;
			++ordinal) {
		const auto &descriptor = nodes[ordinal];
		if (descriptor.mode != State::FieldMode::Rich) {
			continue;
		}
		const auto current = RichTextFromPath(page, descriptor.leaf);
		if (!current || current->text.text.isEmpty()) {
			continue;
		}
		const auto segmentIndex = segmentIndexForEditableOrdinal(ordinal);
		if (segmentIndex < 0
			|| (editableOrdinalForSegment(segmentIndex) != ordinal)) {
			continue;
		}
		const auto length = int(current->text.text.size());
		if (!_structuralSelection.empty()
			&& LeafSelectedStructurally(
				page,
				descriptor.leaf,
				_structuralSelection)
			&& (length > 0)) {
			return true;
		}
		if (!hasTextSelection
			|| (normalizedSelection.from.segment > segmentIndex)
			|| (normalizedSelection.to.segment < segmentIndex)) {
			continue;
		}
		auto from = 0;
		auto till = length;
		if (normalizedSelection.from.segment == segmentIndex) {
			from = normalizedSelection.from.offset;
		}
		if (normalizedSelection.to.segment == segmentIndex) {
			till = normalizedSelection.to.offset;
		}
		from = std::clamp(from, 0, length);
		till = std::clamp(till, from, length);
		if (from < till) {
			return true;
		}
	}
	return false;
}

std::vector<State::TextNodeSpan> Widget::broaderSelectionTextSpans() const {
	auto result = std::vector<State::TextNodeSpan>();
	const auto &nodes = _state->textNodes();
	const auto &page = _state->richPage();
	const auto hasTextSelection = !_selection.empty()
		&& _selectionEndpoints.from.valid()
		&& _selectionEndpoints.to.valid();
	const auto normalizedSelection = hasTextSelection
		? NormalizeSelection(_selection)
		: Markdown::MarkdownArticleSelection();
	result.reserve(nodes.size());
	for (auto ordinal = 0, count = int(nodes.size()); ordinal != count;
			++ordinal) {
		const auto &descriptor = nodes[ordinal];
		if (descriptor.mode != State::FieldMode::Rich) {
			continue;
		}
		const auto current = RichTextFromPath(page, descriptor.leaf);
		if (!current || current->text.text.isEmpty()) {
			continue;
		}
		const auto segmentIndex = segmentIndexForEditableOrdinal(ordinal);
		if (segmentIndex < 0
			|| (editableOrdinalForSegment(segmentIndex) != ordinal)) {
			continue;
		}
		const auto length = int(current->text.text.size());
		if (!_structuralSelection.empty()
			&& LeafSelectedStructurally(
				page,
				descriptor.leaf,
				_structuralSelection)) {
			result.push_back({
				.leaf = descriptor.leaf,
				.from = 0,
				.till = length,
			});
			continue;
		}
		if (!hasTextSelection
			|| (normalizedSelection.from.segment > segmentIndex)
			|| (normalizedSelection.to.segment < segmentIndex)) {
			continue;
		}
		auto from = 0;
		auto till = length;
		if (normalizedSelection.from.segment == segmentIndex) {
			from = normalizedSelection.from.offset;
		}
		if (normalizedSelection.to.segment == segmentIndex) {
			till = normalizedSelection.to.offset;
		}
		from = std::clamp(from, 0, length);
		till = std::clamp(till, from, length);
		if (from < till) {
			result.push_back({
				.leaf = descriptor.leaf,
				.from = from,
				.till = till,
			});
		}
	}
	return result;
}

std::vector<State::BlockPath> Widget::broaderSelectionMediaBlocks() const {
	auto result = std::vector<State::BlockPath>();
	if (_structuralSelection.empty()) {
		return result;
	}
	const auto &page = _state->richPage();
	EnumerateBlockPaths(
		page,
		StateBlockContainerPath(),
		[&](const StateBlockPath &path, const RichPage::Block &block) {
			if (BlockSelectedStructurally(path, _structuralSelection)
				&& MediaBlockSupportsSpoiler(block)) {
				result.push_back(path);
			}
		});
	return result;
}

void Widget::clearSelection() {
	const auto changed = !_selection.empty()
		|| _selectionEndpoints.from.valid()
		|| _selectionEndpoints.to.valid()
		|| hasStructuralSelection()
		|| _articleSelectionDrag.active;
	_selection = {};
	_selectionEndpoints = {};
	setStructuralSelection({});
	finishArticleSelection();
	if (changed) {
		update();
		notifyToolbarStateChanged();
	}
}

void Widget::clearTextSelection() {
	const auto changed = !_selection.empty()
		|| _selectionEndpoints.from.valid()
		|| _selectionEndpoints.to.valid()
		|| (_articleSelectionDrag.active
			&& _articleSelectionDrag.mode == DragSelectionMode::Text);
	_selection = {};
	_selectionEndpoints = {};
	if (_articleSelectionDrag.mode == DragSelectionMode::Text) {
		finishArticleSelection();
	} else {
		_articleSelectionDrag.textSegment = -1;
		_articleSelectionDrag.textOffset = 0;
	}
	if (changed) {
		update();
		notifyToolbarStateChanged();
	}
}

void Widget::clearStructuralSelection() {
	const auto changed = hasStructuralSelection()
		|| (_articleSelectionDrag.active
			&& _articleSelectionDrag.mode == DragSelectionMode::Structural);
	setStructuralSelection({});
	if (_articleSelectionDrag.mode == DragSelectionMode::Structural) {
		finishArticleSelection();
	}
	if (changed) {
		update();
		notifyToolbarStateChanged();
	}
}

bool Widget::hasStructuralSelection() const {
	return !_structuralSelection.empty();
}

void Widget::startArticleSelection(
		QPoint pressPoint,
		QPoint globalPressPoint,
		const Markdown::MarkdownArticleHitTestResult &hit,
		const PreparedEditHit &editHit,
		bool fromField,
		bool startedBelow) {
	const auto isTextHit = hit.valid()
		&& !hit.codeHeaderCopy
		&& hit.direct
		&& _article->segmentIsText(hit.segmentIndex);
	if (isTextHit) {
		clearStructuralSelection();
	} else {
		clearTextSelection();
		clearStructuralSelection();
	}
	_articleSelectionDrag = {
		.active = true,
		.fromField = fromField,
		.startedBelow = startedBelow,
		.codeHeader = hit.codeHeaderCopy,
		.pressPoint = pressPoint,
		.globalPressPoint = globalPressPoint,
		.anchorHit = editHit,
		.textSegment = -1,
		.textOffset = 0,
		.operation = ArticleSelectionOperation::GrowSelection,
		.mode = DragSelectionMode::None,
	};
	if (!isTextHit) {
		const auto mathFormulaHit = editHit.leaf
			&& (editHit.leaf->kind == PreparedEditLeafKind::MathFormula);
		if (editHit.valid()
			&& !mathFormulaHit
			&& !hit.codeHeaderCopy
			&& !startedBelow) {
			_articleSelectionDrag.mode = DragSelectionMode::Structural;
		}
		return;
	}
	const auto offset = _article->selectionOffsetFromHit(
		hit,
		TextSelectType::Letters);
	_articleSelectionDrag.textSegment = hit.segmentIndex;
	_articleSelectionDrag.textOffset = offset;
	_articleSelectionDrag.mode = DragSelectionMode::Text;
	_selection = {
		{ hit.segmentIndex, offset },
		{ hit.segmentIndex, offset },
	};
	_selectionEndpoints = {
		.from = MakeSelectionEndpoint(hit),
		.to = MakeSelectionEndpoint(hit),
	};
	update();
}

bool Widget::startSelectionDragFromExistingState(
		QPoint pressPoint,
		QPoint globalPressPoint,
		const PreparedEditHit &editHit,
		bool fromField) {
	auto drag = ArticleSelectionDrag{
		.active = true,
		.fromField = fromField,
		.startedBelow = false,
		.codeHeader = false,
		.pressPoint = pressPoint,
		.globalPressPoint = globalPressPoint,
		.anchorHit = editHit,
		.textSegment = -1,
		.textOffset = 0,
		.operation = ArticleSelectionOperation::DragSelection,
		.mode = DragSelectionMode::None,
	};
	if (fromField) {
		if (_settingField
			|| _field->isHidden()
			|| (_activeSegmentIndex < 0)
			|| (_state->activeFieldMode() != State::FieldMode::Rich)) {
			return false;
		}
		const auto sourceLeaf = _state->activeLeafPath();
		const auto preparedSource = _state->activePreparedLeafSource();
		if (!sourceLeaf || !preparedSource) {
			return false;
		}
		const auto full = ConvertEditorTagsToRichText(
			_field->getTextWithAppliedMarkdown());
		const auto cursor = _field->textCursor();
		if (!cursor.hasSelection()) {
			return false;
		}
		const auto length = int(full.text.size());
		auto from = richOffsetForFieldOffset(full, cursor.selectionStart());
		auto till = richOffsetForFieldOffset(full, cursor.selectionEnd());
		from = std::clamp(from, 0, length);
		till = std::clamp(till, from, length);
		if (from >= till) {
			return false;
		}
		drag.textSegment = _activeSegmentIndex;
		drag.textOffset = std::clamp(
			cursor.position(),
			0,
			int(_field->getLastText().size()));
		drag.mode = DragSelectionMode::Text;
		drag.inlineSource = TextNodeSpan{
			.leaf = *sourceLeaf,
			.from = from,
			.till = till,
		};
		drag.sourceLeaf = *preparedSource;
		drag.sourceSegment = _activeSegmentIndex;
		drag.sourceFrom = from;
		drag.sourceTo = till;
		_articleSelectionDrag = std::move(drag);
		return true;
	}
	if (!_structuralSelection.empty()
		&& ((_structuralSelection.kind == PreparedEditSelectionKind::Blocks)
			|| (_structuralSelection.kind
				== PreparedEditSelectionKind::ListItems))) {
		drag.mode = DragSelectionMode::Structural;
		drag.structuralSource = _structuralSelection;
		_articleSelectionDrag = std::move(drag);
		return true;
	}
	const auto selection = NormalizeSelection(_selection);
	if (selection.empty()
		|| !_selectionEndpoints.from.valid()
		|| !_selectionEndpoints.to.valid()
		|| (selection.from.segment != selection.to.segment)
		|| !_article->segmentIsText(selection.from.segment)
		|| !editHit.leaf) {
		return false;
	}
	const auto ordinal = editableOrdinalForSegment(selection.from.segment);
	const auto &nodes = _state->textNodes();
	if ((ordinal < 0) || (ordinal >= int(nodes.size()))) {
		return false;
	}
	drag.textSegment = selection.from.segment;
	drag.textOffset = selection.from.offset;
	drag.mode = DragSelectionMode::Text;
	drag.inlineSource = TextNodeSpan{
		.leaf = nodes[ordinal].leaf,
		.from = selection.from.offset,
		.till = selection.to.offset,
	};
	drag.sourceLeaf = *editHit.leaf;
	drag.sourceSegment = selection.from.segment;
	drag.sourceFrom = selection.from.offset;
	drag.sourceTo = selection.to.offset;
	_articleSelectionDrag = std::move(drag);
	return true;
}

void Widget::updateArticleSelection(
		QPoint articlePoint,
		const Markdown::MarkdownArticleHitTestResult &hit,
		const PreparedEditHit &editHit) {
	if (!_articleSelectionDrag.active
		|| (_articleSelectionDrag.operation
			!= ArticleSelectionOperation::GrowSelection)) {
		return;
	}
	const auto dragSegment = _articleSelectionDrag.textSegment;
	const auto originalMathFormulaHit = [&] {
		return _articleSelectionDrag.anchorHit.leaf
			&& (_articleSelectionDrag.anchorHit.leaf->kind
				== Markdown::PreparedEditLeafKind::MathFormula)
			&& editHit.leaf
			&& (*editHit.leaf == *_articleSelectionDrag.anchorHit.leaf);
	};
	const auto directOriginalTextHit = [&] {
		return (dragSegment >= 0)
			&& hit.valid()
			&& hit.direct
			&& (hit.segmentIndex == dragSegment)
			&& _article->segmentIsText(hit.segmentIndex);
	};
	const auto directOriginalEditableHit = [&] {
		return ((dragSegment >= 0)
			&& hit.valid()
			&& hit.direct
			&& (hit.segmentIndex == dragSegment)
			&& _article->segmentIsEditable(hit.segmentIndex))
			|| originalMathFormulaHit();
	};
	const auto updateTextSelection = [&](bool forceUpdate) {
		const auto offset = _article->selectionOffsetFromHit(
			hit,
			TextSelectType::Letters);
		const auto adjusted = _article->adjustSelection(
			dragSegment,
			TextSelection(
				uint16(std::clamp(
					std::min(_articleSelectionDrag.textOffset, offset),
					0,
					0xFFFF)),
				uint16(std::clamp(
					std::max(_articleSelectionDrag.textOffset, offset),
					0,
					0xFFFF))),
			TextSelectType::Letters);
		const auto selection = NormalizeSelection({
			{ dragSegment, adjusted.from },
			{ dragSegment, adjusted.to },
		});
		const auto endpoints = Markdown::MarkdownArticleSelectionEndpoints{
			.from = _selectionEndpoints.from.valid()
				? _selectionEndpoints.from
				: Markdown::MarkdownArticleSelectionEndpoint{
					dragSegment,
					false },
			.to = MakeSelectionEndpoint(hit),
		};
		const auto endpointsChanged
			= (_selectionEndpoints.from.segment != endpoints.from.segment)
			|| (_selectionEndpoints.from.direct != endpoints.from.direct)
			|| (_selectionEndpoints.to.segment != endpoints.to.segment)
			|| (_selectionEndpoints.to.direct != endpoints.to.direct);
		if (_selection != selection || endpointsChanged || forceUpdate) {
			_selection = selection;
			_selectionEndpoints = endpoints;
			update();
		} else {
			_selectionEndpoints = endpoints;
		}
	};
	const auto clearFieldSelection = [&] {
		if (!_articleSelectionDrag.fromField) {
			return;
		}
		auto cursor = _field->textCursor();
		if (!cursor.hasSelection()) {
			return;
		}
		cursor.clearSelection();
		_field->setTextCursor(cursor);
	};
	if (_articleSelectionDrag.mode == DragSelectionMode::Structural) {
		if (directOriginalTextHit()) {
			const auto forceUpdate = !_structuralSelection.empty();
			setStructuralSelection({});
			_articleSelectionDrag.mode = DragSelectionMode::Text;
			updateTextSelection(forceUpdate);
			return;
		}
		if (directOriginalEditableHit()) {
			const auto changed = !_structuralSelection.empty();
			setStructuralSelection({});
			_articleSelectionDrag.mode = DragSelectionMode::None;
			if (changed) {
				update();
			}
			return;
		}
		const auto selection = structuralSelectionFromHits(
			_articleSelectionDrag.anchorHit,
			editHit);
		if (_structuralSelection != selection) {
			setStructuralSelection(selection);
			update();
		}
		return;
	} else if (_articleSelectionDrag.mode == DragSelectionMode::None) {
		if (directOriginalEditableHit()) {
			return;
		}
		if (!editHit.valid()
			|| (_articleSelectionDrag.startedBelow
				&& articlePoint.y() >= _articleHeight)) {
			return;
		}
		_articleSelectionDrag.mode = DragSelectionMode::Structural;
		const auto selection = structuralSelectionFromHits(
			_articleSelectionDrag.anchorHit,
			editHit);
		if (_structuralSelection != selection) {
			setStructuralSelection(selection);
			update();
		}
		return;
	}
	if (_articleSelectionDrag.mode != DragSelectionMode::Text) {
		return;
	}
	if (directOriginalTextHit()) {
		updateTextSelection(false);
		return;
	}
	const auto selection = structuralSelectionFromHits(
		_articleSelectionDrag.anchorHit,
		editHit);
	const auto changed = !_selection.empty()
		|| _selectionEndpoints.from.valid()
		|| _selectionEndpoints.to.valid()
		|| (_structuralSelection != selection);
	_selection = {};
	_selectionEndpoints = {};
	setStructuralSelection(selection);
	_articleSelectionDrag.mode = DragSelectionMode::Structural;
	clearFieldSelection();
	if (changed) {
		update();
	}
}

void Widget::updateArticleDropTarget(QPoint articlePoint) {
	if (!_articleSelectionDrag.active
		|| (_articleSelectionDrag.operation
			!= ArticleSelectionOperation::DragSelection)) {
		clearArticleDropTarget();
		return;
	}
	const auto structuralSource = _articleSelectionDrag.structuralSource;
	auto location = (_articleSelectionDrag.mode == DragSelectionMode::Structural
			&& structuralSource)
		? _article->editStructuralDropTarget(articlePoint, *structuralSource)
		: _article->editDropTarget(articlePoint);
	auto supported = false;
	if (location.valid()) {
		const auto &target = *location.target;
		switch (_articleSelectionDrag.mode) {
		case DragSelectionMode::Structural:
			if (const auto source = structuralSource) {
				if (const auto block = std::get_if<PreparedEditBlockDropTarget>(
						&target)) {
					supported = (source->kind
						== PreparedEditSelectionKind::Blocks);
					if (supported
						&& (block->container == source->blocks.container)
						&& (block->insertIndex >= source->blocks.from)
						&& (block->insertIndex <= source->blocks.till)) {
						supported = false;
					} else if (supported
						&& PreparedContainerNestedInSelection(
							block->container,
							*source)) {
						supported = false;
					}
				} else if (const auto list
					= std::get_if<PreparedEditListItemDropTarget>(&target)) {
					supported = (source->kind
						== PreparedEditSelectionKind::ListItems);
					if (supported
						&& SamePreparedEditBlockPath(
							list->block,
							source->listItems.block)
						&& (list->insertIndex >= source->listItems.from)
						&& (list->insertIndex <= source->listItems.till)) {
						supported = false;
					} else if (supported
						&& PreparedBlockPathInSelection(
							list->block,
							*source)) {
						supported = false;
					}
				}
			}
			break;
		case DragSelectionMode::Text:
			if (const auto text = std::get_if<PreparedEditTextDropTarget>(
					&target)) {
				supported = (text->leaf.kind != PreparedEditLeafKind::MathFormula);
				if (supported
					&& _articleSelectionDrag.sourceLeaf
					&& (*_articleSelectionDrag.sourceLeaf == text->leaf)
					&& (text->offset >= _articleSelectionDrag.sourceFrom)
					&& (text->offset <= _articleSelectionDrag.sourceTo)) {
					supported = false;
				}
			} else {
				supported = std::holds_alternative<PreparedEditBlockDropTarget>(
					target);
			}
			break;
		case DragSelectionMode::None:
			break;
		}
	}
	if (!supported) {
		location = {};
	}
	const auto oldRect = _articleSelectionDrag.indicatorRect;
	_articleSelectionDrag.dropTarget = location.valid()
		? location.target
		: std::nullopt;
	_articleSelectionDrag.indicatorRect = location.valid()
		? location.indicatorRect
		: QRect();
	if (oldRect != _articleSelectionDrag.indicatorRect) {
		update();
	}
}

void Widget::clearArticleDropTarget() {
	const auto oldRect = _articleSelectionDrag.indicatorRect;
	_articleSelectionDrag.dropTarget = std::nullopt;
	_articleSelectionDrag.indicatorRect = QRect();
	if (!oldRect.isEmpty()) {
		update();
	}
}

void Widget::updateExternalDropTarget(QPoint articlePoint) {
	auto target = std::optional<Markdown::PreparedEditBlockDropTarget>();
	auto rect = QRect();
	const auto location = _article->editBlockDropTarget(articlePoint);
	if (location.valid()) {
		if (const auto block
			= std::get_if<Markdown::PreparedEditBlockDropTarget>(
				&*location.target)) {
			target = *block;
			rect = location.indicatorRect;
		}
	}
	const auto oldRect = _externalMediaDrag.indicatorRect;
	_externalMediaDrag.dropTarget = target;
	_externalMediaDrag.indicatorRect = rect;
	if (oldRect != rect) {
		update();
	}
}

void Widget::clearExternalDropTarget() {
	const auto oldRect = _externalMediaDrag.indicatorRect;
	_externalMediaDrag.dropTarget = std::nullopt;
	_externalMediaDrag.indicatorRect = QRect();
	if (!oldRect.isEmpty()) {
		update();
	}
}

void Widget::dragEnterEvent(QDragEnterEvent *e) {
	if (!_article || !IsAcceptableDropMedia(e->mimeData())) {
		clearExternalDropTarget();
		e->ignore();
		return;
	}
	updateExternalDropTarget(e->pos() - articleTopLeft());
	e->setDropAction(Qt::CopyAction);
	e->accept();
}

void Widget::dragMoveEvent(QDragMoveEvent *e) {
	if (!_article || !IsAcceptableDropMedia(e->mimeData())) {
		clearExternalDropTarget();
		e->ignore();
		return;
	}
	updateExternalDropTarget(e->pos() - articleTopLeft());
	e->setDropAction(Qt::CopyAction);
	e->accept();
}

void Widget::dragLeaveEvent(QDragLeaveEvent *e) {
	clearExternalDropTarget();
}

void Widget::dropEvent(QDropEvent *e) {
	const auto clear = gsl::finally([&] { clearExternalDropTarget(); });
	if (!_article
		|| !_applyPreparedMedia
		|| !IsAcceptableDropMedia(e->mimeData())) {
		return;
	}
	updateExternalDropTarget(e->pos() - articleTopLeft());
	const auto target = _externalMediaDrag.dropTarget;
	if (!target) {
		return;
	}
	auto list = PreparedMediaFromClipboard(e->mimeData(), _session->premium());
	if (!list) {
		return;
	}
	e->setDropAction(Qt::CopyAction);
	e->accept();
	auto paste = PreparedMediaPasteTarget{ .blockDrop = *target };
	crl::on_main(this, [=, list = std::move(*list)]() mutable {
		_applyPreparedMedia(
			not_null<Widget*>(this),
			std::move(list),
			std::move(paste));
	});
}

Ui::ElasticScroll *Widget::selectionScrollArea() const {
	for (auto parent = parentWidget(); parent; parent = parent->parentWidget()) {
		if (const auto scroll = dynamic_cast<Ui::ElasticScroll*>(parent)) {
			return scroll;
		}
	}
	return nullptr;
}

bool Widget::searchBlockedByLayer() const {
	const auto editorWindow = dynamic_cast<Window*>(window());
	return editorWindow && editorWindow->isLayerShown();
}

bool Widget::articleSelectionAutoScrollActive() const {
	return _articleSelectionDrag.active
		&& _articleSelectionDrag.dragStarted
		&& ((_articleSelectionDrag.operation
				== ArticleSelectionOperation::GrowSelection)
			|| (_articleSelectionDrag.operation
				== ArticleSelectionOperation::DragSelection));
}

void Widget::updateArticleSelectionAutoScroll(QPoint widgetPoint) {
	if (!articleSelectionAutoScrollActive() || !selectionScrollArea()) {
		_selectScroll.cancel();
		return;
	}
	_selectScroll.checkDeltaScroll(
		widgetPoint,
		_visibleRange.top,
		_visibleRange.bottom);
}

bool Widget::restoreFieldFromBoundaryOrigin() {
	if (!_boundarySelectionOrigin) {
		return false;
	}
	const auto expected = _boundarySelectionOrigin->leafSelection;
	const auto ordinal = _state->textOrdinalForLeafPath(expected.leaf);
	if (ordinal < 0) {
		return false;
	}
	activateTextOrdinal(
		ordinal,
		expected.anchorOffset,
		expected.cursorOffset,
		ActivateReveal::Reveal);
	const auto restored = [&]() {
		if (hasStructuralSelection()) {
			return false;
		}
		const auto viewState = captureHistoryViewState();
		return viewState.leafSelection
			&& (*viewState.leafSelection == expected);
	}();
	if (restored) {
		setStructuralSelection({});
		_boundarySelectionOrigin = std::nullopt;
	}
	return restored;
}

void Widget::revealStructuralSelectionEdge(bool forward) {
	if (!_article || _structuralSelection.empty()) {
		return;
	}
	const auto ordinal = StructuralSelectionEdgeTextOrdinal(
		*_state,
		_structuralSelection,
		forward);
	const auto scroll = selectionScrollArea();
	if (!ordinal || !scroll) {
		return;
	}
	const auto segmentIndex = segmentIndexForEditableOrdinal(*ordinal);
	if (segmentIndex < 0
		|| (editableOrdinalForSegment(segmentIndex) != *ordinal)) {
		return;
	}
	auto rect = _article->segmentRect(segmentIndex);
	if (!rect.isValid() || rect.isEmpty()) {
		return;
	}
	if (const auto inner = scroll->widget()) {
		rect.translate(articleTopLeft());
		rect.moveTopLeft(mapTo(inner, rect.topLeft()));
		scrollRangeToMakeVisible(scroll, rect.y(), rect.y() + rect.height());
	}
}

void Widget::updateArticleSelectionDragAtArticlePoint(
		QPoint articlePoint,
		const Markdown::MarkdownArticleHitTestResult &hit,
		const PreparedEditHit &editHit) {
	if (!_articleSelectionDrag.active
		|| !_articleSelectionDrag.dragStarted
		|| !_article) {
		return;
	}
	switch (_articleSelectionDrag.operation) {
	case ArticleSelectionOperation::GrowSelection:
		updateArticleSelection(articlePoint, hit, editHit);
		return;
	case ArticleSelectionOperation::DragSelection:
		updateArticleDropTarget(articlePoint);
		return;
	case ArticleSelectionOperation::None:
		_selectScroll.cancel();
		return;
	}
}

void Widget::updateArticleSelectionDragAtWidgetPoint(QPoint widgetPoint) {
	const auto articlePoint = widgetPoint - articleTopLeft();
	const auto hit = _article->hitTest(
		articlePoint,
		Ui::Text::StateRequest::Flag::LookupSymbol);
	const auto editHit = _article->editHitTest(articlePoint);
	updateArticleSelectionDragAtArticlePoint(articlePoint, hit, editHit);
}

void Widget::updateArticleSelectionDragFromCursor() {
	if (!articleSelectionAutoScrollActive()) {
		_selectScroll.cancel();
		return;
	}
	const auto widgetPoint = mapFromGlobal(QCursor::pos());
	updateArticleSelectionDragAtWidgetPoint(widgetPoint);
	updateArticleSelectionAutoScroll(widgetPoint);
}

void Widget::finishArticleSelection() {
	const auto repaint = !_articleSelectionDrag.indicatorRect.isEmpty();
	_selectScroll.cancel();
	_articleSelectionDrag = {};
	if (repaint) {
		update();
	}
}

bool Widget::applyStructuralSelectionDrop() {
	if (!_articleSelectionDrag.structuralSource
		|| !_articleSelectionDrag.dropTarget) {
		return false;
	}
	const auto clearOverlay = gsl::finally([&] {
		clearArticleDropTarget();
	});
	const auto selection = *_articleSelectionDrag.structuralSource;
	const auto target = *_articleSelectionDrag.dropTarget;
	auto applied = false;
	recordMutationTransaction([&] {
		const auto hadVisibleField = !_field->isHidden();
		const auto source = hadVisibleField
			? _state->activePreparedLeafSource()
			: std::optional<PreparedEditLeafSource>();
		auto committed = ApplyResult::Unchanged;
		if (hadVisibleField) {
			committed = commitInlineField();
			if (committed == ApplyResult::Failed) {
				return MutationTransactionResult{
					.committed = committed,
					.failed = true,
				};
			}
		}
		_pendingOrdinal = -1;
		_pendingCursorOffset = 0;
		hideInlineField();
		clearInlineFieldEditSession();
		const auto moved = _state->moveStructuralSelectionToDropTarget(
			selection,
			target);
		if (moved.result == ApplyResult::Failed) {
			showLastLimitToast();
			if (hadVisibleField) {
				refreshAfterInlineFieldCommit(committed, source);
			}
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		} else if (moved.result == ApplyResult::Unchanged) {
			if (hadVisibleField) {
				refreshAfterInlineFieldCommit(committed, source);
			}
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		applied = true;
		refreshPreparedContent();
		switch (moved.destination.action) {
		case State::BoundaryTarget::Action::StructuralSelection:
			_boundarySelectionOrigin = std::nullopt;
			_selection = {};
			_selectionEndpoints = {};
			setStructuralSelection(moved.destination.structuralSelection);
			update();
			break;
		case State::BoundaryTarget::Action::Text:
			activateTextOrdinal(moved.destination.textOrdinal, 0);
			break;
		case State::BoundaryTarget::Action::None:
		case State::BoundaryTarget::Action::RemoveActiveOwner: {
			const auto ordinal = _state->activeTextOrdinal();
			if (ordinal >= 0 && ordinal < _state->textNodeCount()) {
				activateTextOrdinal(ordinal, 0);
			} else {
				activateInitialNode();
			}
		} break;
		}
		return MutationTransactionResult{
			.committed = committed,
			.changed = true,
		};
	});
	return applied;
}

bool Widget::applyInlineSelectionDrop() {
	if (!_articleSelectionDrag.inlineSource
		|| !_articleSelectionDrag.dropTarget) {
		return false;
	}
	const auto clearOverlay = gsl::finally([&] {
		clearArticleDropTarget();
	});
	const auto target = *_articleSelectionDrag.dropTarget;
	auto applied = false;
	recordMutationTransaction([&] {
		const auto restoreField = !_field->isHidden();
		const auto restoreLeaf = restoreField
			? _fieldLeaf
			: std::optional<State::LeafPath>();
		const auto restoreStyleKey = restoreField
			? _activeFieldStyleKey
			: std::optional<InlineFieldStyleKey>();
		const auto restoreMode = _fieldMode;
		const auto restoreSelection = restoreField
			? captureHistoryViewState().leafSelection
			: std::optional<HistoryLeafSelection>();
		auto committed = ApplyResult::Unchanged;
		if (restoreField) {
			committed = commitInlineField();
			if (committed == ApplyResult::Failed) {
				return MutationTransactionResult{
					.committed = committed,
					.failed = true,
				};
			}
			_pendingOrdinal = -1;
			_pendingCursorOffset = 0;
			hideInlineField();
			clearInlineFieldEditSession(true);
		}
		auto restore = restoreField;
		const auto restoreInlineField = gsl::finally([&] {
			if (!restore) {
				return;
			}
			if (restoreLeaf && restoreStyleKey) {
				if (auto revived = reviveRetainedLeafField(
						_historyIndex,
						*restoreLeaf,
						restoreMode,
						*restoreStyleKey)) {
					_field = std::move(revived);
					_activeFieldStyleKey = restoreStyleKey;
					_fieldMode = restoreMode;
					_fieldLeaf = *restoreLeaf;
					refreshInlineFieldPlaceholder();
					_fieldUndoAvailable = _field->isUndoAvailable();
					_fieldRedoAvailable = _field->isRedoAvailable();
					clearFieldUndoRedoNoopState();
				}
			}
			if (!_fieldLeaf && restoreSelection) {
				const auto ordinal = _state->textOrdinalForLeafPath(
					restoreSelection->leaf);
				if (ordinal >= 0) {
					activateTextOrdinal(
						ordinal,
						restoreSelection->anchorOffset,
						restoreSelection->cursorOffset);
					return;
				}
			}
			_field->show();
			syncInlineFieldGeometry();
			updateInlineFieldHeightOverride();
			syncArticleVisibleTopBottom();
			revealActiveInlineField();
			_field->raise();
			_field->setFocusFast();
			notifyToolbarStateChanged();
		});
		const auto sourceSpans = _articleSelectionDrag.fromField
			? (_articleSelectionDrag.sourceLeaf
				? _state->resolveTextSpansForPreparedLeafRange(
					*_articleSelectionDrag.sourceLeaf,
					_articleSelectionDrag.sourceFrom,
					_articleSelectionDrag.sourceTo)
				: std::vector<TextNodeSpan>())
			: std::vector<TextNodeSpan>{ *_articleSelectionDrag.inlineSource };
		if (sourceSpans.empty()) {
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		const auto moved = _state->moveTextSelectionToDropTarget(
			sourceSpans,
			target);
		if (moved.result == ApplyResult::Failed) {
			showLastLimitToast();
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		} else if (moved.result == ApplyResult::Unchanged) {
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		restore = false;
		applied = true;
		refreshPreparedContent();
		const auto ordinal = moved.destinationLeaf
			? _state->textOrdinalForLeafPath(*moved.destinationLeaf)
			: _state->activeTextOrdinal();
		if (ordinal >= 0) {
			activateTextOrdinal(
				ordinal,
				moved.selectionFrom,
				moved.selectionTo);
		} else {
			activateInitialNode();
		}
		return MutationTransactionResult{
			.committed = committed,
			.changed = true,
		};
	});
	return applied;
}

bool Widget::handleStructuralSelectionKey(QKeyEvent *e) {
	if (!hasStructuralSelection()) {
		return false;
	}
	const auto key = e->key();
	if (key == Qt::Key_Escape) {
		clearStructuralSelection();
		e->accept();
		return true;
	}
	const auto modifiers = e->modifiers()
		& ~(Qt::KeypadModifier | Qt::GroupSwitchModifier);
	if (modifiers == Qt::ShiftModifier) {
		const auto vertical = (key == Qt::Key_Up)
			|| (key == Qt::Key_Down)
			|| (key == Qt::Key_PageUp)
			|| (key == Qt::Key_PageDown);
		if (!vertical) {
			return false;
		}
		if (adjustStructuralSelectionFromKeyboard(
				(key == Qt::Key_Down) || (key == Qt::Key_PageDown),
				(key == Qt::Key_PageUp) || (key == Qt::Key_PageDown))) {
			e->accept();
			return true;
		}
		e->accept();
		return true;
	}
	if (modifiers != Qt::NoModifier) {
		return false;
	}
	const auto forward = (key == Qt::Key_Delete);
	if (!forward && key != Qt::Key_Backspace) {
		return false;
	}
	if (_state->canRemoveStructuralSelection(_structuralSelection)) {
		removeStructuralSelectionAndReposition(forward);
	}
	e->accept();
	return true;
}

void Widget::removeStructuralSelectionAndReposition(bool forward) {
	const auto origin = [&]() -> std::optional<BoundarySelectionOrigin> {
		if (_boundarySelectionOrigin
			&& _boundarySelectionOrigin->forward == forward) {
			return _boundarySelectionOrigin;
		}
		return std::nullopt;
	}();
	const auto target = removeCurrentStructuralSelection(forward);
	if (hasStructuralSelection()) {
		return;
	}
	if (origin) {
		const auto ordinal = _state->textOrdinalForLeafPath(
			origin->leafSelection.leaf);
		if (ordinal >= 0 && ordinal < _state->textNodeCount()) {
			activateTextOrdinal(
				ordinal,
				origin->leafSelection.anchorOffset,
				origin->leafSelection.cursorOffset);
			return;
		}
	}
	if (target) {
		if (forward) {
			activateTextOrdinal(*target, 0);
		} else {
			activateTextOrdinalAtEnd(*target);
		}
	} else {
		activateInitialNode();
	}
}

std::optional<int> Widget::removeCurrentStructuralSelection(bool forward) {
	if (!hasStructuralSelection()) {
		return std::nullopt;
	}
	const auto selection = _structuralSelection;
	auto target = std::optional<int>();
	const auto result = recordMutationTransaction([&] {
		const auto committed = commitInlineField();
		if (committed == ApplyResult::Failed) {
			return MutationTransactionResult{
				.committed = committed,
				.failed = true,
			};
		}
		_pendingOrdinal = -1;
		_pendingCursorOffset = 0;
		hideInlineField();
		clearInlineFieldEditSession();
		target = _state->removeStructuralSelection(selection, forward);
		clearSelection();
		refreshPreparedContent();
		return MutationTransactionResult{
			.committed = committed,
			.changed = true,
		};
	});
	if (result.failed) {
		return std::nullopt;
	}
	return target;
}

bool Widget::handleFieldMouseEvent(QEvent *event) {
	if (!_article || _field->isHidden() || _activeSegmentIndex < 0) {
		return false;
	}
	const auto type = event->type();
	const auto mouse = static_cast<QMouseEvent*>(event);
	if (type == QEvent::MouseButtonPress) {
		if (mouse->button() != Qt::LeftButton) {
			return false;
		}
		const auto segmentRect = _article->segmentRect(_activeSegmentIndex);
		if (segmentRect.isEmpty()) {
			return false;
		}
		auto anchorHit = _article->editHitTest(segmentRect.center());
		if (!anchorHit.valid()) {
			anchorHit = _article->editHitTest(segmentRect.topLeft());
		}
		if (!anchorHit.valid()) {
			return false;
		}
		const auto globalPoint = mouse->globalPos();
		const auto widgetPoint = mapFromGlobal(globalPoint);
		const auto articlePoint = widgetPoint - articleTopLeft();
		const auto cursor = _field->textCursor();
		const auto raw = _field->rawTextEdit();
		const auto pressCursor = raw->cursorForPosition(
			raw->viewport()->mapFromGlobal(globalPoint));
		const auto pressingCurrentSelection
			= (_state->activeFieldMode() == State::FieldMode::Rich)
			&& cursor.hasSelection()
			&& (pressCursor.position() >= cursor.selectionStart())
			&& (pressCursor.position() < cursor.selectionEnd());
		_selectScroll.cancel();
		_trackingPointerPress = true;
		if (pressingCurrentSelection
			&& startSelectionDragFromExistingState(
				articlePoint,
				globalPoint,
				anchorHit,
				true)) {
			return false;
		}
		clearTextSelection();
		clearStructuralSelection();
		_articleSelectionDrag = {
			.active = true,
			.fromField = true,
			.startedBelow = false,
			.codeHeader = false,
			.pressPoint = articlePoint,
			.globalPressPoint = globalPoint,
			.anchorHit = anchorHit,
			.textSegment = _activeSegmentIndex,
			.textOffset = std::clamp(
				cursor.position(),
				0,
				int(_field->getLastText().size())),
			.operation = ArticleSelectionOperation::GrowSelection,
			.mode = DragSelectionMode::Text,
		};
		return false;
	} else if (!_articleSelectionDrag.active
		|| !_articleSelectionDrag.fromField) {
		return false;
	} else if (type == QEvent::MouseButtonRelease
		&& mouse->button() != Qt::LeftButton) {
		return false;
	} else if (type == QEvent::MouseMove
		&& !(mouse->buttons() & Qt::LeftButton)) {
		finishArticleSelection();
		_trackingPointerPress = false;
		return false;
	}

	const auto globalPoint = mouse->globalPos();
	const auto widgetPoint = mapFromGlobal(globalPoint);
	const auto articlePoint = widgetPoint - articleTopLeft();
	const auto operation = _articleSelectionDrag.operation;
	const auto movedFarEnough = (globalPoint
		- _articleSelectionDrag.globalPressPoint).manhattanLength()
		>= QApplication::startDragDistance();
	if (type == QEvent::MouseMove && !_articleSelectionDrag.dragStarted) {
		if (!movedFarEnough) {
			_selectScroll.cancel();
			return false;
		}
		_articleSelectionDrag.dragStarted = true;
	}
	const auto clickLike = (type == QEvent::MouseButtonRelease)
		&& !_articleSelectionDrag.dragStarted
		&& !movedFarEnough;
	const auto hit = _article->hitTest(
		articlePoint,
		Ui::Text::StateRequest::Flag::LookupSymbol);
	const auto editHit = _article->editHitTest(articlePoint);
	const auto insideActiveField = _field->rect().contains(
		_field->mapFromGlobal(globalPoint));
	const auto originalSegmentHit = hit.valid()
		&& hit.direct
		&& (hit.segmentIndex == _articleSelectionDrag.textSegment)
		&& _article->segmentIsEditable(hit.segmentIndex);
	const auto originalMathFormulaHit = _articleSelectionDrag.anchorHit.leaf
		&& (_articleSelectionDrag.anchorHit.leaf->kind
			== Markdown::PreparedEditLeafKind::MathFormula)
		&& editHit.leaf
		&& (*editHit.leaf == *_articleSelectionDrag.anchorHit.leaf);
	const auto clearArticleSelection = [&] {
		const auto changed = !_selection.empty()
			|| _selectionEndpoints.from.valid()
			|| _selectionEndpoints.to.valid()
			|| hasStructuralSelection();
		_selection = {};
		_selectionEndpoints = {};
		setStructuralSelection({});
		if (changed) {
			update();
		}
	};
	if (insideActiveField || originalSegmentHit || originalMathFormulaHit) {
		if ((operation == ArticleSelectionOperation::GrowSelection)
			&& (_articleSelectionDrag.mode == DragSelectionMode::Structural)) {
			clearArticleSelection();
			_articleSelectionDrag.mode = DragSelectionMode::Text;
		}
		if (type == QEvent::MouseButtonRelease) {
			clearArticleDropTarget();
			finishArticleSelection();
			_trackingPointerPress = false;
		} else {
			_selectScroll.cancel();
		}
		return false;
	}

	if (clickLike) {
		clearArticleDropTarget();
		finishArticleSelection();
		_trackingPointerPress = false;
		return false;
	}
	updateArticleSelectionDragAtArticlePoint(articlePoint, hit, editHit);
	updateArticleSelectionAutoScroll(widgetPoint);
	if (type == QEvent::MouseButtonRelease) {
		if (operation == ArticleSelectionOperation::DragSelection) {
			if (_articleSelectionDrag.dropTarget) {
				if (_articleSelectionDrag.mode == DragSelectionMode::Structural) {
					static_cast<void>(applyStructuralSelectionDrop());
				} else if (_articleSelectionDrag.mode
					== DragSelectionMode::Text) {
					static_cast<void>(applyInlineSelectionDrop());
				}
			}
			clearArticleDropTarget();
			finishArticleSelection();
			_trackingPointerPress = false;
			mouse->accept();
			return true;
		}
		if (hasStructuralSelection()) {
			const auto committed = recordMutationTransaction([&] {
				return commitInlineField();
			});
			if (committed == ApplyResult::Failed) {
				mouse->accept();
				return true;
			}
			_pendingOrdinal = -1;
			_pendingCursorOffset = 0;
			hideInlineField();
			clearInlineFieldEditSession();
			refreshAfterInlineFieldCommit(committed);
			finishArticleSelection();
			_trackingPointerPress = false;
			mouse->accept();
			return true;
		}
		finishArticleSelection();
		_trackingPointerPress = false;
		return false;
	}
	if ((operation == ArticleSelectionOperation::DragSelection)
		|| (_articleSelectionDrag.mode == DragSelectionMode::Structural)) {
		mouse->accept();
		return true;
	}
	return false;
}

PreparedEditSelection Widget::structuralSelectionFromHits(
		const PreparedEditHit &anchor,
		const PreparedEditHit &focus) const {
	const auto anchorOwner = StructuralOwnerFromHit(anchor);
	const auto focusOwner = StructuralOwnerFromHit(focus);
	if (!anchorOwner.valid() || !focusOwner.valid()) {
		return {};
	}
	const auto anchorCell = TableCellFromOwner(anchorOwner);
	const auto focusCell = TableCellFromOwner(focusOwner);
	if (anchorCell && focusCell) {
		const auto range = TableRangesUnion(
			TableRangeFromCell(*anchorCell),
			TableRangeFromCell(*focusCell));
		if (!range.empty()
			&& TableRangeContainsCell(range, *anchorCell)
			&& TableRangeContainsCell(range, *focusCell)) {
			return {
				.kind = PreparedEditSelectionKind::TableCells,
				.tableCells = range,
			};
		}
	}
	const auto anchorRow = TableRowFromOwner(anchorOwner);
	const auto focusRow = TableRowFromOwner(focusOwner);
	if (anchorRow
		&& focusRow
		&& SamePreparedEditBlockPath(anchorRow->block, focusRow->block)) {
		const auto range = NormalizeIntegerRange(
			anchorRow->tableRowIndex,
			focusRow->tableRowIndex);
		if (!range.empty()) {
			return {
				.kind = PreparedEditSelectionKind::TableRows,
				.tableRows = {
					.block = anchorRow->block,
					.from = range.from,
					.till = range.till,
				},
			};
		}
	}
	const auto anchorListItem = ListItemFromOwner(anchorOwner);
	const auto focusListItem = ListItemFromOwner(focusOwner);
	if (anchorListItem
		&& focusListItem
		&& SamePreparedEditBlockPath(
			anchorListItem->block,
			focusListItem->block)) {
		const auto range = NormalizeIntegerRange(
			anchorListItem->listItemIndex,
			focusListItem->listItemIndex);
		if (!range.empty()) {
			return {
				.kind = PreparedEditSelectionKind::ListItems,
				.listItems = {
					.block = anchorListItem->block,
					.from = range.from,
					.till = range.till,
				},
			};
		}
	}
	const auto anchorBlock = BlockPathFromOwner(anchorOwner);
	const auto focusBlock = BlockPathFromOwner(focusOwner);
	if (!anchorBlock || !focusBlock) {
		return {};
	}
	if (ComparePreparedEditBlockContainerPaths(
			anchorBlock->container,
			focusBlock->container) == 0) {
		const auto blockSelection = BlockSelectionFromIndexes(
			anchorBlock->container,
			anchorBlock->index,
			focusBlock->index);
		if (!blockSelection.empty()) {
			return blockSelection;
		}
	}
	const auto listItemsFromChildren = ListItemSelectionFromSources(
		ListItemSourcesFromOwner(anchorOwner, anchorBlock),
		ListItemSourcesFromOwner(focusOwner, focusBlock));
	const auto liftedBlockSelection = LiftedBlockSelection(
		*anchorBlock,
		*focusBlock);
	if (IsBlockOwner(anchorOwner)
		&& IsBlockOwner(focusOwner)
		&& !liftedBlockSelection.empty()
		&& !IsMultiListItemSelection(listItemsFromChildren)) {
		return liftedBlockSelection;
	}
	if (!listItemsFromChildren.empty()) {
		return listItemsFromChildren;
	}
	if (!liftedBlockSelection.empty()) {
		return liftedBlockSelection;
	}
	return {};
}

int Widget::editableOrdinalForSegment(int segmentIndex) const {
	if (const auto source = _article->editableLeafForSegment(segmentIndex)) {
		const auto ordinal = _state->textOrdinalForLeaf(*source);
		if (ordinal >= 0) {
			return ordinal;
		}
	}
	return _article->editableIndexForSegment(segmentIndex);
}

int Widget::segmentIndexForEditableOrdinal(int ordinal) const {
	if (const auto source = _state->preparedLeafSourceForOrdinal(ordinal)) {
		return _article->segmentIndexForEditableLeaf(*source);
	}
	return _article->segmentIndexForEditableIndex(ordinal);
}

style::margins Widget::effectiveBodyPadding() const {
	const auto base = EditorBodyPadding();
	return style::margins(
		base.left(),
		base.top() + _topContentPadding + _searchSlideHeight.current(),
		base.right(),
		base.bottom() + _bottomContentPadding);
}

QPoint Widget::articleTopLeft() const {
	const auto padding = effectiveBodyPadding();
	const auto outerWidth = std::max(widthNoMargins(), 1);
	const auto available = std::max(
		outerWidth - padding.left() - padding.right(),
		1);
	const auto bodyWidth = articleWidth(outerWidth);
	return {
		padding.left() + std::max((available - bodyWidth) / 2, 0),
		padding.top()
	};
}

int Widget::articleWidth(int outerWidth) const {
	const auto padding = EditorBodyPadding();
	const auto available = std::max(
		outerWidth - padding.left() - padding.right(),
		1);
	const auto maxWidth = (_contentMaxWidth > 0)
		? _contentMaxWidth
		: (_article ? _article->maxWidth() : available);
	return std::min(available, maxWidth);
}

Widget::ArticleColumn Widget::articleColumnForWidth(int outerWidth) const {
	const auto padding = EditorBodyPadding();
	const auto available = std::max(
		outerWidth - padding.left() - padding.right(),
		1);
	const auto width = articleWidth(outerWidth);
	const auto left = padding.left() + std::max((available - width) / 2, 0);
	return { left, width };
}

QRect Widget::outerEditableSegmentRect(int segmentIndex) const {
	const auto rect = _article->logicalSegmentRect(segmentIndex);
	return rect.isEmpty() ? rect : rect.translated(articleTopLeft());
}

QRect Widget::fieldOuterRectForSegment(int segmentIndex) const {
	if (!_article || segmentIndex < 0) {
		return QRect();
	}
	if (!_activeSegmentIsDisplayMath) {
		return outerEditableSegmentRect(segmentIndex);
	}
	const auto rect = _article->displayMathEditRect(segmentIndex);
	return rect.isEmpty() ? rect : rect.translated(articleTopLeft());
}

Markdown::MarkdownArticlePaintContext Widget::textPaintContext(QRect clip) {
	const auto logicalRect = QRect(QPoint(), QSize(
		articleWidth(std::max(widthNoMargins(), 1)),
		std::max(_articleHeight, 1)));
	auto context = Markdown::MarkdownArticlePaintContext(
		_theme->preparePaintContext(
			_style.get(),
			logicalRect,
			logicalRect,
			clip,
			window() ? !window()->isActiveWindow() : false));
	const auto messageStyle = context.messageStyle();
	context.caches = {
		.pre = messageStyle->preCache.get(),
		.blockquote = context.quoteCache({}, 0),
		.colors = _highlightColors,
		.st = &messageStyle->richPageStyle,
		.repaint = [=] {
			crl::on_main(this, [=] {
				update();
			});
		},
		.repaintRect = [=](QRect rect) {
			crl::on_main(this, [=] {
				if (rect.isEmpty()) {
					update();
				} else {
					update(rect.translated(articleTopLeft()));
				}
			});
		},
	};
	const auto hiddenSegmentIndex = _field->isHidden()
		? -1
		: _activeSegmentIndex;
	context.hiddenTextSegmentIndex = hiddenSegmentIndex;
	context.hiddenSegmentIndex = hiddenSegmentIndex;
	context.selectionState.selection = _selection;
	context.selectionState.endpoints = &_selectionEndpoints;
	if (!_structuralSelection.empty()) {
		context.selectionState.structuralSelection = &_structuralSelection;
	}
	return context;
}

} // namespace Iv::Editor
