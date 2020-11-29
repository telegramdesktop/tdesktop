/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/themes/window_theme_editor.h"

#include "window/themes/window_theme.h"
#include "window/themes/window_theme_editor_block.h"
#include "window/themes/window_theme_editor_box.h"
#include "window/themes/window_themes_embedded.h"
#include "window/window_controller.h"
#include "main/main_account.h"
#include "mainwindow.h"
#include "storage/localstorage.h"
#include "boxes/confirm_box.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/multi_select.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/toast/toast.h"
#include "ui/ui_utility.h"
#include "base/parse_helper.h"
#include "base/zlib_help.h"
#include "base/call_delayed.h"
#include "core/file_utilities.h"
#include "core/application.h"
#include "boxes/edit_color_box.h"
#include "lang/lang_keys.h"
#include "facades.h"
#include "app.h"
#include "styles/style_window.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

namespace Window {
namespace Theme {
namespace {

template <size_t Size>
QByteArray qba(const char(&string)[Size]) {
	return QByteArray::fromRawData(string, Size - 1);
}

const auto kCloudInTextStart = qba("// THEME EDITOR SERVICE INFO START\n");
const auto kCloudInTextEnd = qba("// THEME EDITOR SERVICE INFO END\n\n");

struct ReadColorResult {
	ReadColorResult(QColor color, bool error = false) : color(color), error(error) {
	}
	QColor color;
	bool error = false;
};

ReadColorResult colorError(const QString &name) {
	return { QColor(), true };
}

ReadColorResult readColor(const QString &name, const char *data, int size) {
	if (size != 6 && size != 8) {
		return colorError(name);
	}
	auto readHex = [](char ch) {
		if (ch >= '0' && ch <= '9') {
			return (ch - '0');
		} else if (ch >= 'a' && ch <= 'f') {
			return (ch - 'a' + 10);
		} else if (ch >= 'A' && ch <= 'F') {
			return (ch - 'A' + 10);
		}
		return -1;
	};
	auto readValue = [readHex](const char *data) {
		auto high = readHex(data[0]);
		auto low = readHex(data[1]);
		return (high >= 0 && low >= 0) ? (high * 0x10 + low) : -1;
	};
	auto r = readValue(data);
	auto g = readValue(data + 2);
	auto b = readValue(data + 4);
	auto a = (size == 8) ? readValue(data + 6) : 255;
	if (r < 0 || g < 0 || b < 0 || a < 0) {
		return colorError(name);
	}
	return { QColor(r, g, b, a) };
}

bool skipComment(const char *&data, const char *end) {
	if (data == end) return false;
	if (*data == '/' && data + 1 != end) {
		if (*(data + 1) == '/') {
			data += 2;
			while (data != end && *data != '\n') {
				++data;
			}
			return true;
		} else if (*(data + 1) == '*') {
			data += 2;
			while (true) {
				while (data != end && *data != '*') {
					++data;
				}
				if (data != end) {
					++data;
					if (data != end && *data == '/') {
						++data;
						break;
					}
				}
				if (data == end) {
					break;
				}
			}
			return true;
		}
	}
	return false;
}

void skipWhitespacesAndComments(const char *&data, const char *end) {
	while (data != end) {
		if (!base::parse::skipWhitespaces(data, end)) return;
		if (!skipComment(data, end)) return;
	}
}

QLatin1String readValue(const char *&data, const char *end) {
	auto start = data;
	if (data != end && *data == '#') {
		++data;
	}
	base::parse::readName(data, end);
	return QLatin1String(start, data - start);
}

bool isValidColorValue(QLatin1String value) {
	auto isValidHexChar = [](char ch) {
		return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f');
	};
	auto data = value.data();
	auto size = value.size();
	if ((size != 7 && size != 9) || data[0] != '#') {
		return false;
	}
	for (auto i = 1; i != size; ++i) {
		if (!isValidHexChar(data[i])) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] QByteArray ColorizeInContent(
		QByteArray content,
		const Colorizer &colorizer) {
	auto validNames = OrderedSet<QLatin1String>();
	content.detach();
	auto start = content.constBegin(), data = start, end = data + content.size();
	while (data != end) {
		skipWhitespacesAndComments(data, end);
		if (data == end) break;

		auto foundName = base::parse::readName(data, end);
		skipWhitespacesAndComments(data, end);
		if (data == end || *data != ':') {
			return "error";
		}
		++data;
		skipWhitespacesAndComments(data, end);
		auto valueStart = data;
		auto value = readValue(data, end);
		auto valueEnd = data;
		if (value.size() == 0) {
			return "error";
		}
		if (isValidColorValue(value)) {
			const auto colorized = Colorize(value, colorizer);
			Assert(colorized.size() == value.size());
			memcpy(
				content.data() + (data - start) - value.size(),
				colorized.data(),
				value.size());
		}
		skipWhitespacesAndComments(data, end);
		if (data == end || *data != ';') {
			return "error";
		}
		++data;
	}
	return content;
}

QString bytesToUtf8(QLatin1String bytes) {
	return QString::fromUtf8(bytes.data(), bytes.size());
}

} // namespace

class Editor::Inner : public TWidget, private base::Subscriber {
public:
	Inner(QWidget *parent, const QString &path);

	void setErrorCallback(Fn<void()> callback) {
		_errorCallback = std::move(callback);
	}
	void setFocusCallback(Fn<void()> callback) {
		_focusCallback = std::move(callback);
	}
	void setScrollCallback(Fn<void(int top, int bottom)> callback) {
		_scrollCallback = std::move(callback);
	}

	void prepare();
	[[nodiscard]] QByteArray paletteContent() const {
		return _paletteContent;
	}

	void filterRows(const QString &query);
	void chooseRow();

	void selectSkip(int direction);
	void selectSkipPage(int delta, int direction);

	void applyNewPalette(const QByteArray &newContent);
	void recreateRows();

	~Inner() {
		if (_context.box) _context.box->closeBox();
	}

protected:
	void paintEvent(QPaintEvent *e) override;
	int resizeGetHeight(int newWidth) override;

private:
	bool readData();
	bool readExistingRows();
	bool feedExistingRow(const QString &name, QLatin1String value);

	void error() {
		if (_errorCallback) {
			_errorCallback();
		}
	}
	void applyEditing(const QString &name, const QString &copyOf, QColor value);

	void sortByAccentDistance();

	EditorBlock::Context _context;

	QString _path;
	QByteArray _paletteContent;
	Fn<void()> _errorCallback;
	Fn<void()> _focusCallback;
	Fn<void(int top, int bottom)> _scrollCallback;

	object_ptr<EditorBlock> _existingRows;
	object_ptr<EditorBlock> _newRows;

	bool _applyingUpdate = false;

};

QByteArray ColorHexString(const QColor &color) {
	auto result = QByteArray();
	result.reserve(9);
	result.append('#');
	const auto addHex = [&](int code) {
		if (code >= 0 && code < 10) {
			result.append('0' + code);
		} else if (code >= 10 && code < 16) {
			result.append('a' + (code - 10));
		}
	};
	const auto addValue = [&](int code) {
		addHex(code / 16);
		addHex(code % 16);
	};
	addValue(color.red());
	addValue(color.green());
	addValue(color.blue());
	if (color.alpha() != 255) {
		addValue(color.alpha());
	}
	return result;
}

QByteArray ReplaceValueInPaletteContent(
		const QByteArray &content,
		const QByteArray &name,
		const QByteArray &value) {
	auto validNames = OrderedSet<QLatin1String>();
	auto start = content.constBegin(), data = start, end = data + content.size();
	auto lastValidValueStart = end, lastValidValueEnd = end;
	while (data != end) {
		skipWhitespacesAndComments(data, end);
		if (data == end) break;

		auto foundName = base::parse::readName(data, end);
		skipWhitespacesAndComments(data, end);
		if (data == end || *data != ':') {
			return "error";
		}
		++data;
		skipWhitespacesAndComments(data, end);
		auto valueStart = data;
		auto value = readValue(data, end);
		auto valueEnd = data;
		if (value.size() == 0) {
			return "error";
		}
		auto validValue = validNames.contains(value) || isValidColorValue(value);
		if (validValue) {
			validNames.insert(foundName);
			if (foundName == name) {
				lastValidValueStart = valueStart;
				lastValidValueEnd = valueEnd;
			}
		}
		skipWhitespacesAndComments(data, end);
		if (data == end || *data != ';') {
			return "error";
		}
		++data;
	}
	if (lastValidValueStart != end) {
		auto result = QByteArray();
		result.reserve((lastValidValueStart - start) + value.size() + (end - lastValidValueEnd));
		result.append(start, lastValidValueStart - start);
		result.append(value);
		if (end - lastValidValueEnd > 0) result.append(lastValidValueEnd, end - lastValidValueEnd);
		return result;
	}
	auto newline = (content.indexOf("\r\n") >= 0 ? "\r\n" : "\n");
	auto addedline = (content.endsWith('\n') ? "" : newline);
	return content + addedline + name + ": " + value + ";" + newline;
}

[[nodiscard]] QByteArray WriteCloudToText(const Data::CloudTheme &cloud) {
	auto result = QByteArray();
	const auto add = [&](const QByteArray &key, const QString &value) {
		result.append("// " + key + ": " + value.toLatin1() + "\n");
	};
	result.append(kCloudInTextStart);
	add("ID", QString::number(cloud.id));
	add("ACCESS", QString::number(cloud.accessHash));
	result.append(kCloudInTextEnd);
	return result;
}

[[nodiscard]] Data::CloudTheme ReadCloudFromText(const QByteArray &text) {
	const auto index = text.indexOf(kCloudInTextEnd);
	if (index <= 1) {
		return Data::CloudTheme();
	}
	auto result = Data::CloudTheme();
	const auto list = text.mid(0, index - 1).split('\n');
	const auto take = [&](uint64 &value, int index) {
		if (list.size() <= index) {
			return false;
		}
		const auto &entry = list[index];
		const auto position = entry.indexOf(": ");
		if (position < 0) {
			return false;
		}
		value = QString::fromLatin1(entry.mid(position + 2)).toULongLong();
		return true;
	};
	if (!take(result.id, 1) || !take(result.accessHash, 2)) {
		return Data::CloudTheme();
	}
	return result;
}

QByteArray StripCloudTextFields(const QByteArray &text) {
	const auto firstValue = text.indexOf(": #");
	auto start = 0;
	while (true) {
		const auto index = text.indexOf(kCloudInTextEnd, start);
		if (index < 0 || index > firstValue) {
			break;
		}
		start = index + kCloudInTextEnd.size();
	}
	return (start > 0) ? text.mid(start) : text;
}

Editor::Inner::Inner(QWidget *parent, const QString &path) : TWidget(parent)
, _path(path)
, _existingRows(this, EditorBlock::Type::Existing, &_context)
, _newRows(this, EditorBlock::Type::New, &_context) {
	resize(st::windowMinWidth, st::windowMinHeight);
	subscribe(_context.resized, [this] {
		resizeToWidth(width());
	});
	subscribe(_context.pending, [this](const EditorBlock::Context::EditionData &data) {
		applyEditing(data.name, data.copyOf, data.value);
	});
	subscribe(_context.updated, [this] {
		if (_context.name.isEmpty() && _focusCallback) {
			_focusCallback();
		}
	});
	subscribe(_context.scroll, [this](const EditorBlock::Context::ScrollData &data) {
		if (_scrollCallback) {
			auto top = (data.type == EditorBlock::Type::Existing ? _existingRows : _newRows)->y();
			top += data.position;
			_scrollCallback(top, top + data.height);
		}
	});
	subscribe(Background(), [this](const BackgroundUpdate &update) {
		if (_applyingUpdate || !Background()->editingTheme()) {
			return;
		}

		if (update.type == BackgroundUpdate::Type::TestingTheme) {
			Revert();
			base::call_delayed(st::slideDuration, this, [] {
				Ui::show(Box<InformBox>(
					tr::lng_theme_editor_cant_change_theme(tr::now)));
			});
		}
	});
}

void Editor::Inner::recreateRows() {
	_existingRows.create(this, EditorBlock::Type::Existing, &_context);
	_existingRows->show();
	_newRows.create(this, EditorBlock::Type::New, &_context);
	_newRows->show();
	if (!readData()) {
		error();
	}
}

void Editor::Inner::prepare() {
	QFile f(_path);
	if (!f.open(QIODevice::ReadOnly)) {
		LOG(("Theme Error: could not open color palette file '%1'").arg(_path));
		error();
		return;
	}

	_paletteContent = f.readAll();
	if (f.error() != QFileDevice::NoError) {
		LOG(("Theme Error: could not read content from palette file '%1'").arg(_path));
		error();
		return;
	}
	f.close();

	if (!readData()) {
		error();
	}
}

void Editor::Inner::filterRows(const QString &query) {
	if (query == ":sort-for-accent") {
		sortByAccentDistance();
		filterRows(QString());
		return;
	}
	_existingRows->filterRows(query);
	_newRows->filterRows(query);
}

void Editor::Inner::chooseRow() {
	if (!_existingRows->hasSelected() && !_newRows->hasSelected()) {
		selectSkip(1);
	}
	if (_existingRows->hasSelected()) {
		_existingRows->chooseRow();
	} else if (_newRows->hasSelected()) {
		_newRows->chooseRow();
	}
}

// Block::selectSkip(-1) removes the selection if it can't select anything
// Block::selectSkip(1) leaves the selection if it can't select anything
void Editor::Inner::selectSkip(int direction) {
	if (direction > 0) {
		if (_newRows->hasSelected()) {
			_existingRows->clearSelected();
			_newRows->selectSkip(direction);
		} else if (_existingRows->hasSelected()) {
			if (!_existingRows->selectSkip(direction)) {
				if (_newRows->selectSkip(direction)) {
					_existingRows->clearSelected();
				}
			}
		} else {
			if (!_existingRows->selectSkip(direction)) {
				_newRows->selectSkip(direction);
			}
		}
	} else {
		if (_existingRows->hasSelected()) {
			_newRows->clearSelected();
			_existingRows->selectSkip(direction);
		} else if (_newRows->hasSelected()) {
			if (!_newRows->selectSkip(direction)) {
				_existingRows->selectSkip(direction);
			}
		}
	}
}

void Editor::Inner::selectSkipPage(int delta, int direction) {
	auto defaultRowHeight = st::themeEditorMargin.top()
		+ st::themeEditorSampleSize.height()
		+ st::themeEditorDescriptionSkip
		+ st::defaultTextStyle.font->height
		+ st::themeEditorMargin.bottom();
	for (auto i = 0, count = ceilclamp(delta, defaultRowHeight, 1, delta); i != count; ++i) {
		selectSkip(direction);
	}
}

void Editor::Inner::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.setFont(st::boxTitleFont);
	p.setPen(st::windowFg);
	if (!_newRows->isHidden()) {
		p.drawTextLeft(st::themeEditorMargin.left(), _existingRows->y() + _existingRows->height() + st::boxTitlePosition.y(), width(), tr::lng_theme_editor_new_keys(tr::now));
	}
}

int Editor::Inner::resizeGetHeight(int newWidth) {
	auto rowsWidth = newWidth;
	_existingRows->resizeToWidth(rowsWidth);
	_newRows->resizeToWidth(rowsWidth);

	_existingRows->moveToLeft(0, 0);
	_newRows->moveToLeft(0, _existingRows->height() + st::boxTitleHeight);

	auto lowest = (_newRows->isHidden() ? _existingRows : _newRows).data();

	return lowest->y() + lowest->height();
}

bool Editor::Inner::readData() {
	if (!readExistingRows()) {
		return false;
	}

	const auto rows = style::main_palette::data();
	for (const auto &row : rows) {
		auto name = bytesToUtf8(row.name);
		auto description = bytesToUtf8(row.description);
		if (!_existingRows->feedDescription(name, description)) {
			if (row.value.data()[0] == '#') {
				auto result = readColor(name, row.value.data() + 1, row.value.size() - 1);
				Assert(!result.error);
				_newRows->feed(name, result.color);
				//if (!_newRows->feedFallbackName(name, row.fallback.utf16())) {
				//	Unexpected("Row for fallback not found");
				//}
			} else {
				auto copyOf = bytesToUtf8(row.value);
				if (auto result = _existingRows->find(copyOf)) {
					_newRows->feed(name, *result, copyOf);
				} else if (!_newRows->feedCopy(name, copyOf)) {
					Unexpected("Copy of unknown value in the default palette");
				}
				Assert(row.fallback.size() == 0);
			}
			if (!_newRows->feedDescription(name, description)) {
				Unexpected("Row for description not found");
			}
		}
	}

	return true;
}

void Editor::Inner::sortByAccentDistance() {
	const auto accent = *_existingRows->find("windowBgActive");
	_existingRows->sortByDistance(accent);
	_newRows->sortByDistance(accent);
}

bool Editor::Inner::readExistingRows() {
	return ReadPaletteValues(_paletteContent, [this](QLatin1String name, QLatin1String value) {
		return feedExistingRow(name, value);
	});
}

bool Editor::Inner::feedExistingRow(const QString &name, QLatin1String value) {
	auto data = value.data();
	auto size = value.size();
	if (data[0] != '#') {
		return _existingRows->feedCopy(name, QString(value));
	}
	auto result = readColor(name, data + 1, size - 1);
	if (result.error) {
		LOG(("Theme Warning: Skipping value '%1: %2' (expected a color value in #rrggbb or #rrggbbaa or a previously defined key in the color scheme)").arg(name).arg(value));
	} else {
		_existingRows->feed(name, result.color);
	}
	return true;
}

void Editor::Inner::applyEditing(const QString &name, const QString &copyOf, QColor value) {
	auto plainName = name.toLatin1();
	auto plainValue = copyOf.isEmpty() ? ColorHexString(value) : copyOf.toLatin1();
	auto newContent = ReplaceValueInPaletteContent(_paletteContent, plainName, plainValue);
	if (newContent == "error") {
		LOG(("Theme Error: could not replace '%1: %2' in content").arg(name).arg(copyOf.isEmpty() ? QString::fromLatin1(ColorHexString(value)) : copyOf));
		error();
		return;
	}
	applyNewPalette(newContent);
}

void Editor::Inner::applyNewPalette(const QByteArray &newContent) {
	QFile f(_path);
	if (!f.open(QIODevice::WriteOnly)) {
		LOG(("Theme Error: could not open '%1' for writing a palette update.").arg(_path));
		error();
		return;
	}
	if (f.write(newContent) != newContent.size()) {
		LOG(("Theme Error: could not write all content to '%1' while writing a palette update.").arg(_path));
		error();
		return;
	}
	f.close();

	_applyingUpdate = true;
	if (!ApplyEditedPalette(newContent)) {
		LOG(("Theme Error: could not apply newly composed content :("));
		error();
		return;
	}
	_applyingUpdate = false;

	_paletteContent = newContent;
}

Editor::Editor(
	QWidget*,
	not_null<Window::Controller*> window,
	const Data::CloudTheme &cloud)
: _window(window)
, _cloud(cloud)
, _scroll(this, st::themesScroll)
, _close(this, st::defaultMultiSelect.fieldCancel)
, _menuToggle(this, st::themesMenuToggle)
, _select(this, st::defaultMultiSelect, tr::lng_country_ph())
, _leftShadow(this)
, _topShadow(this)
, _save(this, tr::lng_theme_editor_save_button(tr::now).toUpper(), st::dialogsUpdateButton) {
	const auto path = EditingPalettePath();

	_inner = _scroll->setOwnedWidget(object_ptr<Inner>(this, path));

	_save->setClickedCallback(App::LambdaDelayed(
		st::defaultRippleAnimation.hideDuration,
		this,
		[=] { save(); }));

	_inner->setErrorCallback([this] {
		Ui::show(Box<InformBox>(tr::lng_theme_editor_error(tr::now)));

		// This could be from inner->_context observable notification.
		// We should not destroy it while iterating in subscribers.
		crl::on_main(this, [=] {
			closeEditor();
		});
	});
	_inner->setFocusCallback([this] {
		base::call_delayed(2 * st::boxDuration, this, [this] {
			_select->setInnerFocus();
		});
	});
	_inner->setScrollCallback([this](int top, int bottom) {
		_scroll->scrollToY(top, bottom);
	});
	_menuToggle->setClickedCallback([=] {
		showMenu();
	});
	_close->setClickedCallback([=] {
		closeWithConfirmation();
	});
	_close->show(anim::type::instant);

	_select->resizeToWidth(st::windowMinWidth);
	_select->setQueryChangedCallback([this](const QString &query) { _inner->filterRows(query); _scroll->scrollToY(0); });
	_select->setSubmittedCallback([this](Qt::KeyboardModifiers) { _inner->chooseRow(); });

	_inner->prepare();
	resizeToWidth(st::windowMinWidth);
}

void Editor::showMenu() {
	if (_menu) {
		return;
	}
	_menu = base::make_unique_q<Ui::DropdownMenu>(this);
	_menu->setHiddenCallback([weak = Ui::MakeWeak(this), menu = _menu.get()]{
		menu->deleteLater();
		if (weak && weak->_menu == menu) {
			weak->_menu = nullptr;
			weak->_menuToggle->setForceRippled(false);
		}
	});
	_menu->setShowStartCallback(crl::guard(this, [this, menu = _menu.get()]{
		if (_menu == menu) {
			_menuToggle->setForceRippled(true);
		}
	}));
	_menu->setHideStartCallback(crl::guard(this, [this, menu = _menu.get()]{
		if (_menu == menu) {
			_menuToggle->setForceRippled(false);
		}
	}));

	_menuToggle->installEventFilter(_menu);
	_menu->addAction(tr::lng_theme_editor_menu_export(tr::now), [=] {
		base::call_delayed(st::defaultRippleAnimation.hideDuration, this, [=] {
			exportTheme();
		});
	});
	_menu->addAction(tr::lng_theme_editor_menu_import(tr::now), [=] {
		base::call_delayed(st::defaultRippleAnimation.hideDuration, this, [=] {
			importTheme();
		});
	});
	_menu->addAction(tr::lng_theme_editor_menu_show(tr::now), [=] {
		File::ShowInFolder(EditingPalettePath());
	});
	_menu->moveToRight(st::themesMenuPosition.x(), st::themesMenuPosition.y());
	_menu->showAnimated(Ui::PanelAnimation::Origin::TopRight);
}

void Editor::exportTheme() {
	auto caption = tr::lng_theme_editor_choose_name(tr::now);
	auto filter = "Themes (*.tdesktop-theme)";
	auto name = "awesome.tdesktop-theme";
	FileDialog::GetWritePath(this, caption, filter, name, crl::guard(this, [=](const QString &path) {
		const auto result = CollectForExport(_inner->paletteContent());
		QFile f(path);
		if (!f.open(QIODevice::WriteOnly)) {
			LOG(("Theme Error: could not open zip-ed theme file '%1' for writing").arg(path));
			Ui::show(Box<InformBox>(tr::lng_theme_editor_error(tr::now)));
			return;
		}
		if (f.write(result) != result.size()) {
			LOG(("Theme Error: could not write zip-ed theme to file '%1'").arg(path));
			Ui::show(Box<InformBox>(tr::lng_theme_editor_error(tr::now)));
			return;
		}
		Ui::Toast::Show(tr::lng_theme_editor_done(tr::now));
	}));
}

void Editor::importTheme() {
	auto filters = QStringList(
		qsl("Theme files (*.tdesktop-theme *.tdesktop-palette)"));
	filters.push_back(FileDialog::AllFilesFilter());
	const auto callback = crl::guard(this, [=](
		const FileDialog::OpenResult &result) {
		const auto path = result.paths.isEmpty()
			? QString()
			: result.paths.front();
		if (path.isEmpty()) {
			return;
		}
		auto f = QFile(path);
		if (!f.open(QIODevice::ReadOnly)) {
			return;
		}
		auto object = Object();
		object.pathAbsolute = QFileInfo(path).absoluteFilePath();
		object.pathRelative = QDir().relativeFilePath(path);
		object.content = f.readAll();
		if (object.content.isEmpty()) {
			return;
		}
		_select->clearQuery();
		const auto parsed = ParseTheme(object, false, false);
		_inner->applyNewPalette(parsed.palette);
		_inner->recreateRows();
		updateControlsGeometry();
		auto image = App::readImage(parsed.background);
		if (!image.isNull() && !image.size().isEmpty()) {
			Background()->set(Data::CustomWallPaper(), std::move(image));
			Background()->setTile(parsed.tiled);
			Ui::ForceFullRepaint(_window->widget());
		}
	});
	FileDialog::GetOpenPath(
		this,
		tr::lng_theme_editor_menu_import(tr::now),
		filters.join(qsl(";;")),
		crl::guard(this, callback));
}

QByteArray Editor::ColorizeInContent(
		QByteArray content,
		const Colorizer &colorizer) {
	return Window::Theme::ColorizeInContent(content, colorizer);
}

void Editor::save() {
	if (Core::App().passcodeLocked()) {
		Ui::Toast::Show(tr::lng_theme_editor_need_unlock(tr::now));
		return;
	} else if (!_window->account().sessionExists()) {
		Ui::Toast::Show(tr::lng_theme_editor_need_auth(tr::now));
		return;
	} else if (_saving) {
		return;
	}
	_saving = true;
	const auto unlock = crl::guard(this, [=] { _saving = false; });
	SaveTheme(_window, _cloud, _inner->paletteContent(), unlock);
}

void Editor::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void Editor::updateControlsGeometry() {
	_save->resizeToWidth(width());
	_close->moveToRight(0, 0);
	_menuToggle->moveToRight(_close->width(), 0);

	_select->resizeToWidth(width());
	_select->moveToLeft(0, _close->height());

	auto shadowTop = _select->y() + _select->height();

	_topShadow->resize(width() - st::lineWidth, st::lineWidth);
	_topShadow->moveToLeft(st::lineWidth, shadowTop);
	_leftShadow->resize(st::lineWidth, height());
	_leftShadow->moveToLeft(0, 0);
	auto scrollSize = QSize(width(), height() - shadowTop - _save->height());
	if (_scroll->size() != scrollSize) {
		_scroll->resize(scrollSize);
	}
	_inner->resizeToWidth(width());
	_scroll->moveToLeft(0, shadowTop);
	if (!_scroll->isHidden()) {
		auto scrollTop = _scroll->scrollTop();
		_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
	}
	_save->moveToLeft(0, _scroll->y() + _scroll->height());
}

void Editor::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		if (!_select->getQuery().isEmpty()) {
			_select->clearQuery();
		} else if (auto window = App::wnd()) {
			window->setInnerFocus();
		}
	} else if (e->key() == Qt::Key_Down) {
		_inner->selectSkip(1);
	} else if (e->key() == Qt::Key_Up) {
		_inner->selectSkip(-1);
	} else if (e->key() == Qt::Key_PageDown) {
		_inner->selectSkipPage(_scroll->height(), 1);
	} else if (e->key() == Qt::Key_PageUp) {
		_inner->selectSkipPage(_scroll->height(), -1);
	}
}

void Editor::focusInEvent(QFocusEvent *e) {
	_select->setInnerFocus();
}

void Editor::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.fillRect(e->rect(), st::dialogsBg);

	p.setFont(st::boxTitleFont);
	p.setPen(st::windowFg);
	p.drawTextLeft(st::themeEditorMargin.left(), st::themeEditorMargin.top(), width(), tr::lng_theme_editor_title(tr::now));
}

void Editor::closeWithConfirmation() {
	if (!PaletteChanged(_inner->paletteContent(), _cloud)) {
		Background()->clearEditingTheme(ClearEditing::KeepChanges);
		closeEditor();
		return;
	}
	const auto close = crl::guard(this, [=](Fn<void()> &&close) {
		Background()->clearEditingTheme(ClearEditing::RevertChanges);
		closeEditor();
		close();
	});
	_window->show(Box<ConfirmBox>(
		tr::lng_theme_editor_sure_close(tr::now),
		tr::lng_close(tr::now),
		close));
}

void Editor::closeEditor() {
	if (const auto window = App::wnd()) {
		window->showRightColumn(nullptr);
		Background()->clearEditingTheme();
	}
}

} // namespace Theme
} // namespace Window
