/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/editor/iv_editor_page_blocks.h"

#include "iv/editor/iv_editor_text_entities.h"
#include "iv/markdown/iv_markdown_prepare_serialize.h"
#include "ui/text/text_entity.h"

#include <algorithm>

namespace Iv::Editor {
namespace {

using Block = RichPage::Block;
using BlockKind = RichPage::BlockKind;
using ListItem = RichPage::ListItem;
using ListKind = RichPage::ListKind;
using RichText = RichPage::RichText;
using TaskState = RichPage::TaskState;
using TableCell = RichPage::TableCell;

bool StripWrapperEntityInEditMode(EntityType type) {
	switch (type) {
	case EntityType::Url:
	case EntityType::Email:
	case EntityType::Hashtag:
	case EntityType::Cashtag:
	case EntityType::Mention:
	case EntityType::BotCommand:
	case EntityType::Phone:
	case EntityType::BankCard:
		return true;
	default:
		return false;
	}
}

bool ButtonTypeIsUserSendable(HistoryMessageMarkupButton::Type type) {
	using Type = HistoryMessageMarkupButton::Type;
	switch (type) {
	case Type::Url:
	case Type::UserProfile:
	case Type::CopyText:
		return true;
	}
	return false;
}

bool DegradeEditModeButton(HistoryMessageMarkupButton &button) {
	using Type = HistoryMessageMarkupButton::Type;
	if ((button.type == Type::Disabled)
		|| ButtonTypeIsUserSendable(button.type)) {
		return false;
	}
	button = HistoryMessageMarkupButton(
		Type::Disabled,
		button.text,
		button.visual);
	return true;
}

} // namespace

[[nodiscard]] bool BlockCanOwnChildContainer(const Block &block) {
	return (block.kind == BlockKind::Quote)
		|| (block.kind == BlockKind::Details);
}

[[nodiscard]] bool BlockSupportsBlockText(const Block &block) {
	switch (block.kind) {
	case BlockKind::Heading:
	case BlockKind::Paragraph:
	case BlockKind::Footer:
	case BlockKind::Code:
	case BlockKind::Quote:
	case BlockKind::Table:
	case BlockKind::Details:
		return true;
	default:
		return false;
	}
}

[[nodiscard]] bool BlockSupportsBlockCaption(const Block &block) {
	switch (block.kind) {
	case BlockKind::Quote:
	case BlockKind::Photo:
	case BlockKind::Video:
	case BlockKind::Audio:
	case BlockKind::File:
	case BlockKind::Map:
	case BlockKind::GroupedMedia:
		return true;
	default:
		return false;
	}
}

[[nodiscard]] bool StringIsEmpty(const QString &text) {
	return text.trimmed().isEmpty();
}

[[nodiscard]] bool RichTextHasVisibleText(const RichText &text) {
	return !StringIsEmpty(text.text.text);
}

void MergeRichTextAnchors(RichText *target, RichText source) {
	if (!target) {
		return;
	}
	if (!source.anchorId.isEmpty()) {
		if (target->anchorId.isEmpty()) {
			target->anchorId = std::move(source.anchorId);
		} else {
			target->anchorIds.push_back(std::move(source.anchorId));
		}
	}
	for (auto &anchorId : source.anchorIds) {
		if (!anchorId.isEmpty()) {
			target->anchorIds.push_back(std::move(anchorId));
		}
	}
}

[[nodiscard]] bool JoinableTextBlockKind(BlockKind kind) {
	return (kind == BlockKind::Heading)
		|| (kind == BlockKind::Paragraph)
		|| (kind == BlockKind::Footer);
}

[[nodiscard]] int AppendRichTextSeam(RichText *destination, Block &&source) {
	auto updated = std::move(destination->text);
	const auto seamOffset = int(updated.text.size());
	updated.append(std::move(source.text.text));
	destination->text = std::move(updated);
	MergeRichTextAnchors(destination, std::move(source.text));
	if (!source.anchorId.isEmpty()) {
		auto anchor = RichText();
		anchor.anchorId = std::move(source.anchorId);
		MergeRichTextAnchors(destination, std::move(anchor));
	}
	return seamOffset;
}

[[nodiscard]] int AppendParagraphSeam(Block *destination, Block &&source) {
	return AppendRichTextSeam(&destination->text, std::move(source));
}

[[nodiscard]] bool CanEditBlock(const Block &block) {
	switch (block.kind) {
	case BlockKind::Heading:
	case BlockKind::Paragraph:
	case BlockKind::Footer:
	case BlockKind::Code:
	case BlockKind::Divider:
	case BlockKind::Anchor:
	case BlockKind::ButtonRow:
	case BlockKind::GroupedMedia:
	case BlockKind::Photo:
	case BlockKind::Video:
	case BlockKind::Audio:
	case BlockKind::File:
	case BlockKind::Math:
	case BlockKind::Table:
	case BlockKind::Map:
		return true;
	case BlockKind::Quote:
	case BlockKind::Details:
		return CanEditBlocks(block.blocks);
	case BlockKind::List:
		return ranges::all_of(block.listItems, [](const ListItem &item) {
			return CanEditBlocks(item.blocks);
		});
	case BlockKind::Unsupported:
	case BlockKind::Thinking:
	case BlockKind::AuthorDate:
	case BlockKind::Embed:
	case BlockKind::EmbedPost:
	case BlockKind::Channel:
	case BlockKind::RelatedArticles:
		return false;
	}
	return false;
}

[[nodiscard]] bool CanEditBlocks(const std::vector<Block> &blocks) {
	return ranges::all_of(blocks, &CanEditBlock);
}

bool BlockConversionExpandsToActiveLine(InsertBlockType type) {
	switch (type) {
	case InsertBlockType::Heading:
	case InsertBlockType::Blockquote:
	case InsertBlockType::Pullquote:
	case InsertBlockType::Code:
	case InsertBlockType::Footer:
	case InsertBlockType::OrderedList:
	case InsertBlockType::BulletList:
	case InsertBlockType::TaskList:
		return true;
	default:
		return false;
	}
}

TextWithEntities MakeText(QString text) {
	auto result = TextWithEntities();
	result.text = std::move(text);
	return result;
}

Block MakeParagraphBlock() {
	auto block = Block();
	block.kind = BlockKind::Paragraph;
	return block;
}

Block MakeFooterBlock() {
	auto block = Block();
	block.kind = BlockKind::Footer;
	return block;
}

Block MakeHeadingBlock(int level) {
	auto block = Block();
	block.kind = BlockKind::Heading;
	block.headingLevel = std::clamp(level, 1, 6);
	return block;
}

Block MakeQuoteBlock(bool pullquote) {
	auto block = Block();
	block.kind = BlockKind::Quote;
	block.pullquote = pullquote;
	return block;
}

Block MakeCodeBlock() {
	auto block = Block();
	block.kind = BlockKind::Code;
	return block;
}

Block MakeMathBlock() {
	auto block = Block();
	block.kind = BlockKind::Math;
	return block;
}

Block MakeDividerBlock() {
	auto block = Block();
	block.kind = BlockKind::Divider;
	return block;
}

Block MakeAnchorBlock(QString anchorId) {
	auto block = Block();
	block.kind = BlockKind::Anchor;
	block.anchorId = std::move(anchorId);
	return block;
}

Block MakeListBlock(ListKind kind, TaskState taskState) {
	auto block = Block();
	block.kind = BlockKind::List;
	block.listKind = kind;
	auto item = ListItem();
	item.taskState = taskState;
	block.listItems.push_back(std::move(item));
	if (kind != ListKind::Ordered) {
		block.orderedList = {};
	}
	return block;
}

ListItem MakeParagraphListItem(TaskState taskState) {
	auto item = ListItem();
	item.taskState = taskState;
	item.number = {};
	item.blocks.push_back(MakeParagraphBlock());
	return item;
}

Block MakeDetailsBlock() {
	auto block = Block();
	block.kind = BlockKind::Details;
	block.blocks.push_back(MakeParagraphBlock());
	return block;
}

Block MakeTableBlock() {
	auto block = Block();
	block.kind = BlockKind::Table;
	block.bordered = true;
	block.tableRows.reserve(3);
	for (auto rowIndex = 0; rowIndex != 3; ++rowIndex) {
		auto row = RichPage::TableRow();
		row.cells.reserve(3);
		for (auto cellIndex = 0; cellIndex != 3; ++cellIndex) {
			auto cell = TableCell();
			cell.header = (rowIndex == 0);
			row.cells.push_back(std::move(cell));
		}
		block.tableRows.push_back(std::move(row));
	}
	return block;
}

Block MakeMediaBlock(BlockKind kind) {
	auto block = Block();
	block.kind = kind;
	return block;
}

Block MakeMapBlock(double latitude, double longitude) {
	auto block = Block();
	block.kind = BlockKind::Map;
	block.latitude = latitude;
	block.longitude = longitude;
	return block;
}

bool RichTextIsEmpty(const RichText &text) {
	return StringIsEmpty(text.text.text)
		&& text.anchorId.isEmpty()
		&& text.anchorIds.empty();
}

bool ListItemIsBlankLine(const ListItem &item) {
	if (!RichTextIsEmpty(item.text) || !item.anchorId.isEmpty()) {
		return false;
	}
	for (const auto &block : item.blocks) {
		if ((block.kind != BlockKind::Paragraph) || !BlockIsEmpty(block)) {
			return false;
		}
	}
	return true;
}

bool ListItemIsEmpty(const ListItem &item) {
	if (!RichTextIsEmpty(item.text) || !item.anchorId.isEmpty()) {
		return false;
	}
	for (const auto &block : item.blocks) {
		if (!BlockIsEmpty(block)) {
			return false;
		}
	}
	return true;
}

bool BlockIsEmpty(const Block &block) {
	if (!RichTextIsEmpty(block.text)
		|| !RichTextIsEmpty(block.caption)
		|| !StringIsEmpty(block.formula)
		|| !block.language.isEmpty()
		|| !block.anchorId.isEmpty()
		|| !block.url.isEmpty()
		|| !block.html.isEmpty()
		|| !block.author.isEmpty()
		|| !block.username.isEmpty()
		|| !block.channelTitle.isEmpty()
		|| !block.audioTitle.isEmpty()
		|| !block.audioPerformer.isEmpty()
		|| !block.fileName.isEmpty()
		|| block.photo
		|| block.document
		|| block.peer
		|| block.photoId
		|| block.documentId
		|| block.channelId
		|| block.accessHash
		|| block.latitude != 0.
		|| block.longitude != 0.
		|| !block.mediaItems.empty()
		|| !block.buttons.empty()) {
		return false;
	}
	for (const auto &child : block.blocks) {
		if (!BlockIsEmpty(child)) {
			return false;
		}
	}
	for (const auto &item : block.listItems) {
		if (!ListItemIsEmpty(item)) {
			return false;
		}
	}
	for (const auto &row : block.tableRows) {
		for (const auto &cell : row.cells) {
			if (!RichTextIsEmpty(cell.text)) {
				return false;
			}
		}
	}
	return true;
}

TextWithEntities StripEditModeWrapperEntities(TextWithEntities text) {
	auto filtered = EntitiesInText();
	filtered.reserve(text.entities.size());
	for (const auto &entity : text.entities) {
		if (!StripWrapperEntityInEditMode(entity.type())) {
			filtered.push_back(entity);
		}
	}
	text.entities = std::move(filtered);
	return text;
}

void StripEditModeWrapperEntities(RichPage::RichText &text) {
	const auto strip = ranges::any_of(
		text.text.entities,
		[](const EntityInText &entity) {
			return StripWrapperEntityInEditMode(entity.type());
		});
	if (strip) {
		text.text = StripEditModeWrapperEntities(std::move(text.text));
	}
}

void StripEditModeWrapperEntities(
		std::vector<RichPage::Block> &blocks) {
	for (auto &block : blocks) {
		StripEditModeWrapperEntities(block.text);
		StripEditModeWrapperEntities(block.caption);
		StripEditModeWrapperEntities(block.blocks);
		for (auto &item : block.listItems) {
			StripEditModeWrapperEntities(item.text);
			StripEditModeWrapperEntities(item.blocks);
		}
		for (auto &row : block.tableRows) {
			for (auto &cell : row.cells) {
				StripEditModeWrapperEntities(cell.text);
			}
		}
	}
}

bool DegradeBlockOnlyEntities(TextWithEntities &text) {
	// RichText keeps inline monospace, but has no code block and no quote.
	auto result = false;
	auto entities = EntitiesInText();
	entities.reserve(text.entities.size());
	for (auto &entity : text.entities) {
		const auto type = entity.type();
		if (type == EntityType::Pre) {
			entities.push_back({
				EntityType::Code,
				entity.offset(),
				entity.length(),
			});
			result = true;
		} else if (type == EntityType::Blockquote) {
			result = true;
		} else {
			entities.push_back(std::move(entity));
		}
	}
	text.entities = std::move(entities);
	return result;
}

bool DegradeEditModeInlineButtons(TextWithEntities &text) {
	using Type = HistoryMessageMarkupButton::Type;
	auto result = false;
	if (Markdown::TextHasInlineLinkButton(text)) {
		Markdown::ExpandInlineLinkButtons(
			&text,
			Markdown::RichButtonLabelDates::Keep,
			nullptr);
		result = true;
	}
	for (auto &entity : text.entities) {
		if (entity.type() != EntityType::CustomEmoji) {
			continue;
		}
		auto parsed = Markdown::ParseInlineTextObjectEntity(entity.data());
		if (!parsed) {
			continue;
		}
		const auto button = std::get_if<
			Markdown::InlineTextObjectButtonData>(&parsed->data);
		if (!button
			|| (button->type == Type::Disabled)
			|| ButtonTypeIsUserSendable(button->type)) {
			continue;
		}
		*button = Markdown::InlineTextObjectButtonData{
			.label = std::move(button->label),
			.type = Type::Disabled,
			.color = button->color,
		};
		entity = EntityInText(
			EntityType::CustomEmoji,
			entity.offset(),
			entity.length(),
			Markdown::SerializeInlineTextObjectEntity(*parsed));
		result = true;
	}
	return result;
}

bool DegradeEditModeButtons(std::vector<RichPage::Block> &blocks) {
	auto result = false;
	for (auto &block : blocks) {
		result |= DegradeEditModeInlineButtons(block.text.text);
		result |= DegradeEditModeInlineButtons(block.caption.text);
		result |= DegradeEditModeButtons(block.blocks);
		for (auto &item : block.listItems) {
			result |= DegradeEditModeInlineButtons(item.text.text);
			result |= DegradeEditModeButtons(item.blocks);
		}
		for (auto &row : block.tableRows) {
			for (auto &cell : row.cells) {
				result |= DegradeEditModeInlineButtons(cell.text.text);
			}
		}
		for (auto &button : block.buttons) {
			result |= DegradeEditModeButton(button.button);
		}
	}
	return result;
}

bool CanEditRichPage(const RichPage &page) {
	return CanEditBlocks(page.blocks);
}

bool CanEditRichPage(const std::shared_ptr<const RichPage> &page) {
	return page && CanEditRichPage(*page);
}

} // namespace Iv::Editor
