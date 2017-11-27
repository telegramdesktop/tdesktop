/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "window/themes/window_theme_editor.h"

#include "window/themes/window_theme.h"
#include "window/themes/window_theme_editor_block.h"
#include "mainwindow.h"
#include "storage/localstorage.h"
#include "boxes/confirm_box.h"
#include "styles/style_window.h"
#include "styles/style_settings.h"
#include "styles/style_dialogs.h"
#include "styles/style_boxes.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/multi_select.h"
#include "base/parse_helper.h"
#include "base/task_queue.h"
#include "base/zlib_help.h"
#include "ui/toast/toast.h"
#include "core/file_utilities.h"
#include "boxes/edit_color_box.h"
#include "lang/lang_keys.h"

namespace Window {
namespace Theme {
namespace {

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

QByteArray replaceValueInContent(const QByteArray &content, const QByteArray &name, const QByteArray &value) {
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
	return QByteArray();
}

QString bytesToUtf8(QLatin1String bytes) {
	return QString::fromUtf8(bytes.data(), bytes.size());
}

} // namespace

class Editor::Inner : public TWidget, private base::Subscriber {
public:
	Inner(QWidget *parent, const QString &path);

	void setErrorCallback(base::lambda<void()> callback) {
		_errorCallback = std::move(callback);
	}
	void setFocusCallback(base::lambda<void()> callback) {
		_focusCallback = std::move(callback);
	}
	void setScrollCallback(base::lambda<void(int top, int bottom)> callback) {
		_scrollCallback = std::move(callback);
	}

	void prepare();

	base::lambda<void()> exportCallback();

	void filterRows(const QString &query);
	void chooseRow();

	void selectSkip(int direction);
	void selectSkipPage(int delta, int direction);

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

	EditorBlock::Context _context;

	QString _path;
	QByteArray _paletteContent;
	base::lambda<void()> _errorCallback;
	base::lambda<void()> _focusCallback;
	base::lambda<void(int top, int bottom)> _scrollCallback;

	object_ptr<EditorBlock> _existingRows;
	object_ptr<EditorBlock> _newRows;

	bool _applyingUpdate = false;

};

class ThemeExportBox : public BoxContent {
public:
	ThemeExportBox(QWidget*, const QByteArray &paletteContent, const QImage &background, const QByteArray &backgroundContent, bool tileBackground);

protected:
	void prepare() override;

	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void updateThumbnail();
	void chooseBackgroundFromFile();
	void exportTheme();

	QByteArray _paletteContent;

	QImage _background;
	QByteArray _backgroundContent;
	bool _isPng = false;
	QString _imageText;
	QPixmap _thumbnail;

	object_ptr<Ui::LinkButton> _chooseFromFile;
	object_ptr<Ui::Checkbox> _tileBackground;

};

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
		if (_applyingUpdate) return;

		if (update.type == BackgroundUpdate::Type::TestingTheme) {
			Revert();
			App::CallDelayed(st::slideDuration, this, [] {
				Ui::show(Box<InformBox>(lang(lng_theme_editor_cant_change_theme)));
			});
		}
	});
}

void Editor::Inner::prepare() {
	if (!readData()) {
		error();
	}
}

base::lambda<void()> Editor::Inner::exportCallback() {
	return App::LambdaDelayed(st::defaultRippleAnimation.hideDuration, this, [this] {
		auto background = Background()->pixmap().toImage();
		auto backgroundContent = QByteArray();
		auto tiled = Background()->tile();
		{
			QBuffer buffer(&backgroundContent);
			background.save(&buffer, "JPG", 87);
		}
		Ui::show(Box<ThemeExportBox>(_paletteContent, background, backgroundContent, tiled));
	});
}

void Editor::Inner::filterRows(const QString &query) {
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

	p.setFont(st::settingsFixedBarFont);
	p.setPen(st::windowFg);
	if (!_newRows->isHidden()) {
		p.drawTextLeft(st::themeEditorMargin.left(), _existingRows->y() + _existingRows->height() + st::settingsFixedBarTextPosition.y(), width(), lang(lng_theme_editor_new_keys));
	}
}

int Editor::Inner::resizeGetHeight(int newWidth) {
	auto rowsWidth = newWidth;
	_existingRows->resizeToWidth(rowsWidth);
	_newRows->resizeToWidth(rowsWidth);

	_existingRows->moveToLeft(0, 0);
	_newRows->moveToLeft(0, _existingRows->height() + st::settingsFixedBarHeight);

	auto lowest = (_newRows->isHidden() ? _existingRows : _newRows).data();

	return lowest->y() + lowest->height();
}

bool Editor::Inner::readData() {
	if (!readExistingRows()) {
		return false;
	}

	auto rows = style::main_palette::data();
	for_const (auto &row, rows) {
		auto name = bytesToUtf8(row.name);
		auto description = bytesToUtf8(row.description);
		if (!_existingRows->feedDescription(name, description)) {
			if (row.value.data()[0] == '#') {
				auto result = readColor(name, row.value.data() + 1, row.value.size() - 1);
				Assert(!result.error);
				_newRows->feed(name, result.color);
				//if (!_newRows->feedFallbackName(name, str_const_toString(row.fallback))) {
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

bool Editor::Inner::readExistingRows() {
	QFile f(_path);
	if (!f.open(QIODevice::ReadOnly)) {
		LOG(("Theme Error: could not open color palette file '%1'").arg(_path));
		return false;
	}

	_paletteContent = f.readAll();
	if (f.error() != QFileDevice::NoError) {
		LOG(("Theme Error: could not read content from palette file '%1'").arg(_path));
		return false;
	}
	f.close();

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

QString colorString(QColor color) {
	auto result = QString();
	result.reserve(9);
	result.append('#');
	auto addHex = [&result](int code) {
		if (code >= 0 && code < 10) {
			result.append('0' + code);
		} else if (code >= 10 && code < 16) {
			result.append('a' + (code - 10));
		}
	};
	auto addValue = [addHex](int code) {
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

void Editor::Inner::applyEditing(const QString &name, const QString &copyOf, QColor value) {
	auto plainName = name.toLatin1();
	auto plainValue = (copyOf.isEmpty() ? colorString(value) : copyOf).toLatin1();
	auto newContent = replaceValueInContent(_paletteContent, plainName, plainValue);
	if (newContent == "error") {
		LOG(("Theme Error: could not replace '%1: %2' in content").arg(name).arg(copyOf.isEmpty() ? colorString(value) : copyOf));
		error();
		return;
	}
	if (newContent.isEmpty()) {
		auto newline = (_paletteContent.indexOf("\r\n") >= 0 ? "\r\n" : "\n");
		auto addedline = (_paletteContent.endsWith('\n') ? "" : newline);
		newContent = _paletteContent + addedline + plainName + ": " + plainValue + ";" + newline;
	}
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
	if (!ApplyEditedPalette(_path, newContent)) {
		LOG(("Theme Error: could not apply newly composed content :("));
		error();
		return;
	}
	_applyingUpdate = false;

	_paletteContent = newContent;
}

void writeDefaultPalette(const QString &path) {
	QFile f(path);
	if (!f.open(QIODevice::WriteOnly)) {
		LOG(("Theme Error: could not open '%1' for writing.").arg(path));
		return;
	}

	QTextStream stream(&f);
	stream.setCodec("UTF-8");

	auto rows = style::main_palette::data();
	for_const (auto &row, rows) {
		stream << bytesToUtf8(row.name) << ": " << bytesToUtf8(row.value) << "; // " << bytesToUtf8(row.description).replace('\n', ' ').replace('\r', ' ') << "\n";
	}
}

ThemeExportBox::ThemeExportBox(QWidget*, const QByteArray &paletteContent, const QImage &background, const QByteArray &backgroundContent, bool tileBackground) : BoxContent()
, _paletteContent(paletteContent)
, _background(background)
, _backgroundContent(backgroundContent)
, _chooseFromFile(this, lang(lng_settings_bg_from_file), st::boxLinkButton)
, _tileBackground(this, lang(lng_settings_bg_tile), tileBackground, st::defaultBoxCheckbox) {
	_imageText = lng_theme_editor_saved_to_jpg(lt_size, formatSizeText(_backgroundContent.size()));
	_chooseFromFile->setClickedCallback([this] { chooseBackgroundFromFile(); });
}

void ThemeExportBox::prepare() {
	setTitle(langFactory(lng_theme_editor_background_image));

	addButton(langFactory(lng_theme_editor_export), [this] { exportTheme(); });
	addButton(langFactory(lng_cancel), [this] { closeBox(); });

	auto height = st::settingsSmallSkip + st::settingsBackgroundSize + st::settingsSmallSkip + _tileBackground->height();

	setDimensions(st::boxWideWidth, height);

	updateThumbnail();
}

void ThemeExportBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);

	auto linkLeft = st::boxPadding.left() + st::settingsBackgroundSize + st::settingsSmallSkip;

	p.setPen(st::boxTextFg);
	p.setFont(st::boxTextFont);
	p.drawTextLeft(linkLeft, st::settingsSmallSkip, width(), _imageText);

	p.drawPixmapLeft(st::boxPadding.left(), st::settingsSmallSkip, width(), _thumbnail);
}

void ThemeExportBox::resizeEvent(QResizeEvent *e) {
	auto linkLeft = st::boxPadding.left() + st::settingsBackgroundSize + st::settingsSmallSkip;
	_chooseFromFile->moveToLeft(linkLeft, st::settingsSmallSkip + st::boxTextFont->height + st::settingsSmallSkip);
	_tileBackground->moveToLeft(st::boxPadding.left(), st::settingsSmallSkip + st::settingsBackgroundSize + 2 * st::settingsSmallSkip);
}

void ThemeExportBox::updateThumbnail() {
	int32 size = st::settingsBackgroundSize * cIntRetinaFactor();
	QImage back(size, size, QImage::Format_ARGB32_Premultiplied);
	back.setDevicePixelRatio(cRetinaFactor());
	{
		Painter p(&back);
		PainterHighQualityEnabler hq(p);

		auto &pix = _background;
		int sx = (pix.width() > pix.height()) ? ((pix.width() - pix.height()) / 2) : 0;
		int sy = (pix.height() > pix.width()) ? ((pix.height() - pix.width()) / 2) : 0;
		int s = (pix.width() > pix.height()) ? pix.height() : pix.width();
		p.drawImage(QRect(0, 0, st::settingsBackgroundSize, st::settingsBackgroundSize), pix, QRect(sx, sy, s, s));
	}
	Images::prepareRound(back, ImageRoundRadius::Small);
	_thumbnail = App::pixmapFromImageInPlace(std::move(back));
	_thumbnail.setDevicePixelRatio(cRetinaFactor());
	update();
}

void ThemeExportBox::chooseBackgroundFromFile() {
	FileDialog::GetOpenPath(lang(lng_theme_editor_choose_image), "Image files (*.jpeg *.jpg *.png)", base::lambda_guarded(this, [this](const FileDialog::OpenResult &result) {
		auto content = result.remoteContent;
		if (!result.paths.isEmpty()) {
			QFile f(result.paths.front());
			if (f.open(QIODevice::ReadOnly)) {
				content = f.readAll();
				f.close();
			}
		}
		if (!content.isEmpty()) {
			auto format = QByteArray();
			auto image = App::readImage(content, &format);
			if (!image.isNull() && (format == "jpeg" || format == "jpg" || format == "png")) {
				_background = image;
				_backgroundContent = content;
				_isPng = (format == "png");
				auto sizeText = formatSizeText(_backgroundContent.size());
				_imageText = _isPng ? lng_theme_editor_read_from_png(lt_size, sizeText) : lng_theme_editor_read_from_jpg(lt_size, sizeText);
				_tileBackground->setChecked(false);
				updateThumbnail();
			}
		}
	}));
}

void ThemeExportBox::exportTheme() {
	App::CallDelayed(st::defaultRippleAnimation.hideDuration, this, [this] {
		auto caption = lang(lng_theme_editor_choose_name);
		auto filter = "Themes (*.tdesktop-theme)";
		auto name = "awesome.tdesktop-theme";
		FileDialog::GetWritePath(caption, filter, name, base::lambda_guarded(this, [this](const QString &path) {
			zlib::FileToWrite zip;

			zip_fileinfo zfi = { { 0, 0, 0, 0, 0, 0 }, 0, 0, 0 };
			auto background = std::string(_tileBackground->checked() ? "tiled" : "background") + (_isPng ? ".png" : ".jpg");
			zip.openNewFile(background.c_str(), &zfi, nullptr, 0, nullptr, 0, nullptr, Z_DEFLATED, Z_DEFAULT_COMPRESSION);
			zip.writeInFile(_backgroundContent.constData(), _backgroundContent.size());
			zip.closeFile();
			auto scheme = "colors.tdesktop-theme";
			zip.openNewFile(scheme, &zfi, nullptr, 0, nullptr, 0, nullptr, Z_DEFLATED, Z_DEFAULT_COMPRESSION);
			zip.writeInFile(_paletteContent.constData(), _paletteContent.size());
			zip.closeFile();
			zip.close();

			if (zip.error() != ZIP_OK) {
				LOG(("Theme Error: could not export zip-ed theme, status: %1").arg(zip.error()));
				Ui::show(Box<InformBox>(lang(lng_theme_editor_error)));
				return;
			}
			auto result = zip.result();

			QFile f(path);
			if (!f.open(QIODevice::WriteOnly)) {
				LOG(("Theme Error: could not open zip-ed theme file '%1' for writing").arg(path));
				Ui::show(Box<InformBox>(lang(lng_theme_editor_error)));
				return;
			}
			if (f.write(result) != result.size()) {
				LOG(("Theme Error: could not write zip-ed theme to file '%1'").arg(path));
				Ui::show(Box<InformBox>(lang(lng_theme_editor_error)));
				return;
			}
			Ui::hideLayer();
			Ui::Toast::Show(lang(lng_theme_editor_done));
		}));
	});
}

Editor::Editor(QWidget*, const QString &path)
: _scroll(this, st::settingsScroll)
, _close(this, st::contactsMultiSelect.fieldCancel)
, _select(this, st::contactsMultiSelect, langFactory(lng_country_ph))
, _leftShadow(this)
, _topShadow(this)
, _export(this, lang(lng_theme_editor_export_button).toUpper(), st::dialogsUpdateButton) {
	_inner = _scroll->setOwnedWidget(object_ptr<Inner>(this, path));

	_export->setClickedCallback(_inner->exportCallback());

	_inner->setErrorCallback([this] {
		Ui::show(Box<InformBox>(lang(lng_theme_editor_error)));

		// This could be from inner->_context observable notification.
		// We should not destroy it while iterating in subscribers.
		base::TaskQueue::Main().Put(base::lambda_guarded(this, [this] { closeEditor(); }));
	});
	_inner->setFocusCallback([this] {
		App::CallDelayed(2 * st::boxDuration, this, [this] { _select->setInnerFocus(); });
	});
	_inner->setScrollCallback([this](int top, int bottom) {
		_scroll->scrollToY(top, bottom);
	});
	_close->setClickedCallback([this] { closeEditor(); });
	_close->show(anim::type::instant);

	_select->resizeToWidth(st::windowMinWidth);
	_select->setQueryChangedCallback([this](const QString &query) { _inner->filterRows(query); _scroll->scrollToY(0); });
	_select->setSubmittedCallback([this](bool) { _inner->chooseRow(); });

	_inner->prepare();
	resizeToWidth(st::windowMinWidth);
}

void Editor::resizeEvent(QResizeEvent *e) {
	_export->resizeToWidth(width());
	_close->moveToRight(0, 0);

	_select->resizeToWidth(width());
	_select->moveToLeft(0, _close->height());

	auto shadowTop = _select->y() + _select->height();

	_topShadow->resize(width() - st::lineWidth, st::lineWidth);
	_topShadow->moveToLeft(st::lineWidth, shadowTop);
	_leftShadow->resize(st::lineWidth, height());
	_leftShadow->moveToLeft(0, 0);
	auto scrollSize = QSize(width(), height() - shadowTop - _export->height());
	if (_scroll->size() != scrollSize) {
		_scroll->resize(scrollSize);
	}
	_inner->resizeToWidth(width());
	_scroll->moveToLeft(0, shadowTop);
	if (!_scroll->isHidden()) {
		auto scrollTop = _scroll->scrollTop();
		_inner->setVisibleTopBottom(scrollTop, scrollTop + _scroll->height());
	}
	_export->moveToLeft(0, _scroll->y() + _scroll->height());
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

	p.setFont(st::settingsFixedBarFont);
	p.setPen(st::windowFg);
	p.drawTextLeft(st::themeEditorMargin.left(), st::themeEditorMargin.top(), width(), lang(lng_theme_editor_title));
}

void Editor::Start() {
	auto palettePath = Local::themePaletteAbsolutePath();
	if (palettePath.isEmpty()) {
		FileDialog::GetWritePath(lang(lng_theme_editor_save_palette), "Palette (*.tdesktop-palette)", "colors.tdesktop-palette", [](const QString &path) {
			if (!Local::copyThemeColorsToPalette(path)) {
				writeDefaultPalette(path);
			}
			if (!Apply(path)) {
				Ui::show(Box<InformBox>(lang(lng_theme_editor_error)));
				return;
			}
			KeepApplied();
			if (auto window = App::wnd()) {
				window->showRightColumn(Box<Editor>(path));
			}
		});
	} else if (auto window = App::wnd()) {
		window->showRightColumn(Box<Editor>(palettePath));
	}
}

void Editor::closeEditor() {
	if (auto window = App::wnd()) {
		window->showRightColumn(nullptr);
	}
}

} // namespace Theme
} // namespace Window
