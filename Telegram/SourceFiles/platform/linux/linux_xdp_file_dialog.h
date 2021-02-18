/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/file_utilities.h"

#include <QFileDialog>
#include <QVector>

namespace Platform {
namespace FileDialog {
namespace XDP {

class XDPFileDialogPrivate;
using Type = ::FileDialog::internal::Type;

bool Use(Type type = Type::ReadFile);
bool Get(
	QPointer<QWidget> parent,
	QStringList &files,
	QByteArray &remoteContent,
	const QString &caption,
	const QString &filter,
	Type type,
	QString startFile);

// This is a patched copy of file dialog from qxdgdesktopportal theme plugin.
// It allows using XDP file dialog flexibly,
// without relying on QT_QPA_PLATFORMTHEME variable.
//
// XDP file dialog is a dialog obtained via a DBus service
// provided by the current desktop environment.
class XDPFileDialog : public QDialog {
	Q_OBJECT
	Q_DECLARE_PRIVATE(XDPFileDialog)
public:
	enum ConditionType : uint {
		GlobalPattern = 0,
		MimeType = 1
	};
	// Filters a(sa(us))
	// Example: [('Images', [(0, '*.ico'), (1, 'image/png')]), ('Text', [(0, '*.txt')])]
	struct FilterCondition {
		ConditionType type;
		QString pattern; // E.g. '*ico' or 'image/png'
	};
	typedef QVector<FilterCondition> FilterConditionList;

	struct Filter {
		QString name; // E.g. 'Images' or 'Text
		FilterConditionList filterConditions;; // E.g. [(0, '*.ico'), (1, 'image/png')] or [(0, '*.txt')]
	};
	typedef QVector<Filter> FilterList;

	XDPFileDialog(
		QWidget *parent = nullptr,
		const QString &caption = QString(),
		const QString &directory = QString(),
		const QString &filter = QString());
	~XDPFileDialog();

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

	bool defaultNameFilterDisables() const;
	QUrl directory() const;
	void setDirectory(const QUrl &directory);
	void selectFile(const QUrl &filename);
	QList<QUrl> selectedFiles() const;
	void setFilter();
	void selectNameFilter(const QString &filter);
	QString selectedNameFilter() const;
	void selectMimeTypeFilter(const QString &filter);
	QString selectedMimeTypeFilter() const;

	int exec() override;

private Q_SLOTS:
	void gotResponse(uint response, const QVariantMap &results);

private:
	void initializeDialog();
	void openPortal();

	void showHelper(Qt::WindowFlags windowFlags, Qt::WindowModality windowModality, QWindow *parent);
	void hideHelper();

	rpl::producer<> accepted();
	rpl::producer<> rejected();

	QScopedPointer<XDPFileDialogPrivate> d_ptr;

	// Options
	QFileDialog::Options _options;
	QString _windowTitle = "Choose file";
	QString _initialDirectory;
	QStringList _initialFiles;
	QStringList _nameFilters;
	QFileDialog::AcceptMode _acceptMode = QFileDialog::AcceptOpen;
	QFileDialog::FileMode _fileMode = QFileDialog::ExistingFile;

	rpl::event_stream<> _accept;
	rpl::event_stream<> _reject;
	rpl::lifetime _lifetime;
};

} // namespace XDP
} // namespace FileDialog
} // namespace Platform

Q_DECLARE_METATYPE(Platform::FileDialog::XDP::XDPFileDialog::FilterCondition);
Q_DECLARE_METATYPE(Platform::FileDialog::XDP::XDPFileDialog::FilterConditionList);
Q_DECLARE_METATYPE(Platform::FileDialog::XDP::XDPFileDialog::Filter);
Q_DECLARE_METATYPE(Platform::FileDialog::XDP::XDPFileDialog::FilterList);

