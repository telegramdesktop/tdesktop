/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/linux_gtk_open_with_dialog.h"

#include "platform/linux/linux_gtk_integration_p.h"
#include "platform/linux/linux_gdk_helper.h"
#include "window/window_controller.h"
#include "core/application.h"

#include <private/qguiapplication_p.h>

namespace Platform {
namespace File {
namespace internal {
namespace {

using namespace Platform::Gtk;

bool Supported() {
	return Platform::internal::GdkHelperLoaded()
		&& (gtk_app_chooser_dialog_new != nullptr)
		&& (gtk_app_chooser_get_app_info != nullptr)
		&& (gtk_app_chooser_get_type != nullptr)
		&& (gtk_widget_get_window != nullptr)
		&& (gtk_widget_realize != nullptr)
		&& (gtk_widget_show != nullptr)
		&& (gtk_widget_destroy != nullptr);
}

class GtkOpenWithDialog : public QWindow {
public:
	GtkOpenWithDialog(const QString &filepath);
	~GtkOpenWithDialog();

	bool exec();

private:
	static void handleResponse(GtkOpenWithDialog *dialog, int responseId);

	GFile *_gfileInstance = nullptr;
	GtkWidget *_gtkWidget = nullptr;
	QEventLoop _loop;
	std::optional<bool> _result;
};

GtkOpenWithDialog::GtkOpenWithDialog(const QString &filepath)
: _gfileInstance(g_file_new_for_path(filepath.toUtf8().constData()))
, _gtkWidget(gtk_app_chooser_dialog_new(
		nullptr,
		GTK_DIALOG_MODAL,
		_gfileInstance)) {
	g_signal_connect_swapped(
		_gtkWidget,
		"response",
		G_CALLBACK(handleResponse),
		this);
}

GtkOpenWithDialog::~GtkOpenWithDialog() {
	gtk_widget_destroy(_gtkWidget);
	g_object_unref(_gfileInstance);
}

bool GtkOpenWithDialog::exec() {
	gtk_widget_realize(_gtkWidget);

	if (const auto activeWindow = Core::App().activeWindow()) {
		Platform::internal::XSetTransientForHint(
			gtk_widget_get_window(_gtkWidget),
			activeWindow->widget().get()->windowHandle()->winId());
	}

	QGuiApplicationPrivate::showModalWindow(this);
	gtk_widget_show(_gtkWidget);

	if (!_result.has_value()) {
		_loop.exec();
	}

	QGuiApplicationPrivate::hideModalWindow(this);
	return *_result;
}

void GtkOpenWithDialog::handleResponse(GtkOpenWithDialog *dialog, int responseId) {
	GAppInfo *chosenAppInfo = nullptr;
	dialog->_result = true;

	switch (responseId) {
	case GTK_RESPONSE_OK:
		chosenAppInfo = gtk_app_chooser_get_app_info(
			gtk_app_chooser_cast(dialog->_gtkWidget));

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

} // namespace

bool ShowGtkOpenWithDialog(const QString &filepath) {
	if (!Supported()) {
		return false;
	}

	return GtkOpenWithDialog(filepath).exec();
}

} // namespace internal
} // namespace File
} // namespace Platform
