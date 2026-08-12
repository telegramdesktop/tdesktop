/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/editor/iv_editor_insert_suggestions.h"

#include "lang/lang_keys.h"
#include "ui/effects/premium_graphics.h"
#include "ui/painter.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/scroll_area.h"

#include "styles/palette.h"
#include "styles/style_iv.h"
#include "styles/style_widgets.h"

#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtSvg/QSvgRenderer>
#include <QtWidgets/QTextEdit>

#include <crl/crl_on_main.h>

namespace Iv::Editor {
namespace {

using Command = InsertSuggestionCommand;

struct Entry {
	Command command = Command::Heading1;
	tr::phrase<> title;
	const style::icon *icon = nullptr;
	QString shortcut;
	QString triggers;
	bool premium = false;
	bool needsMedia = false;
	bool needsMap = false;
};

[[nodiscard]] const std::vector<Entry> &Entries() {
	static const auto result = std::vector<Entry>{
		{
			.command = Command::Heading1,
			.title = tr::lng_article_insert_heading1,
			.icon = &st::ivEditorToolbarHeading1Icon,
			.shortcut = u"#"_q,
			.triggers = u"# h1 header title heading"_q,
			.premium = true,
		},
		{
			.command = Command::Heading2,
			.title = tr::lng_article_insert_heading2,
			.icon = &st::ivEditorToolbarHeading2Icon,
			.shortcut = u"##"_q,
			.triggers = u"## h2"_q,
			.premium = true,
		},
		{
			.command = Command::Heading3,
			.title = tr::lng_article_insert_heading3,
			.icon = &st::ivEditorToolbarHeading3Icon,
			.shortcut = u"###"_q,
			.triggers = u"### h3"_q,
			.premium = true,
		},
		{
			.command = Command::Heading4,
			.title = tr::lng_article_insert_heading4,
			.icon = &st::ivEditorToolbarHeading4Icon,
			.shortcut = u"####"_q,
			.triggers = u"#### h4"_q,
			.premium = true,
		},
		{
			.command = Command::Heading5,
			.title = tr::lng_article_insert_heading5,
			.icon = &st::ivEditorToolbarHeading5Icon,
			.shortcut = u"#####"_q,
			.triggers = u"##### h5"_q,
			.premium = true,
		},
		{
			.command = Command::Heading6,
			.title = tr::lng_article_insert_heading6,
			.icon = &st::ivEditorToolbarHeading6Icon,
			.shortcut = u"######"_q,
			.triggers = u"###### h6"_q,
			.premium = true,
		},
		{
			.command = Command::Blockquote,
			.title = tr::lng_menu_formatting_blockquote,
			.icon = &st::ivEditorToolbarBlockquoteIcon,
			.shortcut = u">"_q,
			.triggers = u"> quote blockquote"_q,
		},
		{
			.command = Command::Pullquote,
			.title = tr::lng_article_insert_pullquote,
			.icon = &st::ivEditorToolbarPullquoteIcon,
			.shortcut = u"/pullquote"_q,
			.triggers = u"pullquote"_q,
			.premium = true,
		},
		{
			.command = Command::Code,
			.title = tr::lng_article_insert_code,
			.icon = &st::ivEditorToolbarCodeIcon,
			.shortcut = u"```"_q,
			.triggers = u"``` code pre preformatted monospace"_q,
		},
		{
			.command = Command::Footer,
			.title = tr::lng_article_insert_footer,
			.icon = &st::ivEditorToolbarFooterIcon,
			.shortcut = u"/footer"_q,
			.triggers = u"footer caption"_q,
			.premium = true,
		},
		{
			.command = Command::BulletList,
			.title = tr::lng_article_insert_bullet_list,
			.icon = &st::ivEditorToolbarBulletListIcon,
			.shortcut = u"-"_q,
			.triggers = u"- list bullet unordered ul"_q,
			.premium = true,
		},
		{
			.command = Command::OrderedList,
			.title = tr::lng_article_insert_ordered_list,
			.icon = &st::ivEditorToolbarOrderedListIcon,
			.shortcut = u"1."_q,
			.triggers = u"1. ordered numbered ol"_q,
			.premium = true,
		},
		{
			.command = Command::TaskList,
			.title = tr::lng_article_insert_task_list,
			.icon = &st::ivEditorToolbarTaskListIcon,
			.shortcut = u"[]"_q,
			.triggers = u"[] todo task checklist checkbox"_q,
			.premium = true,
		},
		{
			.command = Command::Details,
			.title = tr::lng_article_insert_details,
			.icon = &st::ivEditorToolbarDetailsIcon,
			.shortcut = u"/details"_q,
			.triggers = u"details toggle collapse"_q,
			.premium = true,
		},
		{
			.command = Command::Button,
			.title = tr::lng_article_insert_button,
			.icon = &st::ivEditorToolbarButtonIcon,
			.shortcut = u"/button"_q,
			.triggers = u"button"_q,
		},
		{
			.command = Command::Table,
			.title = tr::lng_article_insert_table,
			.icon = &st::ivEditorToolbarTableIcon,
			.shortcut = u"/table"_q,
			.triggers = u"table grid"_q,
			.premium = true,
		},
		{
			.command = Command::Math,
			.title = tr::lng_article_insert_math,
			.icon = &st::ivEditorToolbarMathIcon,
			.shortcut = u"/math"_q,
			.triggers = u"math latex formula expression"_q,
			.premium = true,
		},
		{
			.command = Command::Divider,
			.title = tr::lng_article_insert_divider,
			.icon = &st::ivEditorToolbarDividerIcon,
			.shortcut = u"/divider"_q,
			.triggers = u"divider line separator rule"_q,
			.premium = true,
		},
		{
			.command = Command::Media,
			.title = tr::lng_attach_photo_or_video,
			.icon = &st::ivEditorToolbarAttachIcon,
			.shortcut = u"/image"_q,
			.triggers = u"image img pic picture photo video vid media"_q,
			.premium = true,
			.needsMedia = true,
		},
		{
			.command = Command::Audio,
			.title = tr::lng_in_dlg_audio_file,
			.icon = &st::ivEditorToolbarAudioIcon,
			.shortcut = u"/audio"_q,
			.triggers = u"audio music media"_q,
			.premium = true,
			.needsMedia = true,
		},
		{
			.command = Command::Map,
			.title = tr::lng_maps_point,
			.icon = &st::ivEditorToolbarLocationIcon,
			.shortcut = u"/map"_q,
			.triggers = u"map location venue place"_q,
			.premium = true,
			.needsMap = true,
		},
	};
	return result;
}

[[nodiscard]] bool Matches(const Entry &entry, const QString &query) {
	if (query.isEmpty()) {
		return true;
	}
	const auto starts = [&](const QString &word) {
		return word.startsWith(query, Qt::CaseInsensitive);
	};
	for (const auto &word : entry.triggers.split(QChar(' '))) {
		if (starts(word)) {
			return true;
		}
	}
	for (const auto &word : entry.title(tr::now).split(QChar(' '))) {
		if (starts(word)) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] QImage PremiumStarImage(int size) {
	const auto factor = style::DevicePixelRatio();
	const auto side = QSize(size, size);
	auto image = QImage(
		side * factor,
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(factor);
	image.fill(Qt::transparent);
	{
		auto p = QPainter(&image);
		auto svg = QSvgRenderer(
			Ui::Premium::ColorizedSvg(Ui::Premium::ButtonGradientStops()));
		svg.render(&p, QRectF(QPointF(), QSizeF(side)));
	}
	return image;
}

} // namespace

class InsertSuggestions::Inner final : public Ui::RpWidget {
public:
	Inner(
		QWidget *parent,
		const style::Menu &st,
		rpl::producer<bool> premium,
		bool media,
		bool map);

	[[nodiscard]] int rowHeight() const;
	[[nodiscard]] int count() const;
	[[nodiscard]] int naturalWidth() const;

	[[nodiscard]] bool applyQuery(const QString &query);
	void moveSelection(int delta);
	[[nodiscard]] bool chooseSelected();

	[[nodiscard]] rpl::producer<Command> chosen() const;
	[[nodiscard]] rpl::producer<Ui::ScrollToRequest> scrollToRequests() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	[[nodiscard]] int indexAt(QPoint position) const;
	[[nodiscard]] int starReserve() const;
	[[nodiscard]] int rightPadding() const;
	void setSelected(int index, bool scrollTo);
	void refreshPremiumStar();

	const style::Menu &_st;
	const int _rowHeight = 0;
	const bool _media = false;
	const bool _map = false;
	std::vector<not_null<const Entry*>> _entries;
	QImage _premiumStar;
	bool _premium = false;
	int _selected = -1;
	int _pressed = -1;
	rpl::event_stream<Command> _chosen;
	rpl::event_stream<Ui::ScrollToRequest> _scrollToRequests;

};

InsertSuggestions::Inner::Inner(
	QWidget *parent,
	const style::Menu &st,
	rpl::producer<bool> premium,
	bool media,
	bool map)
: RpWidget(parent)
, _st(st)
, _rowHeight(_st.itemPadding.top()
	+ _st.itemStyle.font->height
	+ _st.itemPadding.bottom())
, _media(media)
, _map(map) {
	setMouseTracking(true);

	std::move(premium) | rpl::on_next([=](bool value) {
		if (_premium == value) {
			return;
		}
		_premium = value;
		refreshPremiumStar();
		update();
	}, lifetime());

	style::PaletteChanged() | rpl::on_next([=] {
		refreshPremiumStar();
		update();
	}, lifetime());

	refreshPremiumStar();
}

void InsertSuggestions::Inner::refreshPremiumStar() {
	_premiumStar = _premium
		? QImage()
		: PremiumStarImage(st::ivEditorStyleMenuPremiumStarSize);
}

int InsertSuggestions::Inner::rowHeight() const {
	return _rowHeight;
}

int InsertSuggestions::Inner::count() const {
	return int(_entries.size());
}

int InsertSuggestions::Inner::starReserve() const {
	return _premiumStar.isNull()
		? 0
		: ((_premiumStar.width() / style::DevicePixelRatio())
			+ st::ivEditorSuggestionsShortcutSkip);
}

int InsertSuggestions::Inner::rightPadding() const {
	return _st.itemPadding.right() + starReserve();
}

int InsertSuggestions::Inner::naturalWidth() const {
	const auto &font = _st.itemStyle.font;
	auto title = 0;
	auto shortcut = 0;
	for (const auto &entry : Entries()) {
		title = std::max(title, font->width(entry.title(tr::now)));
		shortcut = std::max(shortcut, font->width(entry.shortcut));
	}
	return std::clamp(
		(_st.itemPadding.left()
			+ title
			+ st::ivEditorSuggestionsShortcutSkip
			+ shortcut
			+ rightPadding()),
		_st.widthMin,
		_st.widthMax);
}

bool InsertSuggestions::Inner::applyQuery(const QString &query) {
	_entries.clear();
	for (const auto &entry : Entries()) {
		if (entry.needsMedia && !_media) {
			continue;
		} else if (entry.needsMap && !_map) {
			continue;
		} else if (Matches(entry, query)) {
			_entries.push_back(&entry);
		}
	}
	_selected = _entries.empty() ? -1 : 0;
	_pressed = -1;
	resize(naturalWidth(), count() * _rowHeight);
	update();
	return !_entries.empty();
}

void InsertSuggestions::Inner::setSelected(int index, bool scrollTo) {
	if (_selected != index) {
		_selected = index;
		update();
	}
	if (scrollTo && index >= 0) {
		_scrollToRequests.fire({
			index * _rowHeight,
			(index + 1) * _rowHeight,
		});
	}
}

void InsertSuggestions::Inner::moveSelection(int delta) {
	const auto rows = count();
	if (!rows) {
		return;
	}
	const auto now = (_selected >= 0)
		? _selected
		: ((delta > 0) ? -1 : 0);
	setSelected(((now + delta) % rows + rows) % rows, true);
}

bool InsertSuggestions::Inner::chooseSelected() {
	const auto index = (_selected >= 0) ? _selected : 0;
	if (index >= count()) {
		return false;
	}
	_chosen.fire_copy(_entries[index]->command);
	return true;
}

rpl::producer<Command> InsertSuggestions::Inner::chosen() const {
	return _chosen.events();
}

auto InsertSuggestions::Inner::scrollToRequests() const
-> rpl::producer<Ui::ScrollToRequest> {
	return _scrollToRequests.events();
}

int InsertSuggestions::Inner::indexAt(QPoint position) const {
	if (position.x() < 0 || position.x() >= width() || position.y() < 0) {
		return -1;
	}
	const auto index = position.y() / _rowHeight;
	return (index < count()) ? index : -1;
}

void InsertSuggestions::Inner::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);

	const auto clip = e->rect();
	const auto from = std::max(clip.top() / _rowHeight, 0);
	const auto till = std::min(
		(clip.top() + clip.height() + _rowHeight - 1) / _rowHeight,
		count());
	const auto shown = (_pressed >= 0) ? _pressed : _selected;
	const auto &font = _st.itemStyle.font;
	const auto right = rightPadding();
	for (auto i = from; i != till; ++i) {
		const auto entry = _entries[i];
		const auto top = i * _rowHeight;
		const auto selected = (i == shown);
		p.fillRect(
			QRect(0, top, width(), _rowHeight),
			selected ? _st.itemBgOver : _st.itemBg);
		entry->icon->paint(
			p,
			_st.itemIconPosition + QPoint(0, top),
			width());

		const auto shortcutWidth = font->width(entry->shortcut);
		const auto available = std::max(
			(width()
				- _st.itemPadding.left()
				- st::ivEditorSuggestionsShortcutSkip
				- shortcutWidth
				- right),
			0);
		p.setFont(font);
		p.setPen(selected ? _st.itemFgOver : _st.itemFg);
		p.drawTextLeft(
			_st.itemPadding.left(),
			top + _st.itemPadding.top(),
			width(),
			font->elided(entry->title(tr::now), available));
		p.setPen(selected ? _st.itemFgShortcutOver : _st.itemFgShortcut);
		p.drawTextRight(
			right,
			top + _st.itemPadding.top(),
			width(),
			entry->shortcut,
			shortcutWidth);
		if (entry->premium && !_premiumStar.isNull()) {
			const auto side = _premiumStar.width()
				/ style::DevicePixelRatio();
			p.drawImage(
				width() - _st.itemPadding.right() - side,
				top + (_rowHeight - side) / 2,
				_premiumStar);
		}
	}
}

void InsertSuggestions::Inner::mouseMoveEvent(QMouseEvent *e) {
	setSelected(indexAt(e->pos()), false);
}

void InsertSuggestions::Inner::mousePressEvent(QMouseEvent *e) {
	_pressed = indexAt(e->pos());
	update();
}

void InsertSuggestions::Inner::mouseReleaseEvent(QMouseEvent *e) {
	const auto pressed = _pressed;
	_pressed = -1;
	update();
	if (pressed >= 0 && pressed == indexAt(e->pos())) {
		setSelected(pressed, false);
		[[maybe_unused]] const auto chosen = chooseSelected();
	}
}

void InsertSuggestions::Inner::leaveEventHook(QEvent *e) {
	if (_pressed < 0) {
		setSelected(-1, false);
	}
}

InsertSuggestions::InsertSuggestions(
	QWidget *parent,
	rpl::producer<bool> premium,
	bool media,
	bool map)
: RpWidget(parent)
, _st(st::popupMenuWithIcons.menu)
, _shadow(st::popupMenuWithIcons.shadow)
, _roundRect(st::ivEditorSuggestionsRadius, _st.itemBg)
, _shadowMargins(_shadow.extend())
, _scroll(this, st::ivEditorSuggestionsScroll)
, _inner(_scroll->setOwnedWidget(object_ptr<Inner>(
	_scroll.data(),
	_st,
	std::move(premium),
	media,
	map))) {
	_scroll->show();
	_inner->show();

	_inner->scrollToRequests(
	) | rpl::on_next([=](Ui::ScrollToRequest request) {
		_scroll->scrollToY(request.ymin, request.ymax);
	}, lifetime());

	hide();
}

InsertSuggestions::~InsertSuggestions() = default;

int InsertSuggestions::chromeHeight() const {
	return _shadowMargins.top()
		+ st::ivEditorSuggestionsPadding.top()
		+ st::ivEditorSuggestionsPadding.bottom()
		+ _shadowMargins.bottom();
}

void InsertSuggestions::resizeToRows(int rows) {
	resize(
		_shadowMargins.left() + _inner->width() + _shadowMargins.right(),
		chromeHeight() + (rows * _inner->rowHeight()));
}

bool InsertSuggestions::applyQuery(const QString &query) {
	if (!_inner->applyQuery(query)) {
		hide();
		return false;
	}
	_scroll->scrollToY(0);
	resizeToRows(std::min(_inner->count(), st::ivEditorSuggestionsMaxRows));
	return true;
}

void InsertSuggestions::moveNear(QRect anchor, QRect available) {
	const auto skip = st::ivEditorSuggestionsSkip;
	const auto below = available.bottom() - anchor.bottom() - skip;
	const auto above = anchor.top() - available.top() - skip;
	const auto rowHeight = _inner->rowHeight();
	const auto fits = [&](int space) {
		return std::clamp(
			(space - chromeHeight()) / rowHeight,
			0,
			std::min(_inner->count(), st::ivEditorSuggestionsMaxRows));
	};
	const auto goesDown = (fits(below) >= fits(above));
	const auto rows = std::max(fits(goesDown ? below : above), 1);
	resizeToRows(rows);

	const auto top = goesDown
		? (anchor.bottom() + skip)
		: std::max(anchor.top() - skip - height(), available.top());
	const auto left = std::clamp(
		anchor.left() - _shadowMargins.left() - _st.itemIconPosition.x(),
		available.left(),
		std::max(available.right() - width() + 1, available.left()));
	move(left, top);
}

bool InsertSuggestions::handleKeyPress(not_null<QKeyEvent*> e) {
	if (!_inner->count()) {
		return false;
	}
	const auto key = e->key();
	if (key == Qt::Key_Up) {
		_inner->moveSelection(-1);
		return true;
	} else if (key == Qt::Key_Down) {
		_inner->moveSelection(1);
		return true;
	} else if (key == Qt::Key_Return
		|| key == Qt::Key_Enter
		|| key == Qt::Key_Tab) {
		return _inner->chooseSelected();
	}
	return false;
}

rpl::producer<InsertSuggestionCommand> InsertSuggestions::chosen() const {
	return _inner->chosen();
}

void InsertSuggestions::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	const auto inner = rect().marginsRemoved(_shadowMargins);
	_shadow.paint(p, inner, st::ivEditorSuggestionsRadius);
	_roundRect.paint(p, inner);
}

void InsertSuggestions::resizeEvent(QResizeEvent *e) {
	const auto &padding = st::ivEditorSuggestionsPadding;
	const auto width = this->width()
		- _shadowMargins.left()
		- _shadowMargins.right();
	_scroll->setGeometry(
		_shadowMargins.left(),
		_shadowMargins.top() + padding.top(),
		width,
		height() - chromeHeight());
	_inner->resize(width, _inner->count() * _inner->rowHeight());
}

std::optional<InsertAction> InsertSuggestionBlock(
		InsertSuggestionCommand command) {
	using Type = InsertBlockType;
	const auto heading = [](int level) {
		return InsertAction{ .type = Type::Heading, .headingLevel = level };
	};
	const auto block = [](Type type) {
		return InsertAction{ .type = type };
	};
	switch (command) {
	case Command::Heading1: return heading(1);
	case Command::Heading2: return heading(2);
	case Command::Heading3: return heading(3);
	case Command::Heading4: return heading(4);
	case Command::Heading5: return heading(5);
	case Command::Heading6: return heading(6);
	case Command::Blockquote: return block(Type::Blockquote);
	case Command::Pullquote: return block(Type::Pullquote);
	case Command::Code: return block(Type::Code);
	case Command::Footer: return block(Type::Footer);
	case Command::BulletList: return block(Type::BulletList);
	case Command::OrderedList: return block(Type::OrderedList);
	case Command::TaskList: return block(Type::TaskList);
	case Command::Details: return block(Type::Details);
	case Command::Table: return block(Type::Table);
	case Command::Divider: return block(Type::Divider);
	case Command::Button:
	case Command::Math:
	case Command::Media:
	case Command::Audio:
	case Command::Map: return std::nullopt;
	}
	Unexpected("Command in InsertSuggestionBlock.");
}

InsertSuggestionsController::InsertSuggestionsController(
	InsertSuggestionsDescriptor descriptor)
: _host(descriptor.host)
, _outer(descriptor.outer)
, _field(std::move(descriptor.field))
, _chosen(std::move(descriptor.chosen))
, _media(descriptor.media)
, _map(descriptor.map)
, _premium(std::move(descriptor.premium)) {
}

InsertSuggestionsController::~InsertSuggestionsController() = default;

bool InsertSuggestionsController::active() const {
	return _active;
}

void InsertSuggestionsController::ensurePanel() {
	if (_panel) {
		return;
	}
	_panel = base::make_unique_q<InsertSuggestions>(
		_outer,
		std::move(_premium),
		_media,
		_map);
	_panel->chosen() | rpl::on_next([=](InsertSuggestionCommand command) {
		if (const auto onstack = _chosen) {
			onstack(command);
		}
	}, _panel->lifetime());
}

void InsertSuggestionsController::open() {
	ensurePanel();
	_active = true;
	scheduleRefresh();
}

void InsertSuggestionsController::scheduleRefresh() {
	if (!_active) {
		return;
	}
	crl::on_main(_host, [=] {
		refresh();
	});
}

void InsertSuggestionsController::refresh() {
	if (!_active) {
		return;
	}
	const auto field = _field();
	if (!field) {
		close();
		return;
	}
	const auto text = field->getLastText();
	const auto spaced = ranges::any_of(text, [](QChar ch) {
		return ch.isSpace();
	});
	if (spaced || !text.startsWith(QChar('/'))) {
		close();
		return;
	}
	ensurePanel();
	if (!_panel->applyQuery(text.mid(1))) {
		close();
		return;
	}
	updatePosition();
	_panel->show();
	_panel->raise();
	refreshQueryRect();
}

void InsertSuggestionsController::updatePosition() {
	const auto field = _active ? _field() : nullptr;
	if (!field || !_panel) {
		return;
	}
	_panel->moveNear(
		QRect(field->mapTo(_outer, QPoint()), field->size()),
		_outer->rect());
}

void InsertSuggestionsController::close() {
	if (!_active) {
		return;
	}
	_active = false;
	if (_panel) {
		_panel->hide();
	}
	refreshQueryRect();
}

bool InsertSuggestionsController::handleKeyPress(not_null<QKeyEvent*> e) {
	if (!_active || !_panel || _panel->isHidden()) {
		return false;
	} else if (e->key() == Qt::Key_Escape) {
		close();
		return true;
	}
	return _panel->handleKeyPress(e);
}

void InsertSuggestionsController::takeQuery() {
	const auto field = _field();
	if (!field) {
		return;
	}
	auto cursor = field->textCursor();
	cursor.movePosition(QTextCursor::Start);
	cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
	cursor.removeSelectedText();
	field->setTextCursor(cursor);
}

QRect InsertSuggestionsController::queryRect() const {
	const auto field = _active ? _field() : nullptr;
	if (!field) {
		return QRect();
	}
	const auto raw = field->rawTextEdit();
	const auto document = raw->document();
	auto from = QTextCursor(document);
	from.movePosition(QTextCursor::Start);
	auto till = QTextCursor(document);
	till.movePosition(QTextCursor::End);
	const auto first = raw->cursorRect(from);
	const auto last = raw->cursorRect(till);
	if (first.isEmpty() || last.isEmpty() || (last.right() <= first.left())) {
		return QRect();
	}
	const auto shift = raw->viewport()->mapTo(_host.get(), QPoint());
	return QRect(
		first.topLeft() + shift,
		QPoint(last.right() + shift.x(), last.bottom() + shift.y()));
}

void InsertSuggestionsController::refreshQueryRect() {
	const auto now = queryRect();
	const auto expanded = now.isEmpty()
		? QRect()
		: now.marginsAdded(st::ivEditorSuggestionsQueryPadding);
	if (_query == expanded) {
		return;
	}
	const auto changed = _query.united(expanded);
	_query = expanded;
	_host->update(changed.marginsAdded(QMargins(1, 1, 1, 1)));
}

void InsertSuggestionsController::paintQuery(QPainter &p) {
	if (_query.isEmpty()) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	auto color = st::windowFg->c;
	color.setAlphaF(color.alphaF() * 0.05);
	const auto radius = st::ivEditorSuggestionsQueryRadius;
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	p.drawRoundedRect(_query, radius, radius);
}

} // namespace Iv::Editor
