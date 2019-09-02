/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/themes/window_theme_create_box.h"

#include "window/themes/window_theme.h"
#include "window/themes/window_theme_editor.h"
#include "boxes/generic_box.h"
#include "boxes/confirm_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "info/profile/info_profile_button.h"
#include "main/main_session.h"
#include "core/file_utilities.h"
#include "lang/lang_keys.h"
#include "storage/localstorage.h"
#include "mainwindow.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"
#include "styles/style_boxes.h"

namespace Window {
namespace Theme {
namespace {

void ImportFromFile(
		not_null<Main::Session*> session,
		not_null<QWidget*> parent) {
	const auto &imgExtensions = cImgExtensions();
	auto filters = QStringList(
		qsl("Theme files (*.tdesktop-theme *.tdesktop-palette)"));
	filters.push_back(FileDialog::AllFilesFilter());
	const auto callback = crl::guard(session, [=](
			const FileDialog::OpenResult &result) {
		if (result.paths.isEmpty()) {
			return;
		}
		Window::Theme::Apply(result.paths.front());
	});
	FileDialog::GetOpenPath(
		parent.get(),
		tr::lng_choose_image(tr::now),
		filters.join(qsl(";;")),
		crl::guard(parent, callback));
}

QString BytesToUTF8(QLatin1String string) {
	return QString::fromUtf8(string.data(), string.size());
}

void WriteDefaultPalette(const QString &path) {
	QFile f(path);
	if (!f.open(QIODevice::WriteOnly)) {
		LOG(("Theme Error: could not open '%1' for writing.").arg(path));
		return;
	}

	QTextStream stream(&f);
	stream.setCodec("UTF-8");

	auto rows = style::main_palette::data();
	for (const auto &row : std::as_const(rows)) {
		stream
			<< BytesToUTF8(row.name)
			<< ": "
			<< BytesToUTF8(row.value)
			<< "; // "
			<< BytesToUTF8(
				row.description
			).replace(
				'\n',
				' '
			).replace(
				'\r',
				' ')
			<< "\n";
	}
}

void StartEditor(
		not_null<Main::Session*> session,
		const QString &title) {
	const auto path = EditingPalettePath();
	if (!Local::copyThemeColorsToPalette(path)) {
		WriteDefaultPalette(path);
	}
	if (!Apply(path)) {
		Ui::show(Box<InformBox>(tr::lng_theme_editor_error(tr::now)));
		return;
	}
	KeepApplied();
	if (const auto window = App::wnd()) {
		window->showRightColumn(Box<Editor>());
	}
}

} // namespace

void CreateBox(not_null<GenericBox*> box, not_null<Main::Session*> session) {
	box->setTitle(tr::lng_theme_editor_create_title(Ui::Text::WithEntities));

	const auto name = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		tr::lng_theme_editor_name()));

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_theme_editor_create_description(),
			st::boxDividerLabel),
		style::margins(
			st::boxRowPadding.left(),
			st::boxRowPadding.left(),
			st::boxRowPadding.right(),
			st::boxRowPadding.right()));

	box->addRow(
		object_ptr<Info::Profile::Button>(
			box,
			tr::lng_theme_editor_import_existing() | Ui::Text::ToUpper(),
			st::createThemeImportButton),
		style::margins()
	)->addClickHandler([=] {
		ImportFromFile(session, box);
	});

	box->setFocusCallback([=] { name->setFocusFast(); });

	box->addButton(tr::lng_box_done(), [=] {
		const auto title = name->getLastText().trimmed();
		if (title.isEmpty()) {
			name->showError();
			return;
		}
		box->closeBox();
		StartEditor(session, title);
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

} // namespace Theme
} // namespace Window
