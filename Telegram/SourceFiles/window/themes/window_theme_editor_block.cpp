/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/themes/window_theme_editor_block.h"

#include "base/call_delayed.h"
#include "boxes/abstract_box.h"
#include "lang/lang_keys.h"
#include "ui/effects/ripple_animation.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/widgets/color_editor.h"
#include "ui/widgets/shadow.h"
#include "styles/style_layers.h"
#include "styles/style_window.h"

namespace Window {
namespace Theme {
namespace {

auto SearchSplitter = QRegularExpression(u"[\\@\\s\\-\\+\\(\\)\\[\\]\\{\\}\\<\\>\\,\\.\\:\\!\\_\\;\\\"\\'\\x0\\#]"_q);

} // namespace

class EditorBlock::Row {
public:
	Row(const QString &name, const QString &copyOf, QColor value);

	QString name() const {
		return _name;
	}

	void setCopyOf(const QString &copyOf) {
		_copyOf = copyOf;
		fillSearchIndex();
	}
	QString copyOf() const {
		return _copyOf;
	}

	void setValue(QColor value);
	const QColor &value() const {
		return _value;
	}

	QString description() const {
		return _description.toString();
	}
	const Ui::Text::String &descriptionText() const {
		return _description;
	}
	void setDescription(const QString &description) {
		_description.setText(st::defaultTextStyle, description);
		fillSearchIndex();
	}

	const base::flat_set<QString> &searchWords() const {
		return _searchWords;
	}
	bool searchWordsContain(const QString &needle) const {
		for (const auto &word : _searchWords) {
			if (word.startsWith(needle)) {
				return true;
			}
		}
		return false;
	}

	const base::flat_set<QChar> &searchStartChars() const {
		return _searchStartChars;
	}

	void setTop(int top) {
		_top = top;
	}
	int top() const {
		return _top;
	}

	void setHeight(int height) {
		_height = height;
	}
	int height() const {
		return _height;
	}

	Ui::RippleAnimation *ripple() const {
		return _ripple.get();
	}
	Ui::RippleAnimation *setRipple(std::unique_ptr<Ui::RippleAnimation> ripple) const {
		_ripple = std::move(ripple);
		return _ripple.get();
	}
	void resetRipple() const {
		_ripple = nullptr;
	}

private:
	void fillValueString();
	void fillSearchIndex();

	QString _name;
	QString _copyOf;
	QColor _value;
	QString _valueString;
	Ui::Text::String _description = { st::windowMinWidth / 2 };

	base::flat_set<QString> _searchWords;
	base::flat_set<QChar> _searchStartChars;

	int _top = 0;
	int _height = 0;

	mutable std::unique_ptr<Ui::RippleAnimation> _ripple;

};

EditorBlock::Row::Row(const QString &name, const QString &copyOf, QColor value)
: _name(name)
, _copyOf(copyOf) {
	setValue(value);
}

void EditorBlock::Row::setValue(QColor value) {
	_value = value;
	fillValueString();
	fillSearchIndex();
}

void EditorBlock::Row::fillValueString() {
	auto addHex = [=](int code) {
		if (code >= 0 && code < 10) {
			_valueString.append(QChar('0' + code));
		} else if (code >= 10 && code < 16) {
			_valueString.append(QChar('a' + (code - 10)));
		}
	};
	auto addCode = [=](int code) {
		addHex(code / 16);
		addHex(code % 16);
	};
	_valueString.resize(0);
	_valueString.reserve(9);
	_valueString.append('#');
	addCode(_value.red());
	addCode(_value.green());
	addCode(_value.blue());
	if (_value.alpha() != 255) {
		addCode(_value.alpha());
	}
}

void EditorBlock::Row::fillSearchIndex() {
	_searchWords.clear();
	_searchStartChars.clear();
	const auto toIndex = _name
		+ ' ' + _copyOf
		+ ' ' + TextUtilities::RemoveAccents(_description.toString())
		+ ' ' + _valueString;
	const auto words = toIndex.toLower().split(
		SearchSplitter,
		Qt::SkipEmptyParts);
	for (const auto &word : words) {
		_searchWords.emplace(word);
		_searchStartChars.emplace(word[0]);
	}
}

EditorBlock::EditorBlock(QWidget *parent, Type type, Context *context)
: RpWidget(parent)
, _type(type)
, _context(context)
, _transparent(style::TransparentPlaceholder()) {
	setMouseTracking(true);

	_context->updated.events(
	) | rpl::start_with_next([=] {
		if (_mouseSelection) {
			_lastGlobalPos = QCursor::pos();
			updateSelected(mapFromGlobal(_lastGlobalPos));
		}
		update();
	}, lifetime());

	if (_type == Type::Existing) {
		_context->appended.events(
		) | rpl::start_with_next([=](const Context::AppendData &added) {
			auto name = added.name;
			auto value = added.value;
			feed(name, value);
			feedDescription(name, added.description);

			auto row = findRow(name);
			Assert(row != nullptr);
			auto possibleCopyOf = added.possibleCopyOf;
			auto copyOf = checkCopyOf(findRowIndex(row), possibleCopyOf) ? possibleCopyOf : QString();
			removeFromSearch(*row);
			row->setCopyOf(copyOf);
			addToSearch(*row);

			_context->changed.fire({ QStringList(name), value });
			_context->resized.fire({});
			_context->pending.fire({ name, copyOf, value });
		}, lifetime());
	} else {
		_context->changed.events(
		) | rpl::start_with_next([=](const Context::ChangeData &data) {
			checkCopiesChanged(0, data.names, data.value);
		}, lifetime());
	}
}

void EditorBlock::feed(const QString &name, QColor value, const QString &copyOfExisting) {
	if (findRow(name)) {
		// Remove the existing row and mark all its copies as unique keys.
		LOG(("Theme Warning: Color value '%1' appears more than once in the color scheme.").arg(name));
		removeRow(name);
	}
	addRow(name, copyOfExisting, value);
}

bool EditorBlock::feedCopy(const QString &name, const QString &copyOf) {
	if (auto row = findRow(copyOf)) {
		if (copyOf == name) {
			LOG(("Theme Warning: Skipping value '%1: %2' (the value refers to itself.)").arg(name, copyOf));
			return true;
		}
		if (findRow(name)) {
			// Remove the existing row and mark all its copies as unique keys.
			LOG(("Theme Warning: Color value '%1' appears more than once in the color scheme.").arg(name));
			removeRow(name);

			// row was invalidated by removeRow() call.
			row = findRow(copyOf);
			// Should not happen, but still check.
			if (!row) {
				return true;
			}
		}
		addRow(name, copyOf, row->value());
	} else {
		LOG(("Theme Warning: Skipping value '%1: %2' (expected a color value in #rrggbb or #rrggbbaa or a previously defined key in the color scheme)").arg(name, copyOf));
	}
	return true;
}

void EditorBlock::removeRow(const QString &name, bool removeCopyReferences) {
	auto it = _indices.find(name);
	Assert(it != _indices.cend());

	auto index = it.value();
	for (auto i = index + 1, count = static_cast<int>(_data.size()); i != count; ++i) {
		auto &row = _data[i];
		removeFromSearch(row);
		_indices[row.name()] = i - 1;
		if (removeCopyReferences && row.copyOf() == name) {
			row.setCopyOf(QString());
		}
	}
	removeFromSearch(_data[index]);
	_data.erase(_data.begin() + index);
	_indices.erase(it);
	for (auto i = index, count = static_cast<int>(_data.size()); i != count; ++i) {
		addToSearch(_data[i]);
	}
}

void EditorBlock::addToSearch(const Row &row) {
	auto query = _searchQuery;
	if (!query.isEmpty()) resetSearch();

	auto index = findRowIndex(&row);
	for (const auto &ch : row.searchStartChars()) {
		_searchIndex[ch].insert(index);
	}

	if (!query.isEmpty()) searchByQuery(query);
}

void EditorBlock::removeFromSearch(const Row &row) {
	auto query = _searchQuery;
	if (!query.isEmpty()) resetSearch();

	auto index = findRowIndex(&row);
	for (const auto &ch : row.searchStartChars()) {
		const auto i = _searchIndex.find(ch);
		if (i != end(_searchIndex)) {
			i->second.remove(index);
			if (i->second.empty()) {
				_searchIndex.erase(i);
			}
		}
	}

	if (!query.isEmpty()) searchByQuery(query);
}

void EditorBlock::filterRows(const QString &query) {
	searchByQuery(query);
}

void EditorBlock::chooseRow() {
	if (_selected < 0) {
		return;
	}
	activateRow(rowAtIndex(_selected));
}

void EditorBlock::activateRow(const Row &row) {
	if (_context->colorEditor.editor) {
		if (_type == Type::Existing) {
			_context->possibleCopyOf = row.name();
			_context->colorEditor.editor->showColor(row.value());
		}
	} else {
		_editing = findRowIndex(&row);
		const auto name = row.name();
		const auto value = row.value();
		Ui::show(Box([=](not_null<Ui::GenericBox*> box) {
			const auto editor = box->addRow(object_ptr<ColorEditor>(
				box,
				ColorEditor::Mode::RGBA,
				value));
			struct State {
				rpl::lifetime cancelLifetime;
			};
			const auto state = editor->lifetime().make_state<State>();

			const auto save = crl::guard(this, [=] {
				state->cancelLifetime.destroy();
				saveEditing(editor->color());
			});
			box->boxClosing(
			) | rpl::start_with_next(crl::guard(this, [=] {
				cancelEditing();
			}), state->cancelLifetime);
			editor->submitRequests(
			) | rpl::start_with_next(save, editor->lifetime());

			box->setFocusCallback([=] {
				editor->setInnerFocus();
			});
			box->addButton(tr::lng_settings_save(), save);
			box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
			box->setTitle(rpl::single(name));
			box->setWidth(editor->width());

			_context->colorEditor.box = box;
			_context->colorEditor.editor = editor;
			_context->name = name;
			_context->updated.fire({});
		}));
	}
}

bool EditorBlock::selectSkip(int direction) {
	_mouseSelection = false;

	auto maxSelected = size_type(isSearch()
		? _searchResults.size()
		: _data.size()) - 1;
	auto newSelected = _selected + direction;
	if (newSelected < -1 || newSelected > maxSelected) {
		newSelected = maxSelected;
	}
	if (newSelected != _selected) {
		setSelected(newSelected);
		scrollToSelected();
		return (newSelected >= 0);
	}
	return false;
}

void EditorBlock::scrollToSelected() {
	if (_selected >= 0) {
		const auto &row = rowAtIndex(_selected);
		_context->scroll.fire({ _type, row.top(), row.height() });
	}
}

void EditorBlock::searchByQuery(QString query) {
	const auto words = TextUtilities::PrepareSearchWords(
		query,
		&SearchSplitter);
	query = words.isEmpty() ? QString() : words.join(' ');
	if (_searchQuery != query) {
		setSelected(-1);
		setPressed(-1);

		_searchQuery = query;
		_searchResults.clear();

		auto toFilter = (base::flat_set<int>*)nullptr;
		for (const auto &word : words) {
			if (word.isEmpty()) continue;

			const auto i = _searchIndex.find(word[0]);
			if (i == end(_searchIndex) || i->second.empty()) {
				toFilter = nullptr;
				break;
			} else if (!toFilter || i->second.size() < toFilter->size()) {
				toFilter = &i->second;
			}
		}
		if (toFilter) {
			const auto allWordsFound = [&](const Row &row) {
				for (const auto &word : words) {
					if (!row.searchWordsContain(word)) {
						return false;
					}
				}
				return true;
			};
			for (const auto index : *toFilter) {
				if (allWordsFound(_data[index])) {
					_searchResults.push_back(index);
				}
			}
		}

		_context->resized.fire({});
	}
}

const QColor *EditorBlock::find(const QString &name) {
	if (auto row = findRow(name)) {
		return &row->value();
	}
	return nullptr;
}

bool EditorBlock::feedDescription(const QString &name, const QString &description) {
	if (auto row = findRow(name)) {
		removeFromSearch(*row);
		row->setDescription(description);
		addToSearch(*row);
		return true;
	}
	return false;
}

void EditorBlock::sortByDistance(const QColor &to) {
	auto toHue = int();
	auto toSaturation = int();
	auto toLightness = int();
	to.getHsl(&toHue, &toSaturation, &toLightness);
	ranges::sort(_data, ranges::less(), [&](const Row &row) {
		auto fromHue = int();
		auto fromSaturation = int();
		auto fromLightness = int();
		row.value().getHsl(&fromHue, &fromSaturation, &fromLightness);
		if (!row.copyOf().isEmpty()) {
			return 365;
		}
		const auto a = std::abs(fromHue - toHue);
		const auto b = 360 + fromHue - toHue;
		const auto c = 360 + toHue - fromHue;
		if (std::min(a, std::min(b, c)) > 15) {
			return 363;
		}
		return 255 - fromSaturation;
	});
}

template <typename Callback>
void EditorBlock::enumerateRows(Callback callback) {
	if (isSearch()) {
		for (const auto index : _searchResults) {
			if (!callback(_data[index])) {
				break;
			}
		}
	} else {
		for (auto &row : _data) {
			if (!callback(row)) {
				break;
			}
		}
	}
}

template <typename Callback>
void EditorBlock::enumerateRows(Callback callback) const {
	if (isSearch()) {
		for (const auto index : _searchResults) {
			if (!callback(_data[index])) {
				break;
			}
		}
	} else {
		for (const auto &row : _data) {
			if (!callback(row)) {
				break;
			}
		}
	}
}

template <typename Callback>
void EditorBlock::enumerateRowsFrom(int top, Callback callback) {
	auto started = false;
	auto index = 0;
	enumerateRows([top, callback, &started, &index](Row &row) {
		if (!started) {
			if (row.top() + row.height() <= top) {
				++index;
				return true;
			}
			started = true;
		}
		return callback(index++, row);
	});
}

template <typename Callback>
void EditorBlock::enumerateRowsFrom(int top, Callback callback) const {
	auto started = false;
	enumerateRows([top, callback, &started](const Row &row) {
		if (!started) {
			if (row.top() + row.height() <= top) {
				return true;
			}
			started = true;
		}
		return callback(row);
	});
}

int EditorBlock::resizeGetHeight(int newWidth) {
	auto result = 0;
	auto descriptionWidth = newWidth - st::themeEditorMargin.left() - st::themeEditorMargin.right();
	enumerateRows([&](Row &row) {
		row.setTop(result);

		auto height = row.height();
		if (!height) {
			height = st::themeEditorMargin.top() + st::themeEditorSampleSize.height();
			if (!row.descriptionText().isEmpty()) {
				height += st::themeEditorDescriptionSkip + row.descriptionText().countHeight(descriptionWidth);
			}
			height += st::themeEditorMargin.bottom();
			row.setHeight(height);
		}
		result += row.height();
		return true;
	});

	if (_type == Type::New) {
		setHidden(!result);
	}
	if (_type == Type::Existing && !result && !isSearch()) {
		return st::noContactsHeight;
	}
	return result;
}

void EditorBlock::mousePressEvent(QMouseEvent *e) {
	updateSelected(e->pos());
	setPressed(_selected);
}

void EditorBlock::mouseReleaseEvent(QMouseEvent *e) {
	auto pressed = _pressed;
	setPressed(-1);
	if (pressed == _selected) {
		if (_context->colorEditor.box) {
			chooseRow();
		} else if (_selected >= 0) {
			base::call_delayed(st::defaultRippleAnimation.hideDuration, this, [this, index = findRowIndex(&rowAtIndex(_selected))] {
				if (index >= 0 && index < _data.size()) {
					activateRow(_data[index]);
				}
			});
		}
	}
}

void EditorBlock::saveEditing(QColor value) {
	if (_editing < 0) {
		return;
	}
	auto &row = _data[_editing];
	auto name = row.name();
	if (_type == Type::New) {
		setSelected(-1);
		setPressed(-1);

		auto possibleCopyOf = _context->possibleCopyOf.isEmpty() ? row.copyOf() : _context->possibleCopyOf;
		auto color = value;
		auto description = row.description();

		removeRow(name, false);

		_context->appended.fire({ name, possibleCopyOf, color, description });
	} else if (_type == Type::Existing) {
		removeFromSearch(row);

		auto valueChanged = (row.value() != value);
		if (valueChanged) {
			row.setValue(value);
		}

		auto possibleCopyOf = _context->possibleCopyOf.isEmpty() ? row.copyOf() : _context->possibleCopyOf;
		auto copyOf = checkCopyOf(_editing, possibleCopyOf) ? possibleCopyOf : QString();
		auto copyOfChanged = (row.copyOf() != copyOf);
		if (copyOfChanged) {
			row.setCopyOf(copyOf);
		}

		addToSearch(row);

		if (valueChanged || copyOfChanged) {
			checkCopiesChanged(_editing + 1, QStringList(name), value);
			_context->pending.fire({ name, copyOf, value });
		}
	}
	cancelEditing();
}

void EditorBlock::checkCopiesChanged(int startIndex, QStringList names, QColor value) {
	for (auto i = startIndex, count = static_cast<int>(_data.size()); i != count; ++i) {
		auto &checkIfIsCopy = _data[i];
		if (names.contains(checkIfIsCopy.copyOf())) {
			removeFromSearch(checkIfIsCopy);
			checkIfIsCopy.setValue(value);
			names.push_back(checkIfIsCopy.name());
			addToSearch(checkIfIsCopy);
		}
	}
	if (_type == Type::Existing) {
		_context->changed.fire({ names, value });
	}
}

void EditorBlock::cancelEditing() {
	if (_editing >= 0) {
		updateRow(_data[_editing]);
	}
	_editing = -1;
	if (const auto box = base::take(_context->colorEditor.box)) {
		box->closeBox();
	}
	_context->possibleCopyOf = QString();
	if (!_context->name.isEmpty()) {
		_context->name = QString();
		_context->updated.fire({});
	}
}

bool EditorBlock::checkCopyOf(int index, const QString &possibleCopyOf) {
	auto copyOfIndex = findRowIndex(possibleCopyOf);
	return (copyOfIndex >= 0
		&& index > copyOfIndex
		&& _data[copyOfIndex].value().toRgb() == _data[index].value().toRgb());
}

void EditorBlock::mouseMoveEvent(QMouseEvent *e) {
	if (_lastGlobalPos != e->globalPos() || _mouseSelection) {
		_lastGlobalPos = e->globalPos();
		updateSelected(e->pos());
	}
}

void EditorBlock::updateSelected(QPoint localPosition) {
	_mouseSelection = true;
	auto top = localPosition.y();
	auto underMouseIndex = -1;
	enumerateRowsFrom(top, [&underMouseIndex, top](int index, const Row &row) {
		if (row.top() <= top) {
			underMouseIndex = index;
		}
		return false;
	});
	setSelected(underMouseIndex);
}

void EditorBlock::leaveEventHook(QEvent *e) {
	_mouseSelection = false;
	setSelected(-1);
}

void EditorBlock::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto clip = e->rect();
	if (_data.empty()) {
		p.fillRect(clip, st::dialogsBg);
		p.setFont(st::noContactsFont);
		p.setPen(st::noContactsColor);
		p.drawText(QRect(0, 0, width(), st::noContactsHeight), tr::lng_theme_editor_no_keys(tr::now));
	}

	auto cliptop = clip.y();
	auto clipbottom = cliptop + clip.height();
	enumerateRowsFrom(cliptop, [&](int index, const Row &row) {
		if (row.top() >= clipbottom) {
			return false;
		}
		paintRow(p, index, row);
		return true;
	});
}

void EditorBlock::paintRow(Painter &p, int index, const Row &row) {
	auto rowTop = row.top() + st::themeEditorMargin.top();

	auto rect = QRect(0, row.top(), width(), row.height());
	auto selected = (_pressed >= 0) ? (index == _pressed) : (index == _selected);
	auto active = (findRowIndex(&row) == _editing);
	p.fillRect(rect, active ? st::dialogsBgActive : selected ? st::dialogsBgOver : st::dialogsBg);
	if (auto ripple = row.ripple()) {
		ripple->paint(p, 0, row.top(), width(), &(active ? st::activeButtonBgRipple : st::windowBgRipple)->c);
		if (ripple->empty()) {
			row.resetRipple();
		}
	}

	auto sample = QRect(width() - st::themeEditorMargin.right() - st::themeEditorSampleSize.width(), rowTop, st::themeEditorSampleSize.width(), st::themeEditorSampleSize.height());
	Ui::Shadow::paint(p, sample, width(), st::defaultRoundShadow);
	if (row.value().alpha() != 255) {
		p.fillRect(myrtlrect(sample), _transparent);
	}
	p.fillRect(myrtlrect(sample), row.value());

	auto rowWidth = width() - st::themeEditorMargin.left() - st::themeEditorMargin.right();
	auto nameWidth = rowWidth - st::themeEditorSampleSize.width() - st::themeEditorDescriptionSkip;

	p.setFont(st::themeEditorNameFont);
	p.setPen(active ? st::dialogsNameFgActive : selected ? st::dialogsNameFgOver : st::dialogsNameFg);
	p.drawTextLeft(st::themeEditorMargin.left(), rowTop, width(), st::themeEditorNameFont->elided(row.name(), nameWidth));

	if (!row.copyOf().isEmpty()) {
		auto copyTop = rowTop + st::themeEditorNameFont->height;
		p.setFont(st::themeEditorCopyNameFont);
		p.drawTextLeft(st::themeEditorMargin.left(), copyTop, width(), st::themeEditorCopyNameFont->elided("= " + row.copyOf(), nameWidth));
	}

	if (!row.descriptionText().isEmpty()) {
		auto descriptionTop = rowTop + st::themeEditorSampleSize.height() + st::themeEditorDescriptionSkip;
		p.setPen(active ? st::dialogsTextFgActive : selected ? st::dialogsTextFgOver : st::dialogsTextFg);
		row.descriptionText().drawLeft(p, st::themeEditorMargin.left(), descriptionTop, rowWidth, width());
	}

	if (isEditing() && !active && (_type == Type::New || (_editing >= 0 && findRowIndex(&row) >= _editing))) {
		p.fillRect(rect, st::layerBg);
	}
}

void EditorBlock::setSelected(int selected) {
	if (isEditing()) {
		if (_type == Type::New) {
			selected = -1;
		} else if (_editing >= 0 && selected >= 0 && findRowIndex(&rowAtIndex(selected)) >= _editing) {
			selected = -1;
		}
	}
	if (_selected != selected) {
		if (_selected >= 0) updateRow(rowAtIndex(_selected));
		_selected = selected;
		if (_selected >= 0) updateRow(rowAtIndex(_selected));
		setCursor((_selected >= 0) ? style::cur_pointer : style::cur_default);
	}
}

void EditorBlock::setPressed(int pressed) {
	if (_pressed != pressed) {
		if (_pressed >= 0) {
			updateRow(rowAtIndex(_pressed));
			stopLastRipple(_pressed);
		}
		_pressed = pressed;
		if (_pressed >= 0) {
			addRowRipple(_pressed);
			updateRow(rowAtIndex(_pressed));
		}
	}
}

void EditorBlock::addRowRipple(int index) {
	auto &row = rowAtIndex(index);
	auto ripple = row.ripple();
	if (!ripple) {
		auto mask = Ui::RippleAnimation::RectMask(QSize(width(), row.height()));
		ripple = row.setRipple(std::make_unique<Ui::RippleAnimation>(st::defaultRippleAnimation, std::move(mask), [this, index = findRowIndex(&row)] {
			updateRow(_data[index]);
		}));
	}
	auto origin = mapFromGlobal(QCursor::pos()) - QPoint(0, row.top());
	ripple->add(origin);
}

void EditorBlock::stopLastRipple(int index) {
	auto &row = rowAtIndex(index);
	if (row.ripple()) {
		row.ripple()->lastStop();
	}
}

void EditorBlock::updateRow(const Row &row) {
	update(0, row.top(), width(), row.height());
}

void EditorBlock::addRow(const QString &name, const QString &copyOf, QColor value) {
	_data.push_back({ name, copyOf, value });
	_indices.insert(name, _data.size() - 1);
	addToSearch(_data.back());
}

EditorBlock::Row &EditorBlock::rowAtIndex(int index) {
	if (isSearch()) {
		return _data[_searchResults[index]];
	}
	return _data[index];
}

int EditorBlock::findRowIndex(const QString &name) const {
	return _indices.value(name, -1);
}

EditorBlock::Row *EditorBlock::findRow(const QString &name) {
	auto index = findRowIndex(name);
	return (index >= 0) ? &_data[index] : nullptr;
}

int EditorBlock::findRowIndex(const Row *row) {
	return row ? (row - &_data[0]) : -1;
}

} // namespace Theme
} // namespace Window
