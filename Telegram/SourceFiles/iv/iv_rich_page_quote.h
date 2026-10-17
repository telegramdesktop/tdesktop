/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "iv/markdown/iv_markdown_prepare.h"

#include <memory>
#include <optional>
#include <vector>

namespace Main {
class Session;
} // namespace Main

namespace Iv {

struct RichPage;

inline constexpr auto kRichQuoteMaxTextDepth = 64;

struct RichQuoteRange {
	int from = 0;
	int till = 0;

	friend inline bool operator==(
		const RichQuoteRange &,
		const RichQuoteRange &) = default;
};

struct RichQuotePageRange {
	Markdown::PreparedEditLeafSource leaf;
	int from = 0;
	int till = 0;

	friend inline bool operator==(
		const RichQuotePageRange &,
		const RichQuotePageRange &) = default;
};

struct RichQuoteNode {
	int from = 0;
	int till = 0;
	std::optional<Markdown::PreparedEditLeafSource> leaf;

	friend inline bool operator==(
		const RichQuoteNode &,
		const RichQuoteNode &) = default;
};

struct RichQuotePoint {
	int node = 0;
	int from = 0;
	int till = 0;

	friend inline bool operator==(
		const RichQuotePoint &,
		const RichQuotePoint &) = default;
};

class RichQuoteModel final {
public:
	[[nodiscard]] const QString &projection() const;
	[[nodiscard]] const std::vector<RichQuoteNode> &nodes() const;
	[[nodiscard]] QStringView nodeText(int index) const;
	[[nodiscard]] const MTPRichText *nodeTree(int index) const;
	[[nodiscard]] bool partial() const;
	[[nodiscard]] bool splitsFormula(int position) const;

	[[nodiscard]] std::vector<RichQuotePageRange> pageRanges(
		RichQuoteRange range) const;
	[[nodiscard]] std::optional<RichQuotePoint> pointAt(
		const Markdown::PreparedEditLeafSource &leaf,
		int position) const;
	[[nodiscard]] std::optional<RichQuotePoint> labelPointAt(
		const Markdown::PreparedEditLeafSource &leaf,
		int buttonPosition,
		int labelOffset,
		bool atEnd) const;

private:
	friend class RichQuoteRecorder;

	struct Piece {
		int from = 0;
		int till = 0;
		int pageFrom = 0;
		int pageTill = 0;
		bool atomic = false;
	};
	struct Label {
		int piece = 0;
		int from = 0;
		int till = 0;
		int length = 0;
	};

	RichQuoteModel() = default;

	[[nodiscard]] int piecesFrom(int node) const;
	[[nodiscard]] int labelsFrom(int piece) const;
	[[nodiscard]] RichQuotePoint labelPoint(
		int node,
		const Label &label,
		int offset,
		bool atEnd) const;
	[[nodiscard]] bool valid(const RichPage &page) const;
	[[nodiscard]] bool validPieces(
		const RichQuoteNode &node,
		int from,
		int till,
		const QString &page) const;
	[[nodiscard]] bool validLabels() const;

	QString _projection;
	std::vector<RichQuoteNode> _nodes;
	std::vector<MTPRichText> _trees;
	std::vector<int> _nodePiecesTill;
	std::vector<Piece> _pieces;
	std::vector<Piece> _labelPieces;
	std::vector<Label> _labels;
	std::vector<RichQuoteRange> _formulas;
	bool _partial = false;

};

class RichQuoteRecorder final {
public:
	struct Mark {
		uint64 node = 0;
		int pieces = 0;
		int projection = 0;
		int floor = 0;
		qsizetype page = 0;
	};
	struct Checkpoint {
		int nodes = 0;
		int pieces = 0;
	};
	struct NormalizedLabel {
		std::vector<int> offsets;
		QString text;
	};

	RichQuoteRecorder();
	RichQuoteRecorder(const RichQuoteRecorder &) = delete;
	RichQuoteRecorder &operator=(const RichQuoteRecorder &) = delete;
	~RichQuoteRecorder();

	[[nodiscard]] bool failed() const;
	[[nodiscard]] bool suppressed() const;
	[[nodiscard]] bool hidden() const;
	[[nodiscard]] bool keepsLabels() const;
	void fail();

	void appendText(QStringView text, qsizetype pageFrom);
	void appendAtom(
		QStringView projection,
		qsizetype pageFrom,
		qsizetype pageTill);
	void appendFormula(
		QStringView source,
		qsizetype pageFrom,
		qsizetype pageTill);
	[[nodiscard]] Mark mark(qsizetype pageFrom);
	void collapse(
		const Mark &mark,
		qsizetype pageTill,
		const NormalizedLabel *label = nullptr);
	[[nodiscard]] std::optional<QString> takeNodeText();

	[[nodiscard]] Checkpoint checkpoint() const;
	void shiftPage(const Checkpoint &from, qsizetype delta);
	void adoptListItemParagraph(
		const Checkpoint &from,
		std::size_t index,
		std::size_t listItem);
	void moveSplitCaption(
		const Checkpoint &from,
		std::size_t index,
		std::size_t lastIndex);

	[[nodiscard]] std::unique_ptr<const RichQuoteModel> finish(
		const RichPage &page);

private:
	friend class RichQuoteScope;

	using Leaf = Markdown::PreparedEditLeafSource;

	enum class Scope : uchar {
		Node,
		Container,
		Suppressed,
		Hidden,
	};
	struct OpenNode {
		std::optional<Leaf> leaf;
		std::optional<MTPRichText> tree;
		int separatorAt = 0;
		int start = 0;
		int pieces = 0;
		bool open = false;
		bool recorded = false;
	};

	[[nodiscard]] bool recording() const;
	[[nodiscard]] bool wantsLeaf() const;
	[[nodiscard]] bool validCheckpoint(const Checkpoint &from) const;
	[[nodiscard]] std::optional<Markdown::PreparedEditBlockPath> blockPath(
		std::size_t index) const;

	void beginNode(
		Markdown::PreparedEditLeafKind kind,
		std::size_t index,
		const MTPRichText &tree);
	void beginListItemNode(
		std::size_t index,
		std::size_t listItem,
		const MTPRichText &tree);
	void beginTableCellNode(
		std::size_t index,
		std::size_t row,
		std::size_t cell,
		const MTPRichText &tree);
	void beginHiddenNode();
	void openNode(std::optional<Leaf> leaf, const MTPRichText *tree);
	void closeNode();
	void pushBlockChildren(std::size_t index);
	void pushListItemChildren(std::size_t index, std::size_t listItem);
	void pushStep(Markdown::PreparedEditBlockContainerStep step, bool valid);
	void pushSuppressed();
	void pushHidden();
	void popScope();
	void addPiece(RichQuoteModel::Piece piece);
	[[nodiscard]] bool labelPieces(
		int from,
		const NormalizedLabel &label,
		std::vector<RichQuoteModel::Piece> *result) const;
	[[nodiscard]] bool labelText(
		const RichQuoteModel::Piece &piece,
		const NormalizedLabel &label,
		std::vector<RichQuoteModel::Piece> *result) const;
	void addLabel(std::vector<RichQuoteModel::Piece> pieces, int length);
	void erasePieces(int from);
	void truncateProjection(int size);
	void retarget(const Checkpoint &from, const Leaf &was, const Leaf &now);

	RichQuoteModel _result;
	OpenNode _node;
	Markdown::PreparedEditBlockContainerPath _path;
	std::vector<Scope> _scopes;
	uint64 _serial = 0;
	int _floor = 0;
	int _suppressed = 0;
	int _hidden = 0;
	bool _failed = false;

};

class RichQuoteScope final {
public:
	RichQuoteScope(const RichQuoteScope &) = delete;
	RichQuoteScope &operator=(const RichQuoteScope &) = delete;
	~RichQuoteScope();

	[[nodiscard]] static RichQuoteScope Node(
		RichQuoteRecorder *recorder,
		Markdown::PreparedEditLeafKind kind,
		std::size_t index,
		const MTPRichText &tree);
	[[nodiscard]] static RichQuoteScope ListItemNode(
		RichQuoteRecorder *recorder,
		std::size_t index,
		std::size_t listItem,
		const MTPRichText &tree);
	[[nodiscard]] static RichQuoteScope TableCellNode(
		RichQuoteRecorder *recorder,
		std::size_t index,
		std::size_t row,
		std::size_t cell,
		const MTPRichText &tree);
	[[nodiscard]] static RichQuoteScope HiddenNode(
		RichQuoteRecorder *recorder);
	[[nodiscard]] static RichQuoteScope BlockChildren(
		RichQuoteRecorder *recorder,
		std::size_t index);
	[[nodiscard]] static RichQuoteScope ListItemChildren(
		RichQuoteRecorder *recorder,
		std::size_t index,
		std::size_t listItem);
	[[nodiscard]] static RichQuoteScope Suppressed(
		RichQuoteRecorder *recorder);
	[[nodiscard]] static RichQuoteScope Hidden(
		RichQuoteRecorder *recorder);

private:
	explicit RichQuoteScope(RichQuoteRecorder *recorder);

	RichQuoteRecorder *_recorder = nullptr;

};

[[nodiscard]] std::optional<std::vector<QString>> RichQuoteFragmentTexts(
	not_null<Main::Session*> session,
	const QVector<MTPRichText> &fragments);

[[nodiscard]] std::optional<RichQuoteRange> LocateRichQuote(
	not_null<Main::Session*> session,
	const RichPage &page,
	const QVector<MTPRichText> &fragments,
	int offset = 0);

} // namespace Iv
