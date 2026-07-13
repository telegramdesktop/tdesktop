/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "ui/text/text_entity.h"

#include <QtCore/QByteArray>

#include <memory>
#include <optional>
#include <vector>

class DocumentData;
class PhotoData;
class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace Iv {

struct RichPage {
	struct RichText {
		TextWithEntities text;
		QString anchorId;
		std::vector<QString> anchorIds;

		friend inline bool operator==(
			const RichText &,
			const RichText &) = default;
	};
	enum class BlockKind : uchar {
		Unsupported,
		Heading,
		Paragraph,
		Footer,
		Thinking,
		AuthorDate,
		Code,
		Divider,
		Anchor,
		List,
		Quote,
		Photo,
		Video,
		Embed,
		EmbedPost,
		GroupedMedia,
		Channel,
		Audio,
		Math,
		Table,
		Details,
		RelatedArticles,
		Map,
	};
	enum class ListKind : uchar {
		Bullet,
		Ordered,
	};
	enum class TaskState : uchar {
		None,
		Unchecked,
		Checked,
	};
	struct OrderedListData {
		bool reversed = false;
		std::optional<int> start;
		std::optional<QString> type;

		friend inline bool operator==(
			const OrderedListData &,
			const OrderedListData &) = default;
	};
	struct OrderedListItemData {
		std::optional<QString> num;
		std::optional<int> value;
		std::optional<QString> type;

		[[nodiscard]] bool isEmpty() const {
			return !num.has_value() || num->isEmpty();
		}
		[[nodiscard]] bool hasRawText() const {
			return num.has_value() && !num->isEmpty();
		}
		[[nodiscard]] QString rawText() const {
			return num.value_or(QString());
		}
		operator QString() const {
			return rawText();
		}

		friend inline bool operator==(
			const OrderedListItemData &,
			const OrderedListItemData &) = default;
	};
	enum class GroupedMediaIntent : uchar {
		Collage,
		Slideshow,
	};
	enum class TableAlignment : uchar {
		Left,
		Center,
		Right,
	};
	enum class TableVerticalAlignment : uchar {
		Top,
		Middle,
		Bottom,
	};
	struct Block;
	struct ListItem {
		TaskState taskState = TaskState::None;
		OrderedListItemData number;
		QString anchorId;
		RichText text;
		std::vector<Block> blocks;

		friend inline bool operator==(
			const ListItem &,
			const ListItem &) = default;
	};
	struct GroupedMediaItem {
		BlockKind kind = BlockKind::Unsupported;
		PhotoData *photo = nullptr;
		DocumentData *document = nullptr;
		uint64 photoId = 0;
		uint64 documentId = 0;
		int width = 0;
		int height = 0;
		bool autoplay = false;
		bool loop = false;
		bool spoiler = false;

		friend inline bool operator==(
			const GroupedMediaItem &,
			const GroupedMediaItem &) = default;
	};
	struct TableCell {
		RichText text;
		int colspan = 1;
		int rowspan = 1;
		bool header = false;
		TableAlignment alignment = TableAlignment::Left;
		TableVerticalAlignment verticalAlignment
			= TableVerticalAlignment::Top;

		friend inline bool operator==(
			const TableCell &,
			const TableCell &) = default;
	};
	struct TableRow {
		std::vector<TableCell> cells;

		friend inline bool operator==(
			const TableRow &,
			const TableRow &) = default;
	};
	struct RelatedArticle {
		QString url;
		uint64 webpageId = 0;
		PhotoData *photo = nullptr;
		uint64 photoId = 0;
		QString title;
		QString description;
		QString author;
		TimeId publishedDate = 0;

		friend inline bool operator==(
			const RelatedArticle &,
			const RelatedArticle &) = default;
	};
	struct Block {
		BlockKind kind = BlockKind::Unsupported;
		QString anchorId;
		RichText text;
		RichText caption;
		QString language;
		QString formula;
		QString url;
		QByteArray html;
		QString author;
		QString username;
		QString channelTitle;
		QString audioTitle;
		QString audioPerformer;
		QString audioFileName;
		TimeId date = 0;
		int audioDuration = 0;
		int headingLevel = 0;
		int width = 0;
		int height = 0;
		int zoom = 0;
		uint64 photoId = 0;
		uint64 documentId = 0;
		uint64 channelId = 0;
		bool fullWidth = false;
		bool fixedHeight = false;
		bool allowScrolling = false;
		bool autoplay = false;
		bool loop = false;
		bool spoiler = false;
		bool open = false;
		bool bordered = false;
		bool striped = false;
		bool pullquote = false;
		ListKind listKind = ListKind::Bullet;
		OrderedListData orderedList;
		GroupedMediaIntent mediaIntent = GroupedMediaIntent::Collage;
		PhotoData *photo = nullptr;
		DocumentData *document = nullptr;
		PeerData *peer = nullptr;
		float64 latitude = 0.;
		float64 longitude = 0.;
		uint64 accessHash = 0;
		std::vector<Block> blocks;
		std::vector<ListItem> listItems;
		std::vector<GroupedMediaItem> mediaItems;
		std::vector<TableRow> tableRows;
		std::vector<RelatedArticle> relatedArticles;

		friend inline bool operator==(
			const Block &,
			const Block &) = default;
	};
	QString url;
	bool rtl = false;
	bool part = false;
	int views = 0;
	std::vector<Block> blocks;

	friend inline bool operator==(
		const RichPage &,
		const RichPage &) = default;
};

struct RichMessageLimits {
	int lengthLimit = 32768;
	int maxBlocks = 500;
	int maxDepth = 16;
	int maxMedia = 50;
	int maxTableCols = 20;
};

enum class RichMessageLimitError : unsigned char {
	Length,
	Blocks,
	Depth,
	Media,
	TableColumns,
};

struct RichPageLinkUrl {
	QString url;
	uint64 webpageId = 0;
};

[[nodiscard]] RichMessageLimits ResolveRichMessageLimits(
	not_null<Main::Session*> session);
[[nodiscard]] bool RichPagesEqual(
	const RichPage &a,
	const RichPage &b);
[[nodiscard]] std::optional<RichMessageLimitError> ValidateRichMessage(
	const RichPage &page,
	const RichMessageLimits &limits);
[[nodiscard]] QString EncodeRichPageLinkUrl(
	const QString &url,
	uint64 webpageId);
[[nodiscard]] std::optional<RichPageLinkUrl> DecodeRichPageLinkUrl(
	const QString &data);
[[nodiscard]] std::shared_ptr<const RichPage> ParseRichPage(
	not_null<Main::Session*> session,
	const MTPRichMessage &message);
[[nodiscard]] std::shared_ptr<const RichPage> ParseRichPage(
	not_null<Main::Session*> session,
	const MTPPage &page);
[[nodiscard]] std::shared_ptr<const RichPage> ParseRichPage(
	not_null<Main::Session*> session,
	const MTPDwebPage &webpage);
[[nodiscard]] std::optional<TextWithEntities> SerializeAsSimple(
	const RichPage &page,
	not_null<Main::Session*> session);
[[nodiscard]] bool CanSerializeAsSimple(
	const RichPage &page,
	not_null<Main::Session*> session);
[[nodiscard]] bool RichPageUsesPremiumFormatting(const RichPage &page);
[[nodiscard]] bool RichPageIsFlattenSafe(const RichPage &page);
[[nodiscard]] RichPage SplitTextIntoRichPage(TextWithEntities text);
[[nodiscard]] TextWithEntities FlattenRichPageSummary(
	const RichPage &page,
	bool emptyFallback = true);
[[nodiscard]] TextWithEntities FlattenRichPageSummary(
	const std::shared_ptr<const RichPage> &page,
	bool emptyFallback = true);
[[nodiscard]] TextWithEntities FlattenRichPageToSimpleText(
	const RichPage &page);

} // namespace Iv
