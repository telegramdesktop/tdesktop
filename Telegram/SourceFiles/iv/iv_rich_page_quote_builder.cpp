/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/iv_rich_page_quote_builder.h"

#include "iv/iv_rich_page.h"
#include "iv/iv_rich_page_quote.h"

#include <algorithm>
#include <limits>

namespace Iv {
namespace {

constexpr auto kMaxFragments = 32;
constexpr auto kMaxLength = qsizetype(std::numeric_limits<int>::max());

enum class ShapeKind : uchar {
	Empty,
	Plain,
	Image,
	Atom,
	Date,
	Wrapper,
	Concat,
	Diff,
	Button,
};

struct Shape {
	ShapeKind kind = ShapeKind::Empty;
	QString text;
	const MTPRichText *inner = nullptr;
	const MTPRichText *old = nullptr;
	const QVector<MTPRichText> *parts = nullptr;
};

[[nodiscard]] Shape WrapperShape(const MTPRichText &inner) {
	return { .kind = ShapeKind::Wrapper, .inner = &inner };
}

[[nodiscard]] Shape ShapeOf(const MTPRichText &text) {
	return text.match([](const MTPDtextEmpty &) {
		return Shape();
	}, [](const MTPDtextPlain &data) {
		return Shape{ .kind = ShapeKind::Plain, .text = qs(data.vtext()) };
	}, [](const MTPDtextBold &data) {
		return WrapperShape(data.vtext());
	}, [](const MTPDtextItalic &data) {
		return WrapperShape(data.vtext());
	}, [](const MTPDtextUnderline &data) {
		return WrapperShape(data.vtext());
	}, [](const MTPDtextStrike &data) {
		return WrapperShape(data.vtext());
	}, [](const MTPDtextFixed &data) {
		return WrapperShape(data.vtext());
	}, [](const MTPDtextUrl &data) {
		return WrapperShape(data.vtext());
	}, [](const MTPDtextEmail &data) {
		return WrapperShape(data.vtext());
	}, [](const MTPDtextConcat &data) {
		return Shape{ .kind = ShapeKind::Concat, .parts = &data.vtexts().v };
	}, [](const MTPDtextSubscript &data) {
		return WrapperShape(data.vtext());
	}, [](const MTPDtextSuperscript &data) {
		return WrapperShape(data.vtext());
	}, [](const MTPDtextMarked &data) {
		return WrapperShape(data.vtext());
	}, [](const MTPDtextPhone &data) {
		return WrapperShape(data.vtext());
	}, [](const MTPDtextImage &) {
		return Shape{ .kind = ShapeKind::Image };
	}, [](const MTPDtextAnchor &data) {
		return WrapperShape(data.vtext());
	}, [](const MTPDtextMath &data) {
		return Shape{ .kind = ShapeKind::Atom, .text = qs(data.vsource()) };
	}, [](const MTPDtextCustomEmoji &data) {
		return Shape{ .kind = ShapeKind::Atom, .text = qs(data.valt()) };
	}, [](const MTPDtextSpoiler &data) {
		return WrapperShape(data.vtext());
	}, [](const MTPDtextMention &data) {
		return WrapperShape(data.vtext());
	}, [](const MTPDtextHashtag &data) {
		return WrapperShape(data.vtext());
	}, [](const MTPDtextBotCommand &data) {
		return WrapperShape(data.vtext());
	}, [](const MTPDtextCashtag &data) {
		return WrapperShape(data.vtext());
	}, [](const MTPDtextAutoUrl &data) {
		return WrapperShape(data.vtext());
	}, [](const MTPDtextAutoEmail &data) {
		return WrapperShape(data.vtext());
	}, [](const MTPDtextAutoPhone &data) {
		return WrapperShape(data.vtext());
	}, [](const MTPDtextBankCard &data) {
		return WrapperShape(data.vtext());
	}, [](const MTPDtextMentionName &data) {
		return WrapperShape(data.vtext());
	}, [](const MTPDtextDate &data) {
		return Shape{ .kind = ShapeKind::Date, .inner = &data.vtext() };
	}, [](const MTPDtextDiff &data) {
		return Shape{
			.kind = ShapeKind::Diff,
			.inner = &data.vtext(),
			.old = &data.vold_text(),
		};
	}, [](const MTPDtextButton &data) {
		return Shape{ .kind = ShapeKind::Button, .inner = &data.vtext() };
	}, [](const MTPDtextTonAddress &data) {
		return WrapperShape(data.vtext());
	});
}

[[nodiscard]] std::optional<MTPRichText> Rewrap(
		const MTPRichText &wrapper,
		const MTPRichText &inner) {
	using Result = std::optional<MTPRichText>;
	return wrapper.match([&](const MTPDtextBold &) {
		return Result(MTP_textBold(inner));
	}, [&](const MTPDtextItalic &) {
		return Result(MTP_textItalic(inner));
	}, [&](const MTPDtextUnderline &) {
		return Result(MTP_textUnderline(inner));
	}, [&](const MTPDtextStrike &) {
		return Result(MTP_textStrike(inner));
	}, [&](const MTPDtextFixed &) {
		return Result(MTP_textFixed(inner));
	}, [&](const MTPDtextUrl &data) {
		return Result(MTP_textUrl(inner, data.vurl(), data.vwebpage_id()));
	}, [&](const MTPDtextEmail &data) {
		return Result(MTP_textEmail(inner, data.vemail()));
	}, [&](const MTPDtextSubscript &) {
		return Result(MTP_textSubscript(inner));
	}, [&](const MTPDtextSuperscript &) {
		return Result(MTP_textSuperscript(inner));
	}, [&](const MTPDtextMarked &) {
		return Result(MTP_textMarked(inner));
	}, [&](const MTPDtextPhone &data) {
		return Result(MTP_textPhone(inner, data.vphone()));
	}, [&](const MTPDtextAnchor &data) {
		return Result(MTP_textAnchor(inner, data.vname()));
	}, [&](const MTPDtextSpoiler &) {
		return Result(MTP_textSpoiler(inner));
	}, [&](const MTPDtextMention &) {
		return Result(MTP_textMention(inner));
	}, [&](const MTPDtextHashtag &) {
		return Result(MTP_textHashtag(inner));
	}, [&](const MTPDtextBotCommand &) {
		return Result(MTP_textBotCommand(inner));
	}, [&](const MTPDtextCashtag &) {
		return Result(MTP_textCashtag(inner));
	}, [&](const MTPDtextAutoUrl &) {
		return Result(MTP_textAutoUrl(inner));
	}, [&](const MTPDtextAutoEmail &) {
		return Result(MTP_textAutoEmail(inner));
	}, [&](const MTPDtextAutoPhone &) {
		return Result(MTP_textAutoPhone(inner));
	}, [&](const MTPDtextBankCard &) {
		return Result(MTP_textBankCard(inner));
	}, [&](const MTPDtextMentionName &data) {
		return Result(MTP_textMentionName(inner, data.vuser_id()));
	}, [&](const MTPDtextDate &data) {
		return Result(MTP_textDate(data.vflags(), inner, data.vdate()));
	}, [&](const MTPDtextTonAddress &) {
		return Result(MTP_textTonAddress(inner));
	}, [](const auto &) {
		return Result();
	});
}

struct Measured {
	QString text;
	std::vector<RichQuoteRange> atoms;
};

[[nodiscard]] bool Measure(
		const MTPRichText &text,
		int depth,
		bool atoms,
		Measured *to) {
	if (depth >= kRichQuoteMaxTextDepth) {
		return false;
	}
	const auto shape = ShapeOf(text);
	switch (shape.kind) {
	case ShapeKind::Empty:
	case ShapeKind::Image:
		return true;
	case ShapeKind::Plain:
	case ShapeKind::Atom: {
		const auto from = to->text.size();
		if (shape.text.size() > kMaxLength - from) {
			return false;
		}
		to->text.append(shape.text);
		if (atoms
			&& shape.kind == ShapeKind::Atom
			&& !shape.text.isEmpty()) {
			to->atoms.push_back({
				.from = int(from),
				.till = int(to->text.size()),
			});
		}
		return true;
	}
	case ShapeKind::Date: {
		const auto from = int(to->text.size());
		if (!Measure(*shape.inner, depth + 1, false, to)) {
			return false;
		}
		const auto till = int(to->text.size());
		if (atoms && till > from) {
			to->atoms.push_back({ .from = from, .till = till });
		}
		return true;
	}
	case ShapeKind::Wrapper:
	case ShapeKind::Diff:
	case ShapeKind::Button:
		return Measure(*shape.inner, depth + 1, atoms, to);
	case ShapeKind::Concat:
		for (const auto &part : *shape.parts) {
			if (!Measure(part, depth + 1, atoms, to)) {
				return false;
			}
		}
		return true;
	}
	return false;
}

[[nodiscard]] std::optional<Measured> MeasureNode(
		const RichQuoteModel &model,
		int index) {
	const auto tree = model.nodeTree(index);
	auto result = Measured();
	if (!tree
		|| !Measure(*tree, 0, true, &result)
		|| QStringView(result.text) != model.nodeText(index)) {
		return std::nullopt;
	}
	return result;
}

[[nodiscard]] int SnapOutOfAtom(
		const std::vector<RichQuoteRange> &atoms,
		int position,
		bool forward) {
	const auto atom = std::upper_bound(
		begin(atoms),
		end(atoms),
		position,
		[](int value, const RichQuoteRange &entry) {
			return value < entry.till;
		});
	if (atom == end(atoms) || atom->from >= position) {
		return position;
	}
	return forward ? atom->till : atom->from;
}

[[nodiscard]] bool SplitsSurrogatePair(QStringView text, int position) {
	return (position > 0)
		&& (position < text.size())
		&& text[position - 1].isHighSurrogate()
		&& text[position].isLowSurrogate();
}

struct Edges {
	int from = 0;
	int till = 0;
	bool left = false;
	bool right = false;
};

class Cutter final {
public:
	explicit Cutter(Edges edges);

	[[nodiscard]] std::optional<MTPRichText> cut(const MTPRichText &tree);

private:
	struct Piece {
		std::optional<MTPRichText> text;
		bool same = false;
	};
	enum class Coverage : uchar {
		None,
		Whole,
		Partial,
	};

	[[nodiscard]] Coverage coverage(int from, int till) const;
	[[nodiscard]] Piece visit(const MTPRichText &text, int depth);
	[[nodiscard]] Piece whole(const MTPRichText &text, int depth);
	[[nodiscard]] Piece partial(
		const MTPRichText &text,
		const Shape &shape,
		int depth);
	[[nodiscard]] Piece diff(
		const MTPRichText &text,
		Piece inner,
		const MTPRichText &old,
		int depth);
	[[nodiscard]] bool advance(qsizetype length);
	[[nodiscard]] Piece fail();
	[[nodiscard]] MTPRichText cutPlain(const QString &text, int from) const;

	Edges _edges;
	int _offset = 0;
	bool _failed = false;

};

Cutter::Cutter(Edges edges)
: _edges(edges) {
}

std::optional<MTPRichText> Cutter::cut(const MTPRichText &tree) {
	auto result = visit(tree, 0);
	if (_failed) {
		return std::nullopt;
	}
	return std::move(result.text);
}

Cutter::Coverage Cutter::coverage(int from, int till) const {
	const auto left = _edges.left;
	const auto right = _edges.right;
	if (from == till) {
		return ((!left || from > _edges.from)
			&& (!right || from < _edges.till))
			? Coverage::Whole
			: Coverage::None;
	} else if ((left && till <= _edges.from)
		|| (right && from >= _edges.till)) {
		return Coverage::None;
	} else if ((!left || from >= _edges.from)
		&& (!right || till <= _edges.till)) {
		return Coverage::Whole;
	}
	return Coverage::Partial;
}

Cutter::Piece Cutter::visit(const MTPRichText &text, int depth) {
	if (_failed) {
		return {};
	} else if (depth >= kRichQuoteMaxTextDepth) {
		return fail();
	}
	auto measured = Measured();
	if (!Measure(text, depth, false, &measured)) {
		return fail();
	}
	const auto from = _offset;
	const auto length = measured.text.size();
	if (length > kMaxLength - from) {
		return fail();
	}
	const auto till = from + int(length);
	switch (coverage(from, till)) {
	case Coverage::None:
		_offset = till;
		return {};
	case Coverage::Whole:
		return whole(text, depth);
	case Coverage::Partial:
		return partial(text, ShapeOf(text), depth);
	}
	return fail();
}

Cutter::Piece Cutter::whole(const MTPRichText &text, int depth) {
	if (_failed) {
		return {};
	} else if (depth >= kRichQuoteMaxTextDepth) {
		return fail();
	}
	const auto shape = ShapeOf(text);
	switch (shape.kind) {
	case ShapeKind::Empty:
	case ShapeKind::Plain:
	case ShapeKind::Atom:
		return advance(shape.text.size()) ? Piece{ text, true } : Piece();
	case ShapeKind::Image:
		return {};
	case ShapeKind::Button: {
		// a label's formatting is compared exactly, only the button as text
		const auto from = _offset;
		auto inner = whole(*shape.inner, depth + 1);
		if (_failed || !inner.text || _offset == from) {
			return {};
		}
		return { std::move(inner.text), false };
	}
	case ShapeKind::Date:
	case ShapeKind::Wrapper: {
		auto inner = whole(*shape.inner, depth + 1);
		if (_failed || !inner.text) {
			return {};
		} else if (inner.same) {
			return { text, true };
		}
		auto result = Rewrap(text, *inner.text);
		return result ? Piece{ std::move(result), false } : fail();
	}
	case ShapeKind::Concat: {
		auto parts = QVector<MTPRichText>();
		auto same = true;
		for (const auto &part : *shape.parts) {
			auto piece = whole(part, depth + 1);
			if (_failed) {
				return {};
			}
			same = same && piece.same;
			if (piece.text) {
				parts.push_back(std::move(*piece.text));
			}
		}
		if (same) {
			return { text, true };
		} else if (parts.isEmpty()) {
			return {};
		}
		return {
			MTP_textConcat(MTP_vector<MTPRichText>(std::move(parts))),
			false,
		};
	}
	case ShapeKind::Diff:
		return diff(text, whole(*shape.inner, depth + 1), *shape.old, depth);
	}
	return fail();
}

Cutter::Piece Cutter::partial(
		const MTPRichText &text,
		const Shape &shape,
		int depth) {
	switch (shape.kind) {
	case ShapeKind::Plain: {
		const auto from = _offset;
		if (!advance(shape.text.size())) {
			return {};
		}
		return { cutPlain(shape.text, from), false };
	}
	case ShapeKind::Button: {
		auto inner = visit(*shape.inner, depth + 1);
		if (_failed || !inner.text) {
			return fail();
		}
		return { std::move(inner.text), false };
	}
	case ShapeKind::Wrapper: {
		auto inner = visit(*shape.inner, depth + 1);
		if (_failed || !inner.text) {
			return fail();
		}
		auto result = Rewrap(text, *inner.text);
		return result ? Piece{ std::move(result), false } : fail();
	}
	case ShapeKind::Concat: {
		auto parts = QVector<MTPRichText>();
		for (const auto &part : *shape.parts) {
			auto piece = visit(part, depth + 1);
			if (_failed) {
				return {};
			} else if (piece.text) {
				parts.push_back(std::move(*piece.text));
			}
		}
		if (parts.isEmpty()) {
			return fail();
		}
		return {
			MTP_textConcat(MTP_vector<MTPRichText>(std::move(parts))),
			false,
		};
	}
	case ShapeKind::Diff: {
		auto inner = visit(*shape.inner, depth + 1);
		if (_failed || !inner.text) {
			return fail();
		}
		return diff(text, std::move(inner), *shape.old, depth);
	}
	case ShapeKind::Empty:
	case ShapeKind::Image:
	case ShapeKind::Atom:
	case ShapeKind::Date:
		return fail();
	}
	return fail();
}

Cutter::Piece Cutter::diff(
		const MTPRichText &text,
		Piece inner,
		const MTPRichText &old,
		int depth) {
	if (_failed) {
		return {};
	}
	// WHY: the parser never visits old_text, so this walk is its only
	// depth bound, and a button or an image in it must not reach the server.
	const auto offset = base::take(_offset);
	auto was = whole(old, depth + 1);
	_offset = offset;
	if (_failed) {
		return {};
	} else if (inner.same && was.same) {
		return { text, true };
	}
	return {
		MTP_textDiff(
			inner.text.value_or(MTP_textEmpty()),
			was.text.value_or(MTP_textEmpty())),
		false,
	};
}

bool Cutter::advance(qsizetype length) {
	if (length > kMaxLength - _offset) {
		_failed = true;
		return false;
	}
	_offset += int(length);
	return true;
}

Cutter::Piece Cutter::fail() {
	_failed = true;
	return {};
}

MTPRichText Cutter::cutPlain(const QString &text, int from) const {
	const auto till = from + int(text.size());
	const auto a = _edges.left ? std::max(_edges.from, from) : from;
	const auto b = _edges.right ? std::min(_edges.till, till) : till;
	return MTP_textPlain(MTP_string(text.mid(a - from, b - a)));
}

struct CodePoints {
	int position = 0;
	int count = 0;
};

[[nodiscard]] CodePoints AdvanceCodePoints(
		QStringView text,
		int from,
		int till,
		int limit) {
	auto result = CodePoints{ .position = from };
	while (result.position < till && result.count < limit) {
		const auto pair = (result.position + 1 < till)
			&& text[result.position].isHighSurrogate()
			&& text[result.position + 1].isLowSurrogate();
		result.position += pair ? 2 : 1;
		++result.count;
	}
	return result;
}

struct Span {
	int first = 0;
	int last = 0;
	int start = 0;
	int end = 0;
	bool overflown = false;
};

[[nodiscard]] std::optional<Span> CoveredSpan(
		const RichQuoteModel &model,
		RichQuoteRange range) {
	const auto &nodes = model.nodes();
	const auto size = int(model.projection().size());
	const auto from = std::clamp(range.from, 0, size);
	const auto till = std::clamp(range.till, 0, size);
	if (from >= till) {
		return std::nullopt;
	}
	const auto first = std::upper_bound(
		begin(nodes),
		end(nodes),
		from,
		[](int value, const RichQuoteNode &entry) {
			return value < entry.till;
		});
	const auto last = std::lower_bound(
		first,
		end(nodes),
		till,
		[](const RichQuoteNode &entry, int value) {
			return entry.from < value;
		});
	if (first == last) {
		return std::nullopt;
	}
	return Span{
		.first = int(first - begin(nodes)),
		.last = int(last - begin(nodes)) - 1,
		.start = std::max(from, first->from),
		.end = std::min(till, (last - 1)->till),
	};
}

[[nodiscard]] bool MoveEdgesOutOfAtoms(
		const RichQuoteModel &model,
		Span *span) {
	const auto first = MeasureNode(model, span->first);
	const auto last = (span->last == span->first)
		? first
		: MeasureNode(model, span->last);
	if (!first || !last) {
		return false;
	}
	const auto &nodes = model.nodes();
	const auto firstFrom = nodes[span->first].from;
	const auto lastFrom = nodes[span->last].from;
	span->start = firstFrom
		+ SnapOutOfAtom(first->atoms, span->start - firstFrom, false);
	span->end = lastFrom
		+ SnapOutOfAtom(last->atoms, span->end - lastFrom, true);
	const auto projection = QStringView(model.projection());
	if (SplitsSurrogatePair(projection, span->start)) {
		--span->start;
	}
	if (SplitsSurrogatePair(projection, span->end)) {
		++span->end;
	}
	return true;
}

void LimitFragments(const RichQuoteModel &model, Span *span) {
	if (span->last - span->first >= kMaxFragments) {
		span->last = span->first + kMaxFragments - 1;
		span->end = model.nodes()[span->last].till;
		span->overflown = true;
	}
}

[[nodiscard]] bool LimitLength(
		const RichQuoteModel &model,
		Span *span,
		int lengthLimit) {
	const auto &nodes = model.nodes();
	const auto projection = QStringView(model.projection());
	auto remaining = lengthLimit;
	for (auto i = span->first; i <= span->last; ++i) {
		const auto &node = nodes[i];
		const auto from = (i == span->first) ? span->start : node.from;
		const auto till = (i == span->last) ? span->end : node.till;
		const auto advanced = AdvanceCodePoints(
			projection,
			from,
			till,
			remaining);
		if (advanced.position >= till) {
			remaining -= advanced.count;
			continue;
		}
		span->overflown = true;
		const auto measured = MeasureNode(model, i);
		if (!measured) {
			return false;
		}
		const auto cut = node.from + SnapOutOfAtom(
			measured->atoms,
			advanced.position - node.from,
			false);
		if (cut > from) {
			span->last = i;
			span->end = cut;
		} else if (i > span->first) {
			span->last = i - 1;
			span->end = nodes[i - 1].till;
		} else {
			return false;
		}
		return true;
	}
	return true;
}

[[nodiscard]] std::optional<QVector<MTPRichText>> CutFragments(
		const RichQuoteModel &model,
		const Span &span) {
	const auto &nodes = model.nodes();
	auto result = QVector<MTPRichText>();
	result.reserve(span.last - span.first + 1);
	for (auto i = span.first; i <= span.last; ++i) {
		const auto &node = nodes[i];
		const auto tree = model.nodeTree(i);
		if (!tree || !MeasureNode(model, i)) {
			return std::nullopt;
		}
		auto fragment = Cutter(Edges{
			.from = span.start - node.from,
			.till = span.end - node.from,
			.left = (i == span.first && span.start > node.from),
			.right = (i == span.last && span.end < node.till),
		}).cut(*tree);
		if (!fragment) {
			return std::nullopt;
		}
		result.push_back(std::move(*fragment));
	}
	return result;
}

} // namespace

std::optional<BuiltRichQuote> BuildRichQuote(
		not_null<Main::Session*> session,
		const RichPage &page,
		RichQuoteRange range,
		int lengthLimit) {
	const auto model = page.quoteModel.get();
	if (!model || lengthLimit <= 0) {
		return std::nullopt;
	}
	auto span = CoveredSpan(*model, range);
	if (!span || !MoveEdgesOutOfAtoms(*model, &*span)) {
		return std::nullopt;
	}
	LimitFragments(*model, &*span);
	if (!LimitLength(*model, &*span, lengthLimit)) {
		return std::nullopt;
	}
	auto fragments = CutFragments(*model, *span);
	const auto built = RichQuoteRange{
		.from = span->start,
		.till = span->end,
	};
	if (!fragments
		|| LocateRichQuote(session, page, *fragments, built.from) != built) {
		return std::nullopt;
	}
	return BuiltRichQuote{
		.fragments = MTP_vector<MTPRichText>(std::move(*fragments)),
		.offset = built.from,
		.overflown = span->overflown,
	};
}

} // namespace Iv
