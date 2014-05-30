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

#ifndef QFILEDIALOG_H
#define QFILEDIALOG_H

#include <QtCore/qdir.h>
#include <QtCore/qstring.h>
#include <QtCore/qurl.h>
#include <QtWidgets/qdialog.h>

QT_BEGIN_NAMESPACE


#ifndef QT_NO_FILEDIALOG

class QModelIndex;
class QItemSelection;
struct QFileDialogArgs;
class QFileIconProvider;
class QFileDialogPrivate;
class QAbstractItemDelegate;
class QAbstractProxyModel;

class Q_WIDGETS_EXPORT QFileDialog : public QDialog
{
    Q_OBJECT
    Q_ENUMS(ViewMode FileMode AcceptMode Option)
    Q_FLAGS(Options)
    Q_PROPERTY(ViewMode viewMode READ viewMode WRITE setViewMode)
    Q_PROPERTY(FileMode fileMode READ fileMode WRITE setFileMode)
    Q_PROPERTY(AcceptMode acceptMode READ acceptMode WRITE setAcceptMode)
    Q_PROPERTY(bool readOnly READ isReadOnly WRITE setReadOnly DESIGNABLE false)
    Q_PROPERTY(bool resolveSymlinks READ resolveSymlinks WRITE setResolveSymlinks DESIGNABLE false)
    Q_PROPERTY(bool confirmOverwrite READ confirmOverwrite WRITE setConfirmOverwrite DESIGNABLE false)
    Q_PROPERTY(QString defaultSuffix READ defaultSuffix WRITE setDefaultSuffix)
    Q_PROPERTY(bool nameFilterDetailsVisible READ isNameFilterDetailsVisible
               WRITE setNameFilterDetailsVisible DESIGNABLE false)
    Q_PROPERTY(Options options READ options WRITE setOptions)

public:
    enum ViewMode { Detail, List };
    enum FileMode { AnyFile, ExistingFile, Directory, ExistingFiles, DirectoryOnly };
    enum AcceptMode { AcceptOpen, AcceptSave };
    enum DialogLabel { LookIn, FileName, FileType, Accept, Reject };

    enum Option
    {
        ShowDirsOnly                = 0x00000001,
        DontResolveSymlinks         = 0x00000002,
        DontConfirmOverwrite        = 0x00000004,
        DontUseSheet                = 0x00000008,
        DontUseNativeDialog         = 0x00000010,
        ReadOnly                    = 0x00000020,
        HideNameFilterDetails       = 0x00000040,
        DontUseCustomDirectoryIcons = 0x00000080
    };
    Q_DECLARE_FLAGS(Options, Option)

    QFileDialog(QWidget *parent, Qt::WindowFlags f);
    explicit QFileDialog(QWidget *parent = 0,
                         const QString &caption = QString(),
                         const QString &directory = QString(),
                         const QString &filter = QString());
    ~QFileDialog();

    void setDirectory(const QString &directory);
    inline void setDirectory(const QDir &directory);
    QDir directory() const;

    void setDirectoryUrl(const QUrl &directory);
    QUrl directoryUrl() const;

    void selectFile(const QString &filename);
    QStringList selectedFiles() const;
	QByteArray selectedRemoteContent() const;

    void selectUrl(const QUrl &url);
    QList<QUrl> selectedUrls() const;

    void setNameFilterDetailsVisible(bool enabled);
    bool isNameFilterDetailsVisible() const;

    void setNameFilter(const QString &filter);
    void setNameFilters(const QStringList &filters);
    QStringList nameFilters() const;
    void selectNameFilter(const QString &filter);
    QString selectedNameFilter() const;

    void setMimeTypeFilters(const QStringList &filters);
    QStringList mimeTypeFilters() const;
    void selectMimeTypeFilter(const QString &filter);

    QDir::Filters filter() const;
    void setFilter(QDir::Filters filters);

    void setViewMode(ViewMode mode);
    ViewMode viewMode() const;

    void setFileMode(FileMode mode);
    FileMode fileMode() const;

    void setAcceptMode(AcceptMode mode);
    AcceptMode acceptMode() const;

    void setReadOnly(bool enabled);
    bool isReadOnly() const;

    void setResolveSymlinks(bool enabled);
    bool resolveSymlinks() const;

    void setSidebarUrls(const QList<QUrl> &urls);
    QList<QUrl> sidebarUrls() const;

    QByteArray saveState() const;
    bool restoreState(const QByteArray &state);

    void setConfirmOverwrite(bool enabled);
    bool confirmOverwrite() const;

    void setDefaultSuffix(const QString &suffix);
    QString defaultSuffix() const;

    void setHistory(const QStringList &paths);
    QStringList history() const;

    void setItemDelegate(QAbstractItemDelegate *delegate);
    QAbstractItemDelegate *itemDelegate() const;

    void setIconProvider(QFileIconProvider *provider);
    QFileIconProvider *iconProvider() const;

    void setLabelText(DialogLabel label, const QString &text);
    QString labelText(DialogLabel label) const;

#ifndef QT_NO_PROXYMODEL
    void setProxyModel(QAbstractProxyModel *model);
    QAbstractProxyModel *proxyModel() const;
#endif

    void setOption(Option option, bool on = true);
    bool testOption(Option option) const;
    void setOptions(Options options);
    Options options() const;

#ifdef Q_NO_USING_KEYWORD
#ifndef Q_QDOC
    void open() { QDialog::open(); }
#endif
#else
    using QDialog::open;
#endif
    void open(QObject *receiver, const char *member);
    void setVisible(bool visible);

Q_SIGNALS:
    void fileSelected(const QString &file);
    void filesSelected(const QStringList &files);
    void currentChanged(const QString &path);
    void directoryEntered(const QString &directory);

    void urlSelected(const QUrl &url);
    void urlsSelected(const QList<QUrl> &urls);
    void currentUrlChanged(const QUrl &url);
    void directoryUrlEntered(const QUrl &directory);

    void filterSelected(const QString &filter);

public:

    static QString getOpenFileName(QWidget *parent = 0,
                                   const QString &caption = QString(),
                                   const QString &dir = QString(),
                                   const QString &filter = QString(),
                                   QString *selectedFilter = 0,
                                   Options options = 0);

    static QUrl getOpenFileUrl(QWidget *parent = 0,
                               const QString &caption = QString(),
                               const QUrl &dir = QUrl(),
                               const QString &filter = QString(),
                               QString *selectedFilter = 0,
                               Options options = 0,
                               const QStringList &supportedSchemes = QStringList());

    static QString getSaveFileName(QWidget *parent = 0,
                                   const QString &caption = QString(),
                                   const QString &dir = QString(),
                                   const QString &filter = QString(),
                                   QString *selectedFilter = 0,
                                   Options options = 0);

    static QUrl getSaveFileUrl(QWidget *parent = 0,
                               const QString &caption = QString(),
                               const QUrl &dir = QUrl(),
                               const QString &filter = QString(),
                               QString *selectedFilter = 0,
                               Options options = 0,
                               const QStringList &supportedSchemes = QStringList());

    static QString getExistingDirectory(QWidget *parent = 0,
                                        const QString &caption = QString(),
                                        const QString &dir = QString(),
                                        Options options = ShowDirsOnly);

    static QUrl getExistingDirectoryUrl(QWidget *parent = 0,
                                        const QString &caption = QString(),
                                        const QUrl &dir = QUrl(),
                                        Options options = ShowDirsOnly,
                                        const QStringList &supportedSchemes = QStringList());

    static QStringList getOpenFileNames(QWidget *parent = 0,
                                        const QString &caption = QString(),
                                        const QString &dir = QString(),
                                        const QString &filter = QString(),
                                        QString *selectedFilter = 0,
                                        Options options = 0);

    static QList<QUrl> getOpenFileUrls(QWidget *parent = 0,
                                       const QString &caption = QString(),
                                       const QUrl &dir = QUrl(),
                                       const QString &filter = QString(),
                                       QString *selectedFilter = 0,
                                       Options options = 0,
                                       const QStringList &supportedSchemes = QStringList());


protected:
    QFileDialog(const QFileDialogArgs &args);
    void done(int result);
    void accept();
    void changeEvent(QEvent *e);

private:
    Q_DECLARE_PRIVATE(QFileDialog)
    Q_DISABLE_COPY(QFileDialog)

    Q_PRIVATE_SLOT(d_func(), void _q_pathChanged(const QString &))

    Q_PRIVATE_SLOT(d_func(), void _q_navigateBackward())
    Q_PRIVATE_SLOT(d_func(), void _q_navigateForward())
    Q_PRIVATE_SLOT(d_func(), void _q_navigateToParent())
    Q_PRIVATE_SLOT(d_func(), void _q_createDirectory())
    Q_PRIVATE_SLOT(d_func(), void _q_showListView())
    Q_PRIVATE_SLOT(d_func(), void _q_showDetailsView())
    Q_PRIVATE_SLOT(d_func(), void _q_showContextMenu(const QPoint &))
    Q_PRIVATE_SLOT(d_func(), void _q_renameCurrent())
    Q_PRIVATE_SLOT(d_func(), void _q_deleteCurrent())
    Q_PRIVATE_SLOT(d_func(), void _q_showHidden())
    Q_PRIVATE_SLOT(d_func(), void _q_updateOkButton())
    Q_PRIVATE_SLOT(d_func(), void _q_currentChanged(const QModelIndex &index))
    Q_PRIVATE_SLOT(d_func(), void _q_enterDirectory(const QModelIndex &index))
    Q_PRIVATE_SLOT(d_func(), void _q_nativeFileSelected(const QUrl &))
    Q_PRIVATE_SLOT(d_func(), void _q_nativeFilesSelected(const QList<QUrl> &))
    Q_PRIVATE_SLOT(d_func(), void _q_nativeCurrentChanged(const QUrl &))
    Q_PRIVATE_SLOT(d_func(), void _q_nativeEnterDirectory(const QUrl&))
    Q_PRIVATE_SLOT(d_func(), void _q_goToDirectory(const QString &path))
    Q_PRIVATE_SLOT(d_func(), void _q_useNameFilter(int index))
    Q_PRIVATE_SLOT(d_func(), void _q_selectionChanged())
    Q_PRIVATE_SLOT(d_func(), void _q_goToUrl(const QUrl &url))
    Q_PRIVATE_SLOT(d_func(), void _q_goHome())
    Q_PRIVATE_SLOT(d_func(), void _q_showHeader(QAction *))
    Q_PRIVATE_SLOT(d_func(), void _q_autoCompleteFileName(const QString &text))
    Q_PRIVATE_SLOT(d_func(), void _q_rowsInserted(const QModelIndex & parent))
    Q_PRIVATE_SLOT(d_func(), void _q_fileRenamed(const QString &path,
                const QString oldName, const QString newName))
    friend class QPlatformDialogHelper;
};

inline void QFileDialog::setDirectory(const QDir &adirectory)
{ setDirectory(adirectory.absolutePath()); }

Q_DECLARE_OPERATORS_FOR_FLAGS(QFileDialog::Options)

#endif // QT_NO_FILEDIALOG

QT_END_NAMESPACE

#endif // QFILEDIALOG_H
