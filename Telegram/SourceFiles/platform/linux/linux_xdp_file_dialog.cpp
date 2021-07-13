/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/linux_xdp_file_dialog.h"

#include "platform/platform_file_utilities.h"
#include "base/platform/base_platform_info.h"
#include "base/platform/linux/base_linux_glibmm_helper.h"
#include "platform/linux/linux_wayland_integration.h"
#include "storage/localstorage.h"
#include "base/openssl_help.h"
#include "base/qt_adapters.h"

#include <QtGui/QWindow>
#include <QtWidgets/QFileDialog>

#include <glibmm.h>
#include <giomm.h>

using Platform::internal::WaylandIntegration;

namespace Platform {
namespace FileDialog {
namespace XDP {
namespace {

using Type = ::FileDialog::internal::Type;

constexpr auto kXDGDesktopPortalService = "org.freedesktop.portal.Desktop"_cs;
constexpr auto kXDGDesktopPortalObjectPath = "/org/freedesktop/portal/desktop"_cs;
constexpr auto kXDGDesktopPortalFileChooserInterface = "org.freedesktop.portal.FileChooser"_cs;
constexpr auto kPropertiesInterface = "org.freedesktop.DBus.Properties"_cs;

const char *filterRegExp =
"^(.*)\\(([a-zA-Z0-9_.,*? +;#\\-\\[\\]@\\{\\}/!<>\\$%&=^~:\\|]*)\\)$";

std::optional<uint> FileChooserPortalVersion;

auto QStringListToStd(const QStringList &list) {
	std::vector<Glib::ustring> result;
	ranges::transform(
		list,
		ranges::back_inserter(result),
		&QString::toStdString);
	return result;
}

auto MakeFilterList(const QString &filter) {
	std::vector<Glib::ustring> result;
	QString f(filter);

	if (f.isEmpty()) {
		return result;
	}

	QString sep(QLatin1String(";;"));
	int i = f.indexOf(sep, 0);
	if (i == -1) {
		if (f.indexOf(QLatin1Char('\n'), 0) != -1) {
			sep = QLatin1Char('\n');
			i = f.indexOf(sep, 0);
		}
	}

	ranges::transform(
		f.split(sep),
		ranges::back_inserter(result),
		[](const QString &string) {
			return string.simplified().toStdString();
		});

	return result;
}

void ComputeFileChooserPortalVersion() {
	const auto connection = [] {
		try {
			return Gio::DBus::Connection::get_sync(
				Gio::DBus::BusType::BUS_TYPE_SESSION);
		} catch (...) {
			return Glib::RefPtr<Gio::DBus::Connection>();
		}
	}();

	if (!connection) {
		return;
	}

	connection->call(
		std::string(kXDGDesktopPortalObjectPath),
		std::string(kPropertiesInterface),
		"Get",
		base::Platform::MakeGlibVariant(std::tuple{
			Glib::ustring(
				std::string(kXDGDesktopPortalFileChooserInterface)),
			Glib::ustring("version"),
		}),
		[=](const Glib::RefPtr<Gio::AsyncResult> &result) {
			try {
				auto reply = connection->call_finish(result);

				FileChooserPortalVersion =
					base::Platform::GlibVariantCast<uint>(
						base::Platform::GlibVariantCast<Glib::VariantBase>(
							reply.get_child(0)));
			} catch (const Glib::Error &e) {
				static const auto NotSupportedErrors = {
					"org.freedesktop.DBus.Error.ServiceUnknown",
				};

				const auto errorName =
					Gio::DBus::ErrorUtils::get_remote_error(e);

				if (!ranges::contains(NotSupportedErrors, errorName)) {
					LOG(("XDP File Dialog Error: %1").arg(
						QString::fromStdString(e.what())));
				}
			} catch (const std::exception &e) {
				LOG(("XDP File Dialog Error: %1").arg(
					QString::fromStdString(e.what())));
			}
		},
		std::string(kXDGDesktopPortalService));
}

// This is a patched copy of file dialog from qxdgdesktopportal theme plugin.
// It allows using XDP file dialog flexibly,
// without relying on QT_QPA_PLATFORMTHEME variable.
//
// XDP file dialog is a dialog obtained via a DBus service
// provided by the current desktop environment.
class XDPFileDialog : public QDialog {
public:
	enum ConditionType : uint {
		GlobalPattern = 0,
		MimeType = 1
	};
	// Filters a(sa(us))
	// Example: [('Images', [(0, '*.ico'), (1, 'image/png')]), ('Text', [(0, '*.txt')])]
	typedef std::tuple<uint, Glib::ustring> FilterCondition;
	typedef std::vector<FilterCondition> FilterConditionList;
	typedef std::tuple<Glib::ustring, FilterConditionList> Filter;
	typedef std::vector<Filter> FilterList;

	XDPFileDialog(
		QWidget *parent = nullptr,
		const QString &caption = QString(),
		const QString &directory = QString(),
		const QString &nameFilter = QString(),
		const QStringList &mimeTypeFilters = QStringList());
	~XDPFileDialog();

	void setVisible(bool visible) override;

	void setWindowTitle(const QString &windowTitle) {
		_windowTitle = windowTitle.toStdString();
	}
	void setAcceptLabel(const QString &acceptLabel) {
		_acceptLabel = acceptLabel.toStdString();
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

	QUrl directory() const;
	void setDirectory(const QUrl &directory);
	void selectFile(const QUrl &filename);
	QList<QUrl> selectedFiles() const;

	int exec() override;

	bool failedToOpen() const {
		return _failedToOpen;
	}

private:
	void openPortal();
	void gotResponse(
		uint response,
		const std::map<Glib::ustring, Glib::VariantBase> &results);

	void showHelper(
		Qt::WindowFlags windowFlags,
		Qt::WindowModality windowModality,
		QWindow *parent);
	void hideHelper();

	rpl::producer<> accepted();
	rpl::producer<> rejected();

	Glib::RefPtr<Gio::DBus::Connection> _dbusConnection;
	uint _requestSignalId = 0;

	// Options
	QWindow *_parent = nullptr;
	QFileDialog::Options _options;
	QFileDialog::AcceptMode _acceptMode = QFileDialog::AcceptOpen;
	QFileDialog::FileMode _fileMode = QFileDialog::ExistingFile;
	bool _modal = false;
	Glib::ustring _windowTitle = "Choose file";
	Glib::ustring _acceptLabel;
	Glib::ustring _directory;
	std::vector<Glib::ustring> _nameFilters;
	std::vector<Glib::ustring> _mimeTypesFilters;
	// maps user-visible name for portal to full name filter
	std::map<Glib::ustring, Glib::ustring> _userVisibleToNameFilter;
	Glib::ustring _selectedMimeTypeFilter;
	Glib::ustring _selectedNameFilter;
	std::vector<Glib::ustring> _selectedFiles;
	bool _failedToOpen = false;

	rpl::event_stream<> _accept;
	rpl::event_stream<> _reject;
	rpl::lifetime _lifetime;
};

XDPFileDialog::XDPFileDialog(
		QWidget *parent,
		const QString &caption,
		const QString &directory,
		const QString &nameFilter,
		const QStringList &mimeTypeFilters)
: QDialog(parent)
, _windowTitle(caption.toStdString())
, _directory(directory.toStdString())
, _nameFilters(MakeFilterList(nameFilter))
, _mimeTypesFilters(QStringListToStd(mimeTypeFilters))
, _selectedMimeTypeFilter(!_mimeTypesFilters.empty()
		? _mimeTypesFilters[0]
		: Glib::ustring())
, _selectedNameFilter(!_nameFilters.empty()
		? _nameFilters[0]
		: Glib::ustring()) {
	accepted(
	) | rpl::start_with_next([=] {
		accept();
	}, _lifetime);

	rejected(
	) | rpl::start_with_next([=] {
		reject();
	}, _lifetime);
}

XDPFileDialog::~XDPFileDialog() {
	if (_dbusConnection && _requestSignalId != 0) {
		_dbusConnection->signal_unsubscribe(_requestSignalId);
	}
}

void XDPFileDialog::openPortal() {
	std::stringstream parentWindowId;

	if (const auto integration = WaylandIntegration::Instance()) {
		if (const auto handle = integration->nativeHandle(_parent)
			; !handle.isEmpty()) {
			parentWindowId << "wayland:" << handle.toStdString();
		}
	} else if (IsX11() && _parent) {
		parentWindowId << "x11:" << std::hex << _parent->winId();
	}

	std::map<Glib::ustring, Glib::VariantBase> options;
	if (!_acceptLabel.empty()) {
		options["accept_label"] = Glib::Variant<Glib::ustring>::create(
			_acceptLabel);
	}

	options["modal"] = Glib::Variant<bool>::create(_modal);
	options["multiple"] = Glib::Variant<bool>::create(
		_fileMode == QFileDialog::ExistingFiles);

	options["directory"] = Glib::Variant<bool>::create(
		_fileMode == QFileDialog::Directory
			|| _options.testFlag(QFileDialog::ShowDirsOnly));

	if (_acceptMode == QFileDialog::AcceptSave) {
		if (!_directory.empty()) {
			options["current_folder"] = Glib::Variant<std::string>::create(
				_directory + '\0');
		}

		if (!_selectedFiles.empty()) {
			options["current_file"] = Glib::Variant<std::string>::create(
				_selectedFiles[0] + '\0');

			options["current_name"] = Glib::Variant<Glib::ustring>::create(
				Glib::path_get_basename(_selectedFiles[0]));
		}
	}

	// Insert filters
	FilterList filterList;
	auto selectedFilterIndex = filterList.size() - 1;

	_userVisibleToNameFilter.clear();

	if (!_mimeTypesFilters.empty()) {
		for (const auto &mimeTypeFilter : _mimeTypesFilters) {
			auto mimeTypeUncertain = false;
			const auto mimeType = Gio::content_type_guess(
				mimeTypeFilter,
				nullptr,
				0,
				mimeTypeUncertain);

			// Creates e.g. (1, "image/png")
			const auto filterCondition = FilterCondition{
				MimeType,
				mimeTypeFilter,
			};

			// Creates e.g. [("Images", [((1, "image/png"))])]
			filterList.push_back({
				Gio::content_type_get_description(mimeType),
				FilterConditionList{filterCondition},
			});

			if (!_selectedMimeTypeFilter.empty()
				&& _selectedMimeTypeFilter == mimeTypeFilter) {
				selectedFilterIndex = filterList.size() - 1;
			}
		}
	} else if (!_nameFilters.empty()) {
		for (const auto &nameFilter : _nameFilters) {
			// Do parsing:
			// Supported format is ("Images (*.png *.jpg)")
			const auto regexp = Glib::Regex::create(filterRegExp);

			Glib::MatchInfo match;
			regexp->match(nameFilter, match);

			if (match.matches()) {
				const auto userVisibleName = match.fetch(1);
				const auto filterStrings = Glib::Regex::create(" ")->split(
					match.fetch(2),
					Glib::RegexMatchFlags::REGEX_MATCH_NOTEMPTY);

				if (filterStrings.empty()) {
					LOG((
						"XDP File Dialog Error: "
						"Filter %1 is empty and will be ignored.")
						.arg(QString::fromStdString(userVisibleName)));
					continue;
				}

				FilterConditionList filterConditions;
				for (const auto &filterString : filterStrings) {
					filterConditions.push_back({
						GlobalPattern,
						filterString,
					});
				}

				filterList.push_back({
					userVisibleName,
					filterConditions,
				});

				_userVisibleToNameFilter[userVisibleName] = nameFilter;

				if (!_selectedNameFilter.empty()
					&& _selectedNameFilter == nameFilter) {
					selectedFilterIndex = filterList.size() - 1;
				}
			}
		}
	}

	if (!filterList.empty()) {
		options["filters"] = Glib::Variant<FilterList>::create(filterList);
	}

	if (selectedFilterIndex != -1) {
		options["current_filter"] = Glib::Variant<Filter>::create(
			filterList[selectedFilterIndex]);
	}

	const auto handleToken = Glib::ustring("tdesktop")
		+ std::to_string(openssl::RandomValue<uint>());

	options["handle_token"] = Glib::Variant<Glib::ustring>::create(
		handleToken);

	// TODO choices a(ssa(ss)s)
	// List of serialized combo boxes to add to the file chooser.

	try {
		_dbusConnection = Gio::DBus::Connection::get_sync(
			Gio::DBus::BusType::BUS_TYPE_SESSION);

		auto uniqueName = _dbusConnection->get_unique_name();
		uniqueName.erase(0, 1);
		uniqueName.replace(uniqueName.find('.'), 1, 1, '_');

		const auto requestPath = Glib::ustring(
				"/org/freedesktop/portal/desktop/request/")
			+ uniqueName
			+ '/'
			+ handleToken;

		const auto responseCallback = crl::guard(this, [=](
			const Glib::RefPtr<Gio::DBus::Connection> &connection,
			const Glib::ustring &sender_name,
			const Glib::ustring &object_path,
			const Glib::ustring &interface_name,
			const Glib::ustring &signal_name,
			const Glib::VariantContainerBase &parameters) {
			try {
				auto parametersCopy = parameters;

				const auto response = base::Platform::GlibVariantCast<uint>(
					parametersCopy.get_child(0));

				const auto results = base::Platform::GlibVariantCast<
					std::map<
						Glib::ustring,
						Glib::VariantBase
					>>(parametersCopy.get_child(1));

				gotResponse(response, results);
			} catch (const std::exception &e) {
				LOG(("XDP File Dialog Error: %1").arg(
					QString::fromStdString(e.what())));

				_reject.fire({});
			}
		});

		_requestSignalId = _dbusConnection->signal_subscribe(
			responseCallback,
			{},
			"org.freedesktop.portal.Request",
			"Response",
			requestPath);

		_dbusConnection->call(
			std::string(kXDGDesktopPortalObjectPath),
			std::string(kXDGDesktopPortalFileChooserInterface),
			_acceptMode == QFileDialog::AcceptSave
				? "SaveFile"
				: "OpenFile",
			base::Platform::MakeGlibVariant(std::tuple{
				Glib::ustring(parentWindowId.str()),
				_windowTitle,
				options,
			}),
			crl::guard(this, [=](const Glib::RefPtr<Gio::AsyncResult> &result) {
				try {
					auto reply = _dbusConnection->call_finish(result);

					const auto handle = base::Platform::GlibVariantCast<
						Glib::ustring>(reply.get_child(0));

					if (handle != requestPath) {
						_dbusConnection->signal_unsubscribe(
							_requestSignalId);

						_requestSignalId = _dbusConnection->signal_subscribe(
							responseCallback,
							{},
							"org.freedesktop.portal.Request",
							"Response",
							handle);
					}
				} catch (const Glib::Error &e) {
					static const auto NotSupportedErrors = {
						"org.freedesktop.DBus.Error.ServiceUnknown",
					};

					const auto errorName =
						Gio::DBus::ErrorUtils::get_remote_error(e);

					if (!ranges::contains(NotSupportedErrors, errorName)) {
						LOG(("XDP File Dialog Error: %1").arg(
							QString::fromStdString(e.what())));
					}

					crl::on_main([=] {
						_failedToOpen = true;
						_reject.fire({});
					});
				} catch (const std::exception &e) {
					LOG(("XDP File Dialog Error: %1").arg(
						QString::fromStdString(e.what())));

					crl::on_main([=] {
						_failedToOpen = true;
						_reject.fire({});
					});
				}
			}),
			std::string(kXDGDesktopPortalService));
	} catch (...) {
		_failedToOpen = true;
		_reject.fire({});
	}
}

void XDPFileDialog::setDirectory(const QUrl &directory) {
	_directory = directory.path().toStdString();
}

QUrl XDPFileDialog::directory() const {
	return QString::fromStdString(_directory);
}

void XDPFileDialog::selectFile(const QUrl &filename) {
	_selectedFiles.push_back(filename.path().toStdString());
}

QList<QUrl> XDPFileDialog::selectedFiles() const {
	QList<QUrl> files;
	ranges::transform(
		_selectedFiles,
		ranges::back_inserter(files),
		[](const Glib::ustring &string) {
			return QUrl(QString::fromStdString(string));
		});
	return files;
}

int XDPFileDialog::exec() {
	setAttribute(Qt::WA_DeleteOnClose, false);

	bool wasShowModal = testAttribute(Qt::WA_ShowModal);
	setAttribute(Qt::WA_ShowModal, true);
	setResult(0);

	show();
	if (failedToOpen()) {
		return result();
	}

	QPointer<QDialog> guard = this;

	// HACK we have to avoid returning until we emit
	// that the dialog was accepted or rejected
	const auto loop = Glib::MainLoop::create();
	rpl::lifetime lifetime;

	accepted(
	) | rpl::start_with_next([&] {
		loop->quit();
	}, lifetime);

	rejected(
	) | rpl::start_with_next([&] {
		loop->quit();
	}, lifetime);

	loop->run();

	if (guard.isNull()) {
		return QDialog::Rejected;
	}

	setAttribute(Qt::WA_ShowModal, wasShowModal);

	return result();
}

void XDPFileDialog::setVisible(bool visible) {
	if (visible) {
		if (testAttribute(Qt::WA_WState_ExplicitShowHide)
			&& !testAttribute(Qt::WA_WState_Hidden)) {
			return;
		}
	} else if (testAttribute(Qt::WA_WState_ExplicitShowHide)
		&& testAttribute(Qt::WA_WState_Hidden)) {
		return;
	}

	if (visible) {
		showHelper(
			windowFlags(),
			windowModality(),
			parentWidget()
				? parentWidget()->windowHandle()
				: nullptr);
	} else {
		hideHelper();
	}

	// Set WA_DontShowOnScreen so that QDialog::setVisible(visible) below
	// updates the state correctly, but skips showing the non-native version:
	setAttribute(Qt::WA_DontShowOnScreen);

	QDialog::setVisible(visible);
}

void XDPFileDialog::hideHelper() {
}

void XDPFileDialog::showHelper(
		Qt::WindowFlags windowFlags,
		Qt::WindowModality windowModality,
		QWindow *parent) {
	_modal = windowModality != Qt::NonModal;
	_parent = parent;

	openPortal();
}

void XDPFileDialog::gotResponse(
		uint response,
		const std::map<Glib::ustring, Glib::VariantBase> &results) {
	try {
		if (!response) {
			if (const auto i = results.find("uris"); i != end(results)) {
				_selectedFiles = base::Platform::GlibVariantCast<
					std::vector<Glib::ustring>>(i->second);

				_directory = _selectedFiles.empty()
					? Glib::ustring()
					: Glib::ustring(
						Glib::path_get_dirname(_selectedFiles.back()));
			}

			if (const auto i = results.find("current_filter");
				i != end(results)) {
				auto selectedFilter = base::Platform::GlibVariantCast<
					Filter>(i->second);

				if (!std::get<1>(selectedFilter).empty()
					&& std::get<0>(
						std::get<1>(selectedFilter)[0]) == MimeType) {
					// s.a. XDPFileDialog::openPortal
					// which basically does the inverse
					_selectedMimeTypeFilter = std::get<1>(
						std::get<1>(selectedFilter)[0]);
					_selectedNameFilter.clear();
				} else {
					_selectedNameFilter =
						_userVisibleToNameFilter[std::get<0>(selectedFilter)];
					_selectedMimeTypeFilter.clear();
				}
			}

			_accept.fire({});
		} else {
			_reject.fire({});
		}
	} catch (const std::exception &e) {
		LOG(("XDP File Dialog Error: %1").arg(
			QString::fromStdString(e.what())));

		_reject.fire({});
	}
}

rpl::producer<> XDPFileDialog::accepted() {
	return _accept.events();
}

rpl::producer<> XDPFileDialog::rejected() {
	return _reject.events();
}

} // namespace

void Start() {
	ComputeFileChooserPortalVersion();
}

std::optional<bool> Get(
		QPointer<QWidget> parent,
		QStringList &files,
		QByteArray &remoteContent,
		const QString &caption,
		const QString &filter,
		Type type,
		QString startFile) {
	if (!FileChooserPortalVersion.has_value()
		|| (type == Type::ReadFolder && *FileChooserPortalVersion < 3)) {
		return std::nullopt;
	}

	static const auto docRegExp = QRegularExpression("^/run/user/\\d+/doc");
	if (cDialogLastPath().isEmpty()
		|| cDialogLastPath().contains(docRegExp)) {
		InitLastPath();
	}

	XDPFileDialog dialog(parent, caption, QString(), filter, QStringList());

	dialog.setModal(true);
	if (type == Type::ReadFile || type == Type::ReadFiles) {
		dialog.setFileMode((type == Type::ReadFiles)
			? QFileDialog::ExistingFiles
			: QFileDialog::ExistingFile);
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
	dialog.setDirectory(QFileInfo(startFile).absoluteDir().absolutePath());
	dialog.selectFile(startFile);

	const auto res = dialog.exec();
	if (dialog.failedToOpen()) {
		return std::nullopt;
	}

	if (type != Type::ReadFolder) {
		// Save last used directory for all queries except directory choosing.
		const auto path = dialog.directory().path();
		if (!path.isEmpty()
			&& !path.contains(docRegExp)
			&& path != cDialogLastPath()) {
			cSetDialogLastPath(path);
			Local::writeSettings();
		}
	}

	if (res == QDialog::Accepted) {
		QStringList selectedFilesStrings;
		ranges::transform(
			dialog.selectedFiles(),
			ranges::back_inserter(selectedFilesStrings),
			[](const QUrl &url) { return url.path(); });

		if (type == Type::ReadFiles) {
			files = selectedFilesStrings;
		} else {
			files = selectedFilesStrings.mid(0, 1);
		}
		return true;
	}

	files = QStringList();
	remoteContent = QByteArray();
	return false;
}

} // namespace XDP
} // namespace FileDialog
} // namespace Platform
