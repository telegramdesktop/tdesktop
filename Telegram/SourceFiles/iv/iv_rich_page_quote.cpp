/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/iv_rich_page_quote.h"

#include "base/algorithm.h"
#include "iv/iv_rich_page.h"
#include "iv/markdown/iv_markdown_prepare_native_blocks.h"

#include <algorithm>
#include <limits>

namespace Iv {
namespace {

constexpr auto kMaxSize = qsizetype(std::numeric_limits<int>::max());

[[nodiscard]] std::optional<int> CheckedIndex(std::size_t value) {
	return (value <= std::size_t(kMaxSize))
		? std::make_optional(int(value))
		: std::nullopt;
}

[[nodiscard]] bool IsBlockLeafKind(Markdown::PreparedEditLeafKind kind) {
	switch (kind) {
	case Markdown::PreparedEditLeafKind::BlockText:
	case Markdown::PreparedEditLeafKind::BlockCaption:
	case Markdown::PreparedEditLeafKind::MathFormula:
		return true;
	case Markdown::PreparedEditLeafKind::ListItemText:
	case Markdown::PreparedEditLeafKind::TableCellText:
		return false;
	}
	return false;
}

struct NearestOccurrences {
	int offset = 0;
	std::optional<RichQuoteRange> before;
	std::optional<RichQuoteRange> after;
};

[[nodiscard]] bool AddOccurrence(
		NearestOccurrences *nearest,
		RichQuoteRange range) {
	if (range.from < nearest->offset) {
		nearest->before = range;
		return true;
	}
	nearest->after = range;
	return false;
}

[[nodiscard]] bool CutsFormula(
		const RichQuoteModel &model,
		RichQuoteRange range) {
	return model.splitsFormula(range.from)
		|| model.splitsFormula(range.till);
}

[[nodiscard]] std::optional<RichQuoteRange> ChooseOccurrence(
		const NearestOccurrences &nearest) {
	const auto &before = nearest.before;
	const auto &after = nearest.after;
	if (!before) {
		return after;
	} else if (!after) {
		return before;
	}
	const auto offset = nearest.offset;
	return (offset - before->from < after->from - offset) ? before : after;
}

template <typename Equal>
[[nodiscard]] int ExtendMatch(
		const std::vector<int> &borders,
		int matched,
		Equal &&equal) {
	while (!equal(matched)) {
		if (!matched) {
			return 0;
		}
		matched = borders[matched - 1];
	}
	return matched + 1;
}

template <typename Equal>
[[nodiscard]] std::vector<int> CountBorders(int size, Equal &&equal) {
	auto result = std::vector<int>(size);
	for (auto i = 1; i < size; ++i) {
		result[i] = ExtendMatch(result, result[i - 1], [&](int k) {
			return equal(k, i);
		});
	}
	return result;
}

[[nodiscard]] std::optional<RichQuoteRange> LocateFragment(
		const RichQuoteModel &model,
		QStringView fragment,
		int offset) {
	const auto &projection = model.projection();
	const auto &nodes = model.nodes();
	const auto length = int(fragment.size());
	const auto borders = CountBorders(length, [&](int a, int b) {
		return (fragment[a] == fragment[b]);
	});
	auto nearest = NearestOccurrences{ .offset = offset };
	auto node = begin(nodes);
	auto matched = 0;
	for (auto i = 0, size = int(projection.size()); i != size; ++i) {
		if (matched == length) {
			matched = borders.back();
		}
		const auto symbol = projection[i];
		matched = ExtendMatch(borders, matched, [&](int k) {
			return (fragment[k] == symbol);
		});
		if (matched != length) {
			continue;
		}
		const auto from = i + 1 - length;
		const auto range = RichQuoteRange{ .from = from, .till = i + 1 };
		while (node != end(nodes) && node->till <= from) {
			++node;
		}
		if (node == end(nodes)) {
			break;
		} else if (node->from <= from
			&& i < node->till
			&& !CutsFormula(model, range)
			&& !AddOccurrence(&nearest, range)) {
			break;
		}
	}
	return ChooseOccurrence(nearest);
}

[[nodiscard]] std::optional<RichQuoteRange> LocateFragments(
		const RichQuoteModel &model,
		const std::vector<QString> &fragments,
		int offset) {
	const auto &nodes = model.nodes();
	const auto count = int(nodes.size());
	const auto middles = int(fragments.size()) - 2;
	const auto middle = [&](int index) {
		return QStringView(fragments[index + 1]);
	};
	auto whole = std::vector<bool>(std::size_t(count) + 1, !middles);
	auto tail = std::vector<bool>(std::size_t(count) + 1, false);
	tail[count] = true;
	if (middles > 0) {
		const auto borders = CountBorders(middles, [&](int a, int b) {
			return (middle(a) == middle(b));
		});
		auto matched = 0;
		for (auto i = 0; i != count; ++i) {
			if (matched == middles) {
				matched = borders.back();
			}
			const auto text = model.nodeText(i);
			matched = ExtendMatch(borders, matched, [&](int k) {
				return (middle(k) == text);
			});
			if (matched == middles) {
				whole[i + 1 - middles] = true;
			}
		}
		for (auto k = matched; k > 0; k = borders[k - 1]) {
			tail[count - k] = true;
		}
	}
	const auto first = QStringView(fragments.front());
	const auto last = QStringView(fragments.back());
	auto nearest = NearestOccurrences{ .offset = offset };
	for (auto i = 0; i != count; ++i) {
		auto till = 0;
		if (middles < count - 1 - i) {
			const auto index = i + 1 + middles;
			if (!whole[i + 1]
				|| !model.nodeText(i).endsWith(first)
				|| !model.nodeText(index).startsWith(last)) {
				continue;
			}
			till = nodes[index].from + int(last.size());
		} else if (model.partial()
			&& tail[i + 1]
			&& model.nodeText(i).endsWith(first)) {
			till = int(model.projection().size());
		} else {
			continue;
		}
		const auto from = nodes[i].till - int(first.size());
		const auto range = RichQuoteRange{ .from = from, .till = till };
		if (!CutsFormula(model, range) && !AddOccurrence(&nearest, range)) {
			break;
		}
	}
	return ChooseOccurrence(nearest);
}

[[nodiscard]] std::optional<RichQuoteRange> LocateFragmentTexts(
		const RichQuoteModel &model,
		const std::vector<QString> &fragments,
		int offset) {
	const auto empty = [](const QString &text) {
		return text.isEmpty();
	};
	if (model.nodes().empty()
		|| fragments.empty()
		|| fragments.size() > std::size_t(kMaxSize)
		|| std::any_of(begin(fragments), end(fragments), empty)) {
		return std::nullopt;
	}
	const auto size = int(model.projection().size());
	const auto clamped = std::clamp(offset, 0, size);
	return (fragments.size() == 1)
		? LocateFragment(model, fragments.front(), clamped)
		: LocateFragments(model, fragments, clamped);
}

} // namespace

RichQuoteModelSlot::RichQuoteModelSlot() noexcept = default;

RichQuoteModelSlot::RichQuoteModelSlot(const RichQuoteModelSlot &) noexcept {
}

RichQuoteModelSlot::RichQuoteModelSlot(RichQuoteModelSlot &&other) noexcept {
	other._model = nullptr;
}

RichQuoteModelSlot &RichQuoteModelSlot::operator=(
		const RichQuoteModelSlot &) noexcept {
	_model = nullptr;
	return *this;
}

RichQuoteModelSlot &RichQuoteModelSlot::operator=(
		RichQuoteModelSlot &&other) noexcept {
	_model = nullptr;
	other._model = nullptr;
	return *this;
}

RichQuoteModelSlot::~RichQuoteModelSlot() = default;

void RichQuoteModelSlot::set(std::unique_ptr<const RichQuoteModel> model) {
	_model = std::move(model);
}

const QString &RichQuoteModel::projection() const {
	return _projection;
}

const std::vector<RichQuoteNode> &RichQuoteModel::nodes() const {
	return _nodes;
}

QStringView RichQuoteModel::nodeText(int index) const {
	if (index < 0 || index >= int(_nodes.size())) {
		return QStringView();
	}
	const auto &node = _nodes[index];
	return QStringView(_projection).mid(node.from, node.till - node.from);
}

const MTPRichText *RichQuoteModel::nodeTree(int index) const {
	return (index >= 0 && index < int(_trees.size()))
		? &_trees[index]
		: nullptr;
}

bool RichQuoteModel::partial() const {
	return _partial;
}

bool RichQuoteModel::splitsFormula(int position) const {
	const auto formula = std::upper_bound(
		begin(_formulas),
		end(_formulas),
		position,
		[](int value, const RichQuoteRange &entry) {
			return value < entry.till;
		});
	return (formula != end(_formulas)) && (formula->from < position);
}

std::vector<RichQuotePageRange> RichQuoteModel::pageRanges(
		RichQuoteRange range) const {
	auto result = std::vector<RichQuotePageRange>();
	const auto size = int(_projection.size());
	const auto from = std::clamp(range.from, 0, size);
	const auto till = std::clamp(range.till, 0, size);
	if (from >= till) {
		return result;
	}
	auto lastNode = -1;
	auto node = std::upper_bound(
		begin(_nodes),
		end(_nodes),
		from,
		[](int value, const RichQuoteNode &entry) {
			return value < entry.till;
		});
	for (; node != end(_nodes) && node->from < till; ++node) {
		if (!node->leaf) {
			continue;
		}
		const auto index = int(node - begin(_nodes));
		const auto piecesTill = begin(_pieces) + _nodePiecesTill[index];
		auto piece = std::upper_bound(
			begin(_pieces) + piecesFrom(index),
			piecesTill,
			from,
			[](int value, const Piece &entry) {
				return value < entry.till;
			});
		for (; piece != piecesTill && piece->from < till; ++piece) {
			if (piece->from == piece->till
				|| piece->pageFrom == piece->pageTill) {
				continue;
			}
			auto pageFrom = piece->pageFrom;
			auto pageTill = piece->pageTill;
			if (!piece->atomic) {
				pageFrom += std::max(from, piece->from) - piece->from;
				pageTill -= piece->till - std::min(till, piece->till);
			}
			if (!result.empty()
				&& result.back().till == pageFrom
				&& (lastNode == index || result.back().leaf == *node->leaf)) {
				result.back().till = pageTill;
			} else {
				result.push_back({
					.leaf = *node->leaf,
					.from = pageFrom,
					.till = pageTill,
				});
			}
			lastNode = index;
		}
	}
	return result;
}

std::optional<RichQuotePoint> RichQuoteModel::pointAt(
		const Markdown::PreparedEditLeafSource &leaf,
		int position) const {
	if (position < 0) {
		return std::nullopt;
	}
	const auto candidate = [&](const Piece &piece) {
		if (!piece.atomic) {
			const auto at = piece.from + (position - piece.pageFrom);
			return std::make_pair(at, at);
		} else if (piece.pageFrom == piece.pageTill
			|| (position > piece.pageFrom && position < piece.pageTill)) {
			return std::make_pair(piece.from, piece.till);
		} else if (position == piece.pageFrom) {
			return std::make_pair(piece.from, piece.from);
		}
		return std::make_pair(piece.till, piece.till);
	};
	for (auto index = 0, count = int(_nodes.size()); index != count; ++index) {
		const auto &node = _nodes[index];
		const auto from = begin(_pieces) + piecesFrom(index);
		const auto till = begin(_pieces) + _nodePiecesTill[index];
		if (!node.leaf
			|| *node.leaf != leaf
			|| from == till
			|| position < from->pageFrom
			|| position > (till - 1)->pageTill) {
			continue;
		}
		auto piece = std::lower_bound(
			from,
			till,
			position,
			[](const Piece &entry, int value) {
				return entry.pageTill < value;
			});
		auto result = std::optional<RichQuotePoint>();
		for (; piece != till && piece->pageFrom <= position; ++piece) {
			const auto [a, b] = candidate(*piece);
			if (!result) {
				result = RichQuotePoint{ .node = index, .from = a, .till = b };
			} else {
				result->from = std::min(result->from, a);
				result->till = std::max(result->till, b);
			}
		}
		if (result) {
			return result;
		}
	}
	return std::nullopt;
}

std::optional<RichQuotePoint> RichQuoteModel::labelPointAt(
		const Markdown::PreparedEditLeafSource &leaf,
		int buttonPosition,
		int labelOffset,
		bool atEnd) const {
	if (buttonPosition < 0 || labelOffset < 0) {
		return std::nullopt;
	}
	for (auto index = 0, count = int(_nodes.size()); index != count; ++index) {
		const auto &node = _nodes[index];
		const auto from = begin(_pieces) + piecesFrom(index);
		const auto till = begin(_pieces) + _nodePiecesTill[index];
		if (!node.leaf
			|| *node.leaf != leaf
			|| from == till
			|| buttonPosition < from->pageFrom
			|| buttonPosition >= (till - 1)->pageTill) {
			continue;
		}
		const auto piece = std::upper_bound(
			from,
			till,
			buttonPosition,
			[](int value, const Piece &entry) {
				return value < entry.pageTill;
			});
		if (!piece->atomic) {
			return std::nullopt;
		}
		const auto owner = int(piece - begin(_pieces));
		const auto label = labelsFrom(owner);
		if (label == int(_labels.size()) || _labels[label].piece != owner) {
			return RichQuotePoint{
				.node = index,
				.from = piece->from,
				.till = piece->till,
			};
		} else if (piece->pageFrom != buttonPosition
			|| labelOffset > _labels[label].length) {
			return std::nullopt;
		}
		return labelPoint(index, _labels[label], labelOffset, atEnd);
	}
	return std::nullopt;
}

int RichQuoteModel::piecesFrom(int node) const {
	return node ? _nodePiecesTill[node - 1] : 0;
}

int RichQuoteModel::labelsFrom(int piece) const {
	const auto label = std::lower_bound(
		begin(_labels),
		end(_labels),
		piece,
		[](const Label &entry, int value) {
			return entry.piece < value;
		});
	return int(label - begin(_labels));
}

RichQuotePoint RichQuoteModel::labelPoint(
		int node,
		const Label &label,
		int offset,
		bool atEnd) const {
	const auto &owner = _pieces[label.piece];
	const auto from = begin(_labelPieces) + label.from;
	const auto till = begin(_labelPieces) + label.till;
	const Piece *piece = nullptr;
	if (atEnd) {
		const auto after = std::lower_bound(
			from,
			till,
			offset,
			[](const Piece &entry, int value) {
				return entry.pageFrom < value;
			});
		piece = (after != from) ? &*(after - 1) : nullptr;
	} else {
		const auto at = std::upper_bound(
			from,
			till,
			offset,
			[](int value, const Piece &entry) {
				return value < entry.pageTill;
			});
		piece = (at != till) ? &*at : nullptr;
	}
	if (!piece) {
		const auto edge = atEnd ? owner.from : owner.till;
		return { .node = node, .from = edge, .till = edge };
	} else if (!piece->atomic) {
		const auto at = piece->from + (offset - piece->pageFrom);
		return { .node = node, .from = at, .till = at };
	}
	const auto exact = (offset == (atEnd ? piece->pageTill : piece->pageFrom));
	const auto edge = atEnd ? piece->till : piece->from;
	return {
		.node = node,
		.from = exact ? edge : piece->from,
		.till = exact ? edge : piece->till,
	};
}

bool RichQuoteModel::valid(const RichPage &page) const {
	const auto size = _projection.size();
	if (size > kMaxSize
		|| _nodes.size() > std::size_t(kMaxSize)
		|| _pieces.size() > std::size_t(kMaxSize)
		|| _nodePiecesTill.size() != _nodes.size()
		|| _trees.size() != _nodes.size()) {
		return false;
	}
	auto pieces = 0;
	const RichQuoteNode *previous = nullptr;
	auto previousPageTill = 0;
	for (auto i = 0, count = int(_nodes.size()); i != count; ++i) {
		const auto &node = _nodes[i];
		const auto from = i ? (qsizetype(_nodes[i - 1].till) + 1) : 0;
		const auto till = _nodePiecesTill[i];
		if (node.from != from
			|| node.till <= node.from
			|| node.till > size
			|| (i && _projection[node.from - 1] != QChar('\n'))
			|| till < pieces
			|| till > int(_pieces.size())) {
			return false;
		} else if (!node.leaf) {
			if (till != pieces) {
				return false;
			}
			continue;
		}
		const auto text = Markdown::ResolveCanonicalNativeIvLeafText(
			page,
			*node.leaf);
		if (!text
			|| till == pieces
			|| !validPieces(node, pieces, till, *text)) {
			return false;
		}
		const auto &first = _pieces[pieces];
		const auto &last = _pieces[till - 1];
		if (last.pageTill <= first.pageFrom
			|| (previous
				&& *previous->leaf == *node.leaf
				&& first.pageFrom < previousPageTill)) {
			return false;
		}
		previous = &node;
		previousPageTill = last.pageTill;
		pieces = till;
	}
	auto formulasTill = 0;
	for (const auto &formula : _formulas) {
		if (formula.from < formulasTill
			|| formula.till <= formula.from
			|| formula.till > size) {
			return false;
		}
		formulasTill = formula.till;
	}
	const auto projectionTill = _nodes.empty() ? 0 : _nodes.back().till;
	return (projectionTill == size)
		&& (pieces == int(_pieces.size()))
		&& validLabels();
}

bool RichQuoteModel::validPieces(
		const RichQuoteNode &node,
		int from,
		int till,
		const QString &page) const {
	auto projection = node.from;
	auto position = _pieces[from].pageFrom;
	for (auto i = from; i != till; ++i) {
		const auto &piece = _pieces[i];
		if (piece.from != projection
			|| piece.till < piece.from
			|| piece.till > node.till
			|| piece.pageFrom != position
			|| piece.pageFrom < 0
			|| piece.pageTill < piece.pageFrom
			|| piece.pageTill > page.size()) {
			return false;
		} else if (!piece.atomic) {
			const auto length = piece.till - piece.from;
			if (piece.pageTill - piece.pageFrom != length
				|| (QStringView(_projection).mid(piece.from, length)
					!= QStringView(page).mid(piece.pageFrom, length))) {
				return false;
			}
		}
		projection = piece.till;
		position = piece.pageTill;
	}
	return (projection == node.till);
}

bool RichQuoteModel::validLabels() const {
	if (_labels.size() > std::size_t(kMaxSize)
		|| _labelPieces.size() > std::size_t(kMaxSize)) {
		return false;
	}
	auto owner = -1;
	auto stored = 0;
	for (const auto &label : _labels) {
		if (label.piece <= owner
			|| label.piece >= int(_pieces.size())
			|| label.from != stored
			|| label.till <= label.from
			|| label.till > int(_labelPieces.size())
			|| label.length <= 0) {
			return false;
		}
		const auto &piece = _pieces[label.piece];
		if (!piece.atomic || piece.pageTill <= piece.pageFrom) {
			return false;
		}
		auto projection = piece.from;
		auto position = 0;
		for (auto i = label.from; i != label.till; ++i) {
			const auto &entry = _labelPieces[i];
			if (entry.from < projection
				|| entry.till < entry.from
				|| entry.till > piece.till
				|| entry.pageFrom != position
				|| entry.pageTill < entry.pageFrom
				|| entry.pageTill > label.length
				|| (!entry.atomic
					&& (entry.pageTill == entry.pageFrom
						|| (entry.till - entry.from
							!= entry.pageTill - entry.pageFrom)))) {
				return false;
			}
			projection = entry.till;
			position = entry.pageTill;
		}
		if (position != label.length) {
			return false;
		}
		owner = label.piece;
		stored = label.till;
	}
	return (stored == int(_labelPieces.size()));
}

RichQuoteRecorder::RichQuoteRecorder() = default;

RichQuoteRecorder::~RichQuoteRecorder() = default;

bool RichQuoteRecorder::failed() const {
	return _failed;
}

bool RichQuoteRecorder::suppressed() const {
	return (_suppressed > 0);
}

bool RichQuoteRecorder::hidden() const {
	return (_hidden > 0);
}

bool RichQuoteRecorder::keepsLabels() const {
	return recording() && _node.leaf.has_value();
}

void RichQuoteRecorder::fail() {
	_failed = true;
	_result = RichQuoteModel();
}

void RichQuoteRecorder::appendText(QStringView text, qsizetype pageFrom) {
	if (!recording() || text.isEmpty()) {
		return;
	}
	auto &projection = _result._projection;
	const auto from = projection.size();
	if (text.size() > kMaxSize - from) {
		fail();
		return;
	}
	projection.append(text);
	if (!_node.leaf) {
		return;
	} else if (pageFrom < 0 || pageFrom > kMaxSize - text.size()) {
		fail();
		return;
	}
	const auto till = int(projection.size());
	const auto pageTill = int(pageFrom + text.size());
	auto &pieces = _result._pieces;
	if (int(pieces.size()) > _floor) {
		auto &last = pieces.back();
		if (!last.atomic
			&& last.till == int(from)
			&& last.pageTill == int(pageFrom)) {
			last.till = till;
			last.pageTill = pageTill;
			return;
		}
	}
	addPiece({
		.from = int(from),
		.till = till,
		.pageFrom = int(pageFrom),
		.pageTill = pageTill,
	});
}

void RichQuoteRecorder::appendAtom(
		QStringView projection,
		qsizetype pageFrom,
		qsizetype pageTill) {
	if (!recording()) {
		return;
	}
	auto &text = _result._projection;
	const auto from = text.size();
	if (projection.size() > kMaxSize - from) {
		fail();
		return;
	}
	text.append(projection);
	if (!_node.leaf) {
		return;
	} else if (pageFrom < 0 || pageTill < pageFrom || pageTill > kMaxSize) {
		fail();
		return;
	} else if (projection.isEmpty() && pageFrom == pageTill) {
		return;
	}
	addPiece({
		.from = int(from),
		.till = int(text.size()),
		.pageFrom = int(pageFrom),
		.pageTill = int(pageTill),
		.atomic = true,
	});
}

void RichQuoteRecorder::appendFormula(
		QStringView source,
		qsizetype pageFrom,
		qsizetype pageTill) {
	const auto from = _result._projection.size();
	appendAtom(source, pageFrom, pageTill);
	if (recording() && !source.isEmpty()) {
		_result._formulas.push_back({
			.from = int(from),
			.till = int(_result._projection.size()),
		});
	}
}

RichQuoteRecorder::Mark RichQuoteRecorder::mark(qsizetype pageFrom) {
	if (!recording()) {
		return Mark();
	}
	const auto result = Mark{
		.node = _serial,
		.pieces = int(_result._pieces.size()),
		.projection = int(_result._projection.size()),
		.floor = _floor,
		.page = pageFrom,
	};
	_floor = result.pieces;
	return result;
}

void RichQuoteRecorder::collapse(
		const Mark &mark,
		qsizetype pageTill,
		const NormalizedLabel *label) {
	if (!recording()) {
		return;
	} else if (mark.node != _serial
		|| mark.floor < _node.pieces
		|| mark.floor > mark.pieces
		|| mark.pieces > int(_result._pieces.size())
		|| mark.projection < _node.start
		|| mark.projection > int(_result._projection.size())) {
		fail();
		return;
	}
	_floor = mark.floor;
	// WHY: a link-style label is shown as its own characters, so the atom
	// keeps where each label position comes from, or a partial selection
	// inside the label would quote the whole label.
	const auto keep = label && _node.leaf && !label->text.isEmpty();
	auto pieces = std::vector<RichQuoteModel::Piece>();
	if (keep && !labelPieces(mark.pieces, *label, &pieces)) {
		fail();
		return;
	}
	erasePieces(mark.pieces);
	if (!_node.leaf) {
		return;
	} else if (mark.page < 0 || pageTill < mark.page || pageTill > kMaxSize) {
		fail();
		return;
	}
	const auto till = int(_result._projection.size());
	if (mark.projection == till && mark.page == pageTill) {
		return;
	}
	addPiece({
		.from = mark.projection,
		.till = till,
		.pageFrom = int(mark.page),
		.pageTill = int(pageTill),
		.atomic = true,
	});
	if (keep) {
		addLabel(std::move(pieces), int(label->text.size()));
	}
}

std::optional<QString> RichQuoteRecorder::takeNodeText() {
	if (!recording()) {
		return std::nullopt;
	}
	auto result = _result._projection.mid(_node.start);
	truncateProjection(_node.start);
	erasePieces(_node.pieces);
	_floor = _node.pieces;
	return result;
}

RichQuoteRecorder::Checkpoint RichQuoteRecorder::checkpoint() const {
	return {
		.nodes = int(_result._nodes.size()),
		.pieces = int(_result._pieces.size()),
	};
}

void RichQuoteRecorder::shiftPage(const Checkpoint &from, qsizetype delta) {
	if (_failed) {
		return;
	} else if (!validCheckpoint(from) || delta < 0 || delta > kMaxSize) {
		fail();
		return;
	}
	auto &pieces = _result._pieces;
	for (auto i = begin(pieces) + from.pieces; i != end(pieces); ++i) {
		if (i->pageTill > kMaxSize - delta) {
			fail();
			return;
		}
		i->pageFrom += int(delta);
		i->pageTill += int(delta);
	}
}

void RichQuoteRecorder::adoptListItemParagraph(
		const Checkpoint &from,
		std::size_t index,
		std::size_t listItem) {
	if (_failed) {
		return;
	}
	const auto block = CheckedIndex(index);
	const auto item = CheckedIndex(listItem);
	if (!validCheckpoint(from) || !block || !item) {
		fail();
		return;
	}
	auto children = _path;
	children.steps.push_back({
		.kind = Markdown::PreparedEditBlockContainerKind::ListItemChildren,
		.blockIndex = *block,
		.listItemIndex = *item,
	});
	retarget(
		from,
		Leaf{
			.kind = Markdown::PreparedEditLeafKind::BlockText,
			.block = { .container = std::move(children), .index = 0 },
		},
		Leaf{
			.kind = Markdown::PreparedEditLeafKind::ListItemText,
			.block = { .container = _path, .index = *block },
			.listItemIndex = *item,
		});
}

void RichQuoteRecorder::moveSplitCaption(
		const Checkpoint &from,
		std::size_t index,
		std::size_t lastIndex) {
	if (_failed) {
		return;
	}
	const auto was = blockPath(index);
	const auto now = blockPath(lastIndex);
	if (!validCheckpoint(from) || !was || !now) {
		fail();
		return;
	}
	retarget(
		from,
		Leaf{
			.kind = Markdown::PreparedEditLeafKind::BlockCaption,
			.block = *was,
		},
		Leaf{
			.kind = Markdown::PreparedEditLeafKind::BlockCaption,
			.block = *now,
		});
}

std::unique_ptr<const RichQuoteModel> RichQuoteRecorder::finish(
		const RichPage &page) {
	const auto complete = !_failed && !_node.open && _scopes.empty();
	_failed = true;
	if (!complete) {
		_result = RichQuoteModel();
		return nullptr;
	}
	_result._partial = page.part;
	if (!_result.valid(page)) {
		_result = RichQuoteModel();
		return nullptr;
	}
	return std::make_unique<RichQuoteModel>(std::move(_result));
}

bool RichQuoteRecorder::recording() const {
	return !_failed && _node.open && _node.recorded;
}

bool RichQuoteRecorder::wantsLeaf() const {
	return !_failed && !suppressed() && !hidden();
}

bool RichQuoteRecorder::validCheckpoint(const Checkpoint &from) const {
	const auto &ends = _result._nodePiecesTill;
	return !_node.open
		&& (from.nodes >= 0)
		&& (from.nodes <= int(ends.size()))
		&& (from.pieces == (from.nodes ? ends[from.nodes - 1] : 0))
		&& (int(_result._pieces.size()) == (ends.empty() ? 0 : ends.back()));
}

std::optional<Markdown::PreparedEditBlockPath> RichQuoteRecorder::blockPath(
		std::size_t index) const {
	const auto checked = CheckedIndex(index);
	if (!checked) {
		return std::nullopt;
	}
	return Markdown::PreparedEditBlockPath{
		.container = _path,
		.index = *checked,
	};
}

void RichQuoteRecorder::beginNode(
		Markdown::PreparedEditLeafKind kind,
		std::size_t index,
		const MTPRichText &tree) {
	auto leaf = std::optional<Leaf>();
	if (wantsLeaf()) {
		const auto path = blockPath(index);
		if (path && IsBlockLeafKind(kind)) {
			leaf = Leaf{ .kind = kind, .block = *path };
		} else {
			fail();
		}
	}
	openNode(std::move(leaf), &tree);
}

void RichQuoteRecorder::beginListItemNode(
		std::size_t index,
		std::size_t listItem,
		const MTPRichText &tree) {
	auto leaf = std::optional<Leaf>();
	if (wantsLeaf()) {
		const auto path = blockPath(index);
		const auto item = CheckedIndex(listItem);
		if (path && item) {
			leaf = Leaf{
				.kind = Markdown::PreparedEditLeafKind::ListItemText,
				.block = *path,
				.listItemIndex = *item,
			};
		} else {
			fail();
		}
	}
	openNode(std::move(leaf), &tree);
}

void RichQuoteRecorder::beginTableCellNode(
		std::size_t index,
		std::size_t row,
		std::size_t cell,
		const MTPRichText &tree) {
	auto leaf = std::optional<Leaf>();
	if (wantsLeaf()) {
		const auto path = blockPath(index);
		const auto rowIndex = CheckedIndex(row);
		const auto cellIndex = CheckedIndex(cell);
		if (path && rowIndex && cellIndex) {
			leaf = Leaf{
				.kind = Markdown::PreparedEditLeafKind::TableCellText,
				.block = *path,
				.tableRowIndex = *rowIndex,
				.tableCellIndex = *cellIndex,
			};
		} else {
			fail();
		}
	}
	openNode(std::move(leaf), &tree);
}

void RichQuoteRecorder::beginHiddenNode() {
	openNode(std::nullopt, nullptr);
}

void RichQuoteRecorder::openNode(
		std::optional<Leaf> leaf,
		const MTPRichText *tree) {
	_scopes.push_back(Scope::Node);
	if (_node.open) {
		fail();
		return;
	}
	_node = OpenNode{ .open = true };
	++_serial;
	if (_failed || suppressed()) {
		return;
	}
	auto &projection = _result._projection;
	const auto separatorAt = projection.size();
	if (!_result._nodes.empty()) {
		if (separatorAt >= kMaxSize) {
			fail();
			return;
		}
		projection.append(QChar('\n'));
	}
	_node.leaf = std::move(leaf);
	_node.separatorAt = int(separatorAt);
	_node.start = int(projection.size());
	_node.pieces = int(_result._pieces.size());
	_node.recorded = true;
	if (tree) {
		_node.tree = *tree;
	}
	_floor = _node.pieces;
}

void RichQuoteRecorder::closeNode() {
	auto node = base::take(_node);
	if (!node.open || !node.recorded || _failed) {
		return;
	}
	const auto till = int(_result._projection.size());
	if (till == node.start) {
		truncateProjection(node.separatorAt);
		erasePieces(node.pieces);
		return;
	}
	auto &pieces = _result._pieces;
	const auto displayed = std::any_of(
		begin(pieces) + node.pieces,
		end(pieces),
		[](const RichQuoteModel::Piece &piece) {
			return (piece.pageTill > piece.pageFrom);
		});
	if (!displayed) {
		node.leaf = std::nullopt;
		erasePieces(node.pieces);
	}
	auto &nodes = _result._nodes;
	if (nodes.size() >= std::size_t(kMaxSize) || !node.tree) {
		fail();
		return;
	}
	nodes.push_back({
		.from = node.start,
		.till = till,
		.leaf = std::move(node.leaf),
	});
	_result._nodePiecesTill.push_back(int(pieces.size()));
	_result._trees.push_back(std::move(*node.tree));
}

void RichQuoteRecorder::pushBlockChildren(std::size_t index) {
	const auto checked = CheckedIndex(index);
	const auto step = Markdown::PreparedEditBlockContainerStep{
		.kind = Markdown::PreparedEditBlockContainerKind::BlockChildren,
		.blockIndex = checked.value_or(-1),
	};
	pushStep(step, checked.has_value());
}

void RichQuoteRecorder::pushListItemChildren(
		std::size_t index,
		std::size_t listItem) {
	const auto checked = CheckedIndex(index);
	const auto item = CheckedIndex(listItem);
	const auto step = Markdown::PreparedEditBlockContainerStep{
		.kind = Markdown::PreparedEditBlockContainerKind::ListItemChildren,
		.blockIndex = checked.value_or(-1),
		.listItemIndex = item.value_or(-1),
	};
	pushStep(step, checked && item);
}

void RichQuoteRecorder::pushStep(
		Markdown::PreparedEditBlockContainerStep step,
		bool valid) {
	_scopes.push_back(Scope::Container);
	_path.steps.push_back(step);
	if (!valid || _node.open) {
		fail();
	}
}

void RichQuoteRecorder::pushSuppressed() {
	_scopes.push_back(Scope::Suppressed);
	++_suppressed;
	if (_node.open) {
		fail();
	}
}

void RichQuoteRecorder::pushHidden() {
	_scopes.push_back(Scope::Hidden);
	++_hidden;
	if (_node.open) {
		fail();
	}
}

void RichQuoteRecorder::popScope() {
	if (_scopes.empty()) {
		fail();
		return;
	}
	const auto scope = _scopes.back();
	_scopes.pop_back();
	switch (scope) {
	case Scope::Node:
		closeNode();
		return;
	case Scope::Container:
		_path.steps.pop_back();
		return;
	case Scope::Suppressed:
		--_suppressed;
		return;
	case Scope::Hidden:
		--_hidden;
		return;
	}
}

void RichQuoteRecorder::addPiece(RichQuoteModel::Piece piece) {
	auto &pieces = _result._pieces;
	if (pieces.size() >= std::size_t(kMaxSize)) {
		fail();
		return;
	}
	pieces.push_back(piece);
}

bool RichQuoteRecorder::labelPieces(
		int from,
		const NormalizedLabel &label,
		std::vector<RichQuoteModel::Piece> *result) const {
	const auto &offsets = label.offsets;
	if (offsets.empty()
		|| offsets.size() > std::size_t(kMaxSize)
		|| offsets.front() != 0
		|| offsets.back() != label.text.size()
		|| !std::is_sorted(begin(offsets), end(offsets))) {
		return false;
	}
	const auto &pieces = _result._pieces;
	const auto &labels = _result._labels;
	const auto &stored = _result._labelPieces;
	const auto size = int(offsets.size()) - 1;
	auto nested = _result.labelsFrom(from);
	auto position = 0;
	for (auto i = from, count = int(pieces.size()); i != count; ++i) {
		const auto &piece = pieces[i];
		if (piece.pageFrom != position
			|| piece.pageTill < piece.pageFrom
			|| piece.pageTill > size
			|| result->size() >= std::size_t(kMaxSize)) {
			return false;
		}
		position = piece.pageTill;
		const auto labelFrom = offsets[piece.pageFrom];
		const auto labelTill = offsets[piece.pageTill];
		if (nested < int(labels.size()) && labels[nested].piece == i) {
			const auto &inner = labels[nested++];
			if (!piece.atomic
				|| inner.length != labelTill - labelFrom
				|| inner.from > inner.till
				|| inner.till > int(stored.size())
				|| (std::size_t(inner.till - inner.from)
					> std::size_t(kMaxSize) - result->size())) {
				return false;
			}
			for (auto k = inner.from; k != inner.till; ++k) {
				auto composed = stored[k];
				composed.pageFrom += labelFrom;
				composed.pageTill += labelFrom;
				result->push_back(composed);
			}
		} else if (piece.atomic) {
			result->push_back({
				.from = piece.from,
				.till = piece.till,
				.pageFrom = labelFrom,
				.pageTill = labelTill,
				.atomic = true,
			});
		} else if (!labelText(piece, label, result)) {
			return false;
		}
	}
	return (position == size) && (nested == int(labels.size()));
}

bool RichQuoteRecorder::labelText(
		const RichQuoteModel::Piece &piece,
		const NormalizedLabel &label,
		std::vector<RichQuoteModel::Piece> *result) const {
	const auto &offsets = label.offsets;
	const auto &projection = _result._projection;
	if (piece.from < 0
		|| piece.till < piece.from
		|| piece.till > projection.size()
		|| piece.till - piece.from != piece.pageTill - piece.pageFrom) {
		return false;
	}
	auto from = piece.pageFrom;
	auto till = piece.pageTill;
	while (from != till && offsets[from] == offsets[from + 1]) {
		++from;
	}
	while (till != from && offsets[till - 1] == offsets[till]) {
		--till;
	}
	if (from == till) {
		return true;
	}
	const auto length = till - from;
	const auto start = piece.from + (from - piece.pageFrom);
	if (offsets[till] - offsets[from] != length
		|| (QStringView(projection).mid(start, length)
			!= QStringView(label.text).mid(offsets[from], length))) {
		return false;
	}
	result->push_back({
		.from = start,
		.till = start + length,
		.pageFrom = offsets[from],
		.pageTill = offsets[till],
	});
	return true;
}

void RichQuoteRecorder::addLabel(
		std::vector<RichQuoteModel::Piece> pieces,
		int length) {
	if (_failed) {
		return;
	}
	auto &stored = _result._labelPieces;
	auto &labels = _result._labels;
	if (pieces.size() > std::size_t(kMaxSize) - stored.size()
		|| labels.size() >= std::size_t(kMaxSize)) {
		fail();
		return;
	}
	const auto from = int(stored.size());
	stored.insert(end(stored), begin(pieces), end(pieces));
	labels.push_back({
		.piece = int(_result._pieces.size()) - 1,
		.from = from,
		.till = int(stored.size()),
		.length = length,
	});
}

void RichQuoteRecorder::erasePieces(int from) {
	auto &pieces = _result._pieces;
	pieces.erase(begin(pieces) + from, end(pieces));
	auto &labels = _result._labels;
	const auto label = _result.labelsFrom(from);
	if (label < int(labels.size())) {
		auto &stored = _result._labelPieces;
		stored.erase(begin(stored) + labels[label].from, end(stored));
		labels.erase(begin(labels) + label, end(labels));
	}
}

void RichQuoteRecorder::truncateProjection(int size) {
	_result._projection.truncate(size);
	auto &formulas = _result._formulas;
	while (!formulas.empty() && formulas.back().till > size) {
		formulas.pop_back();
	}
}

void RichQuoteRecorder::retarget(
		const Checkpoint &from,
		const Leaf &was,
		const Leaf &now) {
	auto &nodes = _result._nodes;
	for (auto i = begin(nodes) + from.nodes; i != end(nodes); ++i) {
		if (i->leaf == was) {
			i->leaf = now;
		}
	}
}

RichQuoteScope::RichQuoteScope(RichQuoteRecorder *recorder)
: _recorder(recorder) {
}

RichQuoteScope::~RichQuoteScope() {
	if (_recorder) {
		_recorder->popScope();
	}
}

RichQuoteScope RichQuoteScope::Node(
		RichQuoteRecorder *recorder,
		Markdown::PreparedEditLeafKind kind,
		std::size_t index,
		const MTPRichText &tree) {
	if (recorder) {
		recorder->beginNode(kind, index, tree);
	}
	return RichQuoteScope(recorder);
}

RichQuoteScope RichQuoteScope::ListItemNode(
		RichQuoteRecorder *recorder,
		std::size_t index,
		std::size_t listItem,
		const MTPRichText &tree) {
	if (recorder) {
		recorder->beginListItemNode(index, listItem, tree);
	}
	return RichQuoteScope(recorder);
}

RichQuoteScope RichQuoteScope::TableCellNode(
		RichQuoteRecorder *recorder,
		std::size_t index,
		std::size_t row,
		std::size_t cell,
		const MTPRichText &tree) {
	if (recorder) {
		recorder->beginTableCellNode(index, row, cell, tree);
	}
	return RichQuoteScope(recorder);
}

RichQuoteScope RichQuoteScope::HiddenNode(RichQuoteRecorder *recorder) {
	if (recorder) {
		recorder->beginHiddenNode();
	}
	return RichQuoteScope(recorder);
}

RichQuoteScope RichQuoteScope::BlockChildren(
		RichQuoteRecorder *recorder,
		std::size_t index) {
	if (recorder) {
		recorder->pushBlockChildren(index);
	}
	return RichQuoteScope(recorder);
}

RichQuoteScope RichQuoteScope::ListItemChildren(
		RichQuoteRecorder *recorder,
		std::size_t index,
		std::size_t listItem) {
	if (recorder) {
		recorder->pushListItemChildren(index, listItem);
	}
	return RichQuoteScope(recorder);
}

RichQuoteScope RichQuoteScope::Suppressed(RichQuoteRecorder *recorder) {
	if (recorder) {
		recorder->pushSuppressed();
	}
	return RichQuoteScope(recorder);
}

RichQuoteScope RichQuoteScope::Hidden(RichQuoteRecorder *recorder) {
	if (recorder) {
		recorder->pushHidden();
	}
	return RichQuoteScope(recorder);
}

std::optional<RichQuoteRange> LocateRichQuote(
		not_null<Main::Session*> session,
		const RichPage &page,
		const QVector<MTPRichText> &fragments,
		int offset) {
	const auto model = page.quoteModel.get();
	if (!model || model->nodes().empty() || fragments.isEmpty()) {
		return std::nullopt;
	}
	const auto texts = RichQuoteFragmentTexts(session, fragments);
	if (!texts) {
		return std::nullopt;
	}
	return LocateFragmentTexts(*model, *texts, offset);
}

} // namespace Iv
