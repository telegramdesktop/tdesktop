/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/boxes/choose_font_box.h"

#include "base/event_filter.h"
#include "lang/lang_keys.h"
#include "ui/boxes/confirm_box.h"
#include "ui/chat/chat_style.h"
#include "ui/effects/ripple_animation.h"
#include "ui/layers/generic_box.h"
#include "ui/style/style_core_font.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/multi_select.h"
#include "ui/widgets/scroll_area.h"
#include "ui/cached_round_corners.h"
#include "ui/painter.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_settings.h"
#include "styles/style_layers.h"
#include "styles/style_window.h"

#include <QtGui/QFontDatabase>

namespace Ui {
namespace {

constexpr auto kMinTextWidth = 120;
constexpr auto kMaxTextWidth = 320;
constexpr auto kMaxTextLines = 3;

struct PreviewRequest {
	QString family;
	QColor msgBg;
	QColor msgShadow;
	QColor replyBar;
	QColor replyNameFg;
	QColor textFg;
	QImage bubbleTail;
};

class PreviewPainter {
public:
	PreviewPainter(const QImage &bg, PreviewRequest request);

	QImage takeResult();

private:
	void layout();

	void paintBubble(QPainter &p);
	void paintContent(QPainter &p);
	void paintReply(QPainter &p);
	void paintMessage(QPainter &p);

	void validateBubbleCache();

	const PreviewRequest _request;
	const style::owned_color _msgBg;
	const style::owned_color _msgShadow;

	QFont _nameFont;
	QFontMetricsF _nameMetrics;
	int _nameFontHeight = 0;
	QFont _textFont;
	QFontMetricsF _textMetrics;
	int _textFontHeight = 0;

	QString _nameText;
	QString _replyText;
	QString _messageText;

	int _boundingLimit = 0;

	QRect _replyRect;
	QRect _name;
	QRect _reply;
	QRect _message;
	QRect _content;
	QRect _bubble;
	QSize _outer;

	Ui::CornersPixmaps _bubbleCorners;
	QPixmap _bubbleShadowBottomRight;

	QImage _result;

};

class Selector final : public Ui::RpWidget {
public:
	Selector(
		not_null<QWidget*> parent,
		const QString &now,
		rpl::producer<QString> filter,
		rpl::producer<> submits,
		Fn<void(QString)> chosen,
		Fn<void(Ui::ScrollToRequest, anim::type)> scrollTo);

	void initScroll(anim::type animated);
	void setMinHeight(int height);
	void selectSkip(Qt::Key direction);

private:
	struct Entry {
		QString id;
		QString key;
		QString text;
		QStringList keywords;
		QImage cache;
		std::unique_ptr<Ui::RadioView> check;
		std::unique_ptr<Ui::RippleAnimation> ripple;
		int paletteVersion = 0;
	};
	[[nodiscard]] static std::vector<Entry> FullList(const QString &now);

	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

	[[nodiscard]] bool searching() const;
	[[nodiscard]] int shownRowsCount() const;
	[[nodiscard]] Entry &shownRowAt(int index);

	void applyFilter(const QString &query);
	void updateSelected(int selected);
	void updatePressed(int pressed);
	void updateRow(int index);
	void updateRow(not_null<Entry*> row, int hint);
	void addRipple(int index, QPoint position);
	void validateCache(Entry &row);
	void choose(Entry &row);

	const style::SettingsButton &_st;
	std::vector<Entry> _rows;
	std::vector<not_null<Entry*>> _filtered;
	QString _chosen;
	int _selected = -1;
	int _pressed = -1;

	std::optional<QPoint> _lastGlobalPoint;
	bool _selectedByKeyboard = false;

	Fn<void(QString)> _callback;
	Fn<void(Ui::ScrollToRequest, anim::type)> _scrollTo;

	int _rowsSkip = 0;
	int _rowHeight = 0;
	int _minHeight = 0;

	QString _query;
	QStringList _queryWords;

	rpl::lifetime _lifetime;

};

Selector::Selector(
	not_null<QWidget*> parent,
	const QString &now,
	rpl::producer<QString> filter,
	rpl::producer<> submits,
	Fn<void(QString)> chosen,
	Fn<void(Ui::ScrollToRequest, anim::type)> scrollTo)
: RpWidget(parent)
, _st(st::settingsButton)
, _rows(FullList(now))
, _chosen(now)
, _callback(std::move(chosen))
, _scrollTo(std::move(scrollTo))
, _rowsSkip(st::settingsInfoPhotoSkip)
, _rowHeight(_st.height + _st.padding.top() + _st.padding.bottom()) {
	setMouseTracking(true);

	std::move(filter) | rpl::start_with_next([=](const QString &query) {
		applyFilter(query);
	}, _lifetime);

	std::move(
		submits
	) | rpl::start_with_next([=] {
		if (_selected >= 0) {
			choose(shownRowAt(_selected));
		} else if (searching() && !_filtered.empty()) {
			choose(*_filtered.front());
		}
	}, _lifetime);
}

void Selector::applyFilter(const QString &query) {
	if (_query == query) {
		return;
	}
	_query = query;

	updateSelected(-1);
	updatePressed(-1);

	_queryWords = TextUtilities::PrepareSearchWords(_query);

	const auto skip = [](
			const QStringList &haystack,
			const QStringList &needles) {
		const auto find = [](
				const QStringList &haystack,
				const QString &needle) {
			for (const auto &item : haystack) {
				if (item.startsWith(needle)) {
					return true;
				}
			}
			return false;
		};
		for (const auto &needle : needles) {
			if (!find(haystack, needle)) {
				return true;
			}
		}
		return false;
	};

	_filtered.clear();
	if (!_queryWords.isEmpty()) {
		_filtered.reserve(_rows.size());
		for (auto &row : _rows) {
			if (!skip(row.keywords, _queryWords)) {
				_filtered.push_back(&row);
			} else {
				row.ripple = nullptr;
			}
		}
	}

	resizeToWidth(width());
	Ui::SendPendingMoveResizeEvents(this);
	update();
}

void Selector::updateSelected(int selected) {
	if (_selected == selected) {
		return;
	}
	const auto was = (_selected >= 0);
	updateRow(_selected);
	_selected = selected;
	updateRow(_selected);
	const auto now = (_selected >= 0);
	if (was != now) {
		setCursor(now ? style::cur_pointer : style::cur_default);
	}
	if (_selectedByKeyboard) {
		const auto top = (_selected > 0)
			? (_rowsSkip + _selected * _rowHeight)
			: 0;
		const auto bottom = (_selected > 0)
			? (top + _rowHeight)
			: _selected
			? 0
			: _rowHeight;
		_scrollTo({ top, bottom }, anim::type::instant);
	}
}

void Selector::updatePressed(int pressed) {
	if (_pressed == pressed) {
		return;
	} else if (_pressed >= 0) {
		if (auto &ripple = shownRowAt(_pressed).ripple) {
			ripple->lastStop();
		}
	}
	updateRow(_pressed);
	_pressed = pressed;
	updateRow(_pressed);
}

void Selector::updateRow(int index) {
	if (index >= 0) {
		update(0, _rowsSkip + index * _rowHeight, width(), _rowHeight);
	}
}

void Selector::updateRow(not_null<Entry*> row, int hint) {
	if (hint >= 0 && hint < shownRowsCount() && &shownRowAt(hint) == row) {
		updateRow(hint);
	} else if (searching()) {
		const auto i = ranges::find(_filtered, row);
		if (i != end(_filtered)) {
			updateRow(int(i - begin(_filtered)));
		}
	} else {
		const auto index = int(row.get() - &_rows[0]);
		Assert(index >= 0 && index < _rows.size());
		updateRow(index);
	}
}

void Selector::validateCache(Entry &row) {
	const auto version = style::PaletteVersion();
	if (row.cache.isNull()) {
		const auto ratio = style::DevicePixelRatio();
		row.cache = QImage(
			QSize(width(), _rowHeight) * ratio,
			QImage::Format_ARGB32_Premultiplied);
		row.cache.setDevicePixelRatio(ratio);
	} else if (row.paletteVersion == version) {
		return;
	}
	row.cache.fill(Qt::transparent);
	auto font = style::ResolveFont(row.id, 0, st::boxFontSize);
	auto p = QPainter(&row.cache);
	p.setFont(font);
	p.setPen(st::windowFg);

	const auto textw = width() - _st.padding.left() - _st.padding.right();
	const auto metrics = QFontMetrics(font);
	const auto textt = (_rowHeight - metrics.height()) / 2.;
	p.drawText(
		_st.padding.left(),
		int(base::SafeRound(textt)) + metrics.ascent(),
		metrics.elidedText(row.text, Qt::ElideRight, textw));
}

bool Selector::searching() const {
	return !_queryWords.isEmpty();
}

int Selector::shownRowsCount() const {
	return searching() ? int(_filtered.size()) : int(_rows.size());
}

Selector::Entry &Selector::shownRowAt(int index) {
	return searching() ? *_filtered[index] : _rows[index];
}

void Selector::setMinHeight(int height) {
	_minHeight = height;
	if (_minHeight > 0) {
		resizeToWidth(width());
	}
}

void Selector::selectSkip(Qt::Key key) {
	const auto count = shownRowsCount();
	if (key == Qt::Key_Down) {
		if (_selected + 1 < count) {
			_selectedByKeyboard = true;
			updateSelected(_selected + 1);
		}
	} else if (key == Qt::Key_Up) {
		if (_selected >= 0) {
			_selectedByKeyboard = true;
			updateSelected(_selected - 1);
		}
	} else if (key == Qt::Key_PageDown) {
		const auto change = _minHeight / _rowHeight;
		if (_selected + 1 < count) {
			_selectedByKeyboard = true;
			updateSelected(std::min(_selected + change, count - 1));
		}
	} else if (key == Qt::Key_PageUp) {
		const auto change = _minHeight / _rowHeight;
		if (_selected > 0) {
			_selectedByKeyboard = true;
			updateSelected(std::max(_selected - change, 0));
		} else if (!_selected) {
			_selectedByKeyboard = true;
			updateSelected(-1);
		}
	}
}

void Selector::initScroll(anim::type animated) {
	const auto index = [&] {
		if (searching()) {
			const auto i = ranges::find(_filtered, _chosen, &Entry::id);
			if (i != end(_filtered)) {
				return int(i - begin(_filtered));
			}
			return -1;
		}
		const auto i = ranges::find(_rows, _chosen, &Entry::id);
		Assert(i != end(_rows));
		return int(i - begin(_rows));
	}();
	if (index >= 0) {
		const auto top = _rowsSkip + index * _rowHeight;
		const auto use = std::max(top - (_minHeight - _rowHeight) / 2, 0);
		_scrollTo({ use, use + _minHeight }, animated);
	}
}

int Selector::resizeGetHeight(int newWidth) {
	const auto added = 2 * _rowsSkip;
	return std::max(added + shownRowsCount() * _rowHeight, _minHeight);
}

void Selector::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	const auto rows = shownRowsCount();
	if (!rows) {
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			QRect(0, 0, width(), height() * 2 / 3),
			tr::lng_font_not_found(tr::now),
			style::al_center);
		return;
	}
	const auto clip = e->rect();
	const auto clipped = std::max(clip.y() - _rowsSkip, 0);
	const auto from = std::min(clipped / _rowHeight, rows);
	const auto till = std::min(
		(clip.y() + clip.height() - _rowsSkip + _rowHeight - 1) / _rowHeight,
		rows);
	const auto active = (_pressed >= 0) ? _pressed : _selected;
	for (auto i = from; i != till; ++i) {
		auto &row = shownRowAt(i);
		const auto y = _rowsSkip + i * _rowHeight;
		const auto bg = (i == active) ? st::windowBgOver : st::windowBg;
		const auto rect = QRect(0, y, width(), _rowHeight);
		p.fillRect(rect, bg);

		if (row.ripple) {
			row.ripple->paint(p, 0, y, width());
			if (row.ripple->empty()) {
				row.ripple = nullptr;
			}
		}

		validateCache(row);
		p.drawImage(0, y, row.cache);

		if (!row.check) {
			row.check = std::make_unique<Ui::RadioView>(
				st::langsRadio,
				(row.id == _chosen),
				[=, row = &row] { updateRow(row, i); });
		}
		row.check->paint(
			p,
			_st.iconLeft,
			y + (_rowHeight - st::langsRadio.diameter) / 2,
			width());
	}
}

void Selector::leaveEventHook(QEvent *e) {
	_lastGlobalPoint = std::nullopt;
	if (!_selectedByKeyboard) {
		updateSelected(-1);
	}
}

void Selector::mouseMoveEvent(QMouseEvent *e) {
	if (!_lastGlobalPoint) {
		_lastGlobalPoint = e->globalPos();
		if (_selectedByKeyboard) {
			return;
		}
	} else if (*_lastGlobalPoint == e->globalPos() && _selectedByKeyboard) {
		return;
	} else {
		_lastGlobalPoint = e->globalPos();
	}
	_selectedByKeyboard = false;
	const auto y = e->y() - _rowsSkip;
	const auto index = (y >= 0) ? (y / _rowHeight) : -1;
	updateSelected((index >= 0 && index < shownRowsCount()) ? index : -1);
}

void Selector::mousePressEvent(QMouseEvent *e) {
	updatePressed(_selected);
	if (_pressed >= 0) {
		addRipple(_pressed, e->pos());
	}
}

void Selector::mouseReleaseEvent(QMouseEvent *e) {
	const auto pressed = _pressed;
	updatePressed(-1);
	if (pressed == _selected) {
		choose(shownRowAt(pressed));
	}
}

void Selector::choose(Entry &row) {
	const auto id = row.id;
	if (_chosen != id) {
		const auto i = ranges::find(_rows, _chosen, &Entry::id);
		Assert(i != end(_rows));
		if (i->check) {
			i->check->setChecked(false, anim::type::normal);
		}
		_chosen = id;
		if (row.check) {
			row.check->setChecked(true, anim::type::normal);
		}
	}
	const auto animated = searching()
		? anim::type::instant
		: anim::type::normal;
	_callback(id);
	initScroll(animated);
}

void Selector::addRipple(int index, QPoint position) {
	Expects(index >= 0 && index < shownRowsCount());

	const auto row = &shownRowAt(index);
	if (!row->ripple) {
		row->ripple = std::make_unique<Ui::RippleAnimation>(
			st::defaultRippleAnimation,
			Ui::RippleAnimation::RectMask({ width(), _rowHeight }),
			[=] { updateRow(row, index); });
	}
	row->ripple->add(position - QPoint(0, _rowsSkip + index * _rowHeight));
}

std::vector<Selector::Entry> Selector::FullList(const QString &now) {
	using namespace TextUtilities;

	auto database = QFontDatabase();
	auto families = database.families();
	auto result = std::vector<Entry>();
	result.reserve(families.size() + 3);
	const auto add = [&](const QString &text, const QString &id = {}) {
		result.push_back({
			.id = id,
			.text = text,
			.keywords = PrepareSearchWords(text),
		});
	};
	add(tr::lng_font_default(tr::now));
	add(tr::lng_font_system(tr::now), style::SystemFontTag());
	for (const auto &family : families) {
		if (database.isScalable(family)) {
			result.push_back({ .id = family });
		}
	}
	if (!ranges::contains(result, now, &Entry::id)) {
		result.push_back({ .id = now });
	}
	for (auto i = begin(result) + 2; i != end(result); ++i) {
		i->key = TextUtilities::RemoveAccents(i->id).toLower();
		i->text = i->id;
		i->keywords = TextUtilities::PrepareSearchWords(i->id);
	}
	ranges::sort(begin(result) + 2, end(result), std::less<>(), &Entry::key);
	return result;
}

[[nodiscard]] PreviewRequest PrepareRequest(const QString &family) {
	return {
		.family = family,
		.msgBg = st::msgInBg->c,
		.msgShadow = st::msgInShadow->c,
		.replyBar = st::msgInReplyBarColor->c,
		.replyNameFg = st::msgInServiceFg->c,
		.textFg = st::historyTextInFg->c,
		.bubbleTail = st::historyBubbleTailInLeft.instance(st::msgInBg->c),
	};
}

PreviewPainter::PreviewPainter(const QImage &bg, PreviewRequest request)
: _request(request)
, _msgBg(_request.msgBg)
, _msgShadow(_request.msgShadow)
, _nameFont(style::ResolveFont(
	_request.family,
	style::internal::FontSemibold,
	st::fsize))
, _nameMetrics(_nameFont)
, _nameFontHeight(base::SafeRound(_nameMetrics.height()))
, _textFont(style::ResolveFont(_request.family, 0, st::fsize))
, _textMetrics(_textFont)
, _textFontHeight(base::SafeRound(_textMetrics.height())) {
	layout();

	const auto ratio = style::DevicePixelRatio();
	_result = QImage(
		_outer * ratio,
		QImage::Format_ARGB32_Premultiplied);
	_result.setDevicePixelRatio(ratio);

	auto p = QPainter(&_result);
	p.drawImage(0, 0, bg);

	p.translate(_bubble.topLeft());
	paintBubble(p);
}

void PreviewPainter::paintBubble(QPainter &p) {
	validateBubbleCache();
	const auto bubble = QRect(QPoint(), _bubble.size());
	const auto cornerShadow = _bubbleShadowBottomRight.size()
		/ _bubbleShadowBottomRight.devicePixelRatio();
	p.drawPixmap(
		bubble.width() - cornerShadow.width(),
		bubble.height() + st::msgShadow - cornerShadow.height(),
		_bubbleShadowBottomRight);
	Ui::FillRoundRect(p, bubble, _msgBg.color(), _bubbleCorners);
	const auto &bubbleTail = _request.bubbleTail;
	const auto tail = bubbleTail.size() / bubbleTail.devicePixelRatio();
	p.drawImage(-tail.width(), bubble.height() - tail.height(), bubbleTail);
	p.fillRect(
		-tail.width(),
		bubble.height(),
		tail.width() + bubble.width() - cornerShadow.width(),
		st::msgShadow,
		_request.msgShadow);
	p.translate(_content.topLeft());
	const auto local = _content.translated(-_content.topLeft());
	p.setClipRect(local);
	paintContent(p);
}

void PreviewPainter::validateBubbleCache() {
	if (!_bubbleCorners.p[0].isNull()) {
		return;
	}
	const auto radius = st::bubbleRadiusLarge;
	_bubbleCorners = Ui::PrepareCornerPixmaps(radius, _msgBg.color());
	_bubbleCorners.p[2] = {};
	_bubbleShadowBottomRight
		= Ui::PrepareCornerPixmaps(radius, _msgShadow.color()).p[3];
}

void PreviewPainter::paintContent(QPainter &p) {
	paintReply(p);

	p.translate(_message.topLeft());
	const auto local = _message.translated(-_message.topLeft());
	p.setClipRect(local);
	paintMessage(p);
}

void PreviewPainter::paintReply(QPainter &p) {
	{
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(_request.replyBar);

		const auto outline = st::messageTextStyle.blockquote.outline;
		const auto radius = st::messageTextStyle.blockquote.radius;
		p.setOpacity(Ui::kDefaultOutline1Opacity);
		p.setClipRect(
			_replyRect.x(),
			_replyRect.y(),
			outline,
			_replyRect.height());
		p.drawRoundedRect(_replyRect, radius, radius);
		p.setOpacity(Ui::kDefaultBgOpacity);
		p.setClipRect(
			_replyRect.x() + outline,
			_replyRect.y(),
			_replyRect.width() - outline,
			_replyRect.height());
		p.drawRoundedRect(_replyRect, radius, radius);
	}
	p.setOpacity(1.);
	p.setClipping(false);

	p.setPen(_request.replyNameFg);
	p.setFont(_nameFont);
	const auto name = _nameMetrics.elidedText(
		_nameText,
		Qt::ElideRight,
		_name.width());
	p.drawText(_name.x(), _name.y() + _nameMetrics.ascent(), name);

	p.setPen(_request.textFg);
	p.setFont(_textFont);
	const auto reply = _textMetrics.elidedText(
		_replyText,
		Qt::ElideRight,
		_reply.width());
	p.drawText(_reply.x(), _reply.y() + _textMetrics.ascent(), reply);
}

void PreviewPainter::paintMessage(QPainter &p) {
	p.setPen(_request.textFg);
	p.setFont(_textFont);
	p.drawText(QRect(0, 0, _message.width(), _boundingLimit), _messageText);
}

QImage PreviewPainter::takeResult() {
	return std::move(_result);
}

void PreviewPainter::layout() {
	const auto skip = st::boxRowPadding.left();
	const auto minTextWidth = style::ConvertScale(kMinTextWidth);
	const auto maxTextWidth = st::boxWidth
		- 2 * skip
		- st::msgPadding.left()
		- st::msgPadding.right();
	_boundingLimit = 100 * maxTextWidth;

	const auto textSize = [&](
			const QFontMetricsF &metrics,
			const QString &text,
			int availableWidth,
			bool oneline = false) {
		const auto result = metrics.boundingRect(
			QRect(0, 0, availableWidth, _boundingLimit),
			(Qt::AlignLeft
				| Qt::AlignTop
				| (oneline ? Qt::TextSingleLine : Qt::TextWordWrap)),
			text);
		return QSize(
			int(std::ceil(result.x() + result.width())),
			int(std::ceil(result.y() + result.height())));
	};
	const auto naturalSize = [&](
			const QFontMetricsF &metrics,
			const QString &text,
			bool oneline = false) {
		return textSize(metrics, text, _boundingLimit, oneline);
	};

	_nameText = tr::lng_settings_chat_message_reply_from(tr::now);
	_replyText = tr::lng_background_text2(tr::now);
	_messageText = tr::lng_background_text1(tr::now);

	const auto nameSize = naturalSize(_nameMetrics, _nameText, true);
	const auto nameMaxWidth = nameSize.width();
	const auto replySize = naturalSize(_textMetrics, _replyText, true);
	const auto replyMaxWidth = replySize.width();
	const auto messageSize = naturalSize(_textMetrics, _messageText);
	const auto messageMaxWidth = messageSize.width();

	const auto namePosition = QPoint(
		st::historyReplyPadding.left(),
		st::historyReplyPadding.top());
	const auto replyPosition = QPoint(
		st::historyReplyPadding.left(),
		(st::historyReplyPadding.top() + _nameFontHeight));
	const auto paddingRight = st::historyReplyPadding.right();

	const auto wantedWidth = std::max({
		namePosition.x() + nameMaxWidth + paddingRight,
		replyPosition.x() + replyMaxWidth + paddingRight,
		messageMaxWidth
	});

	const auto messageWidth = std::clamp(
		wantedWidth,
		minTextWidth,
		maxTextWidth);
	const auto messageHeight = textSize(
		_textMetrics,
		_messageText,
		maxTextWidth).height();

	_replyRect = QRect(
		st::msgReplyBarPos.x(),
		st::historyReplyTop,
		messageWidth,
		(st::historyReplyPadding.top()
			+ _nameFontHeight
			+ _textFontHeight
			+ st::historyReplyPadding.bottom()));

	_name = QRect(
		_replyRect.topLeft() + namePosition,
		QSize(messageWidth - namePosition.x(), _nameFontHeight));
	_reply = QRect(
		_replyRect.topLeft() + replyPosition,
		QSize(messageWidth - replyPosition.x(), _textFontHeight));
	_message = QRect(0, 0, messageWidth, messageHeight);

	const auto replySkip = _replyRect.y()
		+ _replyRect.height()
		+ st::historyReplyBottom;
	_message.moveTop(replySkip);

	_content = QRect(0, 0, messageWidth, replySkip + messageHeight);

	const auto msgPadding = st::msgPadding;
	_bubble = _content.marginsAdded(msgPadding);
	_content.moveTopLeft(-_bubble.topLeft());
	_bubble.moveTopLeft({});

	_outer = QSize(st::boxWidth, st::boxWidth / 2);

	_bubble.moveTopLeft({ skip, std::max(
		(_outer.height() - _bubble.height()) / 2,
		st::msgMargin.top()) });
}

[[nodiscard]] QImage GeneratePreview(
		const QImage &bg,
		PreviewRequest request) {
	return PreviewPainter(bg, request).takeResult();
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakePreview(
		not_null<QWidget*> parent,
		Fn<QImage()> generatePreviewBg,
		rpl::producer<QString> family) {
	auto result = object_ptr<Ui::RpWidget>(parent.get());
	const auto raw = result.data();

	struct State {
		QImage preview;
		QImage bg;
		QString family;
	};
	const auto state = raw->lifetime().make_state<State>();

	state->bg = generatePreviewBg();
	style::PaletteChanged() | rpl::start_with_next([=] {
		state->bg = generatePreviewBg();
	}, raw->lifetime());

	rpl::combine(
		rpl::single(rpl::empty) | rpl::then(style::PaletteChanged()),
		std::move(family)
	) | rpl::start_with_next([=](const auto &, QString family) {
		state->family = family;
		if (state->preview.isNull()) {
			state->preview = GeneratePreview(
				state->bg,
				PrepareRequest(family));
			const auto ratio = state->preview.devicePixelRatio();
			raw->resize(state->preview.size() / int(ratio));
		} else {
			const auto weak = Ui::MakeWeak(raw);
			const auto request = PrepareRequest(family);
			crl::async([=, bg = state->bg] {
				crl::on_main([
					weak,
					state,
					preview = GeneratePreview(bg, request)
				]() mutable {
					if (const auto strong = weak.data()) {
						state->preview = std::move(preview);
						const auto ratio = state->preview.devicePixelRatio();
						strong->resize(
							strong->width(),
							(state->preview.height() / int(ratio)));
						strong->update();
					}
				});
			});
		}
	}, raw->lifetime());

	raw->paintRequest() | rpl::start_with_next([=](QRect clip) {
		QPainter(raw).drawImage(0, 0, state->preview);
	}, raw->lifetime());

	return result;
}

} // namespace

void ChooseFontBox(
		not_null<GenericBox*> box,
		Fn<QImage()> generatePreviewBg,
		const QString &family,
		Fn<void(QString)> save) {
	box->setTitle(tr::lng_font_box_title());

	struct State {
		rpl::variable<QString> family;
		rpl::variable<QString> query;
		rpl::event_stream<> submits;
	};
	const auto state = box->lifetime().make_state<State>(State{
		.family = family,
	});

	const auto top = box->setPinnedToTopContent(
		object_ptr<Ui::VerticalLayout>(box));
	top->add(MakePreview(top, generatePreviewBg, state->family.value()));
	const auto filter = top->add(object_ptr<Ui::MultiSelect>(
		top,
		st::defaultMultiSelect,
		tr::lng_participant_filter()));
	top->resizeToWidth(st::boxWidth);

	filter->setSubmittedCallback([=](Qt::KeyboardModifiers) {
		state->submits.fire({});
	});
	filter->setQueryChangedCallback([=](const QString &query) {
		state->query = query;
	});
	filter->setCancelledCallback([=] {
		filter->clearQuery();
	});

	const auto chosen = [=](const QString &value) {
		state->family = value;
		filter->clearQuery();
	};
	const auto scrollTo = [=](
			Ui::ScrollToRequest request,
			anim::type animated) {
		box->scrollTo(request, animated);
	};
	const auto selector = box->addRow(
		object_ptr<Selector>(
			box,
			state->family.current(),
			state->query.value(),
			state->submits.events(),
			chosen,
			scrollTo),
		QMargins());
	box->setMinHeight(st::boxMaxListHeight);
	box->setMaxHeight(st::boxMaxListHeight);

	base::install_event_filter(filter, [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::KeyPress) {
			const auto key = static_cast<QKeyEvent*>(e.get())->key();
			if (key == Qt::Key_Up
				|| key == Qt::Key_Down
				|| key == Qt::Key_PageUp
				|| key == Qt::Key_PageDown) {
				selector->selectSkip(Qt::Key(key));
				return base::EventFilterResult::Cancel;
			}
		}
		return base::EventFilterResult::Continue;
	});

	rpl::combine(
		box->heightValue(),
		top->heightValue()
	) | rpl::start_with_next([=](int box, int top) {
		selector->setMinHeight(box - top);
	}, selector->lifetime());

	const auto apply = [=](QString chosen) {
		if (chosen == family) {
			box->closeBox();
			return;
		}
		box->getDelegate()->show(Ui::MakeConfirmBox({
			.text = tr::lng_settings_need_restart(),
			.confirmed = [=] { save(chosen); },
			.confirmText = tr::lng_settings_restart_now(),
		}));
	};
	const auto refreshButtons = [=](QString chosen) {
		box->clearButtons();
		// Doesn't fit in most languages.
		//if (!chosen.isEmpty()) {
		//	box->addLeftButton(tr::lng_background_reset_default(), [=] {
		//		apply(QString());
		//	});
		//}
		box->addButton(tr::lng_settings_save(), [=] {
			apply(chosen);
		});
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	};
	state->family.value(
	) | rpl::start_with_next(refreshButtons, box->lifetime());

	box->setFocusCallback([=] {
		filter->setInnerFocus();
	});
	box->setInitScrollCallback([=] {
		SendPendingMoveResizeEvents(box);
		selector->initScroll(anim::type::instant);
	});
}

} // namespace Ui
