/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/file_utilities_linux.h"

#include "platform/linux/linux_libs.h"
#include "platform/linux/linux_gdk_helper.h"
#include "platform/linux/linux_desktop_environment.h"
#include "platform/linux/specific_linux.h"
#include "storage/localstorage.h"
#include "base/qt_adapters.h"
#include "window/window_controller.h"
#include "core/application.h"

#include <QtGui/QDesktopServices>

extern "C" {
#undef signals
#include <gio/gio.h>
#define signals public
} // extern "C"

#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION
#include <private/qguiapplication_p.h>

extern "C" {
#undef signals
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#define signals public
} // extern "C"
#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION

namespace Platform {
namespace File {
namespace {

#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION
bool ShowOpenWithSupported() {
	return Platform::internal::GdkHelperLoaded()
		&& (Libs::gtk_app_chooser_dialog_new != nullptr)
		&& (Libs::gtk_app_chooser_get_app_info != nullptr)
		&& (Libs::gtk_app_chooser_get_type != nullptr)
		&& (Libs::gtk_widget_get_window != nullptr)
		&& (Libs::gtk_widget_realize != nullptr)
		&& (Libs::gtk_widget_show != nullptr)
		&& (Libs::gtk_widget_destroy != nullptr);
}

class OpenWithDialog : public QWindow {
public:
	OpenWithDialog(const QString &filepath);
	~OpenWithDialog();

	bool exec();

private:
	static void handleResponse(OpenWithDialog *dialog, int responseId);

	GFile *_gfileInstance = nullptr;
	GtkWidget *_gtkWidget = nullptr;
	QEventLoop _loop;
	std::optional<bool> _result = std::nullopt;
};

OpenWithDialog::OpenWithDialog(const QString &filepath)
: _gfileInstance(g_file_new_for_path(filepath.toUtf8()))
, _gtkWidget(Libs::gtk_app_chooser_dialog_new(
		nullptr,
		GTK_DIALOG_MODAL,
		_gfileInstance)) {
	g_signal_connect_swapped(
		_gtkWidget,
		"response",
		G_CALLBACK(handleResponse),
		this);
}

OpenWithDialog::~OpenWithDialog() {
	Libs::gtk_widget_destroy(_gtkWidget);
	g_object_unref(_gfileInstance);
}

bool OpenWithDialog::exec() {
	Libs::gtk_widget_realize(_gtkWidget);

	if (const auto activeWindow = Core::App().activeWindow()) {
		Platform::internal::XSetTransientForHint(
			Libs::gtk_widget_get_window(_gtkWidget),
			activeWindow->widget().get()->windowHandle()->winId());
	}

	QGuiApplicationPrivate::showModalWindow(this);
	Libs::gtk_widget_show(_gtkWidget);

	if (!_result.has_value()) {
		_loop.exec();
	}

	QGuiApplicationPrivate::hideModalWindow(this);
	return *_result;
}

void OpenWithDialog::handleResponse(OpenWithDialog *dialog, int responseId) {
	GAppInfo *chosenAppInfo = nullptr;
	dialog->_result = true;

	switch (responseId) {
	case GTK_RESPONSE_OK:
		chosenAppInfo = Libs::gtk_app_chooser_get_app_info(
			Libs::gtk_app_chooser_cast(dialog->_gtkWidget));

		if (chosenAppInfo) {
			GList *uris = nullptr;
			uris = g_list_prepend(uris, g_file_get_uri(dialog->_gfileInstance));
			g_app_info_launch_uris(chosenAppInfo, uris, nullptr, nullptr);
			g_list_free(uris);
			g_object_unref(chosenAppInfo);
		}

		break;

	case GTK_RESPONSE_CANCEL:
	case GTK_RESPONSE_DELETE_EVENT:
		break;

	default:
		dialog->_result = false;
		break;
	}

	dialog->_loop.quit();
}
#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION

} // namespace

namespace internal {

QByteArray EscapeShell(const QByteArray &content) {
	auto result = QByteArray();

	auto b = content.constData(), e = content.constEnd();
	for (auto ch = b; ch != e; ++ch) {
		if (*ch == ' ' || *ch == '"' || *ch == '\'' || *ch == '\\') {
			if (result.isEmpty()) {
				result.reserve(content.size() * 2);
			}
			if (ch > b) {
				result.append(b, ch - b);
			}
			result.append('\\');
			b = ch;
		}
	}
	if (result.isEmpty()) {
		return content;
	}

	if (e > b) {
		result.append(b, e - b);
	}
	return result;
}

} // namespace internal

void UnsafeOpenUrl(const QString &url) {
	if (!g_app_info_launch_default_for_uri(
		url.toUtf8(),
		nullptr,
		nullptr)) {
		QDesktopServices::openUrl(url);
	}
}

void UnsafeOpenEmailLink(const QString &email) {
	UnsafeOpenUrl(qstr("mailto:") + email);
}

bool UnsafeShowOpenWith(const QString &filepath) {
#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION
	if (InFlatpak()
		|| InSnap()
		|| !ShowOpenWithSupported()) {
		return false;
	}

	const auto absolutePath = QFileInfo(filepath).absoluteFilePath();
	return OpenWithDialog(absolutePath).exec();
#else // !TDESKTOP_DISABLE_GTK_INTEGRATION
	return false;
#endif // TDESKTOP_DISABLE_GTK_INTEGRATION
}

void UnsafeLaunch(const QString &filepath) {
	const auto absolutePath = QFileInfo(filepath).absoluteFilePath();

	if (!g_app_info_launch_default_for_uri(
		g_filename_to_uri(absolutePath.toUtf8(), nullptr, nullptr),
		nullptr,
		nullptr)) {
		if (!UnsafeShowOpenWith(filepath)) {
			QDesktopServices::openUrl(QUrl::fromLocalFile(filepath));
		}
	}
}

} // namespace File

namespace FileDialog {
namespace {
#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION

// GTK file chooser image preview: thanks to Chromium

// The size of the preview we display for selected image files. We set height
// larger than width because generally there is more free space vertically
// than horiztonally (setting the preview image will alway expand the width of
// the dialog, but usually not the height). The image's aspect ratio will always
// be preserved.
constexpr auto kPreviewWidth = 256;
constexpr auto kPreviewHeight = 512;
#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION

using Type = ::FileDialog::internal::Type;

#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION
bool UseNative(Type type = Type::ReadFile) {
	// use gtk file dialog on gtk-based desktop environments
	// or if QT_QPA_PLATFORMTHEME=(gtk2|gtk3)
	// or if portals are used and operation is to open folder
	// and portal doesn't support folder choosing
	const auto sandboxedOrCustomPortal = InFlatpak()
		|| InSnap()
		|| UseXDGDesktopPortal();

	const auto neededForPortal = (type == Type::ReadFolder)
		&& !CanOpenDirectoryWithPortal();

	const auto neededNonForced = DesktopEnvironment::IsGtkBased()
		|| (sandboxedOrCustomPortal && neededForPortal);

	const auto excludeNonForced = sandboxedOrCustomPortal && !neededForPortal;

	return IsGtkIntegrationForced()
		|| (neededNonForced && !excludeNonForced);
}

bool NativeSupported() {
	return Platform::internal::GdkHelperLoaded()
		&& (Libs::gtk_widget_hide_on_delete != nullptr)
		&& (Libs::gtk_clipboard_store != nullptr)
		&& (Libs::gtk_clipboard_get != nullptr)
		&& (Libs::gtk_widget_destroy != nullptr)
		&& (Libs::gtk_dialog_get_type != nullptr)
		&& (Libs::gtk_dialog_run != nullptr)
		&& (Libs::gtk_widget_realize != nullptr)
		&& (Libs::gdk_window_set_modal_hint != nullptr)
		&& (Libs::gtk_widget_show != nullptr)
		&& (Libs::gdk_window_focus != nullptr)
		&& (Libs::gtk_widget_hide != nullptr)
		&& (Libs::gtk_widget_hide_on_delete != nullptr)
		&& (Libs::gtk_file_chooser_dialog_new != nullptr)
		&& (Libs::gtk_file_chooser_get_type != nullptr)
		&& (Libs::gtk_file_chooser_set_current_folder != nullptr)
		&& (Libs::gtk_file_chooser_get_current_folder != nullptr)
		&& (Libs::gtk_file_chooser_set_current_name != nullptr)
		&& (Libs::gtk_file_chooser_select_filename != nullptr)
		&& (Libs::gtk_file_chooser_get_filenames != nullptr)
		&& (Libs::gtk_file_chooser_set_filter != nullptr)
		&& (Libs::gtk_file_chooser_get_filter != nullptr)
		&& (Libs::gtk_window_get_type != nullptr)
		&& (Libs::gtk_window_set_title != nullptr)
		&& (Libs::gtk_file_chooser_set_local_only != nullptr)
		&& (Libs::gtk_file_chooser_set_action != nullptr)
		&& (Libs::gtk_file_chooser_set_select_multiple != nullptr)
		&& (Libs::gtk_file_chooser_set_do_overwrite_confirmation != nullptr)
		&& (Libs::gtk_file_chooser_remove_filter != nullptr)
		&& (Libs::gtk_file_filter_set_name != nullptr)
		&& (Libs::gtk_file_filter_add_pattern != nullptr)
		&& (Libs::gtk_file_chooser_add_filter != nullptr)
		&& (Libs::gtk_file_filter_new != nullptr);
}

bool PreviewSupported() {
	return NativeSupported()
		&& (Libs::gdk_pixbuf_new_from_file_at_size != nullptr);
}

bool GetNative(
		QPointer<QWidget> parent,
		QStringList &files,
		QByteArray &remoteContent,
		const QString &caption,
		const QString &filter,
		Type type,
		QString startFile) {
	internal::GtkFileDialog dialog(parent, caption, QString(), filter);

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

	int res = dialog.exec();

	QString path = dialog.directory().absolutePath();
	if (path != cDialogLastPath()) {
		cSetDialogLastPath(path);
		Local::writeSettings();
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
#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION

} // namespace

bool Get(
		QPointer<QWidget> parent,
		QStringList &files,
		QByteArray &remoteContent,
		const QString &caption,
		const QString &filter,
		Type type,
		QString startFile) {
	if (parent) {
		parent = parent->window();
	}
#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION
	if (UseNative(type) && NativeSupported()) {
		return GetNative(
			parent,
			files,
			remoteContent,
			caption,
			filter,
			type,
			startFile);
	}
#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION
	return ::FileDialog::internal::GetDefault(
		parent,
		files,
		remoteContent,
		caption,
		filter,
		type,
		startFile);
}

#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION
namespace internal {

QGtkDialog::QGtkDialog(GtkWidget *gtkWidget) : gtkWidget(gtkWidget) {
	g_signal_connect_swapped(G_OBJECT(gtkWidget), "response", G_CALLBACK(onResponse), this);
	g_signal_connect(G_OBJECT(gtkWidget), "delete-event", G_CALLBACK(Libs::gtk_widget_hide_on_delete), nullptr);
	if (PreviewSupported()) {
		_preview = Libs::gtk_image_new();
		g_signal_connect_swapped(G_OBJECT(gtkWidget), "update-preview", G_CALLBACK(onUpdatePreview), this);
		Libs::gtk_file_chooser_set_preview_widget(Libs::gtk_file_chooser_cast(gtkWidget), _preview);
	}
}

QGtkDialog::~QGtkDialog() {
	Libs::gtk_clipboard_store(Libs::gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
	Libs::gtk_widget_destroy(gtkWidget);
}

GtkDialog *QGtkDialog::gtkDialog() const {
	return Libs::gtk_dialog_cast(gtkWidget);
}

void QGtkDialog::exec() {
	if (modality() == Qt::ApplicationModal) {
		// block input to the whole app, including other GTK dialogs
		Libs::gtk_dialog_run(gtkDialog());
	} else {
		// block input to the window, allow input to other GTK dialogs
		QEventLoop loop;
		connect(this, SIGNAL(accept()), &loop, SLOT(quit()));
		connect(this, SIGNAL(reject()), &loop, SLOT(quit()));
		loop.exec();
	}
}

void QGtkDialog::show(Qt::WindowFlags flags, Qt::WindowModality modality, QWindow *parent) {
	connect(parent, &QWindow::destroyed, this, &QGtkDialog::onParentWindowDestroyed,
			Qt::UniqueConnection);
	setParent(parent);
	setFlags(flags);
	setModality(modality);

	Libs::gtk_widget_realize(gtkWidget); // creates X window

	if (parent) {
		Platform::internal::XSetTransientForHint(Libs::gtk_widget_get_window(gtkWidget), parent->winId());
	}

	if (modality != Qt::NonModal) {
		Libs::gdk_window_set_modal_hint(Libs::gtk_widget_get_window(gtkWidget), true);
		QGuiApplicationPrivate::showModalWindow(this);
	}

	Libs::gtk_widget_show(gtkWidget);
	Libs::gdk_window_focus(Libs::gtk_widget_get_window(gtkWidget), 0);
}

void QGtkDialog::hide() {
	QGuiApplicationPrivate::hideModalWindow(this);
	Libs::gtk_widget_hide(gtkWidget);
}

void QGtkDialog::onResponse(QGtkDialog *dialog, int response) {
	if (response == GTK_RESPONSE_OK)
		emit dialog->accept();
	else
		emit dialog->reject();
}

void QGtkDialog::onUpdatePreview(QGtkDialog* dialog) {
	auto filename = Libs::gtk_file_chooser_get_preview_filename(Libs::gtk_file_chooser_cast(dialog->gtkWidget));
	if (!filename) {
		Libs::gtk_file_chooser_set_preview_widget_active(Libs::gtk_file_chooser_cast(dialog->gtkWidget), false);
		return;
	}

	// Don't attempt to open anything which isn't a regular file. If a named pipe,
	// this may hang. See https://crbug.com/534754.
	struct stat stat_buf;
	if (stat(filename, &stat_buf) != 0 || !S_ISREG(stat_buf.st_mode)) {
		g_free(filename);
		Libs::gtk_file_chooser_set_preview_widget_active(Libs::gtk_file_chooser_cast(dialog->gtkWidget), false);
		return;
	}

	// This will preserve the image's aspect ratio.
	auto pixbuf = Libs::gdk_pixbuf_new_from_file_at_size(filename, kPreviewWidth, kPreviewHeight, nullptr);
	g_free(filename);
	if (pixbuf) {
		Libs::gtk_image_set_from_pixbuf(Libs::gtk_image_cast(dialog->_preview), pixbuf);
		g_object_unref(pixbuf);
	}
	Libs::gtk_file_chooser_set_preview_widget_active(Libs::gtk_file_chooser_cast(dialog->gtkWidget), pixbuf ? true : false);
}

void QGtkDialog::onParentWindowDestroyed() {
	// The Gtk*DialogHelper classes own this object. Make sure the parent doesn't delete it.
	setParent(nullptr);
}
#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION

namespace {

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
	Q_ASSERT(regexp.isValid());
	QString f = filter;
	int i = regexp.indexIn(f);
	if (i >= 0)
		f = regexp.cap(2);
	return f.split(QLatin1Char(' '), base::QStringSkipEmptyParts);
}

} // namespace

#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION
GtkFileDialog::GtkFileDialog(QWidget *parent, const QString &caption, const QString &directory, const QString &filter) : QDialog(parent)
, _windowTitle(caption)
, _initialDirectory(directory) {
	auto filters = makeFilterList(filter);
	const int numFilters = filters.count();
	_nameFilters.reserve(numFilters);
	for (int i = 0; i < numFilters; ++i) {
		_nameFilters << filters[i].simplified();
	}

	d.reset(new QGtkDialog(Libs::gtk_file_chooser_dialog_new("", nullptr,
		GTK_FILE_CHOOSER_ACTION_OPEN,
		// https://developer.gnome.org/gtk3/stable/GtkFileChooserDialog.html#gtk-file-chooser-dialog-new
		// first_button_text doesn't need explicit conversion to char*, while all others are vardict
		tr::lng_cancel(tr::now).toUtf8(), GTK_RESPONSE_CANCEL,
		tr::lng_box_ok(tr::now).toUtf8().constData(), GTK_RESPONSE_OK, nullptr)));
	connect(d.data(), SIGNAL(accept()), this, SLOT(onAccepted()));
	connect(d.data(), SIGNAL(reject()), this, SLOT(onRejected()));

	g_signal_connect(Libs::gtk_file_chooser_cast(d->gtkDialog()), "selection-changed", G_CALLBACK(onSelectionChanged), this);
	g_signal_connect_swapped(Libs::gtk_file_chooser_cast(d->gtkDialog()), "current-folder-changed", G_CALLBACK(onCurrentFolderChanged), this);
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
	Libs::gtk_file_chooser_set_current_folder(Libs::gtk_file_chooser_cast(gtkDialog), directory.toUtf8());
}

QDir GtkFileDialog::directory() const {
	// While GtkFileChooserDialog is hidden, gtk_file_chooser_get_current_folder()
	// returns a bogus value -> return the cached value before hiding
	if (!_dir.isEmpty())
		return _dir;

	QString ret;
	GtkDialog *gtkDialog = d->gtkDialog();
	gchar *folder = Libs::gtk_file_chooser_get_current_folder(Libs::gtk_file_chooser_cast(gtkDialog));
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
	GSList *filenames = Libs::gtk_file_chooser_get_filenames(Libs::gtk_file_chooser_cast(gtkDialog));
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
		Libs::gtk_file_chooser_set_filter(Libs::gtk_file_chooser_cast(gtkDialog), gtkFilter);
	}
}

QString GtkFileDialog::selectedNameFilter() const {
	GtkDialog *gtkDialog = d->gtkDialog();
	GtkFileFilter *gtkFilter = Libs::gtk_file_chooser_get_filter(Libs::gtk_file_chooser_cast(gtkDialog));
	return _filterNames.value(gtkFilter);
}

void GtkFileDialog::onAccepted() {
	emit accept();

//	QString filter = selectedNameFilter();
//	if (filter.isEmpty())
//		emit filterSelected(filter);

//	QList<QUrl> files = selectedFiles();
//	emit filesSelected(files);
//	if (files.count() == 1)
//		emit fileSelected(files.first());
}

void GtkFileDialog::onRejected() {
	emit reject();

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

bool CustomButtonsSupported() {
	return (Libs::gtk_dialog_get_widget_for_response != nullptr)
		&& (Libs::gtk_button_set_label != nullptr)
		&& (Libs::gtk_button_get_type != nullptr);
}

void GtkFileDialog::applyOptions() {
	GtkDialog *gtkDialog = d->gtkDialog();

	Libs::gtk_window_set_title(Libs::gtk_window_cast(gtkDialog), _windowTitle.toUtf8());
	Libs::gtk_file_chooser_set_local_only(Libs::gtk_file_chooser_cast(gtkDialog), true);

	const GtkFileChooserAction action = gtkFileChooserAction(_fileMode, _acceptMode);
	Libs::gtk_file_chooser_set_action(Libs::gtk_file_chooser_cast(gtkDialog), action);

	const bool selectMultiple = (_fileMode == QFileDialog::ExistingFiles);
	Libs::gtk_file_chooser_set_select_multiple(Libs::gtk_file_chooser_cast(gtkDialog), selectMultiple);

	const bool confirmOverwrite = !_options.testFlag(QFileDialog::DontConfirmOverwrite);
	Libs::gtk_file_chooser_set_do_overwrite_confirmation(Libs::gtk_file_chooser_cast(gtkDialog), confirmOverwrite);

	if (!_nameFilters.isEmpty())
		setNameFilters(_nameFilters);

	if (!_initialDirectory.isEmpty())
		setDirectory(_initialDirectory);

	for_const (const auto &filename, _initialFiles) {
		if (_acceptMode == QFileDialog::AcceptSave) {
			QFileInfo fi(filename);
			Libs::gtk_file_chooser_set_current_folder(Libs::gtk_file_chooser_cast(gtkDialog), fi.path().toUtf8());
			Libs::gtk_file_chooser_set_current_name(Libs::gtk_file_chooser_cast(gtkDialog), fi.fileName().toUtf8());
		} else if (filename.endsWith('/')) {
			Libs::gtk_file_chooser_set_current_folder(Libs::gtk_file_chooser_cast(gtkDialog), filename.toUtf8());
		} else {
			Libs::gtk_file_chooser_select_filename(Libs::gtk_file_chooser_cast(gtkDialog), filename.toUtf8());
		}
	}

	const QString initialNameFilter = _nameFilters.isEmpty() ? QString() : _nameFilters.front();
	if (!initialNameFilter.isEmpty())
		selectNameFilter(initialNameFilter);

	if (CustomButtonsSupported()) {
		GtkWidget *acceptButton = Libs::gtk_dialog_get_widget_for_response(gtkDialog, GTK_RESPONSE_OK);
		if (acceptButton) {
			/*if (opts->isLabelExplicitlySet(QFileDialogOptions::Accept))
				Libs::gtk_button_set_label(Libs::gtk_button_cast(acceptButton), opts->labelText(QFileDialogOptions::Accept).toUtf8());
			else*/ if (_acceptMode == QFileDialog::AcceptOpen)
				Libs::gtk_button_set_label(Libs::gtk_button_cast(acceptButton), tr::lng_open_link(tr::now).toUtf8());
			else
				Libs::gtk_button_set_label(Libs::gtk_button_cast(acceptButton), tr::lng_settings_save(tr::now).toUtf8());
		}

		GtkWidget *rejectButton = Libs::gtk_dialog_get_widget_for_response(gtkDialog, GTK_RESPONSE_CANCEL);
		if (rejectButton) {
			/*if (opts->isLabelExplicitlySet(QFileDialogOptions::Reject))
				Libs::gtk_button_set_label(Libs::gtk_button_cast(rejectButton), opts->labelText(QFileDialogOptions::Reject).toUtf8());
			else*/
				Libs::gtk_button_set_label(Libs::gtk_button_cast(rejectButton), tr::lng_cancel(tr::now).toUtf8());
		}
	}
}

void GtkFileDialog::setNameFilters(const QStringList &filters) {
	GtkDialog *gtkDialog = d->gtkDialog();
	foreach (GtkFileFilter *filter, _filters)
		Libs::gtk_file_chooser_remove_filter(Libs::gtk_file_chooser_cast(gtkDialog), filter);

	_filters.clear();
	_filterNames.clear();

	for_const (auto &filter, filters) {
		GtkFileFilter *gtkFilter = Libs::gtk_file_filter_new();
		auto name = filter;//.left(filter.indexOf(QLatin1Char('(')));
		auto extensions = cleanFilterList(filter);

		Libs::gtk_file_filter_set_name(gtkFilter, name.isEmpty() ? extensions.join(QStringLiteral(", ")).toUtf8() : name.toUtf8());
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

			Libs::gtk_file_filter_add_pattern(gtkFilter, caseInsensitiveExt.toUtf8());
		}

		Libs::gtk_file_chooser_add_filter(Libs::gtk_file_chooser_cast(gtkDialog), gtkFilter);

		_filters.insert(filter, gtkFilter);
		_filterNames.insert(gtkFilter, filter);
	}
}

} // namespace internal
#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION
} // namespace FileDialog
} // namespace Platform
