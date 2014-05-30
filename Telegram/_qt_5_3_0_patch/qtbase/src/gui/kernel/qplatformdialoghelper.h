/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the QtGui module of the Qt Toolkit.
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

#ifndef QPLATFORMDIALOGHELPER_H
#define QPLATFORMDIALOGHELPER_H

//
//  W A R N I N G
//  -------------
//
// This file is part of the QPA API and is not meant to be used
// in applications. Usage of this API may make your code
// source and binary incompatible with future versions of Qt.
//

#include <QtCore/QtGlobal>
#include <QtCore/QObject>
#include <QtCore/QList>
#include <QtCore/QSharedDataPointer>
#include <QtCore/QSharedPointer>
#include <QtCore/QDir>
#include <QtCore/QUrl>
#include <QtGui/QRgb>

QT_BEGIN_NAMESPACE


class QString;
class QColor;
class QFont;
class QWindow;
class QVariant;
class QUrl;
class QColorDialogOptionsPrivate;
class QFontDialogOptionsPrivate;
class QFileDialogOptionsPrivate;
class QMessageDialogOptionsPrivate;

class Q_GUI_EXPORT QPlatformDialogHelper : public QObject
{
    Q_OBJECT
public:
    enum StyleHint {
    };
    enum DialogCode { Rejected, Accepted };

    enum StandardButton {
        // keep this in sync with QDialogButtonBox::StandardButton and QMessageBox::StandardButton
        NoButton           = 0x00000000,
        Ok                 = 0x00000400,
        Save               = 0x00000800,
        SaveAll            = 0x00001000,
        Open               = 0x00002000,
        Yes                = 0x00004000,
        YesToAll           = 0x00008000,
        No                 = 0x00010000,
        NoToAll            = 0x00020000,
        Abort              = 0x00040000,
        Retry              = 0x00080000,
        Ignore             = 0x00100000,
        Close              = 0x00200000,
        Cancel             = 0x00400000,
        Discard            = 0x00800000,
        Help               = 0x01000000,
        Apply              = 0x02000000,
        Reset              = 0x04000000,
        RestoreDefaults    = 0x08000000,


        FirstButton        = Ok,                // internal
        LastButton         = RestoreDefaults,   // internal
        LowestBit          = 10,                // internal: log2(FirstButton)
        HighestBit         = 27                 // internal: log2(LastButton)
    };

    Q_DECLARE_FLAGS(StandardButtons, StandardButton)

    enum ButtonRole {
        // keep this in sync with QDialogButtonBox::ButtonRole and QMessageBox::ButtonRole
        // TODO Qt 6: make the enum copies explicit, and make InvalidRole == 0 so that
        // AcceptRole can be or'ed with flags, and EOL can be the same as InvalidRole (null-termination)
        InvalidRole = -1,
        AcceptRole,
        RejectRole,
        DestructiveRole,
        ActionRole,
        HelpRole,
        YesRole,
        NoRole,
        ResetRole,
        ApplyRole,

        NRoles,

        RoleMask        = 0x0FFFFFFF,
        AlternateRole   = 0x10000000,
        Stretch         = 0x20000000,
        Reverse         = 0x40000000,
        EOL             = InvalidRole
    };

    enum ButtonLayout {
        // keep this in sync with QDialogButtonBox::ButtonLayout and QMessageBox::ButtonLayout
        UnknownLayout = -1,
        WinLayout,
        MacLayout,
        KdeLayout,
        GnomeLayout,
        MacModelessLayout
    };

    QPlatformDialogHelper();
    virtual ~QPlatformDialogHelper();

    virtual QVariant styleHint(StyleHint hint) const;

    virtual void exec() = 0;
    virtual bool show(Qt::WindowFlags windowFlags,
                          Qt::WindowModality windowModality,
                          QWindow *parent) = 0;
    virtual void hide() = 0;

    static QVariant defaultStyleHint(QPlatformDialogHelper::StyleHint hint);

    static const int *buttonLayout(Qt::Orientation orientation = Qt::Horizontal, ButtonLayout policy = UnknownLayout);
    static ButtonRole buttonRole(StandardButton button);

Q_SIGNALS:
    void accept();
    void reject();
};

class Q_GUI_EXPORT QColorDialogOptions
{
public:
    enum ColorDialogOption {
        ShowAlphaChannel    = 0x00000001,
        NoButtons           = 0x00000002,
        DontUseNativeDialog = 0x00000004
    };

    Q_DECLARE_FLAGS(ColorDialogOptions, ColorDialogOption)

    QColorDialogOptions();
    QColorDialogOptions(const QColorDialogOptions &rhs);
    QColorDialogOptions &operator=(const QColorDialogOptions &rhs);
    ~QColorDialogOptions();

    void swap(QColorDialogOptions &other) { qSwap(d, other.d); }

    QString windowTitle() const;
    void setWindowTitle(const QString &);

    void setOption(ColorDialogOption option, bool on = true);
    bool testOption(ColorDialogOption option) const;
    void setOptions(ColorDialogOptions options);
    ColorDialogOptions options() const;

    static int customColorCount();
    static QRgb customColor(int index);
    static QRgb *customColors();
    static void setCustomColor(int index, QRgb color);

    static QRgb *standardColors();
    static QRgb standardColor(int index);
    static void setStandardColor(int index, QRgb color);

private:
    QSharedDataPointer<QColorDialogOptionsPrivate> d;
};

Q_DECLARE_SHARED(QColorDialogOptions)

class Q_GUI_EXPORT QPlatformColorDialogHelper : public QPlatformDialogHelper
{
    Q_OBJECT
public:
    const QSharedPointer<QColorDialogOptions> &options() const;
    void setOptions(const QSharedPointer<QColorDialogOptions> &options);

    virtual void setCurrentColor(const QColor &) = 0;
    virtual QColor currentColor() const = 0;

Q_SIGNALS:
    void currentColorChanged(const QColor &color);
    void colorSelected(const QColor &color);

private:
    QSharedPointer<QColorDialogOptions> m_options;
};

class Q_GUI_EXPORT QFontDialogOptions
{
public:
    enum FontDialogOption {
        NoButtons           = 0x00000001,
        DontUseNativeDialog = 0x00000002,
        ScalableFonts       = 0x00000004,
        NonScalableFonts    = 0x00000008,
        MonospacedFonts     = 0x00000010,
        ProportionalFonts   = 0x00000020
    };

    Q_DECLARE_FLAGS(FontDialogOptions, FontDialogOption)

    QFontDialogOptions();
    QFontDialogOptions(const QFontDialogOptions &rhs);
    QFontDialogOptions &operator=(const QFontDialogOptions &rhs);
    ~QFontDialogOptions();

    void swap(QFontDialogOptions &other) { qSwap(d, other.d); }

    QString windowTitle() const;
    void setWindowTitle(const QString &);

    void setOption(FontDialogOption option, bool on = true);
    bool testOption(FontDialogOption option) const;
    void setOptions(FontDialogOptions options);
    FontDialogOptions options() const;

private:
    QSharedDataPointer<QFontDialogOptionsPrivate> d;
};

Q_DECLARE_SHARED(QFontDialogOptions)

class Q_GUI_EXPORT QPlatformFontDialogHelper : public QPlatformDialogHelper
{
    Q_OBJECT
public:
    virtual void setCurrentFont(const QFont &) = 0;
    virtual QFont currentFont() const = 0;

    const QSharedPointer<QFontDialogOptions> &options() const;
    void setOptions(const QSharedPointer<QFontDialogOptions> &options);

Q_SIGNALS:
    void currentFontChanged(const QFont &font);
    void fontSelected(const QFont &font);

private:
    QSharedPointer<QFontDialogOptions> m_options;
};

class Q_GUI_EXPORT QFileDialogOptions
{
public:
    enum ViewMode { Detail, List };
    enum FileMode { AnyFile, ExistingFile, Directory, ExistingFiles, DirectoryOnly };
    enum AcceptMode { AcceptOpen, AcceptSave };
    enum DialogLabel { LookIn, FileName, FileType, Accept, Reject, DialogLabelCount };

    enum FileDialogOption
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
    Q_DECLARE_FLAGS(FileDialogOptions, FileDialogOption)

    QFileDialogOptions();
    QFileDialogOptions(const QFileDialogOptions &rhs);
    QFileDialogOptions &operator=(const QFileDialogOptions &rhs);
    ~QFileDialogOptions();

    void swap(QFileDialogOptions &other) { qSwap(d, other.d); }

    QString windowTitle() const;
    void setWindowTitle(const QString &);

    void setOption(FileDialogOption option, bool on = true);
    bool testOption(FileDialogOption option) const;
    void setOptions(FileDialogOptions options);
    FileDialogOptions options() const;

    QDir::Filters filter() const;
    void setFilter(QDir::Filters filters);

    void setViewMode(ViewMode mode);
    ViewMode viewMode() const;

    void setFileMode(FileMode mode);
    FileMode fileMode() const;

    void setAcceptMode(AcceptMode mode);
    AcceptMode acceptMode() const;

    void setSidebarUrls(const QList<QUrl> &urls);
    QList<QUrl> sidebarUrls() const;

    void setNameFilters(const QStringList &filters);
    QStringList nameFilters() const;

    void setMimeTypeFilters(const QStringList &filters);
    QStringList mimeTypeFilters() const;

    void setDefaultSuffix(const QString &suffix);
    QString defaultSuffix() const;

    void setHistory(const QStringList &paths);
    QStringList history() const;

    void setLabelText(DialogLabel label, const QString &text);
    QString labelText(DialogLabel label) const;
    bool isLabelExplicitlySet(DialogLabel label);

    QUrl initialDirectory() const;
    void setInitialDirectory(const QUrl &);

    QString initiallySelectedNameFilter() const;
    void setInitiallySelectedNameFilter(const QString &);

    QList<QUrl> initiallySelectedFiles() const;
    void setInitiallySelectedFiles(const QList<QUrl> &);

private:
    QSharedDataPointer<QFileDialogOptionsPrivate> d;
};

Q_DECLARE_SHARED(QFileDialogOptions)

class Q_GUI_EXPORT QPlatformFileDialogHelper : public QPlatformDialogHelper
{
    Q_OBJECT
public:
    virtual bool defaultNameFilterDisables() const = 0;
    virtual void setDirectory(const QUrl &directory) = 0;
    virtual QUrl directory() const = 0;
    virtual void selectFile(const QUrl &filename) = 0;
    virtual QList<QUrl> selectedFiles() const = 0;
	virtual QByteArray selectedRemoteContent() const { return QByteArray(); }
    virtual void setFilter() = 0;
    virtual void selectNameFilter(const QString &filter) = 0;
    virtual QString selectedNameFilter() const = 0;

    virtual bool isSupportedUrl(const QUrl &url) const;

    const QSharedPointer<QFileDialogOptions> &options() const;
    void setOptions(const QSharedPointer<QFileDialogOptions> &options);

    static QStringList cleanFilterList(const QString &filter);
    static const char *filterRegExp;

Q_SIGNALS:
    void fileSelected(const QUrl &file);
    void filesSelected(const QList<QUrl> &files);
    void currentChanged(const QUrl &path);
    void directoryEntered(const QUrl &directory);
    void filterSelected(const QString &filter);

private:
    QSharedPointer<QFileDialogOptions> m_options;
};

class Q_GUI_EXPORT QMessageDialogOptions
{
public:
    // Keep in sync with QMessageBox::Icon
    enum Icon { NoIcon, Information, Warning, Critical, Question };

    QMessageDialogOptions();
    QMessageDialogOptions(const QMessageDialogOptions &rhs);
    QMessageDialogOptions &operator=(const QMessageDialogOptions &rhs);
    ~QMessageDialogOptions();

    void swap(QMessageDialogOptions &other) { qSwap(d, other.d); }

    QString windowTitle() const;
    void setWindowTitle(const QString &);

    void setIcon(Icon icon);
    Icon icon() const;

    void setText(const QString &text);
    QString text() const;

    void setInformativeText(const QString &text);
    QString informativeText() const;

    void setDetailedText(const QString &text);
    QString detailedText() const;

    void setStandardButtons(QPlatformDialogHelper::StandardButtons buttons);
    QPlatformDialogHelper::StandardButtons standardButtons() const;

private:
    QSharedDataPointer<QMessageDialogOptionsPrivate> d;
};

Q_DECLARE_SHARED(QMessageDialogOptions)

class Q_GUI_EXPORT QPlatformMessageDialogHelper : public QPlatformDialogHelper
{
    Q_OBJECT
public:
    const QSharedPointer<QMessageDialogOptions> &options() const;
    void setOptions(const QSharedPointer<QMessageDialogOptions> &options);

Q_SIGNALS:
    void clicked(QPlatformDialogHelper::StandardButton button, QPlatformDialogHelper::ButtonRole role);

private:
    QSharedPointer<QMessageDialogOptions> m_options;
};

QT_END_NAMESPACE

#endif // QPLATFORMDIALOGHELPER_H
