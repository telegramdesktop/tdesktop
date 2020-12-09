/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_file_utilities.h"

#include <QtGui/QWindow>
#include <QtWidgets/QFileDialog>

#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION
typedef struct _GtkWidget GtkWidget;
typedef struct _GtkDialog GtkDialog;
typedef struct _GtkFileFilter GtkFileFilter;
#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION

namespace Platform {
namespace File {
namespace internal {

QByteArray EscapeShell(const QByteArray &content);

} // namespace internal

inline QString UrlToLocal(const QUrl &url) {
	return ::File::internal::UrlToLocalDefault(url);
}

inline bool UnsafeShowOpenWithDropdown(const QString &filepath, QPoint menuPosition) {
	return false;
}

inline void PostprocessDownloaded(const QString &filepath) {
}

} // namespace File

namespace FileDialog {

inline void InitLastPath() {
	::FileDialog::internal::InitLastPathDefault();
}

namespace internal {
#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION

// This is a patched copy of qgtk2 theme plugin.
// We need to use our own gtk file dialog instead of
// styling Qt file dialog, because Qt only works with gtk2.
// We need to be able to work with gtk2 and gtk3, because
// we use gtk3 to work with appindicator3.
class QGtkDialog : public QWindow {
	Q_OBJECT

public:
	QGtkDialog(GtkWidget *gtkWidget);
	~QGtkDialog();

	GtkDialog *gtkDialog() const;

	void exec();
	void show(Qt::WindowFlags flags, Qt::WindowModality modality, QWindow *parent);
	void hide();

signals:
	void accept();
	void reject();

protected:
	static void onResponse(QGtkDialog *dialog, int response);
	static void onUpdatePreview(QGtkDialog *dialog);

private slots:
	void onParentWindowDestroyed();

private:
	GtkWidget *gtkWidget;
	GtkWidget *_preview = nullptr;

};

class GtkFileDialog : public QDialog {
	Q_OBJECT

public:
	GtkFileDialog(QWidget *parent = Q_NULLPTR,
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

private slots:
	void onAccepted();
	void onRejected();

private:
	static void onSelectionChanged(GtkDialog *dialog, GtkFileDialog *helper);
	static void onCurrentFolderChanged(GtkFileDialog *helper);
	void applyOptions();
	void setNameFilters(const QStringList &filters);

	void showHelper(Qt::WindowFlags flags, Qt::WindowModality modality, QWindow *parent);
	void hideHelper();

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
};
#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION

} // namespace internal
} // namespace FileDialog
} // namespace Platform
