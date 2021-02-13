/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/linux_xdp_file_dialog.h"

#include "platform/platform_file_utilities.h"
#include "platform/linux/specific_linux.h"
#include "storage/localstorage.h"
#include "base/qt_adapters.h"

#include <QtCore/qeventloop.h>

#include <QtDBus/QtDBus>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

#include <QFile>
#include <QMetaType>
#include <QMimeType>
#include <QMimeDatabase>
#include <QRandomGenerator>
#include <QWindow>

namespace Platform {
namespace FileDialog {
namespace XDP {
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

} // namespace

bool Use(Type type) {
	return UseXDGDesktopPortal()
		&& (type != Type::ReadFolder || CanOpenDirectoryWithPortal());
}

bool Get(
		QPointer<QWidget> parent,
		QStringList &files,
		QByteArray &remoteContent,
		const QString &caption,
		const QString &filter,
		Type type,
		QString startFile) {
	static const auto docRegExp = QRegularExpression("^/run/user/\\d+/doc");
	if (cDialogLastPath().isEmpty()
		|| cDialogLastPath().contains(docRegExp)) {
		InitLastPath();
	}

	XDPFileDialog dialog(parent, caption, QString(), filter);

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
	dialog.setDirectory(QFileInfo(startFile).absoluteDir().absolutePath());
	dialog.selectFile(startFile);

	int res = dialog.exec();

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

		QString path = files.isEmpty()
			? QString()
			: QFileInfo(files.back()).absoluteDir().absolutePath();

		if (!path.isEmpty()
			&& !path.contains(docRegExp)
			&& path != cDialogLastPath()) {
			cSetDialogLastPath(path);
			Local::writeSettings();
		}

		return true;
	}

	files = QStringList();
	remoteContent = QByteArray();
	return false;
}

QDBusArgument &operator <<(QDBusArgument &arg, const XDPFileDialog::FilterCondition &filterCondition) {
	arg.beginStructure();
	arg << filterCondition.type << filterCondition.pattern;
	arg.endStructure();
	return arg;
}

const QDBusArgument &operator >>(const QDBusArgument &arg, XDPFileDialog::FilterCondition &filterCondition) {
	uint type;
	QString filterPattern;
	arg.beginStructure();
	arg >> type >> filterPattern;
	filterCondition.type = (XDPFileDialog::ConditionType)type;
	filterCondition.pattern = filterPattern;
	arg.endStructure();

	return arg;
}

QDBusArgument &operator <<(QDBusArgument &arg, const XDPFileDialog::Filter filter) {
	arg.beginStructure();
	arg << filter.name << filter.filterConditions;
	arg.endStructure();
	return arg;
}

const QDBusArgument &operator >>(const QDBusArgument &arg, XDPFileDialog::Filter &filter) {
	QString name;
	XDPFileDialog::FilterConditionList filterConditions;
	arg.beginStructure();
	arg >> name >> filterConditions;
	filter.name = name;
	filter.filterConditions = filterConditions;
	arg.endStructure();

	return arg;
}

class XDPFileDialogPrivate {
public:
	XDPFileDialogPrivate() {
	}

	WId winId = 0;
	bool directoryMode = false;
	bool modal = false;
	bool multipleFiles = false;
	bool saveFile = false;
	QString acceptLabel;
	QString directory;
	QString title;
	QStringList nameFilters;
	QStringList mimeTypesFilters;
	// maps user-visible name for portal to full name filter
	QMap<QString, QString> userVisibleToNameFilter;
	QString selectedMimeTypeFilter;
	QString selectedNameFilter;
	QStringList selectedFiles;
};

XDPFileDialog::XDPFileDialog(QWidget *parent, const QString &caption, const QString &directory, const QString &filter)
: QDialog(parent)
, d_ptr(new XDPFileDialogPrivate())
, _windowTitle(caption)
, _initialDirectory(directory) {
	Q_D(XDPFileDialog);

	auto filters = makeFilterList(filter);
	const int numFilters = filters.count();
	_nameFilters.reserve(numFilters);
	for (int i = 0; i < numFilters; ++i) {
		_nameFilters << filters[i].simplified();
	}

	accepted(
	) | rpl::start_with_next([=] {
		Q_EMIT accept();
	}, _lifetime);

	rejected(
	) | rpl::start_with_next([=] {
		Q_EMIT reject();
	}, _lifetime);
}

XDPFileDialog::~XDPFileDialog() {
}

void XDPFileDialog::initializeDialog() {
	Q_D(XDPFileDialog);

	if (_fileMode == QFileDialog::ExistingFiles)
		d->multipleFiles = true;

	if (_fileMode == QFileDialog::Directory || _options.testFlag(QFileDialog::ShowDirsOnly))
		d->directoryMode = true;

#if 0 // it is commented in GtkFileDialog for some reason, do the same
	if (options()->isLabelExplicitlySet(QFileDialog::Accept))
		d->acceptLabel = options()->labelText(QFileDialog::Accept);
#endif

	if (!_windowTitle.isEmpty())
		d->title = _windowTitle;

	if (_acceptMode == QFileDialog::AcceptSave)
		d->saveFile = true;

	if (!_nameFilters.isEmpty())
		d->nameFilters = _nameFilters;

#if 0 // what is the right way to implement this?
	if (!options()->mimeTypeFilters().isEmpty())
		d->mimeTypesFilters = options()->mimeTypeFilters();

	if (!options()->initiallySelectedMimeTypeFilter().isEmpty())
		d->selectedMimeTypeFilter = options()->initiallySelectedMimeTypeFilter();
#endif

	const auto initialNameFilter = _nameFilters.isEmpty() ? QString() : _nameFilters.front();
	if (!initialNameFilter.isEmpty())
		d->selectedNameFilter = initialNameFilter;

	setDirectory(_initialDirectory);
}

void XDPFileDialog::openPortal() {
	Q_D(XDPFileDialog);

	QDBusMessage message = QDBusMessage::createMethodCall(
		QLatin1String("org.freedesktop.portal.Desktop"),
		QLatin1String("/org/freedesktop/portal/desktop"),
		QLatin1String("org.freedesktop.portal.FileChooser"),
		d->saveFile ? QLatin1String("SaveFile") : QLatin1String("OpenFile"));
	QString parentWindowId = QLatin1String("x11:") + QString::number(d->winId, 16);

	QVariantMap options;
	if (!d->acceptLabel.isEmpty())
		options.insert(QLatin1String("accept_label"), d->acceptLabel);

	options.insert(QLatin1String("modal"), d->modal);
	options.insert(QLatin1String("multiple"), d->multipleFiles);
	options.insert(QLatin1String("directory"), d->directoryMode);

	if (d->saveFile) {
		if (!d->directory.isEmpty())
			options.insert(QLatin1String("current_folder"), QFile::encodeName(d->directory).append('\0'));

		if (!d->selectedFiles.isEmpty())
			options.insert(QLatin1String("current_file"), QFile::encodeName(d->selectedFiles.first()).append('\0'));
	}

	// Insert filters
	qDBusRegisterMetaType<FilterCondition>();
	qDBusRegisterMetaType<FilterConditionList>();
	qDBusRegisterMetaType<Filter>();
	qDBusRegisterMetaType<FilterList>();

	FilterList filterList;
	auto selectedFilterIndex = filterList.size() - 1;

	d->userVisibleToNameFilter.clear();

	if (!d->mimeTypesFilters.isEmpty()) {
		for (const QString &mimeTypefilter : d->mimeTypesFilters) {
			QMimeDatabase mimeDatabase;
			QMimeType mimeType = mimeDatabase.mimeTypeForName(mimeTypefilter);

			// Creates e.g. (1, "image/png")
			FilterCondition filterCondition;
			filterCondition.type = MimeType;
			filterCondition.pattern = mimeTypefilter;

			// Creates e.g. [((1, "image/png"))]
			FilterConditionList filterConditions;
			filterConditions << filterCondition;

			// Creates e.g. [("Images", [((1, "image/png"))])]
			Filter filter;
			filter.name = mimeType.comment();
			filter.filterConditions = filterConditions;

			filterList << filter;

			if (!d->selectedMimeTypeFilter.isEmpty() && d->selectedMimeTypeFilter == mimeTypefilter)
				selectedFilterIndex = filterList.size() - 1;
		}
	} else if (!d->nameFilters.isEmpty()) {
		for (const QString &nameFilter : d->nameFilters) {
			// Do parsing:
			// Supported format is ("Images (*.png *.jpg)")
			QRegularExpression regexp(QString::fromLatin1(filterRegExp));
			QRegularExpressionMatch match = regexp.match(nameFilter);
			if (match.hasMatch()) {
				QString userVisibleName = match.captured(1);
				QStringList filterStrings = match.captured(2).split(QLatin1Char(' '), base::QStringSkipEmptyParts);

				if (filterStrings.isEmpty()) {
					LOG(("XDP File Dialog Error: Filter %1 is empty and will be ignored.").arg(userVisibleName));
					continue;
				}

				FilterConditionList filterConditions;
				for (const QString &filterString : filterStrings) {
					FilterCondition filterCondition;
					filterCondition.type = GlobalPattern;
					filterCondition.pattern = filterString;
					filterConditions << filterCondition;
				}

				Filter filter;
				filter.name = userVisibleName;
				filter.filterConditions = filterConditions;

				filterList << filter;

				d->userVisibleToNameFilter.insert(userVisibleName, nameFilter);

				if (!d->selectedNameFilter.isEmpty() && d->selectedNameFilter == nameFilter)
					selectedFilterIndex = filterList.size() - 1;
			}
		}
	}

	if (!filterList.isEmpty())
		options.insert(QLatin1String("filters"), QVariant::fromValue(filterList));

	if (selectedFilterIndex != -1)
		options.insert(QLatin1String("current_filter"), QVariant::fromValue(filterList[selectedFilterIndex]));

	options.insert(QLatin1String("handle_token"), QStringLiteral("qt%1").arg(QRandomGenerator::global()->generate()));

	// TODO choices a(ssa(ss)s)
	// List of serialized combo boxes to add to the file chooser.

	message << parentWindowId << d->title << options;

	QDBusPendingCall pendingCall = QDBusConnection::sessionBus().asyncCall(message);
	QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(pendingCall);
	connect(watcher, &QDBusPendingCallWatcher::finished, this, [this] (QDBusPendingCallWatcher *watcher) {
		QDBusPendingReply<QDBusObjectPath> reply = *watcher;
		if (reply.isError()) {
			_reject.fire({});
		} else {
			QDBusConnection::sessionBus().connect(
				nullptr,
				reply.value().path(),
				QLatin1String("org.freedesktop.portal.Request"),
				QLatin1String("Response"),
				this,
				SLOT(gotResponse(uint,QVariantMap)));
		}
	});
}

bool XDPFileDialog::defaultNameFilterDisables() const {
	return false;
}

void XDPFileDialog::setDirectory(const QUrl &directory) {
	Q_D(XDPFileDialog);

	d->directory = directory.path();
}

QUrl XDPFileDialog::directory() const {
	Q_D(const XDPFileDialog);

	return d->directory;
}

void XDPFileDialog::selectFile(const QUrl &filename) {
	Q_D(XDPFileDialog);

	d->selectedFiles << filename.path();
}

QList<QUrl> XDPFileDialog::selectedFiles() const {
	Q_D(const XDPFileDialog);

	QList<QUrl> files;
	for (const QString &file : d->selectedFiles) {
		files << QUrl(file);
	}
	return files;
}

void XDPFileDialog::setFilter() {
	Q_D(XDPFileDialog);
}

void XDPFileDialog::selectMimeTypeFilter(const QString &filter) {
	Q_D(XDPFileDialog);
}

QString XDPFileDialog::selectedMimeTypeFilter() const {
	Q_D(const XDPFileDialog);
	return d->selectedMimeTypeFilter;
}

void XDPFileDialog::selectNameFilter(const QString &filter) {
	Q_D(XDPFileDialog);
}

QString XDPFileDialog::selectedNameFilter() const {
	Q_D(const XDPFileDialog);
	return d->selectedNameFilter;
}

int XDPFileDialog::exec() {
	Q_D(XDPFileDialog);

	bool deleteOnClose = testAttribute(Qt::WA_DeleteOnClose);
	setAttribute(Qt::WA_DeleteOnClose, false);

	bool wasShowModal = testAttribute(Qt::WA_ShowModal);
	setAttribute(Qt::WA_ShowModal, true);
	setResult(0);

	show();

	QPointer<QDialog> guard = this;

	// HACK we have to avoid returning until we emit that the dialog was accepted or rejected
	QEventLoop loop;
	rpl::lifetime lifetime;

	accepted(
	) | rpl::start_with_next([&] {
		loop.quit();
	}, lifetime);

	rejected(
	) | rpl::start_with_next([&] {
		loop.quit();
	}, lifetime);

	loop.exec();

	if (guard.isNull())
		return QDialog::Rejected;

	setAttribute(Qt::WA_ShowModal, wasShowModal);

	return result();
}

void XDPFileDialog::setVisible(bool visible) {
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

void XDPFileDialog::hideHelper() {
	Q_D(XDPFileDialog);
}

void XDPFileDialog::showHelper(Qt::WindowFlags windowFlags, Qt::WindowModality windowModality, QWindow *parent) {
	Q_D(XDPFileDialog);

	initializeDialog();

	d->modal = windowModality != Qt::NonModal;
	d->winId = parent ? parent->winId() : 0;

	openPortal();
}

void XDPFileDialog::gotResponse(uint response, const QVariantMap &results) {
	Q_D(XDPFileDialog);

	if (!response) {
		if (results.contains(QLatin1String("uris")))
			d->selectedFiles = results.value(QLatin1String("uris")).toStringList();

		if (results.contains(QLatin1String("current_filter"))) {
			const Filter selectedFilter = qdbus_cast<Filter>(results.value(QStringLiteral("current_filter")));
			if (!selectedFilter.filterConditions.empty() && selectedFilter.filterConditions[0].type == MimeType) {
				// s.a. XDPFileDialog::openPortal which basically does the inverse
				d->selectedMimeTypeFilter = selectedFilter.filterConditions[0].pattern;
				d->selectedNameFilter.clear();
			} else {
				d->selectedNameFilter = d->userVisibleToNameFilter.value(selectedFilter.name);
				d->selectedMimeTypeFilter.clear();
			}
		}
		_accept.fire({});
	} else {
		_reject.fire({});
	}
}

rpl::producer<> XDPFileDialog::accepted() {
	return _accept.events();
}

rpl::producer<> XDPFileDialog::rejected() {
	return _reject.events();
}

} // namespace XDP
} // namespace FileDialog
} // namespace Platform
