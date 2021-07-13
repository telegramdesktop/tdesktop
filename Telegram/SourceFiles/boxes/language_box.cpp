/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/language_box.h"

#include "lang/lang_keys.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/multi_select.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/box_content_divider.h"
#include "ui/text/text_entity.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/effects/ripple_animation.h"
#include "ui/toast/toast.h"
#include "ui/text/text_options.h"
#include "storage/localstorage.h"
#include "boxes/confirm_box.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "core/application.h"
#include "lang/lang_instance.h"
#include "lang/lang_cloud_manager.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"
#include "styles/style_passport.h"
#include "styles/style_chat_helpers.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

namespace {

using Language = Lang::Language;
using Languages = Lang::CloudManager::Languages;

class Rows : public Ui::RpWidget {
public:
	Rows(
		QWidget *parent,
		const Languages &data,
		const QString &chosen,
		bool areOfficial);

	void filter(const QString &query);

	int count() const;
	int selected() const;
	void setSelected(int selected);
	rpl::producer<bool> hasSelection() const;
	rpl::producer<bool> isEmpty() const;

	void activateSelected();
	rpl::producer<Language> activations() const;
	void changeChosen(const QString &chosen);

	Ui::ScrollToRequest rowScrollRequest(int index) const;

	static int DefaultRowHeight();

protected:
	int resizeGetHeight(int newWidth) override;

	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	struct Row {
		Language data;
		Ui::Text::String title = { st::boxWideWidth / 2 };
		Ui::Text::String description = { st::boxWideWidth / 2 };
		int top = 0;
		int height = 0;
		mutable std::unique_ptr<Ui::RippleAnimation> ripple;
		mutable std::unique_ptr<Ui::RippleAnimation> menuToggleRipple;
		bool menuToggleForceRippled = false;
		int titleHeight = 0;
		int descriptionHeight = 0;
		QStringList keywords;
		std::unique_ptr<Ui::RadioView> check;
		bool removed = false;
	};
	struct RowSelection {
		int index = 0;

		inline bool operator==(const RowSelection &other) const {
			return (index == other.index);
		}
	};
	struct MenuSelection {
		int index = 0;

		inline bool operator==(const MenuSelection &other) const {
			return (index == other.index);
		}
	};
	using Selection = std::variant<v::null_t, RowSelection, MenuSelection>;

	void updateSelected(Selection selected);
	void updatePressed(Selection pressed);
	Rows::Row &rowByIndex(int index);
	const Rows::Row &rowByIndex(int index) const;
	Rows::Row &rowBySelection(Selection selected);
	const Rows::Row &rowBySelection(Selection selected) const;
	std::unique_ptr<Ui::RippleAnimation> &rippleBySelection(
		Selection selected);
	[[maybe_unused]] const std::unique_ptr<Ui::RippleAnimation> &rippleBySelection(
		Selection selected) const;
	std::unique_ptr<Ui::RippleAnimation> &rippleBySelection(
		not_null<Row*> row,
		Selection selected);
	[[maybe_unused]] const std::unique_ptr<Ui::RippleAnimation> &rippleBySelection(
		not_null<const Row*> row,
		Selection selected) const;
	void addRipple(Selection selected, QPoint position);
	void ensureRippleBySelection(Selection selected);
	void ensureRippleBySelection(not_null<Row*> row, Selection selected);
	int indexFromSelection(Selection selected) const;
	int countAvailableWidth() const;
	int countAvailableWidth(int newWidth) const;
	QRect menuToggleArea() const;
	QRect menuToggleArea(not_null<const Row*> row) const;
	void repaint(Selection selected);
	void repaint(int index);
	void repaint(const Row &row);
	void repaintChecked(not_null<const Row*> row);
	void activateByIndex(int index);

	void showMenu(int index);
	void setForceRippled(not_null<Row*> row, bool rippled);
	bool canShare(not_null<const Row*> row) const;
	bool canRemove(not_null<const Row*> row) const;
	bool hasMenu(not_null<const Row*> row) const;
	void share(not_null<const Row*> row) const;
	void remove(not_null<Row*> row);
	void restore(not_null<Row*> row);

	std::vector<Row> _rows;
	std::vector<not_null<Row*>> _filtered;
	Selection _selected;
	Selection _pressed;
	QString _chosen;
	QStringList _query;

	bool _areOfficial = false;
	bool _mouseSelection = false;
	QPoint _globalMousePosition;
	base::unique_qptr<Ui::DropdownMenu> _menu;
	int _menuShownIndex = -1;
	bool _menuOtherEntered = false;

	rpl::event_stream<bool> _hasSelection;
	rpl::event_stream<Language> _activations;
	rpl::event_stream<bool> _isEmpty;

};

class Content : public Ui::RpWidget {
public:
	Content(
		QWidget *parent,
		const Languages &recent,
		const Languages &official);

	Ui::ScrollToRequest jump(int rows);
	void filter(const QString &query);
	rpl::producer<Language> activations() const;
	void changeChosen(const QString &chosen);
	void activateBySubmit();

private:
	void setupContent(
		const Languages &recent,
		const Languages &official);

	Fn<Ui::ScrollToRequest(int rows)> _jump;
	Fn<void(const QString &query)> _filter;
	Fn<rpl::producer<Language>()> _activations;
	Fn<void(const QString &chosen)> _changeChosen;
	Fn<void()> _activateBySubmit;

};

std::pair<Languages, Languages> PrepareLists() {
	const auto projId = [](const Language &language) {
		return language.id;
	};
	const auto current = Lang::LanguageIdOrDefault(Lang::Id());
	auto official = Lang::CurrentCloudManager().languageList();
	auto recent = Local::readRecentLanguages();
	ranges::stable_partition(recent, [&](const Language &language) {
		return (language.id == current);
	});
	if (recent.empty() || recent.front().id != current) {
		if (ranges::find(official, current, projId) == end(official)) {
			const auto generate = [&] {
				const auto name = (current == "#custom")
					? "Custom lang pack"
					: Lang::GetInstance().name();
				return Language{
					current,
					QString(),
					QString(),
					name,
					Lang::GetInstance().nativeName()
				};
			};
			recent.insert(begin(recent), generate());
		}
	}
	auto i = begin(official), e = end(official);
	const auto remover = [&](const Language &language) {
		auto k = ranges::find(i, e, language.id, projId);
		if (k == e) {
			return false;
		}
		for (; k != i; --k) {
			std::swap(*k, *(k - 1));
		}
		++i;
		return true;
	};
	recent.erase(ranges::remove_if(recent, remover), end(recent));
	return { std::move(recent), std::move(official) };
}

Rows::Rows(
	QWidget *parent,
	const Languages &data,
	const QString &chosen,
	bool areOfficial)
: RpWidget(parent)
, _chosen(chosen)
, _areOfficial(areOfficial) {
	const auto descriptionOptions = TextParseOptions{
		TextParseMultiline,
		0,
		0,
		Qt::LayoutDirectionAuto
	};
	_rows.reserve(data.size());
	for (const auto &item : data) {
		_rows.push_back(Row{ item });
		auto &row = _rows.back();
		row.check = std::make_unique<Ui::RadioView>(
			st::langsRadio,
			(row.data.id == _chosen),
			[=, row = &row] { repaint(*row); });
		row.title.setText(
			st::semiboldTextStyle,
			item.nativeName,
			Ui::NameTextOptions());
		row.description.setText(
			st::defaultTextStyle,
			item.name,
			descriptionOptions);
		row.keywords = TextUtilities::PrepareSearchWords(
			item.name + ' ' + item.nativeName);
	}
	resizeToWidth(width());
	setAttribute(Qt::WA_MouseTracking);
	update();
}

void Rows::mouseMoveEvent(QMouseEvent *e) {
	const auto position = e->globalPos();
	if (_menu) {
		const auto rect = (_menuShownIndex >= 0)
			? menuToggleArea(&rowByIndex(_menuShownIndex))
			: QRect();
		if (rect.contains(e->pos())) {
			if (!_menuOtherEntered) {
				_menuOtherEntered = true;
				_menu->otherEnter();
			}
		} else {
			if (_menuOtherEntered) {
				_menuOtherEntered = false;
				_menu->otherLeave();
			}
		}
	}
	if (!_mouseSelection && position == _globalMousePosition) {
		return;
	}
	_mouseSelection = true;
	_globalMousePosition = position;
	const auto index = [&] {
		const auto y = e->pos().y();
		if (y < 0) {
			return -1;
		}
		for (auto i = 0, till = count(); i != till; ++i) {
			const auto &row = rowByIndex(i);
			if (row.top + row.height > y) {
				return i;
			}
		}
		return -1;
	}();
	const auto row = (index >= 0) ? &rowByIndex(index) : nullptr;
	const auto inMenuToggle = (index >= 0 && hasMenu(row))
		? menuToggleArea(row).contains(e->pos())
		: false;
	if (index < 0) {
		updateSelected({});
	} else if (inMenuToggle) {
		updateSelected(MenuSelection{ index });
	} else if (!row->removed) {
		updateSelected(RowSelection{ index });
	} else {
		updateSelected({});
	}
}

void Rows::mousePressEvent(QMouseEvent *e) {
	updatePressed(_selected);
	if (!v::is_null(_pressed)
		&& !rowBySelection(_pressed).menuToggleForceRippled) {
		addRipple(_pressed, e->pos());
	}
}

QRect Rows::menuToggleArea() const {
	const auto size = st::topBarSearch.width;
	const auto top = (DefaultRowHeight() - size) / 2;
	const auto skip = st::boxScroll.width
		- st::boxScroll.deltax
		+ top;
	const auto left = width() - skip - size;
	return QRect(left, top, size, size);
}

QRect Rows::menuToggleArea(not_null<const Row*> row) const {
	return menuToggleArea().translated(0, row->top);
}

void Rows::addRipple(Selection selected, QPoint position) {
	Expects(!v::is_null(selected));

	ensureRippleBySelection(selected);

	const auto menu = v::is<MenuSelection>(selected);
	const auto &row = rowBySelection(selected);
	const auto menuArea = menuToggleArea(&row);
	auto &ripple = rippleBySelection(&row, selected);
	const auto topleft = menu ? menuArea.topLeft() : QPoint(0, row.top);
	ripple->add(position - topleft);
}

void Rows::ensureRippleBySelection(Selection selected) {
	ensureRippleBySelection(&rowBySelection(selected), selected);
}

void Rows::ensureRippleBySelection(not_null<Row*> row, Selection selected) {
	auto &ripple = rippleBySelection(row, selected);
	if (ripple) {
		return;
	}
	const auto menu = v::is<MenuSelection>(selected);
	const auto menuArea = menuToggleArea(row);
	auto mask = menu
		? Ui::RippleAnimation::ellipseMask(menuArea.size())
		: Ui::RippleAnimation::rectMask({ width(), row->height });
	ripple = std::make_unique<Ui::RippleAnimation>(
		st::defaultRippleAnimation,
		std::move(mask),
		[=] { repaintChecked(row); });
}

void Rows::mouseReleaseEvent(QMouseEvent *e) {
	if (_menu && e->button() == Qt::LeftButton) {
		if (_menu->isHiding()) {
			_menu->otherEnter();
		} else {
			_menu->otherLeave();
		}
	}
	const auto pressed = _pressed;
	updatePressed({});
	if (pressed == _selected) {
		v::match(pressed, [&](RowSelection data) {
			activateByIndex(data.index);
		}, [&](MenuSelection data) {
			showMenu(data.index);
		}, [](v::null_t) {});
	}
}

bool Rows::canShare(not_null<const Row*> row) const {
	return !_areOfficial && !row->data.id.startsWith('#');
}

bool Rows::canRemove(not_null<const Row*> row) const {
	return !_areOfficial && !row->check->checked();
}

bool Rows::hasMenu(not_null<const Row*> row) const {
	return canShare(row) || canRemove(row);
}

void Rows::share(not_null<const Row*> row) const {
	const auto link = qsl("https://t.me/setlanguage/") + row->data.id;
	QGuiApplication::clipboard()->setText(link);
	Ui::Toast::Show(tr::lng_username_copied(tr::now));
}

void Rows::remove(not_null<Row*> row) {
	row->removed = true;
	Local::removeRecentLanguage(row->data.id);
}

void Rows::restore(not_null<Row*> row) {
	row->removed = false;
	Local::saveRecentLanguages(ranges::views::all(
		_rows
	) | ranges::views::filter([](const Row &row) {
		return !row.removed;
	}) | ranges::views::transform([](const Row &row) {
		return row.data;
	}) | ranges::to_vector);
}

void Rows::showMenu(int index) {
	const auto row = &rowByIndex(index);
	if (_menu || !hasMenu(row)) {
		return;
	}
	_menu = base::make_unique_q<Ui::DropdownMenu>(window());
	const auto weak = _menu.get();
	_menu->setHiddenCallback([=] {
		weak->deleteLater();
		if (_menu == weak) {
			setForceRippled(row, false);
			_menuShownIndex = -1;
		}
	});
	_menu->setShowStartCallback([=] {
		if (_menu == weak) {
			setForceRippled(row, true);
			_menuShownIndex = index;
		}
	});
	_menu->setHideStartCallback([=] {
		if (_menu == weak) {
			setForceRippled(row, false);
			_menuShownIndex = -1;
		}
	});
	const auto addAction = [&](
			const QString &text,
			Fn<void()> callback) {
		return _menu->addAction(text, std::move(callback));
	};
	if (canShare(row)) {
		addAction(tr::lng_proxy_edit_share(tr::now), [=] { share(row); });
	}
	if (canRemove(row)) {
		if (row->removed) {
			addAction(tr::lng_proxy_menu_restore(tr::now), [=] {
				restore(row);
			});
		} else {
			addAction(tr::lng_proxy_menu_delete(tr::now), [=] {
				remove(row);
			});
		}
	}
	const auto toggle = menuToggleArea(row);
	const auto parentTopLeft = window()->mapToGlobal({ 0, 0 });
	const auto buttonTopLeft = mapToGlobal(toggle.topLeft());
	const auto parent = QRect(parentTopLeft, window()->size());
	const auto button = QRect(buttonTopLeft, toggle.size());
	const auto bottom = button.y()
		+ st::proxyDropdownDownPosition.y()
		+ _menu->height()
		- parent.y();
	const auto top = button.y()
		+ st::proxyDropdownUpPosition.y()
		- _menu->height()
		- parent.y();
	_menuShownIndex = index;
	_menuOtherEntered = true;
	if (bottom > parent.height() && top >= 0) {
		const auto left = button.x()
			+ button.width()
			+ st::proxyDropdownUpPosition.x()
			- _menu->width()
			- parent.x();
		_menu->move(left, top);
		_menu->showAnimated(Ui::PanelAnimation::Origin::BottomRight);
	} else {
		const auto left = button.x()
			+ button.width()
			+ st::proxyDropdownDownPosition.x()
			- _menu->width()
			- parent.x();
		_menu->move(left, bottom - _menu->height());
		_menu->showAnimated(Ui::PanelAnimation::Origin::TopRight);
	}
}

void Rows::setForceRippled(not_null<Row*> row, bool rippled) {
	if (row->menuToggleForceRippled != rippled) {
		row->menuToggleForceRippled = rippled;
		auto &ripple = rippleBySelection(row, MenuSelection{});
		if (row->menuToggleForceRippled) {
			ensureRippleBySelection(row, MenuSelection{});
			if (ripple->empty()) {
				ripple->addFading();
			} else {
				ripple->lastUnstop();
			}
		} else {
			if (ripple) {
				ripple->lastStop();
			}
		}
	}
	repaint(*row);
}

void Rows::activateByIndex(int index) {
	_activations.fire_copy(rowByIndex(index).data);
}

void Rows::leaveEventHook(QEvent *e) {
	updateSelected({});
	if (_menu && _menuOtherEntered) {
		_menuOtherEntered = false;
		_menu->otherLeave();
	}
}

void Rows::filter(const QString &query) {
	updateSelected({});
	updatePressed({});
	_menu = nullptr;
	_menuShownIndex = -1;

	_query = TextUtilities::PrepareSearchWords(query);

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

	if (!_query.isEmpty()) {
		_filtered.clear();
		_filtered.reserve(_rows.size());
		for (auto &row : _rows) {
			if (!skip(row.keywords, _query)) {
				_filtered.push_back(&row);
			} else {
				row.ripple = nullptr;
			}
		}
	}

	resizeToWidth(width());
	Ui::SendPendingMoveResizeEvents(this);

	_isEmpty.fire(count() == 0);
}

int Rows::count() const {
	return _query.isEmpty() ? _rows.size() : _filtered.size();
}

int Rows::indexFromSelection(Selection selected) const {
	return v::match(selected, [&](RowSelection data) {
		return data.index;
	}, [&](MenuSelection data) {
		return data.index;
	}, [](v::null_t) {
		return -1;
	});
}

int Rows::selected() const {
	return indexFromSelection(_selected);
}

void Rows::activateSelected() {
	const auto index = selected();
	if (index >= 0) {
		activateByIndex(index);
	}
}

rpl::producer<Language> Rows::activations() const {
	return _activations.events();
}

void Rows::changeChosen(const QString &chosen) {
	for (const auto &row : _rows) {
		row.check->setChecked(row.data.id == chosen, anim::type::normal);
	}
}

void Rows::setSelected(int selected) {
	_mouseSelection = false;
	const auto limit = count();
	if (selected >= 0 && selected < limit) {
		updateSelected(RowSelection{ selected });
	} else {
		updateSelected({});
	}
}

rpl::producer<bool> Rows::hasSelection() const {
	return _hasSelection.events();
}

rpl::producer<bool> Rows::isEmpty() const {
	return _isEmpty.events_starting_with(
		count() == 0
	) | rpl::distinct_until_changed();
}

void Rows::repaint(Selection selected) {
	v::match(selected, [](v::null_t) {
	}, [&](const auto &data) {
		repaint(data.index);
	});
}

void Rows::repaint(int index) {
	if (index >= 0) {
		repaint(rowByIndex(index));
	}
}

void Rows::repaint(const Row &row) {
	update(0, row.top, width(), row.height);
}

void Rows::repaintChecked(not_null<const Row*> row) {
	const auto found = (ranges::find(_filtered, row) != end(_filtered));
	if (_query.isEmpty() || found) {
		repaint(*row);
	}
}

void Rows::updateSelected(Selection selected) {
	const auto changed = (v::is_null(_selected) != v::is_null(selected));
	repaint(_selected);
	_selected = selected;
	repaint(_selected);
	if (changed) {
		_hasSelection.fire(!v::is_null(_selected));
	}
}

void Rows::updatePressed(Selection pressed) {
	if (!v::is_null(_pressed)) {
		if (!rowBySelection(_pressed).menuToggleForceRippled) {
			if (const auto ripple = rippleBySelection(_pressed).get()) {
				ripple->lastStop();
			}
		}
	}
	_pressed = pressed;
}

Rows::Row &Rows::rowByIndex(int index) {
	Expects(index >= 0 && index < count());

	return _query.isEmpty() ? _rows[index] : *_filtered[index];
}

const Rows::Row &Rows::rowByIndex(int index) const {
	Expects(index >= 0 && index < count());

	return _query.isEmpty() ? _rows[index] : *_filtered[index];
}

Rows::Row &Rows::rowBySelection(Selection selected) {
	return rowByIndex(indexFromSelection(selected));
}

const Rows::Row &Rows::rowBySelection(Selection selected) const {
	return rowByIndex(indexFromSelection(selected));
}

std::unique_ptr<Ui::RippleAnimation> &Rows::rippleBySelection(
		Selection selected) {
	return rippleBySelection(&rowBySelection(selected), selected);
}

const std::unique_ptr<Ui::RippleAnimation> &Rows::rippleBySelection(
		Selection selected) const {
	return rippleBySelection(&rowBySelection(selected), selected);
}

std::unique_ptr<Ui::RippleAnimation> &Rows::rippleBySelection(
		not_null<Row*> row,
		Selection selected) {
	return v::is<MenuSelection>(selected)
		? row->menuToggleRipple
		: row->ripple;
}

const std::unique_ptr<Ui::RippleAnimation> &Rows::rippleBySelection(
		not_null<const Row*> row,
		Selection selected) const {
	return const_cast<Rows*>(this)->rippleBySelection(
		const_cast<Row*>(row.get()),
		selected);
}

Ui::ScrollToRequest Rows::rowScrollRequest(int index) const {
	const auto &row = rowByIndex(index);
	return Ui::ScrollToRequest(row.top, row.top + row.height);
}

int Rows::DefaultRowHeight() {
	return st::passportRowPadding.top()
		+ st::semiboldFont->height
		+ st::passportRowSkip
		+ st::normalFont->height
		+ st::passportRowPadding.bottom();
}

int Rows::resizeGetHeight(int newWidth) {
	const auto availableWidth = countAvailableWidth(newWidth);
	auto result = 0;
	for (auto i = 0, till = count(); i != till; ++i) {
		auto &row = rowByIndex(i);
		row.top = result;
		row.titleHeight = row.title.countHeight(availableWidth);
		row.descriptionHeight = row.description.countHeight(availableWidth);
		row.height = st::passportRowPadding.top()
			+ row.titleHeight
			+ st::passportRowSkip
			+ row.descriptionHeight
			+ st::passportRowPadding.bottom();
		result += row.height;
	}
	return result;
}

int Rows::countAvailableWidth(int newWidth) const {
	const auto right = width() - menuToggleArea().x();
	return newWidth
		- st::passportRowPadding.left()
		- st::langsRadio.diameter
		- st::passportRowPadding.left()
		- right
		- st::passportRowIconSkip;
}

int Rows::countAvailableWidth() const {
	return countAvailableWidth(width());
}

void Rows::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto clip = e->rect();

	const auto checkLeft = st::passportRowPadding.left();
	const auto left = checkLeft
		+ st::langsRadio.diameter
		+ st::passportRowPadding.left();
	const auto availableWidth = countAvailableWidth();
	const auto menu = menuToggleArea();
	const auto selectedIndex = (_menuShownIndex >= 0)
		? _menuShownIndex
		: indexFromSelection(!v::is_null(_pressed) ? _pressed : _selected);
	for (auto i = 0, till = count(); i != till; ++i) {
		const auto &row = rowByIndex(i);
		if (row.top + row.height <= clip.y()) {
			continue;
		} else if (row.top >= clip.y() + clip.height()) {
			break;
		}
		p.setOpacity(row.removed ? st::stickersRowDisabledOpacity : 1.);
		p.translate(0, row.top);
		const auto guard = gsl::finally([&] { p.translate(0, -row.top); });

		const auto selected = (selectedIndex == i);
		if (selected && !row.removed) {
			p.fillRect(0, 0, width(), row.height, st::windowBgOver);
		}

		if (row.ripple) {
			row.ripple->paint(p, 0, 0, width());
			if (row.ripple->empty()) {
				row.ripple.reset();
			}
		}

		const auto checkTop = (row.height - st::defaultRadio.diameter) / 2;
		row.check->paint(p, checkLeft, checkTop, width());

		auto top = st::passportRowPadding.top();

		p.setPen(st::passportRowTitleFg);
		row.title.drawLeft(p, left, top, availableWidth, width());
		top += row.titleHeight + st::passportRowSkip;

		p.setPen(selected ? st::windowSubTextFgOver : st::windowSubTextFg);
		row.description.drawLeft(p, left, top, availableWidth, width());
		top += row.descriptionHeight + st::passportRowPadding.bottom();

		if (hasMenu(&row)) {
			p.setOpacity(1.);
			if (selected && row.removed) {
				PainterHighQualityEnabler hq(p);
				p.setPen(Qt::NoPen);
				p.setBrush(st::windowBgOver);
				p.drawEllipse(menu);
			}
			if (row.menuToggleRipple) {
				row.menuToggleRipple->paint(p, menu.x(), menu.y(), width());
				if (row.menuToggleRipple->empty()) {
					row.menuToggleRipple.reset();
				}
			}
			(selected
				? st::topBarMenuToggle.iconOver
				: st::topBarMenuToggle.icon).paintInCenter(p, menu);
		}
	}
}

Content::Content(
	QWidget *parent,
	const Languages &recent,
	const Languages &official)
: RpWidget(parent) {
	setupContent(recent, official);
}

void Content::setupContent(
		const Languages &recent,
		const Languages &official) {
	using namespace rpl::mappers;

	const auto current = Lang::LanguageIdOrDefault(Lang::Id());
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto add = [&](const Languages &list, bool areOfficial) {
		if (list.empty()) {
			return (Rows*)nullptr;
		}
		const auto wrap = content->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				content,
				object_ptr<Ui::VerticalLayout>(content)));
		const auto inner = wrap->entity();
		inner->add(object_ptr<Ui::FixedHeightWidget>(
			inner,
			st::defaultBox.margin.top()));
		const auto rows = inner->add(object_ptr<Rows>(
			inner,
			list,
			current,
			areOfficial));
		inner->add(object_ptr<Ui::FixedHeightWidget>(
			inner,
			st::defaultBox.margin.top()));

		rows->isEmpty() | rpl::start_with_next([=](bool empty) {
			wrap->toggle(!empty, anim::type::instant);
		}, rows->lifetime());

		return rows;
	};
	const auto main = add(recent, false);
	const auto divider = content->add(
		object_ptr<Ui::SlideWrap<Ui::BoxContentDivider>>(
			content,
			object_ptr<Ui::BoxContentDivider>(content)));
	const auto other = add(official, true);
	Ui::ResizeFitChild(this, content);

	if (main && other) {
		rpl::combine(
			main->isEmpty(),
			other->isEmpty(),
			_1 || _2
		) | rpl::start_with_next([=](bool empty) {
			divider->toggle(!empty, anim::type::instant);
		}, divider->lifetime());

		const auto excludeSelections = [](Rows *a, Rows *b) {
			a->hasSelection(
			) | rpl::filter(
				_1
			) | rpl::start_with_next([=] {
				b->setSelected(-1);
			}, a->lifetime());
		};
		excludeSelections(main, other);
		excludeSelections(other, main);
	} else {
		divider->hide(anim::type::instant);
	}

	const auto count = [](Rows *widget) {
		return widget ? widget->count() : 0;
	};
	const auto selected = [](Rows *widget) {
		return widget ? widget->selected() : -1;
	};
	const auto rowsCount = [=] {
		return count(main) + count(other);
	};
	const auto selectedIndex = [=] {
		if (const auto index = selected(main); index >= 0) {
			return index;
		} else if (const auto index = selected(other); index >= 0) {
			return count(main) + index;
		}
		return -1;
	};
	const auto setSelectedIndex = [=](int index) {
		const auto first = count(main);
		if (index >= first) {
			if (main) {
				main->setSelected(-1);
			}
			if (other) {
				other->setSelected(index - first);
			}
		} else {
			if (main) {
				main->setSelected(index);
			}
			if (other) {
				other->setSelected(-1);
			}
		}
	};
	const auto selectedCoords = [=] {
		const auto coords = [=](Rows *rows, int index) {
			const auto result = rows->rowScrollRequest(index);
			const auto shift = rows->mapToGlobal({ 0, 0 }).y()
				- mapToGlobal({ 0, 0 }).y();
			return Ui::ScrollToRequest(
				result.ymin + shift,
				result.ymax + shift);
		};
		if (const auto index = selected(main); index >= 0) {
			return coords(main, index);
		} else if (const auto index = selected(other); index >= 0) {
			return coords(other, index);
		}
		return Ui::ScrollToRequest(-1, -1);
	};
	_jump = [=](int rows) {
		const auto count = rowsCount();
		const auto now = selectedIndex();
		if (now >= 0) {
			const auto changed = now + rows;
			if (changed < 0) {
				setSelectedIndex((now > 0) ? 0 : -1);
			} else if (changed >= count) {
				setSelectedIndex(count - 1);
			} else {
				setSelectedIndex(changed);
			}
		} else if (rows > 0) {
			setSelectedIndex(0);
		}
		return selectedCoords();
	};
	const auto filter = [](Rows *widget, const QString &query) {
		if (widget) {
			widget->filter(query);
		}
	};
	_filter = [=](const QString &query) {
		filter(main, query);
		filter(other, query);
	};
	_activations = [=] {
		if (!main && !other) {
			return rpl::never<Language>() | rpl::type_erased();
		} else if (!main) {
			return other->activations();
		} else if (!other) {
			return main->activations();
		}
		return rpl::merge(
			main->activations(),
			other->activations()
		) | rpl::type_erased();
	};
	_changeChosen = [=](const QString &chosen) {
		if (main) {
			main->changeChosen(chosen);
		}
		if (other) {
			other->changeChosen(chosen);
		}
	};
	_activateBySubmit = [=] {
		if (selectedIndex() < 0) {
			_jump(1);
		}
		if (main) {
			main->activateSelected();
		}
		if (other) {
			other->activateSelected();
		}
	};
}

void Content::filter(const QString &query) {
	_filter(query);
}

rpl::producer<Language> Content::activations() const {
	return _activations();
}

void Content::changeChosen(const QString &chosen) {
	_changeChosen(chosen);
}

void Content::activateBySubmit() {
	_activateBySubmit();
}

Ui::ScrollToRequest Content::jump(int rows) {
	return _jump(rows);
}

} // namespace

void LanguageBox::prepare() {
	addButton(tr::lng_box_ok(), [=] { closeBox(); });

	setTitle(tr::lng_languages());

	const auto select = createMultiSelect();

	using namespace rpl::mappers;

	const auto [recent, official] = PrepareLists();
	const auto inner = setInnerWidget(
		object_ptr<Content>(this, recent, official),
		st::boxScroll,
		select->height());
	inner->resizeToWidth(st::boxWidth);

	const auto max = lifetime().make_state<int>(0);
	rpl::combine(
		inner->heightValue(),
		select->heightValue(),
		_1 + _2
	) | rpl::start_with_next([=](int height) {
		accumulate_max(*max, height);
		setDimensions(st::boxWidth, qMin(*max, st::boxMaxListHeight));
	}, inner->lifetime());

	select->setSubmittedCallback([=](Qt::KeyboardModifiers) {
		inner->activateBySubmit();
	});
	select->setQueryChangedCallback([=](const QString &query) {
		inner->filter(query);
	});
	select->setCancelledCallback([=] {
		select->clearQuery();
	});

	inner->activations(
	) | rpl::start_with_next([=](const Language &language) {
		// "#custom" is applied each time it's passed to switchToLanguage().
		// So we check that the language really has changed.
		const auto currentId = [] {
			return Lang::LanguageIdOrDefault(Lang::Id());
		};
		if (language.id != currentId()) {
			Lang::CurrentCloudManager().switchToLanguage(language);
			if (inner) {
				inner->changeChosen(currentId());
			}
		}
	}, inner->lifetime());

	_setInnerFocus = [=] {
		select->setInnerFocus();
	};
	_jump = [=](int rows) {
		return inner->jump(rows);
	};
}

void LanguageBox::keyPressEvent(QKeyEvent *e) {
	const auto key = e->key();
	if (key == Qt::Key_Escape) {
		closeBox();
		return;
	}
	const auto selected = [&] {
		if (key == Qt::Key_Up) {
			return _jump(-1);
		} else if (key == Qt::Key_Down) {
			return _jump(1);
		} else if (key == Qt::Key_PageUp) {
			return _jump(-rowsInPage());
		} else if (key == Qt::Key_PageDown) {
			return _jump(rowsInPage());
		}
		return Ui::ScrollToRequest(-1, -1);
	}();
	if (selected.ymin >= 0 && selected.ymax >= 0) {
		onScrollToY(selected.ymin, selected.ymax);
	}
}

int LanguageBox::rowsInPage() const {
	return std::max(height() / Rows::DefaultRowHeight(), 1);
}

void LanguageBox::setInnerFocus() {
	_setInnerFocus();
}

not_null<Ui::MultiSelect*> LanguageBox::createMultiSelect() {
	const auto result = Ui::CreateChild<Ui::MultiSelect>(
		this,
		st::defaultMultiSelect,
		tr::lng_participant_filter());
	result->resizeToWidth(st::boxWidth);
	result->moveToLeft(0, 0);
	return result;
}

base::binary_guard LanguageBox::Show() {
	auto result = base::binary_guard();

	auto &manager = Lang::CurrentCloudManager();
	if (manager.languageList().empty()) {
		auto guard = std::make_shared<base::binary_guard>(
			result.make_guard());
		auto lifetime = std::make_shared<rpl::lifetime>();
		manager.languageListChanged(
		) | rpl::take(
			1
		) | rpl::start_with_next([=]() mutable {
			const auto show = guard->alive();
			if (lifetime) {
				base::take(lifetime)->destroy();
			}
			if (show) {
				Ui::show(Box<LanguageBox>());
			}
		}, *lifetime);
	} else {
		Ui::show(Box<LanguageBox>());
	}
	manager.requestLanguageList();

	return result;
}
