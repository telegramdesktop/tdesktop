/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/linux_gtk_file_dialog.h"

#include "platform/platform_file_utilities.h"
#include "platform/linux/linux_gtk_integration_p.h"
#include "platform/linux/linux_gdk_helper.h"
#include "platform/linux/linux_desktop_environment.h"
#include "lang/lang_keys.h"
#include "storage/localstorage.h"
#include "base/qt_adapters.h"

#include <QtGui/QWindow>
#include <QtWidgets/QFileDialog>
#include <private/qguiapplication_p.h>

namespace Platform {
namespace FileDialog {
namespace Gtk {

using namespace Platform::Gtk;

namespace {

// GTK file chooser image preview: thanks to Chromium

// The size of the preview we display for selected image files. We set height
// larger than width because generally there is more free space vertically
// than horiztonally (setting the preview image will alway expand the width of
// the dialog, but usually not the height). The image's aspect ratio will always
// be preserved.
constexpr auto kPreviewWidth = 256;
constexpr auto kPreviewHeight = 512;

const char *filterRegExp =
"^(.*)\\(([a-zA-Z0-9_.,*? +;#\\-\\[\\]@\\{\\}/!<>\\$%&=^~:\\|]*)\\)$";

QStringList makeFilterList(const QString &filter) {
	QString f(filter);

	if (f.isEmpty())
		return QStringList();

	QString sep(QLatin1String(";;"));
	int i = f.indexOf(sep, 0);
	if (i == -1) {
		if (f.indexOf(QLatin1Char('\n'), 0) != -1) {
			sep = QLatin1Char('\n');
			i = f.indexOf(sep, 0);
		}
	}

	return f.split(sep);
}

// Makes a list of filters from a normal filter string "Image Files (*.png *.jpg)"
QStringList cleanFilterList(const QString &filter) {
	QRegExp regexp(QString::fromLatin1(filterRegExp));
	Assert(regexp.isValid());
	QString f = filter;
	int i = regexp.indexIn(f);
	if (i >= 0)
		f = regexp.cap(2);
	return f.split(QLatin1Char(' '), base::QStringSkipEmptyParts);
}

bool Supported() {
	return internal::GdkHelperLoaded()
		&& (gtk_widget_hide_on_delete != nullptr)
		&& (gtk_clipboard_store != nullptr)
		&& (gtk_clipboard_get != nullptr)
		&& (gtk_widget_destroy != nullptr)
		&& (gtk_dialog_get_type != nullptr)
		&& (gtk_dialog_run != nullptr)
		&& (gtk_widget_realize != nullptr)
		&& (gdk_window_set_modal_hint != nullptr)
		&& (gtk_widget_show != nullptr)
		&& (gdk_window_focus != nullptr)
		&& (gtk_widget_hide != nullptr)
		&& (gtk_widget_hide_on_delete != nullptr)
		&& (gtk_file_chooser_dialog_new != nullptr)
		&& (gtk_file_chooser_get_type != nullptr)
		&& (gtk_file_chooser_set_current_folder != nullptr)
		&& (gtk_file_chooser_get_current_folder != nullptr)
		&& (gtk_file_chooser_set_current_name != nullptr)
		&& (gtk_file_chooser_select_filename != nullptr)
		&& (gtk_file_chooser_get_filenames != nullptr)
		&& (gtk_file_chooser_set_filter != nullptr)
		&& (gtk_file_chooser_get_filter != nullptr)
		&& (gtk_window_get_type != nullptr)
		&& (gtk_window_set_title != nullptr)
		&& (gtk_file_chooser_set_local_only != nullptr)
		&& (gtk_file_chooser_set_action != nullptr)
		&& (gtk_file_chooser_set_select_multiple != nullptr)
		&& (gtk_file_chooser_set_do_overwrite_confirmation != nullptr)
		&& (gtk_file_chooser_remove_filter != nullptr)
		&& (gtk_file_filter_set_name != nullptr)
		&& (gtk_file_filter_add_pattern != nullptr)
		&& (gtk_file_chooser_add_filter != nullptr)
		&& (gtk_file_filter_new != nullptr);
}

bool PreviewSupported() {
	return (gdk_pixbuf_new_from_file_at_size != nullptr);
}

bool CustomButtonsSupported() {
	return (gtk_dialog_get_widget_for_response != nullptr)
		&& (gtk_button_set_label != nullptr)
		&& (gtk_button_get_type != nullptr);
}

// This is a patched copy of qgtk2 theme plugin.
// We need to use our own gtk file dialog instead of
// styling Qt file dialog, because Qt only works with gtk2.
// We need to be able to work with gtk2 and gtk3, because
// we use gtk3 to work with appindicator3.
class QGtkDialog : public QWindow {
public:
	QGtkDialog(GtkWidget *gtkWidget);
	~QGtkDialog();

	GtkDialog *gtkDialog() const;

	void exec();
	void show(Qt::WindowFlags flags, Qt::WindowModality modality, QWindow *parent);
	void hide();

	rpl::producer<> accept();
	rpl::producer<> reject();

protected:
	static void onResponse(QGtkDialog *dialog, int response);
	static void onUpdatePreview(QGtkDialog *dialog);

private:
	void onParentWindowDestroyed();

	GtkWidget *gtkWidget = nullptr;
	GtkWidget *_preview = nullptr;

	rpl::event_stream<> _accept;
	rpl::event_stream<> _reject;

	bool _destroyedConnected = false;

};

class GtkFileDialog : public QDialog {
public:
	GtkFileDialog(
		QWidget *parent = nullptr,
		const QString &caption = QString(),
		const QString &directory = QString(),
		const QString &filter = QString());
	~GtkFileDialog();

	void setVisible(bool visible) override;

	void setWindowTitle(const QString &windowTitle) {
		_windowTitle = windowTitle;
	}
	void setAcceptMode(QFileDialog::AcceptMode acceptMode) {
		_acceptMode = acceptMode;
	}
	void setFileMode(QFileDialog::FileMode fileMode) {
		_fileMode = fileMode;
	}
	void setOption(QFileDialog::Option option, bool on = true) {
		if (on) {
			_options |= option;
		} else {
			_options &= ~option;
		}
	}

	int exec() override;

	bool defaultNameFilterDisables() const;
	void setDirectory(const QString &directory);
	QDir directory() const;
	void selectFile(const QString &filename);
	QStringList selectedFiles() const;
	void setFilter();
	void selectNameFilter(const QString &filter);
	QString selectedNameFilter() const;

private:
	static void onSelectionChanged(GtkDialog *dialog, GtkFileDialog *helper);
	static void onCurrentFolderChanged(GtkFileDialog *helper);
	void applyOptions();
	void setNameFilters(const QStringList &filters);

	void showHelper(Qt::WindowFlags flags, Qt::WindowModality modality, QWindow *parent);
	void hideHelper();

	void onAccepted();
	void onRejected();

	// Options
	QFileDialog::Options _options;
	QString _windowTitle = "Choose file";
	QString _initialDirectory;
	QStringList _initialFiles;
	QStringList _nameFilters;
	QFileDialog::AcceptMode _acceptMode = QFileDialog::AcceptOpen;
	QFileDialog::FileMode _fileMode = QFileDialog::ExistingFile;

	QString _dir;
	QStringList _selection;
	QHash<QString, GtkFileFilter*> _filters;
	QHash<GtkFileFilter*, QString> _filterNames;
	QScopedPointer<QGtkDialog> d;

	rpl::lifetime _lifetime;
};

QGtkDialog::QGtkDialog(GtkWidget *gtkWidget) : gtkWidget(gtkWidget) {
	g_signal_connect_swapped(G_OBJECT(gtkWidget), "response", G_CALLBACK(onResponse), this);
	g_signal_connect(G_OBJECT(gtkWidget), "delete-event", G_CALLBACK(gtk_widget_hide_on_delete), nullptr);
	if (PreviewSupported()) {
		_preview = gtk_image_new();
		g_signal_connect_swapped(G_OBJECT(gtkWidget), "update-preview", G_CALLBACK(onUpdatePreview), this);
		gtk_file_chooser_set_preview_widget(gtk_file_chooser_cast(gtkWidget), _preview);
	}
}

QGtkDialog::~QGtkDialog() {
	gtk_clipboard_store(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
	gtk_widget_destroy(gtkWidget);
}

GtkDialog *QGtkDialog::gtkDialog() const {
	return gtk_dialog_cast(gtkWidget);
}

void QGtkDialog::exec() {
	if (modality() == Qt::ApplicationModal) {
		// block input to the whole app, including other GTK dialogs
		gtk_dialog_run(gtkDialog());
	} else {
		// block input to the window, allow input to other GTK dialogs
		QEventLoop loop;
		rpl::lifetime lifetime;

		accept(
		) | rpl::start_with_next([&] {
			loop.quit();
		}, lifetime);

		reject(
		) | rpl::start_with_next([&] {
			loop.quit();
		}, lifetime);

		loop.exec();
	}
}

void QGtkDialog::show(Qt::WindowFlags flags, Qt::WindowModality modality, QWindow *parent) {
	if (!std::exchange(_destroyedConnected, true)) {
		connect(parent, &QWindow::destroyed, this, [=] { onParentWindowDestroyed(); });
	}
	setParent(parent);
	setFlags(flags);
	setModality(modality);

	gtk_widget_realize(gtkWidget); // creates X window

	if (parent) {
		internal::XSetTransientForHint(gtk_widget_get_window(gtkWidget), parent->winId());
	}

	if (modality != Qt::NonModal) {
		gdk_window_set_modal_hint(gtk_widget_get_window(gtkWidget), true);
		QGuiApplicationPrivate::showModalWindow(this);
	}

	gtk_widget_show(gtkWidget);
	gdk_window_focus(gtk_widget_get_window(gtkWidget), 0);
}

void QGtkDialog::hide() {
	QGuiApplicationPrivate::hideModalWindow(this);
	gtk_widget_hide(gtkWidget);
}

rpl::producer<> QGtkDialog::accept() {
	return _accept.events();
}

rpl::producer<> QGtkDialog::reject() {
	return _reject.events();
}

void QGtkDialog::onResponse(QGtkDialog *dialog, int response) {
	if (response == GTK_RESPONSE_OK)
		dialog->_accept.fire({});
	else
		dialog->_reject.fire({});
}

void QGtkDialog::onUpdatePreview(QGtkDialog* dialog) {
	auto filename = gtk_file_chooser_get_preview_filename(gtk_file_chooser_cast(dialog->gtkWidget));
	if (!filename) {
		gtk_file_chooser_set_preview_widget_active(gtk_file_chooser_cast(dialog->gtkWidget), false);
		return;
	}

	// Don't attempt to open anything which isn't a regular file. If a named pipe,
	// this may hang. See https://crbug.com/534754.
	struct stat stat_buf;
	if (stat(filename, &stat_buf) != 0 || !S_ISREG(stat_buf.st_mode)) {
		g_free(filename);
		gtk_file_chooser_set_preview_widget_active(gtk_file_chooser_cast(dialog->gtkWidget), false);
		return;
	}

	// This will preserve the image's aspect ratio.
	auto pixbuf = gdk_pixbuf_new_from_file_at_size(filename, kPreviewWidth, kPreviewHeight, nullptr);
	g_free(filename);
	if (pixbuf) {
		gtk_image_set_from_pixbuf(gtk_image_cast(dialog->_preview), pixbuf);
		g_object_unref(pixbuf);
	}
	gtk_file_chooser_set_preview_widget_active(gtk_file_chooser_cast(dialog->gtkWidget), pixbuf ? true : false);
}

void QGtkDialog::onParentWindowDestroyed() {
	// The Gtk*DialogHelper classes own this object. Make sure the parent doesn't delete it.
	setParent(nullptr);
}

GtkFileDialog::GtkFileDialog(QWidget *parent, const QString &caption, const QString &directory, const QString &filter) : QDialog(parent)
, _windowTitle(caption)
, _initialDirectory(directory) {
	auto filters = makeFilterList(filter);
	const int numFilters = filters.count();
	_nameFilters.reserve(numFilters);
	for (int i = 0; i < numFilters; ++i) {
		_nameFilters << filters[i].simplified();
	}

	d.reset(new QGtkDialog(gtk_file_chooser_dialog_new("", nullptr,
		GTK_FILE_CHOOSER_ACTION_OPEN,
		tr::lng_cancel(tr::now).toUtf8().constData(), GTK_RESPONSE_CANCEL,
		tr::lng_box_ok(tr::now).toUtf8().constData(), GTK_RESPONSE_OK, nullptr)));

	d.data()->accept(
	) | rpl::start_with_next([=] {
		onAccepted();
	}, _lifetime);

	d.data()->reject(
	) | rpl::start_with_next([=] {
		onRejected();
	}, _lifetime);

	g_signal_connect(gtk_file_chooser_cast(d->gtkDialog()), "selection-changed", G_CALLBACK(onSelectionChanged), this);
	g_signal_connect_swapped(gtk_file_chooser_cast(d->gtkDialog()), "current-folder-changed", G_CALLBACK(onCurrentFolderChanged), this);
}

GtkFileDialog::~GtkFileDialog() {
}

void GtkFileDialog::showHelper(Qt::WindowFlags flags, Qt::WindowModality modality, QWindow *parent) {
	_dir.clear();
	_selection.clear();

	applyOptions();
	return d->show(flags, modality, parent);
}

void GtkFileDialog::setVisible(bool visible) {
	if (visible) {
		if (testAttribute(Qt::WA_WState_ExplicitShowHide) && !testAttribute(Qt::WA_WState_Hidden)) {
			return;
		}
	} else if (testAttribute(Qt::WA_WState_ExplicitShowHide) && testAttribute(Qt::WA_WState_Hidden)) {
		return;
	}

	if (visible) {
		showHelper(windowFlags(), windowModality(), parentWidget() ? parentWidget()->windowHandle() : nullptr);
	} else {
		hideHelper();
	}

	// Set WA_DontShowOnScreen so that QDialog::setVisible(visible) below
	// updates the state correctly, but skips showing the non-native version:
	setAttribute(Qt::WA_DontShowOnScreen);

	QDialog::setVisible(visible);
}

int GtkFileDialog::exec() {
	d->setModality(windowModality());

	bool deleteOnClose = testAttribute(Qt::WA_DeleteOnClose);
	setAttribute(Qt::WA_DeleteOnClose, false);

	bool wasShowModal = testAttribute(Qt::WA_ShowModal);
	setAttribute(Qt::WA_ShowModal, true);
	setResult(0);

	show();

	QPointer<QDialog> guard = this;
	d->exec();
	if (guard.isNull())
		return QDialog::Rejected;

	setAttribute(Qt::WA_ShowModal, wasShowModal);

	return result();
}

void GtkFileDialog::hideHelper() {
	// After GtkFileChooserDialog has been hidden, gtk_file_chooser_get_current_folder()
	// & gtk_file_chooser_get_filenames() will return bogus values -> cache the actual
	// values before hiding the dialog
	_dir = directory().absolutePath();
	_selection = selectedFiles();

	d->hide();
}

bool GtkFileDialog::defaultNameFilterDisables() const {
	return false;
}

void GtkFileDialog::setDirectory(const QString &directory) {
	GtkDialog *gtkDialog = d->gtkDialog();
	gtk_file_chooser_set_current_folder(gtk_file_chooser_cast(gtkDialog), directory.toUtf8().constData());
}

QDir GtkFileDialog::directory() const {
	// While GtkFileChooserDialog is hidden, gtk_file_chooser_get_current_folder()
	// returns a bogus value -> return the cached value before hiding
	if (!_dir.isEmpty())
		return _dir;

	QString ret;
	GtkDialog *gtkDialog = d->gtkDialog();
	gchar *folder = gtk_file_chooser_get_current_folder(gtk_file_chooser_cast(gtkDialog));
	if (folder) {
		ret = QString::fromUtf8(folder);
		g_free(folder);
	}
	return QDir(ret);
}

void GtkFileDialog::selectFile(const QString &filename) {
	_initialFiles.clear();
	_initialFiles.append(filename);
}

QStringList GtkFileDialog::selectedFiles() const {
	// While GtkFileChooserDialog is hidden, gtk_file_chooser_get_filenames()
	// returns a bogus value -> return the cached value before hiding
	if (!_selection.isEmpty())
		return _selection;

	QStringList selection;
	GtkDialog *gtkDialog = d->gtkDialog();
	GSList *filenames = gtk_file_chooser_get_filenames(gtk_file_chooser_cast(gtkDialog));
	for (GSList *it  = filenames; it; it = it->next)
		selection += QString::fromUtf8((const char*)it->data);
	g_slist_free(filenames);
	return selection;
}

void GtkFileDialog::setFilter() {
	applyOptions();
}

void GtkFileDialog::selectNameFilter(const QString &filter) {
	GtkFileFilter *gtkFilter = _filters.value(filter);
	if (gtkFilter) {
		GtkDialog *gtkDialog = d->gtkDialog();
		gtk_file_chooser_set_filter(gtk_file_chooser_cast(gtkDialog), gtkFilter);
	}
}

QString GtkFileDialog::selectedNameFilter() const {
	GtkDialog *gtkDialog = d->gtkDialog();
	GtkFileFilter *gtkFilter = gtk_file_chooser_get_filter(gtk_file_chooser_cast(gtkDialog));
	return _filterNames.value(gtkFilter);
}

void GtkFileDialog::onAccepted() {
	accept();

//	QString filter = selectedNameFilter();
//	if (filter.isEmpty())
//		emit filterSelected(filter);

//	QList<QUrl> files = selectedFiles();
//	emit filesSelected(files);
//	if (files.count() == 1)
//		emit fileSelected(files.first());
}

void GtkFileDialog::onRejected() {
	reject();

//
}

void GtkFileDialog::onSelectionChanged(GtkDialog *gtkDialog, GtkFileDialog *helper) {
//	QString selection;
//	gchar *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(gtkDialog));
//	if (filename) {
//		selection = QString::fromUtf8(filename);
//		g_free(filename);
//	}
//	emit helper->currentChanged(QUrl::fromLocalFile(selection));
}

void GtkFileDialog::onCurrentFolderChanged(GtkFileDialog *dialog) {
//	emit dialog->directoryEntered(dialog->directory());
}

GtkFileChooserAction gtkFileChooserAction(QFileDialog::FileMode fileMode, QFileDialog::AcceptMode acceptMode) {
	switch (fileMode) {
	case QFileDialog::AnyFile:
	case QFileDialog::ExistingFile:
	case QFileDialog::ExistingFiles:
		if (acceptMode == QFileDialog::AcceptOpen)
			return GTK_FILE_CHOOSER_ACTION_OPEN;
		else
			return GTK_FILE_CHOOSER_ACTION_SAVE;
	case QFileDialog::Directory:
	default:
		if (acceptMode == QFileDialog::AcceptOpen)
			return GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER;
		else
			return GTK_FILE_CHOOSER_ACTION_CREATE_FOLDER;
	}
}

void GtkFileDialog::applyOptions() {
	GtkDialog *gtkDialog = d->gtkDialog();

	gtk_window_set_title(gtk_window_cast(gtkDialog), _windowTitle.toUtf8().constData());
	gtk_file_chooser_set_local_only(gtk_file_chooser_cast(gtkDialog), true);

	const GtkFileChooserAction action = gtkFileChooserAction(_fileMode, _acceptMode);
	gtk_file_chooser_set_action(gtk_file_chooser_cast(gtkDialog), action);

	const bool selectMultiple = (_fileMode == QFileDialog::ExistingFiles);
	gtk_file_chooser_set_select_multiple(gtk_file_chooser_cast(gtkDialog), selectMultiple);

	const bool confirmOverwrite = !_options.testFlag(QFileDialog::DontConfirmOverwrite);
	gtk_file_chooser_set_do_overwrite_confirmation(gtk_file_chooser_cast(gtkDialog), confirmOverwrite);

	if (!_nameFilters.isEmpty())
		setNameFilters(_nameFilters);

	if (!_initialDirectory.isEmpty())
		setDirectory(_initialDirectory);

	for_const (const auto &filename, _initialFiles) {
		if (_acceptMode == QFileDialog::AcceptSave) {
			QFileInfo fi(filename);
			gtk_file_chooser_set_current_folder(gtk_file_chooser_cast(gtkDialog), fi.path().toUtf8().constData());
			gtk_file_chooser_set_current_name(gtk_file_chooser_cast(gtkDialog), fi.fileName().toUtf8().constData());
		} else if (filename.endsWith('/')) {
			gtk_file_chooser_set_current_folder(gtk_file_chooser_cast(gtkDialog), filename.toUtf8().constData());
		} else {
			gtk_file_chooser_select_filename(gtk_file_chooser_cast(gtkDialog), filename.toUtf8().constData());
		}
	}

	const QString initialNameFilter = _nameFilters.isEmpty() ? QString() : _nameFilters.front();
	if (!initialNameFilter.isEmpty())
		selectNameFilter(initialNameFilter);

	if (CustomButtonsSupported()) {
		GtkWidget *acceptButton = gtk_dialog_get_widget_for_response(gtkDialog, GTK_RESPONSE_OK);
		if (acceptButton) {
			/*if (opts->isLabelExplicitlySet(QFileDialogOptions::Accept))
				gtk_button_set_label(gtk_button_cast(acceptButton), opts->labelText(QFileDialogOptions::Accept).toUtf8().constData());
			else*/ if (_acceptMode == QFileDialog::AcceptOpen)
				gtk_button_set_label(gtk_button_cast(acceptButton), tr::lng_open_link(tr::now).toUtf8().constData());
			else
				gtk_button_set_label(gtk_button_cast(acceptButton), tr::lng_settings_save(tr::now).toUtf8().constData());
		}

		GtkWidget *rejectButton = gtk_dialog_get_widget_for_response(gtkDialog, GTK_RESPONSE_CANCEL);
		if (rejectButton) {
			/*if (opts->isLabelExplicitlySet(QFileDialogOptions::Reject))
				gtk_button_set_label(gtk_button_cast(rejectButton), opts->labelText(QFileDialogOptions::Reject).toUtf8().constData());
			else*/
				gtk_button_set_label(gtk_button_cast(rejectButton), tr::lng_cancel(tr::now).toUtf8().constData());
		}
	}
}

void GtkFileDialog::setNameFilters(const QStringList &filters) {
	GtkDialog *gtkDialog = d->gtkDialog();
	Q_FOREACH (GtkFileFilter *filter, _filters)
		gtk_file_chooser_remove_filter(gtk_file_chooser_cast(gtkDialog), filter);

	_filters.clear();
	_filterNames.clear();

	for_const (auto &filter, filters) {
		GtkFileFilter *gtkFilter = gtk_file_filter_new();
		auto name = filter;//.left(filter.indexOf(QLatin1Char('(')));
		auto extensions = cleanFilterList(filter);

		gtk_file_filter_set_name(gtkFilter, name.isEmpty() ? extensions.join(QStringLiteral(", ")).toUtf8().constData() : name.toUtf8().constData());
		for_const (auto &ext, extensions) {
			auto caseInsensitiveExt = QString();
			caseInsensitiveExt.reserve(4 * ext.size());
			for_const (auto ch, ext) {
				auto chLower = ch.toLower();
				auto chUpper = ch.toUpper();
				if (chLower != chUpper) {
					caseInsensitiveExt.append('[').append(chLower).append(chUpper).append(']');
				} else {
					caseInsensitiveExt.append(ch);
				}
			}

			gtk_file_filter_add_pattern(gtkFilter, caseInsensitiveExt.toUtf8().constData());
		}

		gtk_file_chooser_add_filter(gtk_file_chooser_cast(gtkDialog), gtkFilter);

		_filters.insert(filter, gtkFilter);
		_filterNames.insert(gtkFilter, filter);
	}
}

} // namespace

bool Use(Type type) {
	if (!Supported()) {
		return false;
	}

	return qEnvironmentVariableIsSet("TDESKTOP_USE_GTK_FILE_DIALOG")
		|| DesktopEnvironment::IsGtkBased();
}

bool Get(
		QPointer<QWidget> parent,
		QStringList &files,
		QByteArray &remoteContent,
		const QString &caption,
		const QString &filter,
		Type type,
		QString startFile) {
	if (cDialogLastPath().isEmpty()) {
		InitLastPath();
	}

	GtkFileDialog dialog(parent, caption, QString(), filter);

	dialog.setModal(true);
	if (type == Type::ReadFile || type == Type::ReadFiles) {
		dialog.setFileMode((type == Type::ReadFiles) ? QFileDialog::ExistingFiles : QFileDialog::ExistingFile);
		dialog.setAcceptMode(QFileDialog::AcceptOpen);
	} else if (type == Type::ReadFolder) {
		dialog.setAcceptMode(QFileDialog::AcceptOpen);
		dialog.setFileMode(QFileDialog::Directory);
		dialog.setOption(QFileDialog::ShowDirsOnly);
	} else {
		dialog.setFileMode(QFileDialog::AnyFile);
		dialog.setAcceptMode(QFileDialog::AcceptSave);
	}
	if (startFile.isEmpty() || startFile.at(0) != '/') {
		startFile = cDialogLastPath() + '/' + startFile;
	}
	dialog.selectFile(startFile);

	const auto res = dialog.exec();

	if (type != Type::ReadFolder) {
		// Save last used directory for all queries except directory choosing.
		const auto path = dialog.directory().absolutePath();
		if (!path.isEmpty() && path != cDialogLastPath()) {
			cSetDialogLastPath(path);
			Local::writeSettings();
		}
	}

	if (res == QDialog::Accepted) {
		if (type == Type::ReadFiles) {
			files = dialog.selectedFiles();
		} else {
			files = dialog.selectedFiles().mid(0, 1);
		}
		return true;
	}

	files = QStringList();
	remoteContent = QByteArray();
	return false;
}

} // namespace Gtk
} // namespace FileDialog
} // namespace Platform
