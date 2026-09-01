/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/iv_rich_page.h"

#include <memory>
#include <vector>

namespace Iv::Editor {

enum class InsertBlockType : uchar {
	Heading,
	Blockquote,
	Code,
	Math,
	Footer,
	Divider,
	Anchor,
	OrderedList,
	BulletList,
	TaskList,
	Pullquote,
	Photo,
	Video,
	Audio,
	Details,
	Table,
	Map,
};

struct InsertAction {
	InsertBlockType type;
	int headingLevel = 1;
	int orderedStart = 1;
	bool orderedStartExplicit = false;
	bool taskChecked = false;
	QString codeLanguage;
	double latitude = 0.;
	double longitude = 0.;
};

[[nodiscard]] bool BlockCanOwnChildContainer(const RichPage::Block &block);

[[nodiscard]] bool BlockSupportsBlockText(const RichPage::Block &block);

[[nodiscard]] bool BlockSupportsBlockCaption(const RichPage::Block &block);

[[nodiscard]] bool StringIsEmpty(const QString &text);

[[nodiscard]] bool RichTextHasVisibleText(const RichPage::RichText &text);

void MergeRichTextAnchors(
	RichPage::RichText *target,
	RichPage::RichText source);

[[nodiscard]] bool JoinableTextBlockKind(RichPage::BlockKind kind);

[[nodiscard]] int AppendRichTextSeam(
	RichPage::RichText *destination,
	RichPage::Block &&source);

[[nodiscard]] int AppendParagraphSeam(
	RichPage::Block *destination,
	RichPage::Block &&source);

[[nodiscard]] bool CanEditBlock(const RichPage::Block &block);

[[nodiscard]] bool CanEditBlocks(const std::vector<RichPage::Block> &blocks);

[[nodiscard]] bool BlockConversionExpandsToActiveLine(InsertBlockType type);

[[nodiscard]] TextWithEntities MakeText(QString text);

RichPage::Block MakeParagraphBlock();

RichPage::Block MakeFooterBlock();

RichPage::Block MakeHeadingBlock(int level);

RichPage::Block MakeQuoteBlock(bool pullquote);

RichPage::Block MakeCodeBlock();

RichPage::Block MakeMathBlock();

RichPage::Block MakeDividerBlock();

RichPage::Block MakeAnchorBlock(QString anchorId);

[[nodiscard]] RichPage::Block MakeListBlock(
	RichPage::ListKind kind,
	RichPage::TaskState taskState = RichPage::TaskState::None);

RichPage::ListItem MakeParagraphListItem(RichPage::TaskState taskState);

RichPage::Block MakeDetailsBlock();

RichPage::Block MakeTableBlock();

RichPage::Block MakeMediaBlock(RichPage::BlockKind kind);

RichPage::Block MakeMapBlock(double latitude, double longitude);

[[nodiscard]] bool RichTextIsEmpty(const RichPage::RichText &text);

[[nodiscard]] bool ListItemIsBlankLine(const RichPage::ListItem &item);
[[nodiscard]] bool ListItemIsEmpty(const RichPage::ListItem &item);

[[nodiscard]] bool BlockIsEmpty(const RichPage::Block &block);

[[nodiscard]] TextWithEntities StripEditModeWrapperEntities(
	TextWithEntities text);

void StripEditModeWrapperEntities(RichPage::RichText &text);

void StripEditModeWrapperEntities(
	std::vector<RichPage::Block> &blocks);

bool DegradeBlockOnlyEntities(TextWithEntities &text);

bool DegradeEditModeInlineButtons(TextWithEntities &text);

bool DegradeEditModeButtons(
	std::vector<RichPage::Block> &blocks);

[[nodiscard]] bool CanEditRichPage(const RichPage &page);

[[nodiscard]] bool CanEditRichPage(
	const std::shared_ptr<const RichPage> &page);

} // namespace Iv::Editor
