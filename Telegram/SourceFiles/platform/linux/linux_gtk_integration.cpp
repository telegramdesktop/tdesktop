/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/linux_gtk_integration.h"

#include "platform/linux/linux_gtk_integration_p.h"
#include "base/platform/base_platform_info.h"
#include "platform/linux/linux_xlib_helper.h"
#include "platform/linux/linux_gdk_helper.h"
#include "platform/linux/linux_gtk_file_dialog.h"
#include "platform/linux/linux_open_with_dialog.h"
#include "platform/linux/specific_linux.h"
#include "core/sandbox.h"
#include "core/core_settings.h"
#include "core/application.h"
#include "main/main_domain.h"
#include "mainwindow.h"

namespace Platform {
namespace internal {

using namespace Platform::Gtk;

namespace {

bool GtkTriedToInit = false;
bool GtkLoaded = false;

bool LoadLibrary(QLibrary &lib, const char *name, int version) {
#ifdef LINK_TO_GTK
	return true;
#else // LINK_TO_GTK
	DEBUG_LOG(("Loading '%1' with version %2...").arg(
		QLatin1String(name)).arg(version));
	lib.setFileNameAndVersion(QLatin1String(name), version);
	if (lib.load()) {
		DEBUG_LOG(("Loaded '%1' with version %2!").arg(
			QLatin1String(name)).arg(version));
		return true;
	}
	lib.setFileNameAndVersion(QLatin1String(name), QString());
	if (lib.load()) {
		DEBUG_LOG(("Loaded '%1' without version!").arg(QLatin1String(name)));
		return true;
	}
	LOG(("Could not load '%1' with version %2 :(").arg(
		QLatin1String(name)).arg(version));
	return false;
#endif // !LINK_TO_GTK
}

void GtkMessageHandler(
		const gchar *log_domain,
		GLogLevelFlags log_level,
		const gchar *message,
		gpointer unused_data) {
	// Silence false-positive Gtk warnings (we are using Xlib to set
	// the WM_TRANSIENT_FOR hint).
	if (message != qstr("GtkDialog mapped without a transient parent. "
		"This is discouraged.")) {
		// For other messages, call the default handler.
		g_log_default_handler(log_domain, log_level, message, unused_data);
	}
}

bool SetupGtkBase(QLibrary &lib_gtk) {
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_init_check", gtk_init_check)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_check_version", gtk_check_version)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_settings_get_default", gtk_settings_get_default)) return false;

	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_widget_show", gtk_widget_show)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_widget_hide", gtk_widget_hide)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_widget_get_window", gtk_widget_get_window)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_widget_realize", gtk_widget_realize)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_widget_hide_on_delete", gtk_widget_hide_on_delete)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_widget_destroy", gtk_widget_destroy)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_clipboard_get", gtk_clipboard_get)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_clipboard_store", gtk_clipboard_store)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_clipboard_wait_for_contents", gtk_clipboard_wait_for_contents)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_clipboard_wait_for_image", gtk_clipboard_wait_for_image)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_selection_data_targets_include_image", gtk_selection_data_targets_include_image)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_selection_data_free", gtk_selection_data_free)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_file_chooser_dialog_new", gtk_file_chooser_dialog_new)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_file_chooser_get_type", gtk_file_chooser_get_type)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_image_get_type", gtk_image_get_type)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_file_chooser_set_current_folder", gtk_file_chooser_set_current_folder)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_file_chooser_get_current_folder", gtk_file_chooser_get_current_folder)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_file_chooser_set_current_name", gtk_file_chooser_set_current_name)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_file_chooser_select_filename", gtk_file_chooser_select_filename)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_file_chooser_get_filenames", gtk_file_chooser_get_filenames)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_file_chooser_set_filter", gtk_file_chooser_set_filter)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_file_chooser_get_filter", gtk_file_chooser_get_filter)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_window_get_type", gtk_window_get_type)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_window_set_title", gtk_window_set_title)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_file_chooser_set_local_only", gtk_file_chooser_set_local_only)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_file_chooser_set_action", gtk_file_chooser_set_action)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_file_chooser_set_select_multiple", gtk_file_chooser_set_select_multiple)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_file_chooser_set_do_overwrite_confirmation", gtk_file_chooser_set_do_overwrite_confirmation)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_file_chooser_remove_filter", gtk_file_chooser_remove_filter)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_file_filter_set_name", gtk_file_filter_set_name)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_file_filter_add_pattern", gtk_file_filter_add_pattern)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_file_chooser_add_filter", gtk_file_chooser_add_filter)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_file_chooser_set_preview_widget", gtk_file_chooser_set_preview_widget)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_file_chooser_get_preview_filename", gtk_file_chooser_get_preview_filename)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_file_chooser_set_preview_widget_active", gtk_file_chooser_set_preview_widget_active)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_file_filter_new", gtk_file_filter_new)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_image_new", gtk_image_new)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_image_set_from_pixbuf", gtk_image_set_from_pixbuf)) return false;

	if (!LOAD_GTK_SYMBOL(lib_gtk, "gdk_window_set_modal_hint", gdk_window_set_modal_hint)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gdk_window_focus", gdk_window_focus)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_dialog_get_type", gtk_dialog_get_type)) return false;
	if (!LOAD_GTK_SYMBOL(lib_gtk, "gtk_dialog_run", gtk_dialog_run)) return false;

	if (!LOAD_GTK_SYMBOL(lib_gtk, "gdk_atom_intern", gdk_atom_intern)) return false;

	if (LOAD_GTK_SYMBOL(lib_gtk, "gdk_set_allowed_backends", gdk_set_allowed_backends)) {
		// We work only with Wayland and X11 GDK backends.
		// Otherwise we get segfault in Ubuntu 17.04 in gtk_init_check() call.
		// See https://github.com/telegramdesktop/tdesktop/issues/3176
		// See https://github.com/telegramdesktop/tdesktop/issues/3162
		if(IsWayland()) {
			DEBUG_LOG(("Limit allowed GDK backends to wayland,x11"));
			gdk_set_allowed_backends("wayland,x11");
		} else {
			DEBUG_LOG(("Limit allowed GDK backends to x11,wayland"));
			gdk_set_allowed_backends("x11,wayland");
		}
	}

	// gtk_init will reset the Xlib error handler,
	// and that causes Qt applications to quit on X errors.
	// Therefore, we need to manually restore it.
	XErrorHandlerRestorer handlerRestorer;

	DEBUG_LOG(("Library gtk functions loaded!"));
	GtkTriedToInit = true;
	if (!gtk_init_check(0, 0)) {
		gtk_init_check = nullptr;
		DEBUG_LOG(("Failed to gtk_init_check(0, 0)!"));
		return false;
	}
	DEBUG_LOG(("Checked gtk with gtk_init_check!"));

	// Use our custom log handler.
	g_log_set_handler("Gtk", G_LOG_LEVEL_MESSAGE, GtkMessageHandler, nullptr);

	return true;
}

bool GetImageFromClipboardSupported() {
	return (gtk_clipboard_get != nullptr)
		&& (gtk_clipboard_wait_for_contents != nullptr)
		&& (gtk_clipboard_wait_for_image != nullptr)
		&& (gtk_selection_data_targets_include_image != nullptr)
		&& (gtk_selection_data_free != nullptr)
		&& (gdk_pixbuf_get_pixels != nullptr)
		&& (gdk_pixbuf_get_width != nullptr)
		&& (gdk_pixbuf_get_height != nullptr)
		&& (gdk_pixbuf_get_rowstride != nullptr)
		&& (gdk_pixbuf_get_has_alpha != nullptr)
		&& (gdk_atom_intern != nullptr);
}

template <typename T>
std::optional<T> GtkSetting(const QString &propertyName) {
	const auto integration = GtkIntegration::Instance();
	if (!integration
		|| !integration->loaded()
		|| gtk_settings_get_default == nullptr) {
		return std::nullopt;
	}
	auto settings = gtk_settings_get_default();
	T value;
	g_object_get(settings, propertyName.toUtf8(), &value, nullptr);
	return value;
}

bool IconThemeShouldBeSet() {
	// change the icon theme only if
	// it isn't already set by a platformtheme plugin
	// if QT_QPA_PLATFORMTHEME=(gtk2|gtk3), then force-apply the icon theme
	static const auto Result =
		// QGenericUnixTheme
		(QIcon::themeName() == qstr("hicolor")
			&& QIcon::fallbackThemeName() == qstr("hicolor"))
		// QGnomeTheme
		|| (QIcon::themeName() == qstr("Adwaita")
			&& QIcon::fallbackThemeName() == qstr("gnome"))
		// qt5ct
		|| (QIcon::themeName().isEmpty()
			&& QIcon::fallbackThemeName().isEmpty())
		|| IsGtkIntegrationForced();

	return Result;
}

bool CursorSizeShouldBeSet() {
	// change the cursor size only on Wayland and if it wasn't already set
	static const auto Result = IsWayland()
		&& qEnvironmentVariableIsEmpty("XCURSOR_SIZE");

	return Result;
}

void SetScaleFactor() {
	Core::Sandbox::Instance().customEnterFromEventLoop([] {
		const auto integration = GtkIntegration::Instance();
		const auto ratio = Core::Sandbox::Instance().devicePixelRatio();
		if (!integration || ratio > 1.) {
			return;
		}

		const auto scaleFactor = integration->scaleFactor().value_or(1);
		if (scaleFactor == 1) {
			return;
		}

		LOG(("GTK scale factor: %1").arg(scaleFactor));
		cSetScreenScale(style::CheckScale(scaleFactor * 100));
	});
}

void SetIconTheme() {
	Core::Sandbox::Instance().customEnterFromEventLoop([] {
		const auto integration = GtkIntegration::Instance();
		if (!integration || !IconThemeShouldBeSet()) {
			return;
		}

		const auto themeName = integration->getStringSetting(
			qsl("gtk-icon-theme-name"));

		const auto fallbackThemeName = integration->getStringSetting(
			qsl("gtk-fallback-icon-theme"));

		if (!themeName.has_value() || !fallbackThemeName.has_value()) {
			return;
		}

		DEBUG_LOG(("Setting GTK icon theme"));

		QIcon::setThemeName(*themeName);
		QIcon::setFallbackThemeName(*fallbackThemeName);

		DEBUG_LOG(("New icon theme: %1").arg(QIcon::themeName()));
		DEBUG_LOG(("New fallback icon theme: %1").arg(
			QIcon::fallbackThemeName()));

		SetApplicationIcon(Window::CreateIcon());
		if (App::wnd()) {
			App::wnd()->setWindowIcon(Window::CreateIcon());
		}

		Core::App().domain().notifyUnreadBadgeChanged();
	});
}

void SetCursorSize() {
	Core::Sandbox::Instance().customEnterFromEventLoop([] {
		const auto integration = GtkIntegration::Instance();
		if (!integration || !CursorSizeShouldBeSet()) {
			return;
		}

		const auto newCursorSize = integration->getIntSetting(
			qsl("gtk-cursor-theme-size"));

		if (!newCursorSize.has_value()) {
			return;
		}

		DEBUG_LOG(("Setting GTK cursor size"));
		qputenv("XCURSOR_SIZE", QByteArray::number(*newCursorSize));
		DEBUG_LOG(("New cursor size: %1").arg(*newCursorSize));
	});
}

void DarkModeChanged() {
	Core::Sandbox::Instance().customEnterFromEventLoop([] {
		Core::App().settings().setSystemDarkMode(IsDarkMode());
	});
}

void DecorationLayoutChanged() {
	Core::Sandbox::Instance().customEnterFromEventLoop([] {
		Core::App().settings().setWindowControlsLayout(
			WindowControlsLayout());
	});
}

} // namespace

GtkIntegration::GtkIntegration() {
}

GtkIntegration *GtkIntegration::Instance() {
	static const auto useGtkIntegration = !qEnvironmentVariableIsSet(
		kDisableGtkIntegration.utf8());
	
	if (!useGtkIntegration) {
		return nullptr;
	}

	static GtkIntegration instance;
	return &instance;
}

void GtkIntegration::load() {
	Expects(!GtkLoaded);
	DEBUG_LOG(("Loading GTK"));

	QLibrary lib_gtk;
	lib_gtk.setLoadHints(QLibrary::DeepBindHint);

	if (LoadLibrary(lib_gtk, "gtk-3", 0)) {
		GtkLoaded = SetupGtkBase(lib_gtk);
	}
	if (!GtkLoaded
		&& !GtkTriedToInit
		&& LoadLibrary(lib_gtk, "gtk-x11-2.0", 0)) {
		GtkLoaded = SetupGtkBase(lib_gtk);
	}

	if (GtkLoaded) {
		LOAD_GTK_SYMBOL(lib_gtk, "gdk_display_get_default", gdk_display_get_default);
		LOAD_GTK_SYMBOL(lib_gtk, "gdk_display_get_monitor", gdk_display_get_monitor);
		LOAD_GTK_SYMBOL(lib_gtk, "gdk_display_get_primary_monitor", gdk_display_get_primary_monitor);
		LOAD_GTK_SYMBOL(lib_gtk, "gdk_monitor_get_scale_factor", gdk_monitor_get_scale_factor);

		LOAD_GTK_SYMBOL(lib_gtk, "gdk_pixbuf_new_from_file_at_size", gdk_pixbuf_new_from_file_at_size);
		LOAD_GTK_SYMBOL(lib_gtk, "gdk_pixbuf_get_has_alpha", gdk_pixbuf_get_has_alpha);
		LOAD_GTK_SYMBOL(lib_gtk, "gdk_pixbuf_get_pixels", gdk_pixbuf_get_pixels);
		LOAD_GTK_SYMBOL(lib_gtk, "gdk_pixbuf_get_width", gdk_pixbuf_get_width);
		LOAD_GTK_SYMBOL(lib_gtk, "gdk_pixbuf_get_height", gdk_pixbuf_get_height);
		LOAD_GTK_SYMBOL(lib_gtk, "gdk_pixbuf_get_rowstride", gdk_pixbuf_get_rowstride);

		GdkHelperLoad(lib_gtk);

		LOAD_GTK_SYMBOL(lib_gtk, "gtk_dialog_get_widget_for_response", gtk_dialog_get_widget_for_response);
		LOAD_GTK_SYMBOL(lib_gtk, "gtk_button_set_label", gtk_button_set_label);
		LOAD_GTK_SYMBOL(lib_gtk, "gtk_button_get_type", gtk_button_get_type);

		LOAD_GTK_SYMBOL(lib_gtk, "gtk_app_chooser_dialog_new", gtk_app_chooser_dialog_new);
		LOAD_GTK_SYMBOL(lib_gtk, "gtk_app_chooser_get_app_info", gtk_app_chooser_get_app_info);
		LOAD_GTK_SYMBOL(lib_gtk, "gtk_app_chooser_get_type", gtk_app_chooser_get_type);

		SetScaleFactor();
		SetIconTheme();
		SetCursorSize();

		const auto settings = gtk_settings_get_default();

		g_signal_connect(
			settings,
			"notify::gtk-icon-theme-name",
			G_CALLBACK(SetIconTheme),
			nullptr);

		g_signal_connect(
			settings,
			"notify::gtk-theme-name",
			G_CALLBACK(DarkModeChanged),
			nullptr);

		g_signal_connect(
			settings,
			"notify::gtk-cursor-theme-size",
			G_CALLBACK(SetCursorSize),
			nullptr);

		if (checkVersion(3, 0, 0)) {
			g_signal_connect(
				settings,
				"notify::gtk-application-prefer-dark-theme",
				G_CALLBACK(DarkModeChanged),
				nullptr);
		}

		if (checkVersion(3, 12, 0)) {
			g_signal_connect(
				settings,
				"notify::gtk-decoration-layout",
				G_CALLBACK(DecorationLayoutChanged),
				nullptr);
		}
	} else {
		LOG(("Could not load gtk-3 or gtk-x11-2.0!"));
	}
}

bool GtkIntegration::loaded() const {
	return GtkLoaded;
}

bool GtkIntegration::checkVersion(uint major, uint minor, uint micro) const {
	return (loaded() && gtk_check_version != nullptr)
		? !gtk_check_version(major, minor, micro)
		: false;
}

std::optional<bool> GtkIntegration::getBoolSetting(
		const QString &propertyName) const {
	const auto value = GtkSetting<gboolean>(propertyName);
	if (!value.has_value()) {
		return std::nullopt;
	}
	DEBUG_LOG(("Getting GTK setting, %1: %2")
		.arg(propertyName)
		.arg(Logs::b(*value)));
	return *value;
}

std::optional<int> GtkIntegration::getIntSetting(
		const QString &propertyName) const {
	const auto value = GtkSetting<gint>(propertyName);
	if (value.has_value()) {
		DEBUG_LOG(("Getting GTK setting, %1: %2")
			.arg(propertyName)
			.arg(*value));
	}
	return value;
}

std::optional<QString> GtkIntegration::getStringSetting(
		const QString &propertyName) const {
	auto value = GtkSetting<gchararray>(propertyName);
	if (!value.has_value()) {
		return std::nullopt;
	}
	const auto str = QString::fromUtf8(*value);
	g_free(*value);
	DEBUG_LOG(("Getting GTK setting, %1: '%2'").arg(propertyName).arg(str));
	return str;
}

std::optional<int> GtkIntegration::scaleFactor() const {
	if (!loaded()
		|| (gdk_display_get_default == nullptr)
		|| (gdk_display_get_monitor == nullptr)
		|| (gdk_display_get_primary_monitor == nullptr)
		|| (gdk_monitor_get_scale_factor == nullptr)) {
		return std::nullopt;
	}

	const auto display = gdk_display_get_default();
	if (!display) {
		return std::nullopt;
	}

	const auto monitor = [&] {
		if (const auto primary = gdk_display_get_primary_monitor(display)) {
			return primary;
		}
		return gdk_display_get_monitor(display, 0);
	}();

	if (!monitor) {
		return std::nullopt;
	}

	return gdk_monitor_get_scale_factor(monitor);
}

bool GtkIntegration::fileDialogSupported() const {
	return FileDialog::Gtk::Supported();
}

bool GtkIntegration::useFileDialog(FileDialogType type) const {
	return FileDialog::Gtk::Use(type);
}

bool GtkIntegration::getFileDialog(
		QPointer<QWidget> parent,
		QStringList &files,
		QByteArray &remoteContent,
		const QString &caption,
		const QString &filter,
		FileDialogType type,
		QString startFile) const {
	return FileDialog::Gtk::Get(
		parent,
		files,
		remoteContent,
		caption,
		filter,
		type,
		startFile);
}

bool GtkIntegration::showOpenWithDialog(const QString &filepath) const {
	return File::internal::ShowOpenWithDialog(filepath);
}

QImage GtkIntegration::getImageFromClipboard() const {
	QImage data;

	if (!GetImageFromClipboardSupported()) {
		return data;
	}

	const auto clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
	if (!clipboard) {
		return data;
	}

	auto gsel = gtk_clipboard_wait_for_contents(
		clipboard,
		gdk_atom_intern("TARGETS", true));

	if (gsel) {
		if (gtk_selection_data_targets_include_image(gsel, false)) {
			auto img = gtk_clipboard_wait_for_image(clipboard);

			if (img) {
				data = QImage(
					gdk_pixbuf_get_pixels(img),
					gdk_pixbuf_get_width(img),
					gdk_pixbuf_get_height(img),
					gdk_pixbuf_get_rowstride(img),
					gdk_pixbuf_get_has_alpha(img)
						? QImage::Format_RGBA8888
						: QImage::Format_RGB888).copy();

				g_object_unref(img);
			}
		}

		gtk_selection_data_free(gsel);
	}

	return data;
}

} // namespace internal
} // namespace Platform
