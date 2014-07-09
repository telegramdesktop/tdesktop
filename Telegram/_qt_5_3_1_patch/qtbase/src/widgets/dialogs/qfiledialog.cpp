/****************************************************************************
**
** Copyright (C) 2014 Digia Plc and/or its subsidiary(-ies).
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

#include <qvariant.h>
#include <private/qwidgetitemdata_p.h>
#include "qfiledialog.h"

#ifndef QT_NO_FILEDIALOG
#include "qfiledialog_p.h"
#include <private/qguiapplication_p.h>
#include <qfontmetrics.h>
#include <qaction.h>
#include <qheaderview.h>
#include <qshortcut.h>
#include <qgridlayout.h>
#include <qmenu.h>
#include <qmessagebox.h>
#include <qinputdialog.h>
#include <stdlib.h>
#include <qsettings.h>
#include <qdebug.h>
#include <qmimedatabase.h>
#include <qapplication.h>
#include <qstylepainter.h>
#if !defined(Q_OS_WINCE)
#include "ui_qfiledialog.h"
#else
#define Q_EMBEDDED_SMALLSCREEN
#include "ui_qfiledialog_embedded.h"
#if defined(Q_OS_WINCE)
extern bool qt_priv_ptr_valid;
#endif
#endif
#if defined(Q_OS_UNIX)
#include <pwd.h>
#include <unistd.h> // for pathconf() on OS X
#elif defined(Q_OS_WIN)
#  include <QtCore/qt_windows.h>
#endif

QT_BEGIN_NAMESPACE

Q_GLOBAL_STATIC(QString, lastVisitedDir)

/*!
  \class QFileDialog
  \brief The QFileDialog class provides a dialog that allow users to select files or directories.
  \ingroup standard-dialogs
  \inmodule QtWidgets

  The QFileDialog class enables a user to traverse the file system in
  order to select one or many files or a directory.

  The easiest way to create a QFileDialog is to use the static
  functions. On Windows, Mac OS X, KDE and GNOME, these static functions will
  call the native file dialog when possible.

  \snippet code/src_gui_dialogs_qfiledialog.cpp 0

  In the above example, a modal QFileDialog is created using a static
  function. The dialog initially displays the contents of the "/home/jana"
  directory, and displays files matching the patterns given in the
  string "Image Files (*.png *.jpg *.bmp)". The parent of the file dialog
  is set to \e this, and the window title is set to "Open Image".

  If you want to use multiple filters, separate each one with
  \e two semicolons. For example:

  \snippet code/src_gui_dialogs_qfiledialog.cpp 1

  You can create your own QFileDialog without using the static
  functions. By calling setFileMode(), you can specify what the user must
  select in the dialog:

  \snippet code/src_gui_dialogs_qfiledialog.cpp 2

  In the above example, the mode of the file dialog is set to
  AnyFile, meaning that the user can select any file, or even specify a
  file that doesn't exist. This mode is useful for creating a
  "Save As" file dialog. Use ExistingFile if the user must select an
  existing file, or \l Directory if only a directory may be selected.
  See the \l QFileDialog::FileMode enum for the complete list of modes.

  The fileMode property contains the mode of operation for the dialog;
  this indicates what types of objects the user is expected to select.
  Use setNameFilter() to set the dialog's file filter. For example:

  \snippet code/src_gui_dialogs_qfiledialog.cpp 3

  In the above example, the filter is set to \c{"Images (*.png *.xpm *.jpg)"},
  this means that only files with the extension \c png, \c xpm,
  or \c jpg will be shown in the QFileDialog. You can apply
  several filters by using setNameFilters(). Use selectNameFilter() to select
  one of the filters you've given as the file dialog's default filter.

  The file dialog has two view modes: \l{QFileDialog::}{List} and
  \l{QFileDialog::}{Detail}.
  \l{QFileDialog::}{List} presents the contents of the current directory
  as a list of file and directory names. \l{QFileDialog::}{Detail} also
  displays a list of file and directory names, but provides additional
  information alongside each name, such as the file size and modification
  date. Set the mode with setViewMode():

  \snippet code/src_gui_dialogs_qfiledialog.cpp 4

  The last important function you will need to use when creating your
  own file dialog is selectedFiles().

  \snippet code/src_gui_dialogs_qfiledialog.cpp 5

  In the above example, a modal file dialog is created and shown. If
  the user clicked OK, the file they selected is put in \c fileName.

  The dialog's working directory can be set with setDirectory().
  Each file in the current directory can be selected using
  the selectFile() function.

  The \l{dialogs/standarddialogs}{Standard Dialogs} example shows
  how to use QFileDialog as well as other built-in Qt dialogs.

  By default, a platform-native file dialog will be used if the platform has
  one. In that case, the widgets which would otherwise be used to construct the
  dialog will not be instantiated, so related accessors such as layout() and
  itemDelegate() will return null. You can set the \l DontUseNativeDialog option to
  ensure that the widget-based implementation will be used instead of the
  native dialog.

  \sa QDir, QFileInfo, QFile, QColorDialog, QFontDialog, {Standard Dialogs Example},
      {Application Example}
*/

/*!
    \enum QFileDialog::AcceptMode

    \value AcceptOpen
    \value AcceptSave
*/

/*!
    \enum QFileDialog::ViewMode

    This enum describes the view mode of the file dialog; i.e. what
    information about each file will be displayed.

    \value Detail Displays an icon, a name, and details for each item in
                  the directory.
    \value List   Displays only an icon and a name for each item in the
                  directory.

    \sa setViewMode()
*/

/*!
    \enum QFileDialog::FileMode

    This enum is used to indicate what the user may select in the file
    dialog; i.e. what the dialog will return if the user clicks OK.

    \value AnyFile        The name of a file, whether it exists or not.
    \value ExistingFile   The name of a single existing file.
    \value Directory      The name of a directory. Both files and
                          directories are displayed.
    \value ExistingFiles  The names of zero or more existing files.

    This value is obsolete since Qt 4.5:

    \value DirectoryOnly  Use \c Directory and setOption(ShowDirsOnly, true) instead.

    \sa setFileMode()
*/

/*!
    \enum QFileDialog::Option

    \value ShowDirsOnly Only show directories in the file dialog. By
    default both files and directories are shown. (Valid only in the
    \l Directory file mode.)

    \value DontResolveSymlinks Don't resolve symlinks in the file
    dialog. By default symlinks are resolved.

    \value DontConfirmOverwrite Don't ask for confirmation if an
    existing file is selected.  By default confirmation is requested.

    \value DontUseNativeDialog Don't use the native file dialog. By
    default, the native file dialog is used unless you use a subclass
    of QFileDialog that contains the Q_OBJECT macro, or the platform
    does not have a native dialog of the type that you require.

    \value ReadOnly Indicates that the model is readonly.

    \value HideNameFilterDetails Indicates if the file name filter details are
    hidden or not.

    \value DontUseSheet In previous versions of Qt, the static
    functions would create a sheet by default if the static function
    was given a parent. This is no longer supported and does nothing in Qt 4.5, The
    static functions will always be an application modal dialog. If
    you want to use sheets, use QFileDialog::open() instead.

    \value DontUseCustomDirectoryIcons Always use the default directory icon.
    Some platforms allow the user to set a different icon. Custom icon lookup
    cause a big performance impact over network or removable drives.
    Setting this will enable the QFileIconProvider::DontUseCustomDirectoryIcons
    option in the icon provider. This enum value was added in Qt 5.2.
*/

/*!
  \enum QFileDialog::DialogLabel

  \value LookIn
  \value FileName
  \value FileType
  \value Accept
  \value Reject
*/

/*!
    \fn void QFileDialog::filesSelected(const QStringList &selected)

    When the selection changes for local operations and the dialog is
    accepted, this signal is emitted with the (possibly empty) list
    of \a selected files.

    \sa currentChanged(), QDialog::Accepted
*/

/*!
    \fn void QFileDialog::urlsSelected(const QList<QUrl> &urls)

    When the selection changes and the dialog is accepted, this signal is
    emitted with the (possibly empty) list of selected \a urls.

    \sa currentUrlChanged(), QDialog::Accepted
    \since 5.2
*/

/*!
    \fn void QFileDialog::fileSelected(const QString &file)

    When the selection changes for local operations and the dialog is
    accepted, this signal is emitted with the (possibly empty)
    selected \a file.

    \sa currentChanged(), QDialog::Accepted
*/

/*!
    \fn void QFileDialog::urlSelected(const QUrl &url)

    When the selection changes and the dialog is accepted, this signal is
    emitted with the (possibly empty) selected \a url.

    \sa currentUrlChanged(), QDialog::Accepted
    \since 5.2
*/

/*!
    \fn void QFileDialog::currentChanged(const QString &path)

    When the current file changes for local operations, this signal is
    emitted with the new file name as the \a path parameter.

    \sa filesSelected()
*/

/*!
    \fn void QFileDialog::currentUrlChanged(const QUrl &url)

    When the current file changes, this signal is emitted with the
    new file URL as the \a url parameter.

    \sa urlsSelected()
    \since 5.2
*/

/*!
  \fn void QFileDialog::directoryEntered(const QString &directory)
  \since 4.3

  This signal is emitted for local operations when the user enters
  a \a directory.
*/

/*!
  \fn void QFileDialog::directoryUrlEntered(const QUrl &directory)

  This signal is emitted when the user enters a \a directory.

  \since 5.2
*/

/*!
  \fn void QFileDialog::filterSelected(const QString &filter)
  \since 4.3

  This signal is emitted when the user selects a \a filter.
*/

//#if defined(Q_WS_WIN) || defined(Q_WS_MAC)
//bool Q_WIDGETS_EXPORT qt_use_native_dialogs = true; // for the benefit of testing tools, until we have a proper API
//#endif

QT_BEGIN_INCLUDE_NAMESPACE
#ifdef Q_WS_WIN
#include <qwindowsstyle_p.h>
#endif
#include <qshortcut.h>
#ifdef Q_WS_MAC
#include <qmacstyle_mac_p.h>
#endif
QT_END_INCLUDE_NAMESPACE

/*!
    \fn QFileDialog::QFileDialog(QWidget *parent, Qt::WindowFlags flags)

    Constructs a file dialog with the given \a parent and widget \a flags.
*/
QFileDialog::QFileDialog(QWidget *parent, Qt::WindowFlags f)
    : QDialog(*new QFileDialogPrivate, parent, f)
{
    Q_D(QFileDialog);
    d->init();
}

/*!
    Constructs a file dialog with the given \a parent and \a caption that
    initially displays the contents of the specified \a directory.
    The contents of the directory are filtered before being shown in the
    dialog, using a semicolon-separated list of filters specified by
    \a filter.
*/
QFileDialog::QFileDialog(QWidget *parent,
                     const QString &caption,
                     const QString &directory,
                     const QString &filter)
    : QDialog(*new QFileDialogPrivate, parent, 0)
{
    Q_D(QFileDialog);
    d->init(directory, filter, caption);
}

/*!
    \internal
*/
QFileDialog::QFileDialog(const QFileDialogArgs &args)
    : QDialog(*new QFileDialogPrivate, args.parent, 0)
{
    Q_D(QFileDialog);
    d->init(args.directory, args.filter, args.caption);
    setFileMode(args.mode);
    setOptions(args.options);
    selectFile(args.selection);
}

/*!
    Destroys the file dialog.
*/
QFileDialog::~QFileDialog()
{
#ifndef QT_NO_SETTINGS
    QSettings settings(QSettings::UserScope, QLatin1String("QtProject"));
    settings.beginGroup(QLatin1String("Qt"));
    settings.setValue(QLatin1String("filedialog"), saveState());
#endif
}

/*!
    \since 4.3
    Sets the \a urls that are located in the sidebar.

    For instance:

    \snippet filedialogurls.cpp 0

    The file dialog will then look like this:

    \image filedialogurls.png

    \sa sidebarUrls()
*/
void QFileDialog::setSidebarUrls(const QList<QUrl> &urls)
{
    Q_D(QFileDialog);
    if (!d->nativeDialogInUse)
        d->qFileDialogUi->sidebar->setUrls(urls);
}

/*!
    \since 4.3
    Returns a list of urls that are currently in the sidebar
*/
QList<QUrl> QFileDialog::sidebarUrls() const
{
    Q_D(const QFileDialog);
    return (d->nativeDialogInUse ? QList<QUrl>() : d->qFileDialogUi->sidebar->urls());
}

static const qint32 QFileDialogMagic = 0xbe;

/*!
    \since 4.3
    Saves the state of the dialog's layout, history and current directory.

    Typically this is used in conjunction with QSettings to remember the size
    for a future session. A version number is stored as part of the data.
*/
QByteArray QFileDialog::saveState() const
{
    Q_D(const QFileDialog);
    int version = 3;
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);

    stream << qint32(QFileDialogMagic);
    stream << qint32(version);
    if (d->usingWidgets()) {
        stream << d->qFileDialogUi->splitter->saveState();
        stream << d->qFileDialogUi->sidebar->urls();
    } else {
        stream << QByteArray();
        stream << QList<QUrl>();
    }
    stream << history();
    stream << *lastVisitedDir();
    if (d->usingWidgets())
        stream << d->qFileDialogUi->treeView->header()->saveState();
    else
        stream << QByteArray();
    stream << qint32(viewMode());
    return data;
}

/*!
    \since 4.3
    Restores the dialogs's layout, history and current directory to the \a state specified.

    Typically this is used in conjunction with QSettings to restore the size
    from a past session.

    Returns \c false if there are errors
*/
bool QFileDialog::restoreState(const QByteArray &state)
{
    Q_D(QFileDialog);
    int version = 3;
    QByteArray sd = state;
    QDataStream stream(&sd, QIODevice::ReadOnly);
    if (stream.atEnd())
        return false;
    QByteArray splitterState;
    QByteArray headerData;
    QList<QUrl> bookmarks;
    QStringList history;
    QString currentDirectory;
    qint32 marker;
    qint32 v;
    qint32 viewMode;
    stream >> marker;
    stream >> v;
    if (marker != QFileDialogMagic || v != version)
        return false;

    stream >> splitterState
           >> bookmarks
           >> history
           >> currentDirectory
           >> headerData
           >> viewMode;

    setDirectory(lastVisitedDir()->isEmpty() ? currentDirectory : *lastVisitedDir());
    setViewMode(static_cast<QFileDialog::ViewMode>(viewMode));

    if (!d->usingWidgets())
        return true;

    if (!d->qFileDialogUi->splitter->restoreState(splitterState))
        return false;
    QList<int> list = d->qFileDialogUi->splitter->sizes();
    if (list.count() >= 2 && list.at(0) == 0 && list.at(1) == 0) {
        for (int i = 0; i < list.count(); ++i)
            list[i] = d->qFileDialogUi->splitter->widget(i)->sizeHint().width();
        d->qFileDialogUi->splitter->setSizes(list);
    }

    d->qFileDialogUi->sidebar->setUrls(bookmarks);
    while (history.count() > 5)
        history.pop_front();
    setHistory(history);
    QHeaderView *headerView = d->qFileDialogUi->treeView->header();
    if (!headerView->restoreState(headerData))
        return false;

    QList<QAction*> actions = headerView->actions();
    QAbstractItemModel *abstractModel = d->model;
#ifndef QT_NO_PROXYMODEL
    if (d->proxyModel)
        abstractModel = d->proxyModel;
#endif
    int total = qMin(abstractModel->columnCount(QModelIndex()), actions.count() + 1);
    for (int i = 1; i < total; ++i)
        actions.at(i - 1)->setChecked(!headerView->isSectionHidden(i));

    return true;
}

/*!
    \reimp
*/
void QFileDialog::changeEvent(QEvent *e)
{
    Q_D(QFileDialog);
    if (e->type() == QEvent::LanguageChange) {
        d->retranslateWindowTitle();
        d->retranslateStrings();
    }
    QDialog::changeEvent(e);
}

QFileDialogPrivate::QFileDialogPrivate()
    :
#ifndef QT_NO_PROXYMODEL
        proxyModel(0),
#endif
        model(0),
        currentHistoryLocation(-1),
        renameAction(0),
        deleteAction(0),
        showHiddenAction(0),
        useDefaultCaption(true),
        defaultFileTypes(true),
        qFileDialogUi(0),
        options(new QFileDialogOptions)
{
}

QFileDialogPrivate::~QFileDialogPrivate()
{
}

void QFileDialogPrivate::initHelper(QPlatformDialogHelper *h)
{
    QFileDialog *d = q_func();
    QObject::connect(h, SIGNAL(fileSelected(QUrl)), d, SLOT(_q_nativeFileSelected(QUrl)));
    QObject::connect(h, SIGNAL(filesSelected(QList<QUrl>)), d, SLOT(_q_nativeFilesSelected(QList<QUrl>)));
    QObject::connect(h, SIGNAL(currentChanged(QUrl)), d, SLOT(_q_nativeCurrentChanged(QUrl)));
    QObject::connect(h, SIGNAL(directoryEntered(QUrl)), d, SLOT(_q_nativeEnterDirectory(QUrl)));
    QObject::connect(h, SIGNAL(filterSelected(QString)), d, SIGNAL(filterSelected(QString)));
    static_cast<QPlatformFileDialogHelper *>(h)->setOptions(options);
    nativeDialogInUse = true;
}

void QFileDialogPrivate::helperPrepareShow(QPlatformDialogHelper *)
{
    Q_Q(QFileDialog);
    options->setWindowTitle(q->windowTitle());
    options->setHistory(q->history());
    if (usingWidgets())
        options->setSidebarUrls(qFileDialogUi->sidebar->urls());
    const QDir directory = q->directory();
    options->setInitialDirectory(directory.exists() ?
                                 QUrl::fromLocalFile(directory.absolutePath()) :
                                 QUrl());
    if (options->initiallySelectedNameFilter().isEmpty())
        options->setInitiallySelectedNameFilter(q->selectedNameFilter());
    if (options->initiallySelectedFiles().isEmpty())
        options->setInitiallySelectedFiles(userSelectedFiles());
}

void QFileDialogPrivate::helperDone(QDialog::DialogCode code, QPlatformDialogHelper *)
{
    if (code == QDialog::Accepted) {
        Q_Q(QFileDialog);
        q->setViewMode(static_cast<QFileDialog::ViewMode>(options->viewMode()));
        q->setSidebarUrls(options->sidebarUrls());
        q->setHistory(options->history());
    }
}

void QFileDialogPrivate::retranslateWindowTitle()
{
    Q_Q(QFileDialog);
    if (!useDefaultCaption || setWindowTitle != q->windowTitle())
        return;
    if (q->acceptMode() == QFileDialog::AcceptOpen) {
        const QFileDialog::FileMode fileMode = q->fileMode();
        if (fileMode == QFileDialog::DirectoryOnly || fileMode == QFileDialog::Directory)
            q->setWindowTitle(QFileDialog::tr("Find Directory"));
        else
            q->setWindowTitle(QFileDialog::tr("Open"));
    } else
        q->setWindowTitle(QFileDialog::tr("Save As"));

    setWindowTitle = q->windowTitle();
}

void QFileDialogPrivate::setLastVisitedDirectory(const QString &dir)
{
    *lastVisitedDir() = dir;
}

void QFileDialogPrivate::updateLookInLabel()
{
    if (options->isLabelExplicitlySet(QFileDialogOptions::LookIn))
        setLabelTextControl(QFileDialog::LookIn, options->labelText(QFileDialogOptions::LookIn));
}

void QFileDialogPrivate::updateFileNameLabel()
{
    if (options->isLabelExplicitlySet(QFileDialogOptions::FileName)) {
        setLabelTextControl(QFileDialog::FileName, options->labelText(QFileDialogOptions::FileName));
    } else {
        switch (q_func()->fileMode()) {
        case QFileDialog::DirectoryOnly:
        case QFileDialog::Directory:
            setLabelTextControl(QFileDialog::FileName, QFileDialog::tr("Directory:"));
            break;
        default:
            setLabelTextControl(QFileDialog::FileName, QFileDialog::tr("File &name:"));
            break;
        }
    }
}

void QFileDialogPrivate::updateFileTypeLabel()
{
    if (options->isLabelExplicitlySet(QFileDialogOptions::FileType))
        setLabelTextControl(QFileDialog::FileType, options->labelText(QFileDialogOptions::FileType));
}

void QFileDialogPrivate::updateOkButtonText(bool saveAsOnFolder)
{
    Q_Q(QFileDialog);
    // 'Save as' at a folder: Temporarily change to "Open".
    if (saveAsOnFolder) {
        setLabelTextControl(QFileDialog::Accept, QFileDialog::tr("&Open"));
    } else if (options->isLabelExplicitlySet(QFileDialogOptions::Accept)) {
        setLabelTextControl(QFileDialog::Accept, options->labelText(QFileDialogOptions::Accept));
        return;
    } else {
        switch (q->fileMode()) {
        case QFileDialog::DirectoryOnly:
        case QFileDialog::Directory:
            setLabelTextControl(QFileDialog::Accept, QFileDialog::tr("&Choose"));
            break;
        default:
            setLabelTextControl(QFileDialog::Accept,
                                q->acceptMode() == QFileDialog::AcceptOpen ?
                                    QFileDialog::tr("&Open")  :
                                    QFileDialog::tr("&Save"));
            break;
        }
    }
}

void QFileDialogPrivate::updateCancelButtonText()
{
    if (options->isLabelExplicitlySet(QFileDialogOptions::Reject))
        setLabelTextControl(QFileDialog::Reject, options->labelText(QFileDialogOptions::Reject));
}

void QFileDialogPrivate::retranslateStrings()
{
    Q_Q(QFileDialog);
    /* WIDGETS */
    if (defaultFileTypes)
        q->setNameFilter(QFileDialog::tr("All Files (*)"));
    if (nativeDialogInUse)
        return;

    QList<QAction*> actions = qFileDialogUi->treeView->header()->actions();
    QAbstractItemModel *abstractModel = model;
#ifndef QT_NO_PROXYMODEL
    if (proxyModel)
        abstractModel = proxyModel;
#endif
    int total = qMin(abstractModel->columnCount(QModelIndex()), actions.count() + 1);
    for (int i = 1; i < total; ++i) {
        actions.at(i - 1)->setText(QFileDialog::tr("Show ") + abstractModel->headerData(i, Qt::Horizontal, Qt::DisplayRole).toString());
    }

    /* MENU ACTIONS */
    renameAction->setText(QFileDialog::tr("&Rename"));
    deleteAction->setText(QFileDialog::tr("&Delete"));
    showHiddenAction->setText(QFileDialog::tr("Show &hidden files"));
    newFolderAction->setText(QFileDialog::tr("&New Folder"));
    qFileDialogUi->retranslateUi(q);
    updateLookInLabel();
    updateFileNameLabel();
    updateFileTypeLabel();
    updateCancelButtonText();
}

void QFileDialogPrivate::emitFilesSelected(const QStringList &files)
{
    Q_Q(QFileDialog);
    emit q->filesSelected(files);
    if (files.count() == 1)
        emit q->fileSelected(files.first());
}

bool QFileDialogPrivate::canBeNativeDialog() const
{
    Q_Q(const QFileDialog);
    if (nativeDialogInUse)
        return true;
    if (q->testAttribute(Qt::WA_DontShowOnScreen))
        return false;
    if (q->options() & QFileDialog::DontUseNativeDialog)
        return false;

    QLatin1String staticName(QFileDialog::staticMetaObject.className());
    QLatin1String dynamicName(q->metaObject()->className());
    return (staticName == dynamicName);
}

bool QFileDialogPrivate::usingWidgets() const
{
    return !nativeDialogInUse && qFileDialogUi;
}

/*!
    \since 4.5
    Sets the given \a option to be enabled if \a on is true; otherwise,
    clears the given \a option.

    \sa options, testOption()
*/
void QFileDialog::setOption(Option option, bool on)
{
    const QFileDialog::Options previousOptions = options();
    if (!(previousOptions & option) != !on)
        setOptions(previousOptions ^ option);
}

/*!
    \since 4.5

    Returns \c true if the given \a option is enabled; otherwise, returns
    false.

    \sa options, setOption()
*/
bool QFileDialog::testOption(Option option) const
{
    Q_D(const QFileDialog);
    return d->options->testOption(static_cast<QFileDialogOptions::FileDialogOption>(option));
}

/*!
    \property QFileDialog::options
    \brief the various options that affect the look and feel of the dialog
    \since 4.5

    By default, all options are disabled.

    Options should be set before showing the dialog. Setting them while the
    dialog is visible is not guaranteed to have an immediate effect on the
    dialog (depending on the option and on the platform).

    \sa setOption(), testOption()
*/
void QFileDialog::setOptions(Options options)
{
    Q_D(QFileDialog);

    Options changed = (options ^ QFileDialog::options());
    if (!changed)
        return;

    d->options->setOptions(QFileDialogOptions::FileDialogOptions(int(options)));

    if ((options & DontUseNativeDialog) && !d->usingWidgets())
        d->createWidgets();

    if (d->usingWidgets()) {
        if (changed & DontResolveSymlinks)
            d->model->setResolveSymlinks(!(options & DontResolveSymlinks));
        if (changed & ReadOnly) {
            bool ro = (options & ReadOnly);
            d->model->setReadOnly(ro);
            d->qFileDialogUi->newFolderButton->setEnabled(!ro);
            d->renameAction->setEnabled(!ro);
            d->deleteAction->setEnabled(!ro);
        }

        if (changed & DontUseCustomDirectoryIcons) {
            QFileIconProvider::Options providerOptions = iconProvider()->options();
            if (options & DontUseCustomDirectoryIcons)
                providerOptions |= QFileIconProvider::DontUseCustomDirectoryIcons;
            else
                providerOptions &= ~QFileIconProvider::DontUseCustomDirectoryIcons;
            iconProvider()->setOptions(providerOptions);
        }
    }

    if (changed & HideNameFilterDetails)
        setNameFilters(d->options->nameFilters());

    if (changed & ShowDirsOnly)
        setFilter((options & ShowDirsOnly) ? filter() & ~QDir::Files : filter() | QDir::Files);
}

QFileDialog::Options QFileDialog::options() const
{
    Q_D(const QFileDialog);
    return QFileDialog::Options(int(d->options->options()));
}

/*!
    \overload

    \since 4.5

    This function connects one of its signals to the slot specified by \a receiver
    and \a member. The specific signal depends is filesSelected() if fileMode is
    ExistingFiles and fileSelected() if fileMode is anything else.

    The signal will be disconnected from the slot when the dialog is closed.
*/
void QFileDialog::open(QObject *receiver, const char *member)
{
    Q_D(QFileDialog);
    const char *signal = (fileMode() == ExistingFiles) ? SIGNAL(filesSelected(QStringList))
                                                       : SIGNAL(fileSelected(QString));
    connect(this, signal, receiver, member);
    d->signalToDisconnectOnClose = signal;
    d->receiverToDisconnectOnClose = receiver;
    d->memberToDisconnectOnClose = member;

    QDialog::open();
}


/*!
    \reimp
*/
void QFileDialog::setVisible(bool visible)
{
    Q_D(QFileDialog);
    if (visible){
        if (testAttribute(Qt::WA_WState_ExplicitShowHide) && !testAttribute(Qt::WA_WState_Hidden))
            return;
    } else if (testAttribute(Qt::WA_WState_ExplicitShowHide) && testAttribute(Qt::WA_WState_Hidden))
        return;

    if (d->canBeNativeDialog()){
        if (d->setNativeDialogVisible(visible)){
            // Set WA_DontShowOnScreen so that QDialog::setVisible(visible) below
            // updates the state correctly, but skips showing the non-native version:
            setAttribute(Qt::WA_DontShowOnScreen);
#ifndef QT_NO_FSCOMPLETER
            // So the completer doesn't try to complete and therefore show a popup
            if (!d->nativeDialogInUse)
                d->completer->setModel(0);
#endif
        } else {
            d->createWidgets();
            setAttribute(Qt::WA_DontShowOnScreen, false);
#ifndef QT_NO_FSCOMPLETER
            if (!d->nativeDialogInUse) {
                if (d->proxyModel != 0)
                    d->completer->setModel(d->proxyModel);
                else
                    d->completer->setModel(d->model);
            }
#endif
        }
    }

    if (d->usingWidgets())
        d->qFileDialogUi->fileNameEdit->setFocus();

    QDialog::setVisible(visible);
}

/*!
    \internal
    set the directory to url
*/
void QFileDialogPrivate::_q_goToUrl(const QUrl &url)
{
    //The shortcut in the side bar may have a parent that is not fetched yet (e.g. an hidden file)
    //so we force the fetching
    QFileSystemModelPrivate::QFileSystemNode *node = model->d_func()->node(url.toLocalFile(), true);
    QModelIndex idx =  model->d_func()->index(node);
    _q_enterDirectory(idx);
}

/*!
    \fn void QFileDialog::setDirectory(const QDir &directory)

    \overload
*/

/*!
    Sets the file dialog's current \a directory.
*/
void QFileDialog::setDirectory(const QString &directory)
{
    Q_D(QFileDialog);
    QString newDirectory = directory;
    QFileInfo info(directory);
    //we remove .. and . from the given path if exist
    if (!directory.isEmpty())
        newDirectory = QDir::cleanPath(directory);

    if (!directory.isEmpty() && newDirectory.isEmpty())
        return;

    d->setLastVisitedDirectory(newDirectory);

    d->options->setInitialDirectory(QUrl::fromLocalFile(directory));
    if (!d->usingWidgets()) {
        d->setDirectory_sys(QUrl::fromLocalFile(newDirectory));
        return;
    }
    if (d->rootPath() == newDirectory)
        return;
    QModelIndex root = d->model->setRootPath(newDirectory);
    if (!d->nativeDialogInUse) {
        d->qFileDialogUi->newFolderButton->setEnabled(d->model->flags(root) & Qt::ItemIsDropEnabled);
        if (root != d->rootIndex()) {
#ifndef QT_NO_FSCOMPLETER
            if (directory.endsWith(QLatin1Char('/')))
                d->completer->setCompletionPrefix(newDirectory);
            else
                d->completer->setCompletionPrefix(newDirectory + QLatin1Char('/'));
#endif
            d->setRootIndex(root);
        }
        d->qFileDialogUi->listView->selectionModel()->clear();
    }
}

/*!
    Returns the directory currently being displayed in the dialog.
*/
QDir QFileDialog::directory() const
{
    Q_D(const QFileDialog);
    if (d->nativeDialogInUse) {
        QString dir = d->directory_sys().toLocalFile();
        return QDir(dir.isEmpty() ? d->options->initialDirectory().toLocalFile() : dir);
    }
    return d->rootPath();
}

/*!
    Sets the file dialog's current \a directory url.

    \note The non-native QFileDialog supports only local files.

    \since 5.2
*/
void QFileDialog::setDirectoryUrl(const QUrl &directory)
{
    Q_D(QFileDialog);
    if (!directory.isValid())
        return;

    if (d->nativeDialogInUse)
        d->setDirectory_sys(directory);
    else if (directory.isLocalFile())
        setDirectory(directory.toLocalFile());
    else
        qWarning() << "Non-native QFileDialog supports only local files";
}

/*!
    Returns the url of the directory currently being displayed in the dialog.

    \since 5.2
*/
QUrl QFileDialog::directoryUrl() const
{
    Q_D(const QFileDialog);
    if (d->nativeDialogInUse)
        return d->directory_sys();
    else
        return QUrl::fromLocalFile(directory().absolutePath());
}

// FIXME Qt 5.4: Use upcoming QVolumeInfo class to determine this information?
static inline bool isCaseSensitiveFileSystem(const QString &path)
{
    Q_UNUSED(path)
#if defined(Q_OS_WIN)
    // Return case insensitive unconditionally, even if someone has a case sensitive
    // file system mounted, wrongly capitalized drive letters will cause mismatches.
    return false;
#elif defined(Q_OS_OSX)
    return pathconf(QFile::encodeName(path).constData(), _PC_CASE_SENSITIVE);
#else
    return true;
#endif
}

// Determine the file name to be set on the line edit from the path
// passed to selectFile() in mode QFileDialog::AcceptSave.
static inline QString fileFromPath(const QString &rootPath, QString path)
{
    if (!QFileInfo(path).isAbsolute())
        return path;
    if (path.startsWith(rootPath, isCaseSensitiveFileSystem(rootPath) ? Qt::CaseSensitive : Qt::CaseInsensitive))
        path.remove(0, rootPath.size());

    if (path.isEmpty())
        return path;

    if (path.at(0) == QDir::separator()
#ifdef Q_OS_WIN
            //On Windows both cases can happen
            || path.at(0) == QLatin1Char('/')
#endif
            ) {
            path.remove(0, 1);
    }
    return path;
}

/*!
    Selects the given \a filename in the file dialog.

    \sa selectedFiles()
*/
void QFileDialog::selectFile(const QString &filename)
{
    Q_D(QFileDialog);
    if (filename.isEmpty())
        return;

    if (!d->usingWidgets()) {
        QUrl url = QUrl::fromLocalFile(filename);
        if (QFileInfo(filename).isRelative()) {
            QDir dir(d->options->initialDirectory().toLocalFile());
            url = QUrl::fromLocalFile(dir.absoluteFilePath(filename));
        }
        d->selectFile_sys(url);
        d->options->setInitiallySelectedFiles(QList<QUrl>() << url);
        return;
    }

    if (!QDir::isRelativePath(filename)) {
        QFileInfo info(filename);
        QString filenamePath = info.absoluteDir().path();

        if (d->model->rootPath() != filenamePath)
            setDirectory(filenamePath);
    }

    QModelIndex index = d->model->index(filename);
    d->qFileDialogUi->listView->selectionModel()->clear();
    if (!isVisible() || !d->lineEdit()->hasFocus())
        d->lineEdit()->setText(index.isValid() ? index.data().toString() : fileFromPath(d->rootPath(), filename));
}

/*!
    Selects the given \a url in the file dialog.

    \note The non-native QFileDialog supports only local files.

    \sa selectedUrls()
    \since 5.2
*/
void QFileDialog::selectUrl(const QUrl &url)
{
    Q_D(QFileDialog);
    if (!url.isValid())
        return;

    if (d->nativeDialogInUse)
        d->selectFile_sys(url);
    else if (url.isLocalFile())
        selectFile(url.toLocalFile());
    else
        qWarning() << "Non-native QFileDialog supports only local files";
}

#ifdef Q_OS_UNIX
Q_AUTOTEST_EXPORT QString qt_tildeExpansion(const QString &path, bool *expanded = 0)
{
    if (expanded != 0)
        *expanded = false;
    if (!path.startsWith(QLatin1Char('~')))
        return path;
    QString ret = path;
    QStringList tokens = ret.split(QDir::separator());
    if (tokens.first() == QLatin1String("~")) {
        ret.replace(0, 1, QDir::homePath());
    } else {
        QString userName = tokens.first();
        userName.remove(0, 1);
#if defined(Q_OS_VXWORKS)
        const QString homePath = QDir::homePath();
#elif defined(_POSIX_THREAD_SAFE_FUNCTIONS) && !defined(Q_OS_OPENBSD)
        passwd pw;
        passwd *tmpPw;
        char buf[200];
        const int bufSize = sizeof(buf);
        int err = 0;
#if defined(Q_OS_SOLARIS) && (_POSIX_C_SOURCE - 0 < 199506L)
        tmpPw = getpwnam_r(userName.toLocal8Bit().constData(), &pw, buf, bufSize);
#else
        err = getpwnam_r(userName.toLocal8Bit().constData(), &pw, buf, bufSize, &tmpPw);
#endif
        if (err || !tmpPw)
            return ret;
        const QString homePath = QString::fromLocal8Bit(pw.pw_dir);
#else
        passwd *pw = getpwnam(userName.toLocal8Bit().constData());
        if (!pw)
            return ret;
        const QString homePath = QString::fromLocal8Bit(pw->pw_dir);
#endif
        ret.replace(0, tokens.first().length(), homePath);
    }
    if (expanded != 0)
        *expanded = true;
    return ret;
}
#endif

/**
    Returns the text in the line edit which can be one or more file names
  */
QStringList QFileDialogPrivate::typedFiles() const
{
    Q_Q(const QFileDialog);
    QStringList files;
    QString editText = lineEdit()->text();
    if (!editText.contains(QLatin1Char('"'))) {
#ifdef Q_OS_UNIX
        const QString prefix = q->directory().absolutePath() + QDir::separator();
        if (QFile::exists(prefix + editText))
            files << editText;
        else
            files << qt_tildeExpansion(editText);
#else
        files << editText;
        Q_UNUSED(q)
#endif
    } else {
        // " is used to separate files like so: "file1" "file2" "file3" ...
        // ### need escape character for filenames with quotes (")
        QStringList tokens = editText.split(QLatin1Char('\"'));
        for (int i=0; i<tokens.size(); ++i) {
            if ((i % 2) == 0)
                continue; // Every even token is a separator
#ifdef Q_OS_UNIX
            const QString token = tokens.at(i);
            const QString prefix = q->directory().absolutePath() + QDir::separator();
            if (QFile::exists(prefix + token))
                files << token;
            else
                files << qt_tildeExpansion(token);
#else
            files << toInternal(tokens.at(i));
#endif
        }
    }
    return addDefaultSuffixToFiles(files);
}

// Return selected files without defaulting to the root of the file system model
// used for initializing QFileDialogOptions for native dialogs. The default is
// not suitable for native dialogs since it mostly equals directory().
QList<QUrl> QFileDialogPrivate::userSelectedFiles() const
{
    QList<QUrl> files;

    if (!usingWidgets())
        return addDefaultSuffixToUrls(selectedFiles_sys());

    foreach (const QModelIndex &index, qFileDialogUi->listView->selectionModel()->selectedRows())
        files.append(QUrl::fromLocalFile(index.data(QFileSystemModel::FilePathRole).toString()));

    if (files.isEmpty() && !lineEdit()->text().isEmpty())
        foreach (const QString &path, typedFiles())
            files.append(QUrl::fromLocalFile(path));

    return files;
}

QByteArray QFileDialogPrivate::userSelectedRemoteContent() const
{
	if (nativeDialogInUse)
		return selectedRemoteContent_sys();

	return QByteArray();
}

QStringList QFileDialogPrivate::addDefaultSuffixToFiles(const QStringList filesToFix) const
{
    QStringList files;
    for (int i=0; i<filesToFix.size(); ++i) {
        QString name = toInternal(filesToFix.at(i));
        QFileInfo info(name);
        // if the filename has no suffix, add the default suffix
        const QString defaultSuffix = options->defaultSuffix();
        if (!defaultSuffix.isEmpty() && !info.isDir() && name.lastIndexOf(QLatin1Char('.')) == -1)
            name += QLatin1Char('.') + defaultSuffix;
        if (info.isAbsolute()) {
            files.append(name);
        } else {
            // at this point the path should only have Qt path separators.
            // This check is needed since we might be at the root directory
            // and on Windows it already ends with slash.
            QString path = rootPath();
            if (!path.endsWith(QLatin1Char('/')))
                path += QLatin1Char('/');
            path += name;
            files.append(path);
        }
    }
    return files;
}

QList<QUrl> QFileDialogPrivate::addDefaultSuffixToUrls(const QList<QUrl> &urlsToFix) const
{
    QList<QUrl> urls;
    for (int i=0; i<urlsToFix.size(); ++i) {
        QUrl url = urlsToFix.at(i);
        // if the filename has no suffix, add the default suffix
        const QString defaultSuffix = options->defaultSuffix();
        if (!defaultSuffix.isEmpty() && !url.path().endsWith(QLatin1Char('/')) && url.path().lastIndexOf(QLatin1Char('.')) == -1)
            url.setPath(url.path() + QLatin1Char('.') + defaultSuffix);
        urls.append(url);
    }
    return urls;
}


/*!
    Returns a list of strings containing the absolute paths of the
    selected files in the dialog. If no files are selected, or
    the mode is not ExistingFiles or ExistingFile, selectedFiles() contains the current path in the viewport.

    \sa selectedNameFilter(), selectFile()
*/
QStringList QFileDialog::selectedFiles() const
{
    Q_D(const QFileDialog);

    QStringList files;
    foreach (const QUrl &file, d->userSelectedFiles())
        files.append(file.toLocalFile());
    if (files.isEmpty() && d->usingWidgets()) {
        const FileMode fm = fileMode();
        if (fm != ExistingFile && fm != ExistingFiles)
            files.append(d->rootIndex().data(QFileSystemModel::FilePathRole).toString());
    }
    return files;
}

QByteArray QFileDialog::selectedRemoteContent() const
{
	Q_D(const QFileDialog);

	return d->userSelectedRemoteContent();
}

/*!
    Returns a list of urls containing the selected files in the dialog.
    If no files are selected, or the mode is not ExistingFiles or
    ExistingFile, selectedUrls() contains the current path in the viewport.

    \sa selectedNameFilter(), selectUrl()
    \since 5.2
*/
QList<QUrl> QFileDialog::selectedUrls() const
{
    Q_D(const QFileDialog);
    if (d->nativeDialogInUse) {
        return d->userSelectedFiles();
    } else {
        QList<QUrl> urls;
        foreach (const QString &file, selectedFiles())
            urls.append(QUrl::fromLocalFile(file));
        return urls;
    }
}

/*
    Makes a list of filters from ;;-separated text.
    Used by the mac and windows implementations
*/
QStringList qt_make_filter_list(const QString &filter)
{
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

/*!
    \since 4.4

    Sets the filter used in the file dialog to the given \a filter.

    If \a filter contains a pair of parentheses containing one or more
    filename-wildcard patterns, separated by spaces, then only the
    text contained in the parentheses is used as the filter. This means
    that these calls are all equivalent:

    \snippet code/src_gui_dialogs_qfiledialog.cpp 6

    \sa setMimeTypeFilters(), setNameFilters()
*/
void QFileDialog::setNameFilter(const QString &filter)
{
    setNameFilters(qt_make_filter_list(filter));
}


/*!
    \property QFileDialog::nameFilterDetailsVisible
    \obsolete
    \brief This property holds whether the filter details is shown or not.
    \since 4.4

    When this property is \c true (the default), the filter details are shown
    in the combo box.  When the property is set to false, these are hidden.

    Use setOption(HideNameFilterDetails, !\e enabled) or
    !testOption(HideNameFilterDetails).
*/
void QFileDialog::setNameFilterDetailsVisible(bool enabled)
{
    setOption(HideNameFilterDetails, !enabled);
}

bool QFileDialog::isNameFilterDetailsVisible() const
{
    return !testOption(HideNameFilterDetails);
}


/*
    Strip the filters by removing the details, e.g. (*.*).
*/
QStringList qt_strip_filters(const QStringList &filters)
{
    QStringList strippedFilters;
    QRegExp r(QString::fromLatin1(QPlatformFileDialogHelper::filterRegExp));
    for (int i = 0; i < filters.count(); ++i) {
        QString filterName;
        int index = r.indexIn(filters[i]);
        if (index >= 0)
            filterName = r.cap(1);
        strippedFilters.append(filterName.simplified());
    }
    return strippedFilters;
}


/*!
    \since 4.4

    Sets the \a filters used in the file dialog.

    Note that the filter \b{*.*} is not portable, because the historical
    assumption that the file extension determines the file type is not
    consistent on every operating system. It is possible to have a file with no
    dot in its name (for example, \c Makefile). In a native Windows file
    dialog, \b{*.*} will match such files, while in other types of file dialogs
    it may not. So it is better to use \b{*} if you mean to select any file.

    \snippet code/src_gui_dialogs_qfiledialog.cpp 7

    \l setMimeTypeFilters() has the advantage of providing all possible name
    filters for each file type. For example, JPEG images have three possible
    extensions; if your application can open such files, selecting the
    \c image/jpeg mime type as a filter will allow you to open all of them.
*/
void QFileDialog::setNameFilters(const QStringList &filters)
{
    Q_D(QFileDialog);
    d->defaultFileTypes = (filters == QStringList(QFileDialog::tr("All Files (*)")));
    QStringList cleanedFilters;
    for (int i = 0; i < filters.count(); ++i) {
        cleanedFilters << filters[i].simplified();
    }
    d->options->setNameFilters(cleanedFilters);

    if (!d->usingWidgets())
        return;

    d->qFileDialogUi->fileTypeCombo->clear();
    if (cleanedFilters.isEmpty())
        return;

    if (testOption(HideNameFilterDetails))
        d->qFileDialogUi->fileTypeCombo->addItems(qt_strip_filters(cleanedFilters));
    else
        d->qFileDialogUi->fileTypeCombo->addItems(cleanedFilters);

    d->_q_useNameFilter(0);
}

/*!
    \since 4.4

    Returns the file type filters that are in operation on this file
    dialog.
*/
QStringList QFileDialog::nameFilters() const
{
    return d_func()->options->nameFilters();
}

/*!
    \since 4.4

    Sets the current file type \a filter. Multiple filters can be
    passed in \a filter by separating them with semicolons or spaces.

    \sa setNameFilter(), setNameFilters(), selectedNameFilter()
*/
void QFileDialog::selectNameFilter(const QString &filter)
{
    Q_D(QFileDialog);
    d->options->setInitiallySelectedNameFilter(filter);
    if (!d->usingWidgets()) {
        d->selectNameFilter_sys(filter);
        return;
    }
    int i = -1;
    if (testOption(HideNameFilterDetails)) {
        const QStringList filters = qt_strip_filters(qt_make_filter_list(filter));
        if (!filters.isEmpty())
            i = d->qFileDialogUi->fileTypeCombo->findText(filters.first());
    } else {
        i = d->qFileDialogUi->fileTypeCombo->findText(filter);
    }
    if (i >= 0) {
        d->qFileDialogUi->fileTypeCombo->setCurrentIndex(i);
        d->_q_useNameFilter(d->qFileDialogUi->fileTypeCombo->currentIndex());
    }
}

/*!
    \since 4.4

    Returns the filter that the user selected in the file dialog.

    \sa selectedFiles()
*/
QString QFileDialog::selectedNameFilter() const
{
    Q_D(const QFileDialog);
    if (!d->usingWidgets())
        return d->selectedNameFilter_sys();

    return d->qFileDialogUi->fileTypeCombo->currentText();
}

/*!
    \since 4.4

    Returns the filter that is used when displaying files.

    \sa setFilter()
*/
QDir::Filters QFileDialog::filter() const
{
    Q_D(const QFileDialog);
    if (d->usingWidgets())
        return d->model->filter();
    return d->options->filter();
}

/*!
    \since 4.4

    Sets the filter used by the model to \a filters. The filter is used
    to specify the kind of files that should be shown.

    \sa filter()
*/

void QFileDialog::setFilter(QDir::Filters filters)
{
    Q_D(QFileDialog);
    d->options->setFilter(filters);
    if (!d->usingWidgets()) {
        d->setFilter_sys();
        return;
    }

    d->model->setFilter(filters);
    d->showHiddenAction->setChecked((filters & QDir::Hidden));
}

static QString nameFilterForMime(const QString &mimeType)
{
    QMimeDatabase db;
    QMimeType mime(db.mimeTypeForName(mimeType));
    if (mime.isValid()) {
        if (mime.isDefault()) {
            return QFileDialog::tr("All files (*)");
        } else {
            const QString patterns = mime.globPatterns().join(QLatin1Char(' '));
            return mime.comment() + QStringLiteral(" (") + patterns + QLatin1Char(')');
        }
    }
    return QString();
}

/*!
    \since 5.2

    Sets the \a filters used in the file dialog, from a list of MIME types.

    Convenience method for setNameFilters().
    Uses QMimeType to create a name filter from the glob patterns and description
    defined in each MIME type.

    Use application/octet-stream for the "All files (*)" filter, since that
    is the base MIME type for all files.

    Calling setMimeTypeFilters overrides any previously set name filters,
    and changes the return value of nameFilters().

    \snippet code/src_gui_dialogs_qfiledialog.cpp 13
*/
void QFileDialog::setMimeTypeFilters(const QStringList &filters)
{
    Q_D(QFileDialog);
    QStringList nameFilters;
    foreach (const QString &mimeType, filters) {
        const QString text = nameFilterForMime(mimeType);
        if (!text.isEmpty())
            nameFilters.append(text);
    }
    setNameFilters(nameFilters);
    d->options->setMimeTypeFilters(filters);
}

/*!
    \since 5.2

    Returns the MIME type filters that are in operation on this file
    dialog.
*/
QStringList QFileDialog::mimeTypeFilters() const
{
    return d_func()->options->mimeTypeFilters();
}

/*!
    \since 5.2

    Sets the current MIME type \a filter.

*/
void QFileDialog::selectMimeTypeFilter(const QString &filter)
{
    const QString text = nameFilterForMime(filter);
    if (!text.isEmpty())
        selectNameFilter(text);
}

/*!
    \property QFileDialog::viewMode
    \brief the way files and directories are displayed in the dialog

    By default, the \c Detail mode is used to display information about
    files and directories.

    \sa ViewMode
*/
void QFileDialog::setViewMode(QFileDialog::ViewMode mode)
{
    Q_D(QFileDialog);
    d->options->setViewMode(static_cast<QFileDialogOptions::ViewMode>(mode));
    if (!d->usingWidgets())
        return;
    if (mode == Detail)
        d->_q_showDetailsView();
    else
        d->_q_showListView();
}

QFileDialog::ViewMode QFileDialog::viewMode() const
{
    Q_D(const QFileDialog);
    if (!d->usingWidgets())
        return static_cast<QFileDialog::ViewMode>(d->options->viewMode());
    return (d->qFileDialogUi->stackedWidget->currentWidget() == d->qFileDialogUi->listView->parent() ? QFileDialog::List : QFileDialog::Detail);
}

/*!
    \property QFileDialog::fileMode
    \brief the file mode of the dialog

    The file mode defines the number and type of items that the user is
    expected to select in the dialog.

    By default, this property is set to AnyFile.

    This function will set the labels for the FileName and
    \l{QFileDialog::}{Accept} \l{DialogLabel}s. It is possible to set
    custom text after the call to setFileMode().

    \sa FileMode
*/
void QFileDialog::setFileMode(QFileDialog::FileMode mode)
{
    Q_D(QFileDialog);
    d->options->setFileMode(static_cast<QFileDialogOptions::FileMode>(mode));

    // keep ShowDirsOnly option in sync with fileMode (BTW, DirectoryOnly is obsolete)
    setOption(ShowDirsOnly, mode == DirectoryOnly);

    if (!d->usingWidgets())
        return;

    d->retranslateWindowTitle();

    // set selection mode and behavior
    QAbstractItemView::SelectionMode selectionMode;
    if (mode == QFileDialog::ExistingFiles)
        selectionMode = QAbstractItemView::ExtendedSelection;
    else
        selectionMode = QAbstractItemView::SingleSelection;
    d->qFileDialogUi->listView->setSelectionMode(selectionMode);
    d->qFileDialogUi->treeView->setSelectionMode(selectionMode);
    // set filter
    d->model->setFilter(d->filterForMode(filter()));
    // setup file type for directory
    if (mode == DirectoryOnly || mode == Directory) {
        d->qFileDialogUi->fileTypeCombo->clear();
        d->qFileDialogUi->fileTypeCombo->addItem(tr("Directories"));
        d->qFileDialogUi->fileTypeCombo->setEnabled(false);
    }
    d->updateFileNameLabel();
    d->updateOkButtonText();
    d->qFileDialogUi->fileTypeCombo->setEnabled(!testOption(ShowDirsOnly));
    d->_q_updateOkButton();
}

QFileDialog::FileMode QFileDialog::fileMode() const
{
    Q_D(const QFileDialog);
    return static_cast<FileMode>(d->options->fileMode());
}

/*!
    \property QFileDialog::acceptMode
    \brief the accept mode of the dialog

    The action mode defines whether the dialog is for opening or saving files.

    By default, this property is set to \l{AcceptOpen}.

    \sa AcceptMode
*/
void QFileDialog::setAcceptMode(QFileDialog::AcceptMode mode)
{
    Q_D(QFileDialog);
    d->options->setAcceptMode(static_cast<QFileDialogOptions::AcceptMode>(mode));
    // clear WA_DontShowOnScreen so that d->canBeNativeDialog() doesn't return false incorrectly
    setAttribute(Qt::WA_DontShowOnScreen, false);
    if (!d->usingWidgets())
        return;
    QDialogButtonBox::StandardButton button = (mode == AcceptOpen ? QDialogButtonBox::Open : QDialogButtonBox::Save);
    d->qFileDialogUi->buttonBox->setStandardButtons(button | QDialogButtonBox::Cancel);
    d->qFileDialogUi->buttonBox->button(button)->setEnabled(false);
    d->_q_updateOkButton();
    if (mode == AcceptSave) {
        d->qFileDialogUi->lookInCombo->setEditable(false);
    }
    d->retranslateWindowTitle();
}

/*
    Returns the file system model index that is the root index in the
    views
*/
QModelIndex QFileDialogPrivate::rootIndex() const {
    return mapToSource(qFileDialogUi->listView->rootIndex());
}

QAbstractItemView *QFileDialogPrivate::currentView() const {
    if (!qFileDialogUi->stackedWidget)
        return 0;
    if (qFileDialogUi->stackedWidget->currentWidget() == qFileDialogUi->listView->parent())
        return qFileDialogUi->listView;
    return qFileDialogUi->treeView;
}

QLineEdit *QFileDialogPrivate::lineEdit() const {
    return (QLineEdit*)qFileDialogUi->fileNameEdit;
}

int QFileDialogPrivate::maxNameLength(const QString &path)
{
#if defined(Q_OS_UNIX)
    return ::pathconf(QFile::encodeName(path).data(), _PC_NAME_MAX);
#elif defined(Q_OS_WINCE) || defined(Q_OS_WINRT)
    Q_UNUSED(path);
    return MAX_PATH;
#elif defined(Q_OS_WIN)
    DWORD maxLength;
    const QString drive = path.left(3);
    if (::GetVolumeInformation(reinterpret_cast<const wchar_t *>(drive.utf16()), NULL, 0, NULL, &maxLength, NULL, NULL, 0) == false)
        return -1;
    return maxLength;
#else
    Q_UNUSED(path);
#endif
    return -1;
}

/*
    Sets the view root index to be the file system model index
*/
void QFileDialogPrivate::setRootIndex(const QModelIndex &index) const {
    Q_ASSERT(index.isValid() ? index.model() == model : true);
    QModelIndex idx = mapFromSource(index);
    qFileDialogUi->treeView->setRootIndex(idx);
    qFileDialogUi->listView->setRootIndex(idx);
}
/*
    Select a file system model index
    returns the index that was selected (or not depending upon sortfilterproxymodel)
*/
QModelIndex QFileDialogPrivate::select(const QModelIndex &index) const {
    Q_ASSERT(index.isValid() ? index.model() == model : true);

    QModelIndex idx = mapFromSource(index);
    if (idx.isValid() && !qFileDialogUi->listView->selectionModel()->isSelected(idx))
        qFileDialogUi->listView->selectionModel()->select(idx,
            QItemSelectionModel::Select | QItemSelectionModel::Rows);
    return idx;
}

QFileDialog::AcceptMode QFileDialog::acceptMode() const
{
    Q_D(const QFileDialog);
    return static_cast<AcceptMode>(d->options->acceptMode());
}

/*!
    \property QFileDialog::readOnly
    \obsolete
    \brief Whether the filedialog is read-only

    If this property is set to false, the file dialog will allow renaming,
    and deleting of files and directories and creating directories.

    Use setOption(ReadOnly, \e enabled) or testOption(ReadOnly) instead.
*/
void QFileDialog::setReadOnly(bool enabled)
{
    setOption(ReadOnly, enabled);
}

bool QFileDialog::isReadOnly() const
{
    return testOption(ReadOnly);
}

/*!
    \property QFileDialog::resolveSymlinks
    \obsolete
    \brief whether the filedialog should resolve shortcuts

    If this property is set to true, the file dialog will resolve
    shortcuts or symbolic links.

    Use setOption(DontResolveSymlinks, !\a enabled) or
    !testOption(DontResolveSymlinks).
*/
void QFileDialog::setResolveSymlinks(bool enabled)
{
    setOption(DontResolveSymlinks, !enabled);
}

bool QFileDialog::resolveSymlinks() const
{
    return !testOption(DontResolveSymlinks);
}

/*!
    \property QFileDialog::confirmOverwrite
    \obsolete
    \brief whether the filedialog should ask before accepting a selected file,
    when the accept mode is AcceptSave

    Use setOption(DontConfirmOverwrite, !\e enabled) or
    !testOption(DontConfirmOverwrite) instead.
*/
void QFileDialog::setConfirmOverwrite(bool enabled)
{
    setOption(DontConfirmOverwrite, !enabled);
}

bool QFileDialog::confirmOverwrite() const
{
    return !testOption(DontConfirmOverwrite);
}

/*!
    \property QFileDialog::defaultSuffix
    \brief suffix added to the filename if no other suffix was specified

    This property specifies a string that will be added to the
    filename if it has no suffix already. The suffix is typically
    used to indicate the file type (e.g. "txt" indicates a text
    file).

    If the first character is a dot ('.'), it is removed.
*/
void QFileDialog::setDefaultSuffix(const QString &suffix)
{
    Q_D(QFileDialog);
    d->options->setDefaultSuffix(suffix);
}

QString QFileDialog::defaultSuffix() const
{
    Q_D(const QFileDialog);
    return d->options->defaultSuffix();
}

/*!
    Sets the browsing history of the filedialog to contain the given
    \a paths.
*/
void QFileDialog::setHistory(const QStringList &paths)
{
    Q_D(QFileDialog);
    if (d->usingWidgets())
        d->qFileDialogUi->lookInCombo->setHistory(paths);
}

void QFileDialogComboBox::setHistory(const QStringList &paths)
{
    m_history = paths;
    // Only populate the first item, showPopup will populate the rest if needed
    QList<QUrl> list;
    QModelIndex idx = d_ptr->model->index(d_ptr->rootPath());
    //On windows the popup display the "C:\", convert to nativeSeparators
    QUrl url = QUrl::fromLocalFile(QDir::toNativeSeparators(idx.data(QFileSystemModel::FilePathRole).toString()));
    if (url.isValid())
        list.append(url);
    urlModel->setUrls(list);
}

/*!
    Returns the browsing history of the filedialog as a list of paths.
*/
QStringList QFileDialog::history() const
{
    Q_D(const QFileDialog);
    if (!d->usingWidgets())
        return QStringList();
    QStringList currentHistory = d->qFileDialogUi->lookInCombo->history();
    //On windows the popup display the "C:\", convert to nativeSeparators
    QString newHistory = QDir::toNativeSeparators(d->rootIndex().data(QFileSystemModel::FilePathRole).toString());
    if (!currentHistory.contains(newHistory))
        currentHistory << newHistory;
    return currentHistory;
}

/*!
    Sets the item delegate used to render items in the views in the
    file dialog to the given \a delegate.

    \warning You should not share the same instance of a delegate between views.
    Doing so can cause incorrect or unintuitive editing behavior since each
    view connected to a given delegate may receive the \l{QAbstractItemDelegate::}{closeEditor()}
    signal, and attempt to access, modify or close an editor that has already been closed.

    Note that the model used is QFileSystemModel. It has custom item data roles, which is
    described by the \l{QFileSystemModel::}{Roles} enum. You can use a QFileIconProvider if
    you only want custom icons.

    \sa itemDelegate(), setIconProvider(), QFileSystemModel
*/
void QFileDialog::setItemDelegate(QAbstractItemDelegate *delegate)
{
    Q_D(QFileDialog);
    if (!d->usingWidgets())
        return;
    d->qFileDialogUi->listView->setItemDelegate(delegate);
    d->qFileDialogUi->treeView->setItemDelegate(delegate);
}

/*!
  Returns the item delegate used to render the items in the views in the filedialog.
*/
QAbstractItemDelegate *QFileDialog::itemDelegate() const
{
    Q_D(const QFileDialog);
    if (!d->usingWidgets())
        return 0;
    return d->qFileDialogUi->listView->itemDelegate();
}

/*!
    Sets the icon provider used by the filedialog to the specified \a provider.
*/
void QFileDialog::setIconProvider(QFileIconProvider *provider)
{
    Q_D(QFileDialog);
    if (!d->usingWidgets())
        return;
    d->model->setIconProvider(provider);
    //It forces the refresh of all entries in the side bar, then we can get new icons
    d->qFileDialogUi->sidebar->setUrls(d->qFileDialogUi->sidebar->urls());
}

/*!
    Returns the icon provider used by the filedialog.
*/
QFileIconProvider *QFileDialog::iconProvider() const
{
    Q_D(const QFileDialog);
    return d->model->iconProvider();
}

void QFileDialogPrivate::setLabelTextControl(QFileDialog::DialogLabel label, const QString &text)
{
    if (!qFileDialogUi)
        return;
    switch (label) {
    case QFileDialog::LookIn:
        qFileDialogUi->lookInLabel->setText(text);
        break;
    case QFileDialog::FileName:
        qFileDialogUi->fileNameLabel->setText(text);
        break;
    case QFileDialog::FileType:
        qFileDialogUi->fileTypeLabel->setText(text);
        break;
    case QFileDialog::Accept:
        if (q_func()->acceptMode() == QFileDialog::AcceptOpen) {
            if (QPushButton *button = qFileDialogUi->buttonBox->button(QDialogButtonBox::Open))
                button->setText(text);
        } else {
            if (QPushButton *button = qFileDialogUi->buttonBox->button(QDialogButtonBox::Save))
                button->setText(text);
        }
        break;
    case QFileDialog::Reject:
        if (QPushButton *button = qFileDialogUi->buttonBox->button(QDialogButtonBox::Cancel))
            button->setText(text);
        break;
    }
}

/*!
    Sets the \a text shown in the filedialog in the specified \a label.
*/

void QFileDialog::setLabelText(DialogLabel label, const QString &text)
{
    Q_D(QFileDialog);
    d->options->setLabelText(static_cast<QFileDialogOptions::DialogLabel>(label), text);
    d->setLabelTextControl(label, text);
}

/*!
    Returns the text shown in the filedialog in the specified \a label.
*/
QString QFileDialog::labelText(DialogLabel label) const
{
    Q_D(const QFileDialog);
    if (!d->usingWidgets())
        return d->options->labelText(static_cast<QFileDialogOptions::DialogLabel>(label));
    QPushButton *button;
    switch (label) {
    case LookIn:
        return d->qFileDialogUi->lookInLabel->text();
    case FileName:
        return d->qFileDialogUi->fileNameLabel->text();
    case FileType:
        return d->qFileDialogUi->fileTypeLabel->text();
    case Accept:
        if (acceptMode() == AcceptOpen)
            button = d->qFileDialogUi->buttonBox->button(QDialogButtonBox::Open);
        else
            button = d->qFileDialogUi->buttonBox->button(QDialogButtonBox::Save);
        if (button)
            return button->text();
    case Reject:
        button = d->qFileDialogUi->buttonBox->button(QDialogButtonBox::Cancel);
        if (button)
            return button->text();
    }
    return QString();
}

/*
    For the native file dialogs
*/

#if defined(Q_WS_WIN)
extern QString qt_win_get_open_file_name(const QFileDialogArgs &args,
                                         QString *initialDirectory,
                                         QString *selectedFilter);

extern QString qt_win_get_save_file_name(const QFileDialogArgs &args,
                                         QString *initialDirectory,
                                         QString *selectedFilter);

extern QStringList qt_win_get_open_file_names(const QFileDialogArgs &args,
                                              QString *initialDirectory,
                                              QString *selectedFilter);

extern QString qt_win_get_existing_directory(const QFileDialogArgs &args);
#endif

/*!
    This is a convenience static function that returns an existing file
    selected by the user. If the user presses Cancel, it returns a null string.

    \snippet code/src_gui_dialogs_qfiledialog.cpp 8

    The function creates a modal file dialog with the given \a parent widget.
    If \a parent is not 0, the dialog will be shown centered over the parent
    widget.

    The file dialog's working directory will be set to \a dir. If \a dir
    includes a file name, the file will be selected. Only files that match the
    given \a filter are shown. The filter selected is set to \a selectedFilter.
    The parameters \a dir, \a selectedFilter, and \a filter may be empty
    strings. If you want multiple filters, separate them with ';;', for
    example:

    \code
    "Images (*.png *.xpm *.jpg);;Text files (*.txt);;XML files (*.xml)"
    \endcode

    The \a options argument holds various options about how to run the dialog,
    see the QFileDialog::Option enum for more information on the flags you can
    pass.

    The dialog's caption is set to \a caption. If \a caption is not specified
    then a default caption will be used.

    On Windows, and Mac OS X, this static function will use the
    native file dialog and not a QFileDialog.

    On Windows the dialog will spin a blocking modal event loop that will not
    dispatch any QTimers, and if \a parent is not 0 then it will position the
    dialog just below the parent's title bar.

    On Unix/X11, the normal behavior of the file dialog is to resolve and
    follow symlinks. For example, if \c{/usr/tmp} is a symlink to \c{/var/tmp},
    the file dialog will change to \c{/var/tmp} after entering \c{/usr/tmp}. If
    \a options includes DontResolveSymlinks, the file dialog will treat
    symlinks as regular directories.

    \warning Do not delete \a parent during the execution of the dialog. If you
    want to do this, you should create the dialog yourself using one of the
    QFileDialog constructors.

    \sa getOpenFileNames(), getSaveFileName(), getExistingDirectory()
*/
QString QFileDialog::getOpenFileName(QWidget *parent,
                               const QString &caption,
                               const QString &dir,
                               const QString &filter,
                               QString *selectedFilter,
                               Options options)
{
    QFileDialogArgs args;
    args.parent = parent;
    args.caption = caption;
    args.directory = QFileDialogPrivate::workingDirectory(dir);
    args.selection = QFileDialogPrivate::initialSelection(dir);
    args.filter = filter;
    args.mode = ExistingFile;
    args.options = options;
#if defined(Q_WS_WIN)
    if (QGuiApplicationPrivate::platformIntegration()->usePlatformNativeDialog() && !(args.options & DontUseNativeDialog)) {
        return qt_win_get_open_file_name(args, &(args.directory), selectedFilter);
    }
#endif

    // create a qt dialog
    QFileDialog dialog(args);
    if (selectedFilter && !selectedFilter->isEmpty())
        dialog.selectNameFilter(*selectedFilter);
    if (dialog.exec() == QDialog::Accepted) {
        if (selectedFilter)
            *selectedFilter = dialog.selectedNameFilter();
        return dialog.selectedFiles().value(0);
    }
    return QString();
}

static inline QUrl dialogResultToUrl(const QString &file)
{
    return file.isEmpty() ? QUrl() : QUrl::fromLocalFile(file);
}

/*!
    This is a convenience static function that returns an existing file
    selected by the user. If the user presses Cancel, it returns an
    empty url.

    The function is used similarly to QFileDialog::getOpenFileName(). In
    particular \a parent, \a caption, \a dir, \a filter, \a selectedFilter
    and \a options are used in the exact same way.

    The main difference with QFileDialog::getOpenFileName() comes from
    the ability offered to the user to select a remote file. That's why
    the return type and the type of \a dir is QUrl.

    The \a supportedSchemes argument allows to restrict the type of URLs the
    user will be able to select. It is a way for the application to declare
    the protocols it will support to fetch the file content. An empty list
    means that no restriction is applied (the default).
    Supported for local files ("file" scheme) is implicit and always enabled.
    it is not necessary to include in the restriction.

    When possible, this static function will use the native file dialog and
    not a QFileDialog. On platforms which don't support selecting remote
    files, Qt will allow to select only local files.

    \sa getOpenFileName(), getOpenFileUrls(), getSaveFileUrl(), getExistingDirectoryUrl()
    \since 5.2
*/
QUrl QFileDialog::getOpenFileUrl(QWidget *parent,
                                 const QString &caption,
                                 const QUrl &dir,
                                 const QString &filter,
                                 QString *selectedFilter,
                                 Options options,
                                 const QStringList &supportedSchemes)
{
    Q_UNUSED(supportedSchemes);

    // Falls back to local file
    return dialogResultToUrl(getOpenFileName(parent, caption, dir.toLocalFile(), filter, selectedFilter, options));
}

/*!
    This is a convenience static function that will return one or more existing
    files selected by the user.

    \snippet code/src_gui_dialogs_qfiledialog.cpp 9

    This function creates a modal file dialog with the given \a parent widget.
    If \a parent is not 0, the dialog will be shown centered over the parent
    widget.

    The file dialog's working directory will be set to \a dir. If \a dir
    includes a file name, the file will be selected. The filter is set to
    \a filter so that only those files which match the filter are shown. The
    filter selected is set to \a selectedFilter. The parameters \a dir,
    \a selectedFilter and \a filter may be empty strings. If you need multiple
    filters, separate them with ';;', for instance:

    \code
    "Images (*.png *.xpm *.jpg);;Text files (*.txt);;XML files (*.xml)"
    \endcode

    The dialog's caption is set to \a caption. If \a caption is not specified
    then a default caption will be used.

    On Windows, and Mac OS X, this static function will use the
    native file dialog and not a QFileDialog.

    On Windows the dialog will spin a blocking modal event loop that will not
    dispatch any QTimers, and if \a parent is not 0 then it will position the
    dialog just below the parent's title bar.

    On Unix/X11, the normal behavior of the file dialog is to resolve and
    follow symlinks. For example, if \c{/usr/tmp} is a symlink to \c{/var/tmp},
    the file dialog will change to \c{/var/tmp} after entering \c{/usr/tmp}.
    The \a options argument holds various options about how to run the dialog,
    see the QFileDialog::Option enum for more information on the flags you can
    pass.

    \note If you want to iterate over the list of files, you should iterate
    over a copy. For example:

    \snippet code/src_gui_dialogs_qfiledialog.cpp 10

    \warning Do not delete \a parent during the execution of the dialog. If you
    want to do this, you should create the dialog yourself using one of the
    QFileDialog constructors.

    \sa getOpenFileName(), getSaveFileName(), getExistingDirectory()
*/
QStringList QFileDialog::getOpenFileNames(QWidget *parent,
                                          const QString &caption,
                                          const QString &dir,
                                          const QString &filter,
                                          QString *selectedFilter,
                                          Options options)
{
    QFileDialogArgs args;
    args.parent = parent;
    args.caption = caption;
    args.directory = QFileDialogPrivate::workingDirectory(dir);
    args.selection = QFileDialogPrivate::initialSelection(dir);
    args.filter = filter;
    args.mode = ExistingFiles;
    args.options = options;

#if defined(Q_WS_WIN)
    if (QGuiApplicationPrivate::platformIntegration()->usePlatformNativeDialog() && !(args.options & DontUseNativeDialog)) {
        return qt_win_get_open_file_names(args, &(args.directory), selectedFilter);
    }
#endif

    // create a qt dialog
    QFileDialog dialog(args);
    if (selectedFilter && !selectedFilter->isEmpty())
        dialog.selectNameFilter(*selectedFilter);
    if (dialog.exec() == QDialog::Accepted) {
        if (selectedFilter)
            *selectedFilter = dialog.selectedNameFilter();
        return dialog.selectedFiles();
    }
    return QStringList();
}

/*!
    This is a convenience static function that will return or or more existing
    files selected by the user. If the user presses Cancel, it returns an
    empty list.

    The function is used similarly to QFileDialog::getOpenFileNames(). In
    particular \a parent, \a caption, \a dir, \a filter, \a selectedFilter
    and \a options are used in the exact same way.

    The main difference with QFileDialog::getOpenFileNames() comes from
    the ability offered to the user to select remote files. That's why
    the return type and the type of \a dir are respectively QList<QUrl>
    and QUrl.

    The \a supportedSchemes argument allows to restrict the type of URLs the
    user will be able to select. It is a way for the application to declare
    the protocols it will support to fetch the file content. An empty list
    means that no restriction is applied (the default).
    Supported for local files ("file" scheme) is implicit and always enabled.
    it is not necessary to include in the restriction.

    When possible, this static function will use the native file dialog and
    not a QFileDialog. On platforms which don't support selecting remote
    files, Qt will allow to select only local files.

    \sa getOpenFileNames(), getOpenFileUrl(), getSaveFileUrl(), getExistingDirectoryUrl()
    \since 5.2
*/
QList<QUrl> QFileDialog::getOpenFileUrls(QWidget *parent,
                                         const QString &caption,
                                         const QUrl &dir,
                                         const QString &filter,
                                         QString *selectedFilter,
                                         Options options,
                                         const QStringList &supportedSchemes)
{
    Q_UNUSED(supportedSchemes);

    // Falls back to local files
    QList<QUrl> urls;

    const QStringList fileNames = getOpenFileNames(parent, caption, dir.toLocalFile(), filter, selectedFilter, options);
    foreach (const QString &fileName, fileNames)
        urls << QUrl::fromLocalFile(fileName);

    return urls;
}

/*!
    This is a convenience static function that will return a file name selected
    by the user. The file does not have to exist.

    It creates a modal file dialog with the given \a parent widget. If
    \a parent is not 0, the dialog will be shown centered over the parent
    widget.

    \snippet code/src_gui_dialogs_qfiledialog.cpp 11

    The file dialog's working directory will be set to \a dir. If \a dir
    includes a file name, the file will be selected. Only files that match the
    \a filter are shown. The filter selected is set to \a selectedFilter. The
    parameters \a dir, \a selectedFilter, and \a filter may be empty strings.
    Multiple filters are separated with ';;'. For instance:

    \code
    "Images (*.png *.xpm *.jpg);;Text files (*.txt);;XML files (*.xml)"
    \endcode

    The \a options argument holds various options about how to run the dialog,
    see the QFileDialog::Option enum for more information on the flags you can
    pass.

    The default filter can be chosen by setting \a selectedFilter to the
    desired value.

    The dialog's caption is set to \a caption. If \a caption is not specified,
    a default caption will be used.

    On Windows, and Mac OS X, this static function will use the
    native file dialog and not a QFileDialog.

    On Windows the dialog will spin a blocking modal event loop that will not
    dispatch any QTimers, and if \a parent is not 0 then it will position the
    dialog just below the parent's title bar. On Mac OS X, with its native file
    dialog, the filter argument is ignored.

    On Unix/X11, the normal behavior of the file dialog is to resolve and
    follow symlinks. For example, if \c{/usr/tmp} is a symlink to \c{/var/tmp},
    the file dialog will change to \c{/var/tmp} after entering \c{/usr/tmp}. If
    \a options includes DontResolveSymlinks the file dialog will treat symlinks
    as regular directories.

    \warning Do not delete \a parent during the execution of the dialog. If you
    want to do this, you should create the dialog yourself using one of the
    QFileDialog constructors.

    \sa getOpenFileName(), getOpenFileNames(), getExistingDirectory()
*/
QString QFileDialog::getSaveFileName(QWidget *parent,
                                     const QString &caption,
                                     const QString &dir,
                                     const QString &filter,
                                     QString *selectedFilter,
                                     Options options)
{
    QFileDialogArgs args;
    args.parent = parent;
    args.caption = caption;
    args.directory = QFileDialogPrivate::workingDirectory(dir);
    args.selection = QFileDialogPrivate::initialSelection(dir);
    args.filter = filter;
    args.mode = AnyFile;
    args.options = options;

#if defined(Q_WS_WIN)
    if (QGuiApplicationPrivate::platformIntegration()->usePlatformNativeDialog() && !(args.options & DontUseNativeDialog)) {
        return qt_win_get_save_file_name(args, &(args.directory), selectedFilter);
    }
#endif

    // create a qt dialog
    QFileDialog dialog(args);
    dialog.setAcceptMode(AcceptSave);
    if (selectedFilter && !selectedFilter->isEmpty())
        dialog.selectNameFilter(*selectedFilter);
    if (dialog.exec() == QDialog::Accepted) {
        if (selectedFilter)
            *selectedFilter = dialog.selectedNameFilter();
        return dialog.selectedFiles().value(0);
    }

    return QString();
}

/*!
    This is a convenience static function that returns a file selected by
    the user. The file does not have to exist. If the user presses Cancel,
    it returns an empty url.

    The function is used similarly to QFileDialog::getSaveFileName(). In
    particular \a parent, \a caption, \a dir, \a filter, \a selectedFilter
    and \a options are used in the exact same way.

    The main difference with QFileDialog::getSaveFileName() comes from
    the ability offered to the user to select a remote file. That's why
    the return type and the type of \a dir is QUrl.

    The \a supportedSchemes argument allows to restrict the type of URLs the
    user will be able to select. It is a way for the application to declare
    the protocols it will support to save the file content. An empty list
    means that no restriction is applied (the default).
    Supported for local files ("file" scheme) is implicit and always enabled.
    it is not necessary to include in the restriction.

    When possible, this static function will use the native file dialog and
    not a QFileDialog. On platforms which don't support selecting remote
    files, Qt will allow to select only local files.

    \sa getSaveFileName(), getOpenFileUrl(), getOpenFileUrls(), getExistingDirectoryUrl()
    \since 5.2
*/
QUrl QFileDialog::getSaveFileUrl(QWidget *parent,
                                 const QString &caption,
                                 const QUrl &dir,
                                 const QString &filter,
                                 QString *selectedFilter,
                                 Options options,
                                 const QStringList &supportedSchemes)
{
    Q_UNUSED(supportedSchemes);

    // Falls back to local file
    return dialogResultToUrl(getSaveFileName(parent, caption, dir.toLocalFile(), filter, selectedFilter, options));
}

/*!
    This is a convenience static function that will return an existing
    directory selected by the user.

    \snippet code/src_gui_dialogs_qfiledialog.cpp 12

    This function creates a modal file dialog with the given \a parent widget.
    If \a parent is not 0, the dialog will be shown centered over the parent
    widget.

    The dialog's working directory is set to \a dir, and the caption is set to
    \a caption. Either of these may be an empty string in which case the
    current directory and a default caption will be used respectively.

    The \a options argument holds various options about how to run the dialog,
    see the QFileDialog::Option enum for more information on the flags you can
    pass. To ensure a native file dialog, \l{QFileDialog::}{ShowDirsOnly} must
    be set.

    On Windows, and Mac OS X, this static function will use the
    native file dialog and not a QFileDialog. On Windows CE, if the device has
    no native file dialog, a QFileDialog will be used.

    On Unix/X11, the normal behavior of the file dialog is to resolve and
    follow symlinks. For example, if \c{/usr/tmp} is a symlink to \c{/var/tmp},
    the file dialog will change to \c{/var/tmp} after entering \c{/usr/tmp}. If
    \a options includes DontResolveSymlinks, the file dialog will treat
    symlinks as regular directories.

    On Windows the dialog will spin a blocking modal event loop that will not
    dispatch any QTimers, and if \a parent is not 0 then it will position the
    dialog just below the parent's title bar.

    \warning Do not delete \a parent during the execution of the dialog. If you
    want to do this, you should create the dialog yourself using one of the
    QFileDialog constructors.

    \sa getOpenFileName(), getOpenFileNames(), getSaveFileName()
*/
QString QFileDialog::getExistingDirectory(QWidget *parent,
                                          const QString &caption,
                                          const QString &dir,
                                          Options options)
{
    QFileDialogArgs args;
    args.parent = parent;
    args.caption = caption;
    args.directory = QFileDialogPrivate::workingDirectory(dir);
    args.mode = (options & ShowDirsOnly ? DirectoryOnly : Directory);
    args.options = options;

#if defined(Q_WS_WIN)
    if (QGuiApplicationPrivate::platformIntegration()->usePlatformNativeDialog() && !(args.options & DontUseNativeDialog) && (options & ShowDirsOnly)
#if defined(Q_OS_WINCE)
        && qt_priv_ptr_valid
#endif
        ) {
        return qt_win_get_existing_directory(args);
    }
#endif

    // create a qt dialog
    QFileDialog dialog(args);
    if (dialog.exec() == QDialog::Accepted) {
        return dialog.selectedFiles().value(0);
    }
    return QString();
}

/*!
    This is a convenience static function that will return an existing
    directory selected by the user. If the user presses Cancel, it
    returns an empty url.

    The function is used similarly to QFileDialog::getExistingDirectory().
    In particular \a parent, \a caption, \a dir and \a options are used
    in the exact same way.

    The main difference with QFileDialog::getExistingDirectory() comes from
    the ability offered to the user to select a remote directory. That's why
    the return type and the type of \a dir is QUrl.

    The \a supportedSchemes argument allows to restrict the type of URLs the
    user will be able to select. It is a way for the application to declare
    the protocols it will support to fetch the file content. An empty list
    means that no restriction is applied (the default).
    Supported for local files ("file" scheme) is implicit and always enabled.
    it is not necessary to include in the restriction.

    When possible, this static function will use the native file dialog and
    not a QFileDialog. On platforms which don't support selecting remote
    files, Qt will allow to select only local files.

    \sa getExistingDirectory(), getOpenFileUrl(), getOpenFileUrls(), getSaveFileUrl()
    \since 5.2
*/
QUrl QFileDialog::getExistingDirectoryUrl(QWidget *parent,
                                          const QString &caption,
                                          const QUrl &dir,
                                          Options options,
                                          const QStringList &supportedSchemes)
{
    Q_UNUSED(supportedSchemes);

    // Falls back to local file
    return dialogResultToUrl(getExistingDirectory(parent, caption, dir.toLocalFile(), options));
}

inline static QString _qt_get_directory(const QString &path)
{
    QFileInfo info = QFileInfo(QDir::current(), path);
    if (info.exists() && info.isDir())
        return QDir::cleanPath(info.absoluteFilePath());
    info.setFile(info.absolutePath());
    if (info.exists() && info.isDir())
        return info.absoluteFilePath();
    return QString();
}
/*
    Get the initial directory path

    \sa initialSelection()
 */
QString QFileDialogPrivate::workingDirectory(const QString &path)
{
    if (!path.isEmpty()) {
        QString directory = _qt_get_directory(path);
        if (!directory.isEmpty())
            return directory;
    }
    QString directory = _qt_get_directory(*lastVisitedDir());
    if (!directory.isEmpty())
        return directory;
    return QDir::currentPath();
}

/*
    Get the initial selection given a path.  The initial directory
    can contain both the initial directory and initial selection
    /home/user/foo.txt

    \sa workingDirectory()
 */
QString QFileDialogPrivate::initialSelection(const QString &path)
{
    if (!path.isEmpty()) {
        QFileInfo info(path);
        if (!info.isDir())
            return info.fileName();
    }
    return QString();
}

/*!
 \reimp
*/
void QFileDialog::done(int result)
{
    Q_D(QFileDialog);

    QDialog::done(result);

    if (d->receiverToDisconnectOnClose) {
        disconnect(this, d->signalToDisconnectOnClose,
                   d->receiverToDisconnectOnClose, d->memberToDisconnectOnClose);
        d->receiverToDisconnectOnClose = 0;
    }
    d->memberToDisconnectOnClose.clear();
    d->signalToDisconnectOnClose.clear();
}

/*!
 \reimp
*/
void QFileDialog::accept()
{
    Q_D(QFileDialog);
    QStringList files = selectedFiles();
    if (files.isEmpty())
        return;
    if (!d->usingWidgets()) {
        d->emitFilesSelected(files);
        QDialog::accept();
        return;
    }

    QString lineEditText = d->lineEdit()->text();
    // "hidden feature" type .. and then enter, and it will move up a dir
    // special case for ".."
    if (lineEditText == QLatin1String("..")) {
        d->_q_navigateToParent();
        const QSignalBlocker blocker(d->qFileDialogUi->fileNameEdit);
        d->lineEdit()->selectAll();
        return;
    }

    switch (fileMode()) {
    case DirectoryOnly:
    case Directory: {
        QString fn = files.first();
        QFileInfo info(fn);
        if (!info.exists())
            info = QFileInfo(d->getEnvironmentVariable(fn));
        if (!info.exists()) {
#ifndef QT_NO_MESSAGEBOX
            QString message = tr("%1\nDirectory not found.\nPlease verify the "
                                          "correct directory name was given.");
            QMessageBox::warning(this, windowTitle(), message.arg(info.fileName()));
#endif // QT_NO_MESSAGEBOX
            return;
        }
        if (info.isDir()) {
            d->emitFilesSelected(files);
            QDialog::accept();
        }
        return;
    }

    case AnyFile: {
        QString fn = files.first();
        QFileInfo info(fn);
        if (info.isDir()) {
            setDirectory(info.absoluteFilePath());
            return;
        }

        if (!info.exists()) {
            int maxNameLength = d->maxNameLength(info.path());
            if (maxNameLength >= 0 && info.fileName().length() > maxNameLength)
                return;
        }

        // check if we have to ask for permission to overwrite the file
        if (!info.exists() || !confirmOverwrite() || acceptMode() == AcceptOpen) {
            d->emitFilesSelected(QStringList(fn));
            QDialog::accept();
#ifndef QT_NO_MESSAGEBOX
        } else {
            if (QMessageBox::warning(this, windowTitle(),
                                     tr("%1 already exists.\nDo you want to replace it?")
                                     .arg(info.fileName()),
                                     QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                    == QMessageBox::Yes) {
                d->emitFilesSelected(QStringList(fn));
                QDialog::accept();
            }
#endif
        }
        return;
    }

    case ExistingFile:
    case ExistingFiles:
        for (int i = 0; i < files.count(); ++i) {
            QFileInfo info(files.at(i));
            if (!info.exists())
                info = QFileInfo(d->getEnvironmentVariable(files.at(i)));
            if (!info.exists()) {
#ifndef QT_NO_MESSAGEBOX
                QString message = tr("%1\nFile not found.\nPlease verify the "
                                     "correct file name was given.");
                QMessageBox::warning(this, windowTitle(), message.arg(info.fileName()));
#endif // QT_NO_MESSAGEBOX
                return;
            }
            if (info.isDir()) {
                setDirectory(info.absoluteFilePath());
                d->lineEdit()->clear();
                return;
            }
        }
        d->emitFilesSelected(files);
        QDialog::accept();
        return;
    }
}

/*!
    \internal

    Create widgets, layout and set default values
*/
void QFileDialogPrivate::init(const QString &directory, const QString &nameFilter,
                              const QString &caption)
{
    Q_Q(QFileDialog);
    if (!caption.isEmpty()) {
        useDefaultCaption = false;
        setWindowTitle = caption;
        q->setWindowTitle(caption);
    }

    q->setAcceptMode(QFileDialog::AcceptOpen);
    nativeDialogInUse = (canBeNativeDialog() && platformFileDialogHelper() != 0);
    if (!nativeDialogInUse)
        createWidgets();
    q->setFileMode(QFileDialog::AnyFile);
    if (!nameFilter.isEmpty())
        q->setNameFilter(nameFilter);
    q->setDirectory(workingDirectory(directory));
    q->selectFile(initialSelection(directory));

#ifndef QT_NO_SETTINGS
    const QSettings settings(QSettings::UserScope, QLatin1String("QtProject"));
    q->restoreState(settings.value(QLatin1String("Qt/filedialog")).toByteArray());
#endif

#if defined(Q_EMBEDDED_SMALLSCREEN)
    qFileDialogUi->lookInLabel->setVisible(false);
    qFileDialogUi->fileNameLabel->setVisible(false);
    qFileDialogUi->fileTypeLabel->setVisible(false);
    qFileDialogUi->sidebar->hide();
#endif

    q->resize(q->sizeHint());
}

/*!
    \internal

    Create the widgets, set properties and connections
*/
void QFileDialogPrivate::createWidgets()
{
    if (qFileDialogUi)
        return;
    Q_Q(QFileDialog);
    model = new QFileSystemModel(q);
    options->setFilter(model->filter());
    model->setObjectName(QLatin1String("qt_filesystem_model"));
    if (QPlatformFileDialogHelper *helper = platformFileDialogHelper())
        model->setNameFilterDisables(helper->defaultNameFilterDisables());
    else
        model->setNameFilterDisables(false);
    if (nativeDialogInUse)
        deletePlatformHelper();
    model->d_func()->disableRecursiveSort = true;
    QFileDialog::connect(model, SIGNAL(fileRenamed(QString,QString,QString)), q, SLOT(_q_fileRenamed(QString,QString,QString)));
    QFileDialog::connect(model, SIGNAL(rootPathChanged(QString)),
            q, SLOT(_q_pathChanged(QString)));
    QFileDialog::connect(model, SIGNAL(rowsInserted(QModelIndex,int,int)),
            q, SLOT(_q_rowsInserted(QModelIndex)));
    model->setReadOnly(false);

    qFileDialogUi.reset(new Ui_QFileDialog());
    qFileDialogUi->setupUi(q);

    QList<QUrl> initialBookmarks;
    initialBookmarks << QUrl::fromLocalFile(QLatin1String(""))
                     << QUrl::fromLocalFile(QDir::homePath());
    qFileDialogUi->sidebar->setModelAndUrls(model, initialBookmarks);
    QFileDialog::connect(qFileDialogUi->sidebar, SIGNAL(goToUrl(QUrl)),
                         q, SLOT(_q_goToUrl(QUrl)));

    QObject::connect(qFileDialogUi->buttonBox, SIGNAL(accepted()), q, SLOT(accept()));
    QObject::connect(qFileDialogUi->buttonBox, SIGNAL(rejected()), q, SLOT(reject()));

    qFileDialogUi->lookInCombo->setFileDialogPrivate(this);
    QObject::connect(qFileDialogUi->lookInCombo, SIGNAL(activated(QString)), q, SLOT(_q_goToDirectory(QString)));

    qFileDialogUi->lookInCombo->setInsertPolicy(QComboBox::NoInsert);
    qFileDialogUi->lookInCombo->setDuplicatesEnabled(false);

    // filename
    qFileDialogUi->fileNameEdit->setFileDialogPrivate(this);
#ifndef QT_NO_SHORTCUT
    qFileDialogUi->fileNameLabel->setBuddy(qFileDialogUi->fileNameEdit);
#endif
#ifndef QT_NO_FSCOMPLETER
    completer = new QFSCompleter(model, q);
    qFileDialogUi->fileNameEdit->setCompleter(completer);
#endif // QT_NO_FSCOMPLETER
    QObject::connect(qFileDialogUi->fileNameEdit, SIGNAL(textChanged(QString)),
            q, SLOT(_q_autoCompleteFileName(QString)));
    QObject::connect(qFileDialogUi->fileNameEdit, SIGNAL(textChanged(QString)),
                     q, SLOT(_q_updateOkButton()));

    QObject::connect(qFileDialogUi->fileNameEdit, SIGNAL(returnPressed()), q, SLOT(accept()));

    // filetype
    qFileDialogUi->fileTypeCombo->setDuplicatesEnabled(false);
    qFileDialogUi->fileTypeCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLength);
    qFileDialogUi->fileTypeCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QObject::connect(qFileDialogUi->fileTypeCombo, SIGNAL(activated(int)),
                     q, SLOT(_q_useNameFilter(int)));
    QObject::connect(qFileDialogUi->fileTypeCombo, SIGNAL(activated(QString)),
                     q, SIGNAL(filterSelected(QString)));

    qFileDialogUi->listView->setFileDialogPrivate(this);
    qFileDialogUi->listView->setModel(model);
    QObject::connect(qFileDialogUi->listView, SIGNAL(activated(QModelIndex)),
                     q, SLOT(_q_enterDirectory(QModelIndex)));
    QObject::connect(qFileDialogUi->listView, SIGNAL(customContextMenuRequested(QPoint)),
                    q, SLOT(_q_showContextMenu(QPoint)));
#ifndef QT_NO_SHORTCUT
    QShortcut *shortcut = new QShortcut(qFileDialogUi->listView);
    shortcut->setKey(QKeySequence(QLatin1String("Delete")));
    QObject::connect(shortcut, SIGNAL(activated()), q, SLOT(_q_deleteCurrent()));
#endif

    qFileDialogUi->treeView->setFileDialogPrivate(this);
    qFileDialogUi->treeView->setModel(model);
    QHeaderView *treeHeader = qFileDialogUi->treeView->header();
    QFontMetrics fm(q->font());
    treeHeader->resizeSection(0, fm.width(QLatin1String("wwwwwwwwwwwwwwwwwwwwwwwwww")));
    treeHeader->resizeSection(1, fm.width(QLatin1String("128.88 GB")));
    treeHeader->resizeSection(2, fm.width(QLatin1String("mp3Folder")));
    treeHeader->resizeSection(3, fm.width(QLatin1String("10/29/81 02:02PM")));
    treeHeader->setContextMenuPolicy(Qt::ActionsContextMenu);

    QActionGroup *showActionGroup = new QActionGroup(q);
    showActionGroup->setExclusive(false);
    QObject::connect(showActionGroup, SIGNAL(triggered(QAction*)),
                     q, SLOT(_q_showHeader(QAction*)));;

    QAbstractItemModel *abstractModel = model;
#ifndef QT_NO_PROXYMODEL
    if (proxyModel)
        abstractModel = proxyModel;
#endif
    for (int i = 1; i < abstractModel->columnCount(QModelIndex()); ++i) {
        QAction *showHeader = new QAction(showActionGroup);
        showHeader->setCheckable(true);
        showHeader->setChecked(true);
        treeHeader->addAction(showHeader);
    }

    QScopedPointer<QItemSelectionModel> selModel(qFileDialogUi->treeView->selectionModel());
    qFileDialogUi->treeView->setSelectionModel(qFileDialogUi->listView->selectionModel());

    QObject::connect(qFileDialogUi->treeView, SIGNAL(activated(QModelIndex)),
                     q, SLOT(_q_enterDirectory(QModelIndex)));
    QObject::connect(qFileDialogUi->treeView, SIGNAL(customContextMenuRequested(QPoint)),
                     q, SLOT(_q_showContextMenu(QPoint)));
#ifndef QT_NO_SHORTCUT
    shortcut = new QShortcut(qFileDialogUi->treeView);
    shortcut->setKey(QKeySequence(QLatin1String("Delete")));
    QObject::connect(shortcut, SIGNAL(activated()), q, SLOT(_q_deleteCurrent()));
#endif

    // Selections
    QItemSelectionModel *selections = qFileDialogUi->listView->selectionModel();
    QObject::connect(selections, SIGNAL(selectionChanged(QItemSelection,QItemSelection)),
                     q, SLOT(_q_selectionChanged()));
    QObject::connect(selections, SIGNAL(currentChanged(QModelIndex,QModelIndex)),
                     q, SLOT(_q_currentChanged(QModelIndex)));
    qFileDialogUi->splitter->setStretchFactor(qFileDialogUi->splitter->indexOf(qFileDialogUi->splitter->widget(1)), QSizePolicy::Expanding);

    createToolButtons();
    createMenuActions();

#ifndef QT_NO_SETTINGS
    const QSettings settings(QSettings::UserScope, QLatin1String("QtProject"));
    q->restoreState(settings.value(QLatin1String("Qt/filedialog")).toByteArray());
#endif

    // Initial widget states from options
    q->setFileMode(static_cast<QFileDialog::FileMode>(options->fileMode()));
    q->setAcceptMode(static_cast<QFileDialog::AcceptMode>(options->acceptMode()));
    q->setViewMode(static_cast<QFileDialog::ViewMode>(options->viewMode()));
    q->setOptions(static_cast<QFileDialog::Options>(static_cast<int>(options->options())));
    if (!options->sidebarUrls().isEmpty())
        q->setSidebarUrls(options->sidebarUrls());
    q->setDirectoryUrl(options->initialDirectory());
    if (!options->mimeTypeFilters().isEmpty())
        q->setMimeTypeFilters(options->mimeTypeFilters());
    else if (!options->nameFilters().isEmpty())
        q->setNameFilters(options->nameFilters());
    q->selectNameFilter(options->initiallySelectedNameFilter());
    q->setDefaultSuffix(options->defaultSuffix());
    q->setHistory(options->history());
    if (options->initiallySelectedFiles().count() == 1)
        q->selectFile(options->initiallySelectedFiles().first().fileName());
    foreach (QUrl url, options->initiallySelectedFiles())
        q->selectUrl(url);
    lineEdit()->selectAll();
    _q_updateOkButton();
    retranslateStrings();
    q->resize(q->sizeHint());
}

void QFileDialogPrivate::_q_showHeader(QAction *action)
{
    Q_Q(QFileDialog);
    QActionGroup *actionGroup = qobject_cast<QActionGroup*>(q->sender());
    qFileDialogUi->treeView->header()->setSectionHidden(actionGroup->actions().indexOf(action) + 1, !action->isChecked());
}

#ifndef QT_NO_PROXYMODEL
/*!
    \since 4.3

    Sets the model for the views to the given \a proxyModel.  This is useful if you
    want to modify the underlying model; for example, to add columns, filter
    data or add drives.

    Any existing proxy model will be removed, but not deleted.  The file dialog
    will take ownership of the \a proxyModel.

    \sa proxyModel()
*/
void QFileDialog::setProxyModel(QAbstractProxyModel *proxyModel)
{
    Q_D(QFileDialog);
    if (!d->usingWidgets())
        return;
    if ((!proxyModel && !d->proxyModel)
        || (proxyModel == d->proxyModel))
        return;

    QModelIndex idx = d->rootIndex();
    if (d->proxyModel) {
        disconnect(d->proxyModel, SIGNAL(rowsInserted(QModelIndex,int,int)),
            this, SLOT(_q_rowsInserted(QModelIndex)));
    } else {
        disconnect(d->model, SIGNAL(rowsInserted(QModelIndex,int,int)),
            this, SLOT(_q_rowsInserted(QModelIndex)));
    }

    if (proxyModel != 0) {
        proxyModel->setParent(this);
        d->proxyModel = proxyModel;
        proxyModel->setSourceModel(d->model);
        d->qFileDialogUi->listView->setModel(d->proxyModel);
        d->qFileDialogUi->treeView->setModel(d->proxyModel);
#ifndef QT_NO_FSCOMPLETER
        d->completer->setModel(d->proxyModel);
        d->completer->proxyModel = d->proxyModel;
#endif
        connect(d->proxyModel, SIGNAL(rowsInserted(QModelIndex,int,int)),
            this, SLOT(_q_rowsInserted(QModelIndex)));
    } else {
        d->proxyModel = 0;
        d->qFileDialogUi->listView->setModel(d->model);
        d->qFileDialogUi->treeView->setModel(d->model);
#ifndef QT_NO_FSCOMPLETER
        d->completer->setModel(d->model);
        d->completer->sourceModel = d->model;
        d->completer->proxyModel = 0;
#endif
        connect(d->model, SIGNAL(rowsInserted(QModelIndex,int,int)),
            this, SLOT(_q_rowsInserted(QModelIndex)));
    }
    QScopedPointer<QItemSelectionModel> selModel(d->qFileDialogUi->treeView->selectionModel());
    d->qFileDialogUi->treeView->setSelectionModel(d->qFileDialogUi->listView->selectionModel());

    d->setRootIndex(idx);

    // reconnect selection
    QItemSelectionModel *selections = d->qFileDialogUi->listView->selectionModel();
    QObject::connect(selections, SIGNAL(selectionChanged(QItemSelection,QItemSelection)),
                     this, SLOT(_q_selectionChanged()));
    QObject::connect(selections, SIGNAL(currentChanged(QModelIndex,QModelIndex)),
                     this, SLOT(_q_currentChanged(QModelIndex)));
}

/*!
    Returns the proxy model used by the file dialog.  By default no proxy is set.

    \sa setProxyModel()
*/
QAbstractProxyModel *QFileDialog::proxyModel() const
{
    Q_D(const QFileDialog);
    return d->proxyModel;
}
#endif // QT_NO_PROXYMODEL

/*!
    \internal

    Create tool buttons, set properties and connections
*/
void QFileDialogPrivate::createToolButtons()
{
    Q_Q(QFileDialog);
    qFileDialogUi->backButton->setIcon(q->style()->standardIcon(QStyle::SP_ArrowBack, 0, q));
    qFileDialogUi->backButton->setAutoRaise(true);
    qFileDialogUi->backButton->setEnabled(false);
    QObject::connect(qFileDialogUi->backButton, SIGNAL(clicked()), q, SLOT(_q_navigateBackward()));

    qFileDialogUi->forwardButton->setIcon(q->style()->standardIcon(QStyle::SP_ArrowForward, 0, q));
    qFileDialogUi->forwardButton->setAutoRaise(true);
    qFileDialogUi->forwardButton->setEnabled(false);
    QObject::connect(qFileDialogUi->forwardButton, SIGNAL(clicked()), q, SLOT(_q_navigateForward()));

    qFileDialogUi->toParentButton->setIcon(q->style()->standardIcon(QStyle::SP_FileDialogToParent, 0, q));
    qFileDialogUi->toParentButton->setAutoRaise(true);
    qFileDialogUi->toParentButton->setEnabled(false);
    QObject::connect(qFileDialogUi->toParentButton, SIGNAL(clicked()), q, SLOT(_q_navigateToParent()));

    qFileDialogUi->listModeButton->setIcon(q->style()->standardIcon(QStyle::SP_FileDialogListView, 0, q));
    qFileDialogUi->listModeButton->setAutoRaise(true);
    qFileDialogUi->listModeButton->setDown(true);
    QObject::connect(qFileDialogUi->listModeButton, SIGNAL(clicked()), q, SLOT(_q_showListView()));

    qFileDialogUi->detailModeButton->setIcon(q->style()->standardIcon(QStyle::SP_FileDialogDetailedView, 0, q));
    qFileDialogUi->detailModeButton->setAutoRaise(true);
    QObject::connect(qFileDialogUi->detailModeButton, SIGNAL(clicked()), q, SLOT(_q_showDetailsView()));

    QSize toolSize(qFileDialogUi->fileNameEdit->sizeHint().height(), qFileDialogUi->fileNameEdit->sizeHint().height());
    qFileDialogUi->backButton->setFixedSize(toolSize);
    qFileDialogUi->listModeButton->setFixedSize(toolSize);
    qFileDialogUi->detailModeButton->setFixedSize(toolSize);
    qFileDialogUi->forwardButton->setFixedSize(toolSize);
    qFileDialogUi->toParentButton->setFixedSize(toolSize);

    qFileDialogUi->newFolderButton->setIcon(q->style()->standardIcon(QStyle::SP_FileDialogNewFolder, 0, q));
    qFileDialogUi->newFolderButton->setFixedSize(toolSize);
    qFileDialogUi->newFolderButton->setAutoRaise(true);
    qFileDialogUi->newFolderButton->setEnabled(false);
    QObject::connect(qFileDialogUi->newFolderButton, SIGNAL(clicked()), q, SLOT(_q_createDirectory()));
}

/*!
    \internal

    Create actions which will be used in the right click.
*/
void QFileDialogPrivate::createMenuActions()
{
    Q_Q(QFileDialog);

    QAction *goHomeAction =  new QAction(q);
#ifndef QT_NO_SHORTCUT
    goHomeAction->setShortcut(Qt::CTRL + Qt::Key_H + Qt::SHIFT);
#endif
    QObject::connect(goHomeAction, SIGNAL(triggered()), q, SLOT(_q_goHome()));
    q->addAction(goHomeAction);

    // ### TODO add Desktop & Computer actions

    QAction *goToParent =  new QAction(q);
    goToParent->setObjectName(QLatin1String("qt_goto_parent_action"));
#ifndef QT_NO_SHORTCUT
    goToParent->setShortcut(Qt::CTRL + Qt::UpArrow);
#endif
    QObject::connect(goToParent, SIGNAL(triggered()), q, SLOT(_q_navigateToParent()));
    q->addAction(goToParent);

    renameAction = new QAction(q);
    renameAction->setEnabled(false);
    renameAction->setObjectName(QLatin1String("qt_rename_action"));
    QObject::connect(renameAction, SIGNAL(triggered()), q, SLOT(_q_renameCurrent()));

    deleteAction = new QAction(q);
    deleteAction->setEnabled(false);
    deleteAction->setObjectName(QLatin1String("qt_delete_action"));
    QObject::connect(deleteAction, SIGNAL(triggered()), q, SLOT(_q_deleteCurrent()));

    showHiddenAction = new QAction(q);
    showHiddenAction->setObjectName(QLatin1String("qt_show_hidden_action"));
    showHiddenAction->setCheckable(true);
    QObject::connect(showHiddenAction, SIGNAL(triggered()), q, SLOT(_q_showHidden()));

    newFolderAction = new QAction(q);
    newFolderAction->setObjectName(QLatin1String("qt_new_folder_action"));
    QObject::connect(newFolderAction, SIGNAL(triggered()), q, SLOT(_q_createDirectory()));
}

void QFileDialogPrivate::_q_goHome()
{
    Q_Q(QFileDialog);
    q->setDirectory(QDir::homePath());
}

/*!
    \internal

    Update history with new path, buttons, and combo
*/
void QFileDialogPrivate::_q_pathChanged(const QString &newPath)
{
    Q_Q(QFileDialog);
    QDir dir(model->rootDirectory());
    qFileDialogUi->toParentButton->setEnabled(dir.exists());
    qFileDialogUi->sidebar->selectUrl(QUrl::fromLocalFile(newPath));
    q->setHistory(qFileDialogUi->lookInCombo->history());

    if (currentHistoryLocation < 0 || currentHistory.value(currentHistoryLocation) != QDir::toNativeSeparators(newPath)) {
        while (currentHistoryLocation >= 0 && currentHistoryLocation + 1 < currentHistory.count()) {
            currentHistory.removeLast();
        }
        currentHistory.append(QDir::toNativeSeparators(newPath));
        ++currentHistoryLocation;
    }
    qFileDialogUi->forwardButton->setEnabled(currentHistory.size() - currentHistoryLocation > 1);
    qFileDialogUi->backButton->setEnabled(currentHistoryLocation > 0);
}

/*!
    \internal

    Navigates to the last directory viewed in the dialog.
*/
void QFileDialogPrivate::_q_navigateBackward()
{
    Q_Q(QFileDialog);
    if (!currentHistory.isEmpty() && currentHistoryLocation > 0) {
        --currentHistoryLocation;
        QString previousHistory = currentHistory.at(currentHistoryLocation);
        q->setDirectory(previousHistory);
    }
}

/*!
    \internal

    Navigates to the last directory viewed in the dialog.
*/
void QFileDialogPrivate::_q_navigateForward()
{
    Q_Q(QFileDialog);
    if (!currentHistory.isEmpty() && currentHistoryLocation < currentHistory.size() - 1) {
        ++currentHistoryLocation;
        QString nextHistory = currentHistory.at(currentHistoryLocation);
        q->setDirectory(nextHistory);
    }
}

/*!
    \internal

    Navigates to the parent directory of the currently displayed directory
    in the dialog.
*/
void QFileDialogPrivate::_q_navigateToParent()
{
    Q_Q(QFileDialog);
    QDir dir(model->rootDirectory());
    QString newDirectory;
    if (dir.isRoot()) {
        newDirectory = model->myComputer().toString();
    } else {
        dir.cdUp();
        newDirectory = dir.absolutePath();
    }
    q->setDirectory(newDirectory);
    emit q->directoryEntered(newDirectory);
}

/*!
    \internal

    Creates a new directory, first asking the user for a suitable name.
*/
void QFileDialogPrivate::_q_createDirectory()
{
    Q_Q(QFileDialog);
    qFileDialogUi->listView->clearSelection();

    QString newFolderString = QFileDialog::tr("New Folder");
    QString folderName = newFolderString;
    QString prefix = q->directory().absolutePath() + QDir::separator();
    if (QFile::exists(prefix + folderName)) {
        qlonglong suffix = 2;
        while (QFile::exists(prefix + folderName)) {
            folderName = newFolderString + QString::number(suffix++);
        }
    }

    QModelIndex parent = rootIndex();
    QModelIndex index = model->mkdir(parent, folderName);
    if (!index.isValid())
        return;

    index = select(index);
    if (index.isValid()) {
        qFileDialogUi->treeView->setCurrentIndex(index);
        currentView()->edit(index);
    }
}

void QFileDialogPrivate::_q_showListView()
{
    qFileDialogUi->listModeButton->setDown(true);
    qFileDialogUi->detailModeButton->setDown(false);
    qFileDialogUi->treeView->hide();
    qFileDialogUi->listView->show();
    qFileDialogUi->stackedWidget->setCurrentWidget(qFileDialogUi->listView->parentWidget());
    qFileDialogUi->listView->doItemsLayout();
}

void QFileDialogPrivate::_q_showDetailsView()
{
    qFileDialogUi->listModeButton->setDown(false);
    qFileDialogUi->detailModeButton->setDown(true);
    qFileDialogUi->listView->hide();
    qFileDialogUi->treeView->show();
    qFileDialogUi->stackedWidget->setCurrentWidget(qFileDialogUi->treeView->parentWidget());
    qFileDialogUi->treeView->doItemsLayout();
}

/*!
    \internal

    Show the context menu for the file/dir under position
*/
void QFileDialogPrivate::_q_showContextMenu(const QPoint &position)
{
#ifdef QT_NO_MENU
    Q_UNUSED(position);
#else
    Q_Q(QFileDialog);
    QAbstractItemView *view = 0;
    if (q->viewMode() == QFileDialog::Detail)
        view = qFileDialogUi->treeView;
    else
        view = qFileDialogUi->listView;
    QModelIndex index = view->indexAt(position);
    index = mapToSource(index.sibling(index.row(), 0));

    QMenu menu(view);
    if (index.isValid()) {
        // file context menu
        const bool ro = model && model->isReadOnly();
        QFile::Permissions p(index.parent().data(QFileSystemModel::FilePermissions).toInt());
        renameAction->setEnabled(!ro && p & QFile::WriteUser);
        menu.addAction(renameAction);
        deleteAction->setEnabled(!ro && p & QFile::WriteUser);
        menu.addAction(deleteAction);
        menu.addSeparator();
    }
    menu.addAction(showHiddenAction);
    if (qFileDialogUi->newFolderButton->isVisible()) {
        newFolderAction->setEnabled(qFileDialogUi->newFolderButton->isEnabled());
        menu.addAction(newFolderAction);
    }
    menu.exec(view->viewport()->mapToGlobal(position));
#endif // QT_NO_MENU
}

/*!
    \internal
*/
void QFileDialogPrivate::_q_renameCurrent()
{
    Q_Q(QFileDialog);
    QModelIndex index = qFileDialogUi->listView->currentIndex();
    index = index.sibling(index.row(), 0);
    if (q->viewMode() == QFileDialog::List)
        qFileDialogUi->listView->edit(index);
    else
        qFileDialogUi->treeView->edit(index);
}

bool QFileDialogPrivate::removeDirectory(const QString &path)
{
    QModelIndex modelIndex = model->index(path);
    return model->remove(modelIndex);
}

/*!
    \internal

    Deletes the currently selected item in the dialog.
*/
void QFileDialogPrivate::_q_deleteCurrent()
{
    if (model->isReadOnly())
        return;

    QModelIndexList list = qFileDialogUi->listView->selectionModel()->selectedRows();
    for (int i = list.count() - 1; i >= 0; --i) {
        QModelIndex index = list.at(i);
        if (index == qFileDialogUi->listView->rootIndex())
            continue;

        index = mapToSource(index.sibling(index.row(), 0));
        if (!index.isValid())
            continue;

    QString fileName = index.data(QFileSystemModel::FileNameRole).toString();
    QString filePath = index.data(QFileSystemModel::FilePathRole).toString();
    bool isDir = model->isDir(index);

    QFile::Permissions p(index.parent().data(QFileSystemModel::FilePermissions).toInt());
#ifndef QT_NO_MESSAGEBOX
    Q_Q(QFileDialog);
    if (!(p & QFile::WriteUser) && (QMessageBox::warning(q_func(), QFileDialog::tr("Delete"),
                                QFileDialog::tr("'%1' is write protected.\nDo you want to delete it anyway?")
                                .arg(fileName),
                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::No))
        return;
    else if (QMessageBox::warning(q_func(), QFileDialog::tr("Delete"),
                                  QFileDialog::tr("Are you sure you want to delete '%1'?")
                                  .arg(fileName),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::No)
        return;

#else
    if (!(p & QFile::WriteUser))
        return;
#endif // QT_NO_MESSAGEBOX

        // the event loop has run, we can NOT reuse index because the model might have removed it.
        if (isDir) {
            if (!removeDirectory(filePath)) {
#ifndef QT_NO_MESSAGEBOX
            QMessageBox::warning(q, q->windowTitle(),
                                QFileDialog::tr("Could not delete directory."));
#endif
            }
        } else {
            model->remove(index);
        }
    }
}

void QFileDialogPrivate::_q_autoCompleteFileName(const QString &text)
{
    if (text.startsWith(QLatin1String("//")) || text.startsWith(QLatin1Char('\\'))) {
        qFileDialogUi->listView->selectionModel()->clearSelection();
        return;
    }

    QStringList multipleFiles = typedFiles();
    if (multipleFiles.count() > 0) {
        QModelIndexList oldFiles = qFileDialogUi->listView->selectionModel()->selectedRows();
        QModelIndexList newFiles;
        for (int i = 0; i < multipleFiles.count(); ++i) {
            QModelIndex idx = model->index(multipleFiles.at(i));
            if (oldFiles.contains(idx))
                oldFiles.removeAll(idx);
            else
                newFiles.append(idx);
        }
        for (int i = 0; i < newFiles.count(); ++i)
            select(newFiles.at(i));
        if (lineEdit()->hasFocus())
            for (int i = 0; i < oldFiles.count(); ++i)
                qFileDialogUi->listView->selectionModel()->select(oldFiles.at(i),
                    QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
    }
}

/*!
    \internal
*/
void QFileDialogPrivate::_q_updateOkButton()
{
    Q_Q(QFileDialog);
    QPushButton *button =  qFileDialogUi->buttonBox->button((q->acceptMode() == QFileDialog::AcceptOpen)
                    ? QDialogButtonBox::Open : QDialogButtonBox::Save);
    if (!button)
        return;
    const QFileDialog::FileMode fileMode = q->fileMode();

    bool enableButton = true;
    bool isOpenDirectory = false;

    QStringList files = q->selectedFiles();
    QString lineEditText = lineEdit()->text();

    if (lineEditText.startsWith(QLatin1String("//")) || lineEditText.startsWith(QLatin1Char('\\'))) {
        button->setEnabled(true);
        updateOkButtonText();
        return;
    }

    if (files.isEmpty()) {
        enableButton = false;
    } else if (lineEditText == QLatin1String("..")) {
        isOpenDirectory = true;
    } else {
        switch (fileMode) {
        case QFileDialog::DirectoryOnly:
        case QFileDialog::Directory: {
            QString fn = files.first();
            QModelIndex idx = model->index(fn);
            if (!idx.isValid())
                idx = model->index(getEnvironmentVariable(fn));
            if (!idx.isValid() || !model->isDir(idx))
                enableButton = false;
            break;
        }
        case QFileDialog::AnyFile: {
            QString fn = files.first();
            QFileInfo info(fn);
            QModelIndex idx = model->index(fn);
            QString fileDir;
            QString fileName;
            if (info.isDir()) {
                fileDir = info.canonicalFilePath();
            } else {
                fileDir = fn.mid(0, fn.lastIndexOf(QLatin1Char('/')));
                fileName = fn.mid(fileDir.length() + 1);
            }
            if (lineEditText.contains(QLatin1String(".."))) {
                fileDir = info.canonicalFilePath();
                fileName = info.fileName();
            }

            if (fileDir == q->directory().canonicalPath() && fileName.isEmpty()) {
                enableButton = false;
                break;
            }
            if (idx.isValid() && model->isDir(idx)) {
                isOpenDirectory = true;
                enableButton = true;
                break;
            }
            if (!idx.isValid()) {
                int maxLength = maxNameLength(fileDir);
                enableButton = maxLength < 0 || fileName.length() <= maxLength;
            }
            break;
        }
        case QFileDialog::ExistingFile:
        case QFileDialog::ExistingFiles:
            for (int i = 0; i < files.count(); ++i) {
                QModelIndex idx = model->index(files.at(i));
                if (!idx.isValid())
                    idx = model->index(getEnvironmentVariable(files.at(i)));
                if (!idx.isValid()) {
                    enableButton = false;
                    break;
                }
                if (idx.isValid() && model->isDir(idx)) {
                    isOpenDirectory = true;
                    break;
                }
            }
            break;
        default:
            break;
        }
    }

    button->setEnabled(enableButton);
    updateOkButtonText(isOpenDirectory);
}

/*!
    \internal
*/
void QFileDialogPrivate::_q_currentChanged(const QModelIndex &index)
{
    _q_updateOkButton();
    emit q_func()->currentChanged(index.data(QFileSystemModel::FilePathRole).toString());
}

/*!
    \internal

    This is called when the user double clicks on a file with the corresponding
    model item \a index.
*/
void QFileDialogPrivate::_q_enterDirectory(const QModelIndex &index)
{
    Q_Q(QFileDialog);
    // My Computer or a directory
    QModelIndex sourceIndex = index.model() == proxyModel ? mapToSource(index) : index;
    QString path = sourceIndex.data(QFileSystemModel::FilePathRole).toString();
    if (path.isEmpty() || model->isDir(sourceIndex)) {
        const QFileDialog::FileMode fileMode = q->fileMode();
        q->setDirectory(path);
        emit q->directoryEntered(path);
        if (fileMode == QFileDialog::Directory
                || fileMode == QFileDialog::DirectoryOnly) {
            // ### find out why you have to do both of these.
            lineEdit()->setText(QString());
            lineEdit()->clear();
        }
    } else {
        // Do not accept when shift-clicking to multi-select a file in environments with single-click-activation (KDE)
        if (!q->style()->styleHint(QStyle::SH_ItemView_ActivateItemOnSingleClick)
            || q->fileMode() != QFileDialog::ExistingFiles || !(QGuiApplication::keyboardModifiers() & Qt::CTRL)) {
            q->accept();
        }
    }
}

/*!
    \internal

    Changes the file dialog's current directory to the one specified
    by \a path.
*/
void QFileDialogPrivate::_q_goToDirectory(const QString &path)
{
 #ifndef QT_NO_MESSAGEBOX
    Q_Q(QFileDialog);
#endif
    QModelIndex index = qFileDialogUi->lookInCombo->model()->index(qFileDialogUi->lookInCombo->currentIndex(),
                                                    qFileDialogUi->lookInCombo->modelColumn(),
                                                    qFileDialogUi->lookInCombo->rootModelIndex());
    QString path2 = path;
    if (!index.isValid())
        index = mapFromSource(model->index(getEnvironmentVariable(path)));
    else {
        path2 = index.data(UrlRole).toUrl().toLocalFile();
        index = mapFromSource(model->index(path2));
    }
    QDir dir(path2);
    if (!dir.exists())
        dir = getEnvironmentVariable(path2);

    if (dir.exists() || path2.isEmpty() || path2 == model->myComputer().toString()) {
        _q_enterDirectory(index);
#ifndef QT_NO_MESSAGEBOX
    } else {
        QString message = QFileDialog::tr("%1\nDirectory not found.\nPlease verify the "
                                          "correct directory name was given.");
        QMessageBox::warning(q, q->windowTitle(), message.arg(path2));
#endif // QT_NO_MESSAGEBOX
    }
}

/*!
    \internal

    Sets the current name filter to be nameFilter and
    update the qFileDialogUi->fileNameEdit when in AcceptSave mode with the new extension.
*/
void QFileDialogPrivate::_q_useNameFilter(int index)
{
    QStringList nameFilters = options->nameFilters();
    if (index == nameFilters.size()) {
        QAbstractItemModel *comboModel = qFileDialogUi->fileTypeCombo->model();
        nameFilters.append(comboModel->index(comboModel->rowCount() - 1, 0).data().toString());
        options->setNameFilters(nameFilters);
    }

    QString nameFilter = nameFilters.at(index);
    QStringList newNameFilters = QPlatformFileDialogHelper::cleanFilterList(nameFilter);
    if (q_func()->acceptMode() == QFileDialog::AcceptSave) {
        QString newNameFilterExtension;
        if (newNameFilters.count() > 0)
            newNameFilterExtension = QFileInfo(newNameFilters.at(0)).suffix();

        QString fileName = lineEdit()->text();
        const QString fileNameExtension = QFileInfo(fileName).suffix();
        if (!fileNameExtension.isEmpty() && !newNameFilterExtension.isEmpty()) {
            const int fileNameExtensionLength = fileNameExtension.count();
            fileName.replace(fileName.count() - fileNameExtensionLength,
                             fileNameExtensionLength, newNameFilterExtension);
            qFileDialogUi->listView->clearSelection();
            lineEdit()->setText(fileName);
        }
    }

    model->setNameFilters(newNameFilters);
}

/*!
    \internal

    This is called when the model index corresponding to the current file is changed
    from \a index to \a current.
*/
void QFileDialogPrivate::_q_selectionChanged()
{
    const QFileDialog::FileMode fileMode = q_func()->fileMode();
    QModelIndexList indexes = qFileDialogUi->listView->selectionModel()->selectedRows();
    bool stripDirs = (fileMode != QFileDialog::DirectoryOnly && fileMode != QFileDialog::Directory);

    QStringList allFiles;
    for (int i = 0; i < indexes.count(); ++i) {
        if (stripDirs && model->isDir(mapToSource(indexes.at(i))))
            continue;
        allFiles.append(indexes.at(i).data().toString());
    }
    if (allFiles.count() > 1)
        for (int i = 0; i < allFiles.count(); ++i) {
            allFiles.replace(i, QString(QLatin1Char('"') + allFiles.at(i) + QLatin1Char('"')));
    }

    QString finalFiles = allFiles.join(QLatin1Char(' '));
    if (!finalFiles.isEmpty() && !lineEdit()->hasFocus() && lineEdit()->isVisible())
        lineEdit()->setText(finalFiles);
    else
        _q_updateOkButton();
}

/*!
    \internal

    Includes hidden files and directories in the items displayed in the dialog.
*/
void QFileDialogPrivate::_q_showHidden()
{
    Q_Q(QFileDialog);
    QDir::Filters dirFilters = q->filter();
    if (showHiddenAction->isChecked())
        dirFilters |= QDir::Hidden;
    else
        dirFilters &= ~QDir::Hidden;
    q->setFilter(dirFilters);
}

/*!
    \internal

    When parent is root and rows have been inserted when none was there before
    then select the first one.
*/
void QFileDialogPrivate::_q_rowsInserted(const QModelIndex &parent)
{
    if (!qFileDialogUi->treeView
        || parent != qFileDialogUi->treeView->rootIndex()
        || !qFileDialogUi->treeView->selectionModel()
        || qFileDialogUi->treeView->selectionModel()->hasSelection()
        || qFileDialogUi->treeView->model()->rowCount(parent) == 0)
        return;
}

void QFileDialogPrivate::_q_fileRenamed(const QString &path, const QString oldName, const QString newName)
{
    const QFileDialog::FileMode fileMode = q_func()->fileMode();
    if (fileMode == QFileDialog::Directory || fileMode == QFileDialog::DirectoryOnly) {
        if (path == rootPath() && lineEdit()->text() == oldName)
            lineEdit()->setText(newName);
    }
}

void QFileDialogPrivate::_q_nativeFileSelected(const QUrl &file)
{
    Q_Q(QFileDialog);
    emit q->urlSelected(file);
    if (file.isLocalFile())
        emit q->fileSelected(file.toLocalFile());
}

void QFileDialogPrivate::_q_nativeFilesSelected(const QList<QUrl> &files)
{
    Q_Q(QFileDialog);
    emit q->urlsSelected(files);
    QStringList localFiles;
    foreach (const QUrl &file, files)
        if (file.isLocalFile())
            localFiles.append(file.toLocalFile());
    if (!localFiles.isEmpty())
        emit q->filesSelected(localFiles);
}

void QFileDialogPrivate::_q_nativeCurrentChanged(const QUrl &file)
{
    Q_Q(QFileDialog);
    emit q->currentUrlChanged(file);
    if (file.isLocalFile())
        emit q->currentChanged(file.toLocalFile());
}

void QFileDialogPrivate::_q_nativeEnterDirectory(const QUrl &directory)
{
    Q_Q(QFileDialog);
    emit q->directoryUrlEntered(directory);
    if (!directory.isEmpty() && directory.isLocalFile()) { // Windows native dialogs occasionally emit signals with empty strings.
        *lastVisitedDir() = directory.toLocalFile();
        emit q->directoryEntered(directory.toLocalFile());
    }
}

/*!
    \internal

    For the list and tree view watch keys to goto parent and back in the history

    returns \c true if handled
*/
bool QFileDialogPrivate::itemViewKeyboardEvent(QKeyEvent *event) {

    Q_Q(QFileDialog);
    switch (event->key()) {
    case Qt::Key_Backspace:
        _q_navigateToParent();
        return true;
    case Qt::Key_Back:
#ifdef QT_KEYPAD_NAVIGATION
        if (QApplication::keypadNavigationEnabled())
            return false;
#endif
    case Qt::Key_Left:
        if (event->key() == Qt::Key_Back || event->modifiers() == Qt::AltModifier) {
            _q_navigateBackward();
            return true;
        }
        break;
    case Qt::Key_Escape:
        q->hide();
        return true;
    default:
        break;
    }
    return false;
}

QString QFileDialogPrivate::getEnvironmentVariable(const QString &string)
{
#ifdef Q_OS_UNIX
    if (string.size() > 1 && string.startsWith(QLatin1Char('$'))) {
        return QString::fromLocal8Bit(getenv(string.mid(1).toLatin1().constData()));
    }
#else
    if (string.size() > 2 && string.startsWith(QLatin1Char('%')) && string.endsWith(QLatin1Char('%'))) {
        return QString::fromLocal8Bit(qgetenv(string.mid(1, string.size() - 2).toLatin1().constData()));
    }
#endif
    return string;
}

void QFileDialogComboBox::setFileDialogPrivate(QFileDialogPrivate *d_pointer) {
    d_ptr = d_pointer;
    urlModel = new QUrlModel(this);
    urlModel->showFullPath = true;
    urlModel->setFileSystemModel(d_ptr->model);
    setModel(urlModel);
}

void QFileDialogComboBox::showPopup()
{
    if (model()->rowCount() > 1)
        QComboBox::showPopup();

    urlModel->setUrls(QList<QUrl>());
    QList<QUrl> list;
    QModelIndex idx = d_ptr->model->index(d_ptr->rootPath());
    while (idx.isValid()) {
        QUrl url = QUrl::fromLocalFile(idx.data(QFileSystemModel::FilePathRole).toString());
        if (url.isValid())
            list.append(url);
        idx = idx.parent();
    }
    // add "my computer"
    list.append(QUrl::fromLocalFile(QLatin1String("")));
    urlModel->addUrls(list, 0);
    idx = model()->index(model()->rowCount() - 1, 0);

    // append history
    QList<QUrl> urls;
    for (int i = 0; i < m_history.count(); ++i) {
        QUrl path = QUrl::fromLocalFile(m_history.at(i));
        if (!urls.contains(path))
            urls.prepend(path);
    }
    if (urls.count() > 0) {
        model()->insertRow(model()->rowCount());
        idx = model()->index(model()->rowCount()-1, 0);
        // ### TODO maybe add a horizontal line before this
        model()->setData(idx, QFileDialog::tr("Recent Places"));
        QStandardItemModel *m = qobject_cast<QStandardItemModel*>(model());
        if (m) {
            Qt::ItemFlags flags = m->flags(idx);
            flags &= ~Qt::ItemIsEnabled;
            m->item(idx.row(), idx.column())->setFlags(flags);
        }
        urlModel->addUrls(urls, -1, false);
    }
    setCurrentIndex(0);

    QComboBox::showPopup();
}

// Exact same as QComboBox::paintEvent(), except we elide the text.
void QFileDialogComboBox::paintEvent(QPaintEvent *)
{
    QStylePainter painter(this);
    painter.setPen(palette().color(QPalette::Text));

    // draw the combobox frame, focusrect and selected etc.
    QStyleOptionComboBox opt;
    initStyleOption(&opt);

    QRect editRect = style()->subControlRect(QStyle::CC_ComboBox, &opt,
                                                QStyle::SC_ComboBoxEditField, this);
    int size = editRect.width() - opt.iconSize.width() - 4;
    opt.currentText = opt.fontMetrics.elidedText(opt.currentText, Qt::ElideMiddle, size);
    painter.drawComplexControl(QStyle::CC_ComboBox, opt);

    // draw the icon and text
    painter.drawControl(QStyle::CE_ComboBoxLabel, opt);
}

QFileDialogListView::QFileDialogListView(QWidget *parent) : QListView(parent)
{
}

void QFileDialogListView::setFileDialogPrivate(QFileDialogPrivate *d_pointer)
{
    d_ptr = d_pointer;
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setWrapping(true);
    setResizeMode(QListView::Adjust);
    setEditTriggers(QAbstractItemView::EditKeyPressed);
    setContextMenuPolicy(Qt::CustomContextMenu);
#ifndef QT_NO_DRAGANDDROP
    setDragDropMode(QAbstractItemView::InternalMove);
#endif
}

QSize QFileDialogListView::sizeHint() const
{
    int height = qMax(10, sizeHintForRow(0));
    return QSize(QListView::sizeHint().width() * 2, height * 30);
}

void QFileDialogListView::keyPressEvent(QKeyEvent *e)
{
#ifdef QT_KEYPAD_NAVIGATION
    if (QApplication::navigationMode() == Qt::NavigationModeKeypadDirectional) {
        QListView::keyPressEvent(e);
        return;
    }
#endif // QT_KEYPAD_NAVIGATION

    if (!d_ptr->itemViewKeyboardEvent(e))
        QListView::keyPressEvent(e);
    e->accept();
}

QFileDialogTreeView::QFileDialogTreeView(QWidget *parent) : QTreeView(parent)
{
}

void QFileDialogTreeView::setFileDialogPrivate(QFileDialogPrivate *d_pointer)
{
    d_ptr = d_pointer;
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setRootIsDecorated(false);
    setItemsExpandable(false);
    setSortingEnabled(true);
    header()->setSortIndicator(0, Qt::AscendingOrder);
    header()->setStretchLastSection(false);
    setTextElideMode(Qt::ElideMiddle);
    setEditTriggers(QAbstractItemView::EditKeyPressed);
    setContextMenuPolicy(Qt::CustomContextMenu);
#ifndef QT_NO_DRAGANDDROP
    setDragDropMode(QAbstractItemView::InternalMove);
#endif
}

void QFileDialogTreeView::keyPressEvent(QKeyEvent *e)
{
#ifdef QT_KEYPAD_NAVIGATION
    if (QApplication::navigationMode() == Qt::NavigationModeKeypadDirectional) {
        QTreeView::keyPressEvent(e);
        return;
    }
#endif // QT_KEYPAD_NAVIGATION

    if (!d_ptr->itemViewKeyboardEvent(e))
        QTreeView::keyPressEvent(e);
    e->accept();
}

QSize QFileDialogTreeView::sizeHint() const
{
    int height = qMax(10, sizeHintForRow(0));
    QSize sizeHint = header()->sizeHint();
    return QSize(sizeHint.width() * 4, height * 30);
}

/*!
    // FIXME: this is a hack to avoid propagating key press events
    // to the dialog and from there to the "Ok" button
*/
void QFileDialogLineEdit::keyPressEvent(QKeyEvent *e)
{
#ifdef QT_KEYPAD_NAVIGATION
    if (QApplication::navigationMode() == Qt::NavigationModeKeypadDirectional) {
        QLineEdit::keyPressEvent(e);
        return;
    }
#endif // QT_KEYPAD_NAVIGATION

    int key = e->key();
    QLineEdit::keyPressEvent(e);
    if (key != Qt::Key_Escape && key != Qt::Key_Back)
        e->accept();
}

#ifndef QT_NO_FSCOMPLETER

QString QFSCompleter::pathFromIndex(const QModelIndex &index) const
{
    const QFileSystemModel *dirModel;
    if (proxyModel)
        dirModel = qobject_cast<const QFileSystemModel *>(proxyModel->sourceModel());
    else
        dirModel = sourceModel;
    QString currentLocation = dirModel->rootPath();
    QString path = index.data(QFileSystemModel::FilePathRole).toString();
    if (!currentLocation.isEmpty() && path.startsWith(currentLocation)) {
#if defined(Q_OS_UNIX) || defined(Q_OS_WINCE)
        if (currentLocation == QDir::separator())
            return path.mid(currentLocation.length());
#endif
        if (currentLocation.endsWith(QLatin1Char('/')))
            return path.mid(currentLocation.length());
        else
            return path.mid(currentLocation.length()+1);
    }
    return index.data(QFileSystemModel::FilePathRole).toString();
}

QStringList QFSCompleter::splitPath(const QString &path) const
{
    if (path.isEmpty())
        return QStringList(completionPrefix());

    QString pathCopy = QDir::toNativeSeparators(path);
    QString sep = QDir::separator();
#if defined(Q_OS_WIN)
    if (pathCopy == QLatin1String("\\") || pathCopy == QLatin1String("\\\\"))
        return QStringList(pathCopy);
    QString doubleSlash(QLatin1String("\\\\"));
    if (pathCopy.startsWith(doubleSlash))
        pathCopy = pathCopy.mid(2);
    else
        doubleSlash.clear();
#elif defined(Q_OS_UNIX)
    bool expanded;
    pathCopy = qt_tildeExpansion(pathCopy, &expanded);
    if (expanded) {
        QFileSystemModel *dirModel;
        if (proxyModel)
            dirModel = qobject_cast<QFileSystemModel *>(proxyModel->sourceModel());
        else
            dirModel = sourceModel;
        dirModel->fetchMore(dirModel->index(pathCopy));
    }
#endif

    QRegExp re(QLatin1Char('[') + QRegExp::escape(sep) + QLatin1Char(']'));

#if defined(Q_OS_WIN)
    QStringList parts = pathCopy.split(re, QString::SkipEmptyParts);
    if (!doubleSlash.isEmpty() && !parts.isEmpty())
        parts[0].prepend(doubleSlash);
    if (pathCopy.endsWith(sep))
        parts.append(QString());
#else
    QStringList parts = pathCopy.split(re);
    if (pathCopy[0] == sep[0]) // read the "/" at the beginning as the split removed it
        parts[0] = sep[0];
#endif

#if defined(Q_OS_WIN)
    bool startsFromRoot = !parts.isEmpty() && parts[0].endsWith(QLatin1Char(':'));
#else
    bool startsFromRoot = pathCopy[0] == sep[0];
#endif
    if (parts.count() == 1 || (parts.count() > 1 && !startsFromRoot)) {
        const QFileSystemModel *dirModel;
        if (proxyModel)
            dirModel = qobject_cast<const QFileSystemModel *>(proxyModel->sourceModel());
        else
            dirModel = sourceModel;
        QString currentLocation = QDir::toNativeSeparators(dirModel->rootPath());
#if defined(Q_OS_WIN)
        if (currentLocation.endsWith(QLatin1Char(':')))
            currentLocation.append(sep);
#endif
        if (currentLocation.contains(sep) && path != currentLocation) {
            QStringList currentLocationList = splitPath(currentLocation);
            while (!currentLocationList.isEmpty()
                   && parts.count() > 0
                   && parts.at(0) == QLatin1String("..")) {
                parts.removeFirst();
                currentLocationList.removeLast();
            }
            if (!currentLocationList.isEmpty() && currentLocationList.last().isEmpty())
                currentLocationList.removeLast();
            return currentLocationList + parts;
        }
    }
    return parts;
}

#endif // QT_NO_COMPLETER


QT_END_NAMESPACE

#include "moc_qfiledialog.cpp"

#endif // QT_NO_FILEDIALOG
