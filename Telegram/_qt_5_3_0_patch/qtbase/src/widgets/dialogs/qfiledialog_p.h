/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the QtWidgets module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef QFILEDIALOG_P_H
#define QFILEDIALOG_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

#include <QtCore/qglobal.h>

#ifndef QT_NO_FILEDIALOG

#include "qfiledialog.h"
#include "private/qdialog_p.h"
#include "qplatformdefs.h"

#include "qfilesystemmodel_p.h"
#include <qlistview.h>
#include <qtreeview.h>
#include <qcombobox.h>
#include <qtoolbutton.h>
#include <qlabel.h>
#include <qevent.h>
#include <qlineedit.h>
#include <qurl.h>
#include <qstackedwidget.h>
#include <qdialogbuttonbox.h>
#include <qabstractproxymodel.h>
#include <qcompleter.h>
#include <qpointer.h>
#include <qdebug.h>
#include "qsidebar_p.h"
#include "qfscompleter_p.h"

#if defined (Q_OS_UNIX)
#include <unistd.h>
#endif

QT_BEGIN_NAMESPACE

class QFileDialogListView;
class QFileDialogTreeView;
class QFileDialogLineEdit;
class QGridLayout;
class QCompleter;
class QHBoxLayout;
class Ui_QFileDialog;
class QPlatformDialogHelper;

struct QFileDialogArgs
{
    QFileDialogArgs() : parent(0), mode(QFileDialog::AnyFile) {}

    QWidget *parent;
    QString caption;
    QString directory;
    QString selection;
    QString filter;
    QFileDialog::FileMode mode;
    QFileDialog::Options options;
};

#define UrlRole (Qt::UserRole + 1)

class Q_WIDGETS_EXPORT QFileDialogPrivate : public QDialogPrivate
{
    Q_DECLARE_PUBLIC(QFileDialog)

public:
    QFileDialogPrivate();

    QPlatformFileDialogHelper *platformFileDialogHelper() const
        { return static_cast<QPlatformFileDialogHelper *>(platformHelper()); }

    void createToolButtons();
    void createMenuActions();
    void createWidgets();

    void init(const QString &directory = QString(), const QString &nameFilter = QString(),
              const QString &caption = QString());
    bool itemViewKeyboardEvent(QKeyEvent *event);
    QString getEnvironmentVariable(const QString &string);
    static QString workingDirectory(const QString &path);
    static QString initialSelection(const QString &path);
    QStringList typedFiles() const;
    QList<QUrl> userSelectedFiles() const;
	QByteArray userSelectedRemoteContent() const;
    QStringList addDefaultSuffixToFiles(const QStringList filesToFix) const;
    QList<QUrl> addDefaultSuffixToUrls(const QList<QUrl> &urlsToFix) const;
    bool removeDirectory(const QString &path);
    void setLabelTextControl(QFileDialog::DialogLabel label, const QString &text);
    inline void updateLookInLabel();
    inline void updateFileNameLabel();
    inline void updateFileTypeLabel();
    void updateOkButtonText(bool saveAsOnFolder = false);
    void updateCancelButtonText();

    inline QModelIndex mapToSource(const QModelIndex &index) const;
    inline QModelIndex mapFromSource(const QModelIndex &index) const;
    inline QModelIndex rootIndex() const;
    inline void setRootIndex(const QModelIndex &index) const;
    inline QModelIndex select(const QModelIndex &index) const;
    inline QString rootPath() const;

    QLineEdit *lineEdit() const;

    static int maxNameLength(const QString &path);

    QString basename(const QString &path) const
    {
        int separator = QDir::toNativeSeparators(path).lastIndexOf(QDir::separator());
        if (separator != -1)
            return path.mid(separator + 1);
        return path;
    }

    QDir::Filters filterForMode(QDir::Filters filters) const
    {
        const QFileDialog::FileMode fileMode = q_func()->fileMode();
        if (fileMode == QFileDialog::DirectoryOnly) {
            filters |= QDir::Drives | QDir::AllDirs | QDir::Dirs;
            filters &= ~QDir::Files;
        } else {
            filters |= QDir::Drives | QDir::AllDirs | QDir::Files | QDir::Dirs;
        }
        return filters;
    }

    QAbstractItemView *currentView() const;

    static inline QString toInternal(const QString &path)
    {
#if defined(Q_OS_WIN)
        QString n(path);
        n.replace(QLatin1Char('\\'), QLatin1Char('/'));
#if defined(Q_OS_WINCE)
        if ((n.size() > 1) && (n.startsWith(QLatin1String("//"))))
            n = n.mid(1);
#endif
        return n;
#else // the compile should optimize away this
        return path;
#endif
    }

    void setLastVisitedDirectory(const QString &dir);
    void retranslateWindowTitle();
    void retranslateStrings();
    void emitFilesSelected(const QStringList &files);

    void _q_goHome();
    void _q_pathChanged(const QString &);
    void _q_navigateBackward();
    void _q_navigateForward();
    void _q_navigateToParent();
    void _q_createDirectory();
    void _q_showListView();
    void _q_showDetailsView();
    void _q_showContextMenu(const QPoint &position);
    void _q_renameCurrent();
    void _q_deleteCurrent();
    void _q_showHidden();
    void _q_showHeader(QAction *);
    void _q_updateOkButton();
    void _q_currentChanged(const QModelIndex &index);
    void _q_enterDirectory(const QModelIndex &index);
    void _q_nativeFileSelected(const QUrl &file);
    void _q_nativeFilesSelected(const QList<QUrl> &files);
    void _q_nativeCurrentChanged(const QUrl &file);
    void _q_nativeEnterDirectory(const QUrl &directory);
    void _q_goToDirectory(const QString &);
    void _q_useNameFilter(int index);
    void _q_selectionChanged();
    void _q_goToUrl(const QUrl &url);
    void _q_autoCompleteFileName(const QString &);
    void _q_rowsInserted(const QModelIndex & parent);
    void _q_fileRenamed(const QString &path, const QString oldName, const QString newName);

    // layout
#ifndef QT_NO_PROXYMODEL
    QAbstractProxyModel *proxyModel;
#endif

    // data
    QStringList watching;
    QFileSystemModel *model;

#ifndef QT_NO_FSCOMPLETER
    QFSCompleter *completer;
#endif //QT_NO_FSCOMPLETER

    QString setWindowTitle;

    QStringList currentHistory;
    int currentHistoryLocation;

    QAction *renameAction;
    QAction *deleteAction;
    QAction *showHiddenAction;
    QAction *newFolderAction;

    bool useDefaultCaption;
    bool defaultFileTypes;

    // setVisible_sys returns true if it ends up showing a native
    // dialog. Returning false means that a non-native dialog must be
    // used instead.
    bool canBeNativeDialog() const;
    inline bool usingWidgets() const;

    void setDirectory_sys(const QUrl &directory);
    QUrl directory_sys() const;
    void selectFile_sys(const QUrl &filename);
    QList<QUrl> selectedFiles_sys() const;
	QByteArray selectedRemoteContent_sys() const;
    void setFilter_sys();
    void selectNameFilter_sys(const QString &filter);
    QString selectedNameFilter_sys() const;
    //////////////////////////////////////////////

    QScopedPointer<Ui_QFileDialog> qFileDialogUi;

    QString acceptLabel;

    QPointer<QObject> receiverToDisconnectOnClose;
    QByteArray memberToDisconnectOnClose;
    QByteArray signalToDisconnectOnClose;

    QSharedPointer<QFileDialogOptions> options;

    ~QFileDialogPrivate();

private:
    virtual void initHelper(QPlatformDialogHelper *);
    virtual void helperPrepareShow(QPlatformDialogHelper *);
    virtual void helperDone(QDialog::DialogCode, QPlatformDialogHelper *);

    Q_DISABLE_COPY(QFileDialogPrivate)
};

class QFileDialogLineEdit : public QLineEdit
{
public:
    QFileDialogLineEdit(QWidget *parent = 0) : QLineEdit(parent), d_ptr(0){}
    void setFileDialogPrivate(QFileDialogPrivate *d_pointer) {d_ptr = d_pointer; }
    void keyPressEvent(QKeyEvent *e);
    bool hideOnEsc;
private:
    QFileDialogPrivate *d_ptr;
};

class QFileDialogComboBox : public QComboBox
{
public:
    QFileDialogComboBox(QWidget *parent = 0) : QComboBox(parent), urlModel(0) {}
    void setFileDialogPrivate(QFileDialogPrivate *d_pointer);
    void showPopup();
    void setHistory(const QStringList &paths);
    QStringList history() const { return m_history; }
    void paintEvent(QPaintEvent *);

private:
    QUrlModel *urlModel;
    QFileDialogPrivate *d_ptr;
    QStringList m_history;
};

class QFileDialogListView : public QListView
{
public:
    QFileDialogListView(QWidget *parent = 0);
    void setFileDialogPrivate(QFileDialogPrivate *d_pointer);
    QSize sizeHint() const;
protected:
    void keyPressEvent(QKeyEvent *e);
private:
    QFileDialogPrivate *d_ptr;
};

class QFileDialogTreeView : public QTreeView
{
public:
    QFileDialogTreeView(QWidget *parent);
    void setFileDialogPrivate(QFileDialogPrivate *d_pointer);
    QSize sizeHint() const;

protected:
    void keyPressEvent(QKeyEvent *e);
private:
    QFileDialogPrivate *d_ptr;
};

inline QModelIndex QFileDialogPrivate::mapToSource(const QModelIndex &index) const {
#ifdef QT_NO_PROXYMODEL
    return index;
#else
    return proxyModel ? proxyModel->mapToSource(index) : index;
#endif
}
inline QModelIndex QFileDialogPrivate::mapFromSource(const QModelIndex &index) const {
#ifdef QT_NO_PROXYMODEL
    return index;
#else
    return proxyModel ? proxyModel->mapFromSource(index) : index;
#endif
}

inline QString QFileDialogPrivate::rootPath() const {
    return (model ? model->rootPath() : QStringLiteral("/"));
}

inline void QFileDialogPrivate::setDirectory_sys(const QUrl &directory)
{
    QPlatformFileDialogHelper *helper = platformFileDialogHelper();

    if (!helper)
        return;

    if (helper->isSupportedUrl(directory))
        helper->setDirectory(directory);
}

inline QUrl QFileDialogPrivate::directory_sys() const
{
    if (QPlatformFileDialogHelper *helper = platformFileDialogHelper())
        return helper->directory();
    return QString();
}

inline void QFileDialogPrivate::selectFile_sys(const QUrl &filename)
{
    QPlatformFileDialogHelper *helper = platformFileDialogHelper();

    if (!helper)
        return;

    if (helper->isSupportedUrl(filename))
        helper->selectFile(filename);
}

inline QList<QUrl> QFileDialogPrivate::selectedFiles_sys() const
{
    if (QPlatformFileDialogHelper *helper = platformFileDialogHelper())
        return helper->selectedFiles();
    return QList<QUrl>();
}

inline QByteArray QFileDialogPrivate::selectedRemoteContent_sys() const
{
	if (QPlatformFileDialogHelper *helper = platformFileDialogHelper())
		return helper->selectedRemoteContent();
	return QByteArray();
}

inline void QFileDialogPrivate::setFilter_sys()
{
    if (QPlatformFileDialogHelper *helper = platformFileDialogHelper())
        helper->setFilter();
}

inline void QFileDialogPrivate::selectNameFilter_sys(const QString &filter)
{
    if (QPlatformFileDialogHelper *helper = platformFileDialogHelper())
        helper->selectNameFilter(filter);
}

inline QString QFileDialogPrivate::selectedNameFilter_sys() const
{
    if (QPlatformFileDialogHelper *helper = platformFileDialogHelper())
        return helper->selectedNameFilter();
    return QString();
}

QT_END_NAMESPACE

#endif // QT_NO_FILEDIALOG

#endif // QFILEDIALOG_P_H
