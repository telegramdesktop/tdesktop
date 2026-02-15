/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/boxes/country_select_box.h"

#include "lang/lang_keys.h"
#include "base/screen_reader_state.h"
#include "ui/accessible/ui_accessible_item.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/multi_select.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "base/invoke_queued.h"
#include "countries/countries_instance.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_intro.h"

#include <unordered_map>

#include <QtCore/QRegularExpression>
#include <QtWidgets/QApplication>

namespace Ui {
namespace {

QString LastValidISO;

} // namespace

class CountrySelectBox::Inner : public RpWidget {
public:
	Inner(QWidget *parent, const QString &iso, Type type);
	~Inner();

	void updateFilter(QString filter = QString());

	void selectSkip(int32 dir);
	void selectSkipPage(int32 h, int32 dir);

	void chooseCountry();

	void refresh();

	[[nodiscard]] rpl::producer<Entry> countryChosen() const {
		return _countryChosen.events();
	}

	[[nodiscard]] rpl::producer<ScrollToRequest> mustScrollTo() const {
		return _mustScrollTo.events();
	}

	QAccessible::Role accessibilityRole() override {
		return QAccessible::List;
	}

	QAccessible::Role accessibilityChildRole() const override {
		return QAccessible::ListItem;
	}

	QAccessible::State accessibilityChildState(int index) const override {
		QAccessible::State state;
		state.selectable = true;
		if (base::ScreenReaderState::Instance()->active()) {
			state.focusable = true;
		}
		if (index == _selected) {
			state.selected = true;
			state.active = true;
			if (hasFocus()) {
				state.focused = true;
			}
		}
		return state;
	}

	int accessibilityChildCount() const override {
		return int(current().size());
	}

	QString accessibilityChildName(int index) const override {
		const auto &list = current();
		if (index < 0 || index >= int(list.size())) {
			return {};
		}
		if (_type == Type::Phones) {
			return list[index].country + u", +"_q + list[index].code;
		}
		return list[index].country;
	}

	QRect accessibilityChildRect(int index) const override {
		const auto &list = current();
		if (index < 0 || index >= int(list.size())) {
			return QRect();
		}
		return QRect(0, st::countriesSkip + index * _rowHeight, width(), _rowHeight);
	}

	int accessibilityChildColumnCount(int row) const override {
		return (_type == Type::Phones) ? 2 : 1;
	}

	QAccessible::Role accessibilityChildSubItemRole() const override {
		return QAccessible::Cell;
	}

	QString accessibilityChildSubItemName(int row, int column) const override {
		if (column == 0) {
			return tr::lng_sr_country_column_name(tr::now);
		} else if (column == 1 && _type == Type::Phones) {
			return tr::lng_country_code(tr::now);
		}
		return {};
	}

	QString accessibilityChildSubItemValue(int row, int column) const override {
		const auto &list = current();
		if (row < 0 || row >= int(list.size())) {
			return {};
		}
		if (column == 0) {
			return list[row].country;
		} else if (column == 1 && _type == Type::Phones) {
			return u"+"_q + list[row].code;
		}
		return {};
	}

protected:
	void focusInEvent(QFocusEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	void init();
	void updateSelected() {
		updateSelected(mapFromGlobal(QCursor::pos()));
	}
	void updateSelected(QPoint localPos);
	void updateSelectedRow();
	void updateRow(int index);
	void setPressed(int pressed);
	const std::vector<Entry> &current() const;

	Type _type = Type::Phones;
	int _rowHeight = 0;

	int _selected = -1;
	int _pressed = -1;
	QString _filter;
	bool _mouseSelection = false;

	std::vector<std::unique_ptr<RippleAnimation>> _ripples;

	std::vector<Entry> _list;
	std::vector<Entry> _filtered;
	base::flat_map<QChar, std::vector<int>> _byLetter;
	std::vector<std::vector<QString>> _namesList;

	rpl::event_stream<Entry> _countryChosen;
	rpl::event_stream<ScrollToRequest> _mustScrollTo;

};

CountrySelectBox::CountrySelectBox(QWidget*)
: CountrySelectBox(nullptr, QString(), Type::Phones) {
}

CountrySelectBox::CountrySelectBox(QWidget*, const QString &iso, Type type)
: _select(this, st::defaultMultiSelect, tr::lng_country_ph())
, _ownedInner(this, iso, type) {
}

rpl::producer<QString> CountrySelectBox::countryChosen() const {
	Expects(_ownedInner != nullptr || _inner != nullptr);

	return (_ownedInner
		? _ownedInner.data()
		: _inner.data())->countryChosen() | rpl::map([](const Entry &e) {
			return e.iso2;
		});
}

rpl::producer<CountrySelectBox::Entry> CountrySelectBox::entryChosen() const {
	Expects(_ownedInner != nullptr || _inner != nullptr);

	return (_ownedInner
		? _ownedInner.data()
		: _inner.data())->countryChosen();
}

void CountrySelectBox::prepare() {
	setTitle(tr::lng_country_select());

	_select->resizeToWidth(st::boxWidth);
	_select->setQueryChangedCallback([=](const QString &query) {
		applyFilterUpdate(query);
	});
	_select->setSubmittedCallback([=](Qt::KeyboardModifiers) {
		submit();
	});

	_inner = setInnerWidget(
		std::move(_ownedInner),
		st::countriesScroll,
		_select->height());

	addButton(tr::lng_close(), [=] { closeBox(); });

	setDimensions(st::boxWidth, st::boxMaxListHeight);

	_inner->mustScrollTo(
	) | rpl::on_next([=](ScrollToRequest request) {
		scrollToY(request.ymin, request.ymax);
	}, lifetime());
}

void CountrySelectBox::submit() {
	_inner->chooseCountry();
}

void CountrySelectBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Down) {
		_inner->selectSkip(1);
	} else if (e->key() == Qt::Key_Up) {
		_inner->selectSkip(-1);
	} else if (e->key() == Qt::Key_PageDown) {
		_inner->selectSkipPage(height() - _select->height(), 1);
	} else if (e->key() == Qt::Key_PageUp) {
		_inner->selectSkipPage(height() - _select->height(), -1);
	} else {
		BoxContent::keyPressEvent(e);
	}
}

void CountrySelectBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_select->resizeToWidth(width());
	_select->moveToLeft(0, 0);

	_inner->resizeToWidth(width());
}

void CountrySelectBox::applyFilterUpdate(const QString &query) {
	scrollToY(0);
	_inner->updateFilter(query);
}

void CountrySelectBox::setInnerFocus() {
	_select->setInnerFocus();
}

CountrySelectBox::Inner::Inner(
	QWidget *parent,
	const QString &iso,
	Type type)
: RpWidget(parent)
, _type(type)
, _rowHeight(st::countryRowHeight) {
	setAttribute(Qt::WA_OpaquePaintEvent);

	const auto &byISO2 = Countries::Instance().byISO2();

	if (byISO2.contains(iso)) {
		LastValidISO = iso;
	}

	rpl::single(
	) | rpl::then(
		Countries::Instance().updated()
	) | rpl::on_next([=] {
		_mustScrollTo.fire(ScrollToRequest(0, 0));
		_list.clear();
		_namesList.clear();
		init();
		const auto filter = _filter;
		_filter = u"a"_q;
		updateFilter(filter);
	}, lifetime());

	setAccessibleName(tr::lng_country_select(tr::now));

	base::ScreenReaderState::Instance()->activeValue(
	) | rpl::on_next([=](bool active) {
		setFocusPolicy(active ? Qt::TabFocus : Qt::NoFocus);
	}, lifetime());
}

void CountrySelectBox::Inner::init() {
	const auto &byISO2 = Countries::Instance().byISO2();

	const auto extractEntries = [&](const Countries::Info &info) {
		for (const auto &code : info.codes) {
			_list.push_back(Entry{
				.country = info.name,
				.iso2 = info.iso2,
				.code = code.callingCode,
				.alternativeName = info.alternativeName,
			});
		}
	};

	_list.reserve(byISO2.size());
	_namesList.reserve(byISO2.size());

	const auto l = byISO2.constFind(LastValidISO);
	const auto lastValid = (l != byISO2.cend()) ? (*l) : nullptr;
	if (lastValid) {
		extractEntries(*lastValid);
	}
	for (const auto &entry : Countries::Instance().list()) {
		if (&entry != lastValid) {
			extractEntries(entry);
		}
	}
	auto index = 0;
	for (const auto &info : _list) {
		static const auto RegExp = QRegularExpression("[\\s\\-]");
		auto full = info.country
			+ ' '
			+ (!info.alternativeName.isEmpty()
				? info.alternativeName
				: QString());
		const auto namesList = std::move(full).toLower().split(
			RegExp,
			Qt::SkipEmptyParts);
		auto &names = _namesList.emplace_back();
		names.reserve(namesList.size());
		for (const auto &name : namesList) {
			const auto part = name.trimmed();
			if (part.isEmpty()) {
				continue;
			}

			const auto ch = part[0];
			auto &byLetter = _byLetter[ch];
			if (byLetter.empty() || byLetter.back() != index) {
				byLetter.push_back(index);
			}
			names.push_back(part);
		}
		++index;
	}
}

void CountrySelectBox::Inner::focusInEvent(QFocusEvent *e) {
	// Select first item when focus enters.
	const auto &list = current();
	if (_selected < 0 && !list.empty()) {
		_selected = 0;
		updateSelectedRow();
	}

	RpWidget::focusInEvent(e);

	if (_selected >= 0 && base::ScreenReaderState::Instance()->active()) {
		const auto index = _selected;
		InvokeQueued(this, [=] {
			if (_selected != index || !hasFocus()) {
				return;
			}
			accessibilityChildFocused(index);
		});
	}
}

void CountrySelectBox::Inner::paintEvent(QPaintEvent *e) {
	Painter p(this);
	QRect r(e->rect());
	p.setClipRect(r);

	const auto &list = current();
	if (list.empty()) {
		p.fillRect(r, st::boxBg);
		p.setFont(st::noContactsFont);
		p.setPen(st::noContactsColor);
		p.drawText(QRect(0, 0, width(), st::noContactsHeight), tr::lng_country_none(tr::now), style::al_center);
		return;
	}
	const auto l = int(list.size());
	if (r.intersects(QRect(0, 0, width(), st::countriesSkip))) {
		p.fillRect(r.intersected(QRect(0, 0, width(), st::countriesSkip)), st::countryRowBg);
	}
	int32 from = std::clamp((r.y() - st::countriesSkip) / _rowHeight, 0, l);
	int32 to = std::clamp((r.y() + r.height() - st::countriesSkip + _rowHeight - 1) / _rowHeight, 0, l);
	for (int32 i = from; i < to; ++i) {
		auto selected = (i == (_pressed >= 0 ? _pressed : _selected));
		auto y = st::countriesSkip + i * _rowHeight;

		p.fillRect(0, y, width(), _rowHeight, selected ? st::countryRowBgOver : st::countryRowBg);
		if (_ripples.size() > i && _ripples[i]) {
			_ripples[i]->paint(p, 0, y, width());
			if (_ripples[i]->empty()) {
				_ripples[i].reset();
			}
		}

		auto code = QString("+") + list[i].code;
		auto codeWidth = st::countryRowCodeFont->width(code);

		auto name = list[i].country;
		auto nameWidth = st::countryRowNameFont->width(name);
		auto availWidth = width() - st::countryRowPadding.left() - st::countryRowPadding.right() - codeWidth - st::boxScroll.width;
		if (nameWidth > availWidth) {
			name = st::countryRowNameFont->elided(name, availWidth);
			nameWidth = st::countryRowNameFont->width(name);
		}

		p.setFont(st::countryRowNameFont);
		p.setPen(st::countryRowNameFg);
		p.drawTextLeft(st::countryRowPadding.left(), y + st::countryRowPadding.top(), width(), name);

		if (_type == Type::Phones) {
			p.setFont(st::countryRowCodeFont);
			p.setPen(selected ? st::countryRowCodeFgOver : st::countryRowCodeFg);
			p.drawTextLeft(st::countryRowPadding.left() + nameWidth + st::countryRowPadding.right(), y + st::countryRowPadding.top(), width(), code);
		}
	}
}

void CountrySelectBox::Inner::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Down) {
		selectSkip(1);
	} else if (e->key() == Qt::Key_Up) {
		selectSkip(-1);
	} else if (e->key() == Qt::Key_PageDown || e->key() == Qt::Key_PageUp) {
		const auto visibleHeight = visibleRegion().boundingRect().height();
		const auto rowsPerPage = std::max(visibleHeight / _rowHeight, 1);
		selectSkip(e->key() == Qt::Key_PageDown ? rowsPerPage : -rowsPerPage);
	} else if (e->key() == Qt::Key_Home) {
		const auto &list = current();
		if (!list.empty()) {
			_selected = 0;
			_mustScrollTo.fire(ScrollToRequest(
				st::countriesSkip,
				st::countriesSkip + _rowHeight));
			update();
			if (base::ScreenReaderState::Instance()->active()) {
				accessibilityChildNameChanged(_selected);
				accessibilityChildFocused(_selected);
			}
		}
	} else if (e->key() == Qt::Key_End) {
		const auto &list = current();
		if (!list.empty()) {
			_selected = int(list.size()) - 1;
			_mustScrollTo.fire(ScrollToRequest(
				st::countriesSkip + _selected * _rowHeight,
				st::countriesSkip + (_selected + 1) * _rowHeight));
			update();
			if (base::ScreenReaderState::Instance()->active()) {
				accessibilityChildNameChanged(_selected);
				accessibilityChildFocused(_selected);
			}
		}
	} else if (!e->isAutoRepeat()
		&& (e->key() == Qt::Key_Return
			|| e->key() == Qt::Key_Enter)) {
		chooseCountry();
	} else {
		RpWidget::keyPressEvent(e);
	}
}

void CountrySelectBox::Inner::enterEventHook(QEnterEvent *e) {
	setMouseTracking(true);
}

void CountrySelectBox::Inner::leaveEventHook(QEvent *e) {
	_mouseSelection = false;
	setMouseTracking(false);
	if (_selected >= 0) {
		updateSelectedRow();
		_selected = -1;
	}
}

void CountrySelectBox::Inner::mouseMoveEvent(QMouseEvent *e) {
	_mouseSelection = true;
	updateSelected(e->pos());
}

void CountrySelectBox::Inner::mousePressEvent(QMouseEvent *e) {
	_mouseSelection = true;
	updateSelected(e->pos());

	setPressed(_selected);
	const auto &list = current();
	if (_pressed >= 0 && _pressed < list.size()) {
		if (_ripples.size() <= _pressed) {
			_ripples.reserve(_pressed + 1);
			while (_ripples.size() <= _pressed) {
				_ripples.push_back(nullptr);
			}
		}
		if (!_ripples[_pressed]) {
			auto mask = RippleAnimation::RectMask(QSize(width(), _rowHeight));
			_ripples[_pressed] = std::make_unique<RippleAnimation>(st::countryRipple, std::move(mask), [this, index = _pressed] {
				updateRow(index);
			});
			_ripples[_pressed]->add(e->pos() - QPoint(0, st::countriesSkip + _pressed * _rowHeight));
		}
	}
}

void CountrySelectBox::Inner::mouseReleaseEvent(QMouseEvent *e) {
	auto pressed = _pressed;
	setPressed(-1);
	updateSelectedRow();
	if (e->button() == Qt::LeftButton) {
		if ((pressed >= 0) && pressed == _selected) {
			chooseCountry();
		}
	}
}

void CountrySelectBox::Inner::updateFilter(QString filter) {
	const auto words = TextUtilities::PrepareSearchWords(filter);
	filter = words.isEmpty() ? QString() : words.join(' ');
	if (_filter == filter) {
		return;
	}
	_filter = filter;

	const auto findWord = [&](
			const std::vector<QString> &names,
			const QString &word) {
		for (const auto &name : names) {
			if (name.startsWith(word)) {
				return true;
			}
		}
		return false;
	};
	const auto hasAllWords = [&](const std::vector<QString> &names) {
		for (const auto &word : words) {
			if (!findWord(names, word)) {
				return false;
			}
		}
		return true;
	};
	if (!_filter.isEmpty()) {
		_filtered.clear();
		for (const auto index : _byLetter[_filter[0].toLower()]) {
			if (hasAllWords(_namesList[index])) {
				_filtered.push_back(_list[index]);
			}
		}
	}
	refresh();
	_selected = current().empty() ? -1 : 0;
	update();
}

void CountrySelectBox::Inner::selectSkip(int32 dir) {
	_mouseSelection = false;

	const auto &list = current();
	int cur = (_selected >= 0) ? _selected : -1;
	cur += dir;
	if (cur <= 0) {
		_selected = list.empty() ? -1 : 0;
	} else if (cur >= list.size()) {
		_selected = -1;
	} else {
		_selected = cur;
	}
	if (_selected >= 0) {
		_mustScrollTo.fire(ScrollToRequest(
			st::countriesSkip + _selected * _rowHeight,
			st::countriesSkip + (_selected + 1) * _rowHeight));
	}
	update();
	if (_selected >= 0 && base::ScreenReaderState::Instance()->active()) {
		accessibilityChildNameChanged(_selected);
		accessibilityChildFocused(_selected);
	}
}

void CountrySelectBox::Inner::selectSkipPage(int32 h, int32 dir) {
	int32 points = h / _rowHeight;
	if (!points) return;
	selectSkip(points * dir);
}

void CountrySelectBox::Inner::chooseCountry() {
	const auto &list = current();
	_countryChosen.fire_copy((_selected >= 0 && _selected < list.size())
		? list[_selected]
		: Entry());
}

void CountrySelectBox::Inner::refresh() {
	const auto &list = current();
	resize(width(), list.empty() ? st::noContactsHeight : (list.size() * _rowHeight + st::countriesSkip));
}

void CountrySelectBox::Inner::updateSelected(QPoint localPos) {
	if (!_mouseSelection) return;

	auto in = parentWidget()->rect().contains(parentWidget()->mapFromGlobal(QCursor::pos()));

	const auto &list = current();
	auto selected = (in && localPos.y() >= st::countriesSkip && localPos.y() < st::countriesSkip + list.size() * _rowHeight) ? ((localPos.y() - st::countriesSkip) / _rowHeight) : -1;
	if (_selected != selected) {
		updateSelectedRow();
		_selected = selected;
		updateSelectedRow();
		if (_selected >= 0 && base::ScreenReaderState::Instance()->active()) {
			accessibilityChildNameChanged(_selected);
			accessibilityChildFocused(_selected);
		}
	}
}

auto CountrySelectBox::Inner::current() const
-> const std::vector<CountrySelectBox::Entry> & {
	return _filter.isEmpty() ? _list : _filtered;
}

void CountrySelectBox::Inner::updateSelectedRow() {
	updateRow(_selected);
}

void CountrySelectBox::Inner::updateRow(int index) {
	if (index >= 0) {
		update(0, st::countriesSkip + index * _rowHeight, width(), _rowHeight);
	}
}

void CountrySelectBox::Inner::setPressed(int pressed) {
	if (_pressed >= 0 && _pressed < _ripples.size() && _ripples[_pressed]) {
		_ripples[_pressed]->lastStop();
	}
	_pressed = pressed;
}

CountrySelectBox::Inner::~Inner() = default;

} // namespace Ui
