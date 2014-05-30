/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the plugins of the Qt Toolkit.
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

#define QT_NO_URL_CAST_FROM_STRING 1

#define _WIN32_WINNT 0x0600

#include "qwindowsdialoghelpers.h"

#include "qwindowscontext.h"
#include "qwindowswindow.h"
#include "qwindowsintegration.h"
#include "qwindowstheme.h" // Color conversion helpers

#include <QtGui/QGuiApplication>
#include <QtGui/QColor>

#include <QtCore/QDebug>
#include <QtCore/QRegExp>
#include <QtCore/QTimer>
#include <QtCore/QDir>
#include <QtCore/QScopedArrayPointer>
#include <QtCore/QSharedPointer>
#include <QtCore/QObject>
#include <QtCore/QThread>
#include <QtCore/QSysInfo>
#include <QtCore/QSharedData>
#include <QtCore/QExplicitlySharedDataPointer>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/private/qsystemlibrary_p.h>

#include <algorithm>

#include "qtwindows_additional.h"

#define STRICT_TYPED_ITEMIDS
#include <shlobj.h>
#include <shlwapi.h>

// #define USE_NATIVE_COLOR_DIALOG /* Testing purposes only */

#ifdef Q_CC_MINGW  /* Add missing declarations for MinGW */

#ifndef __IShellLibrary_FWD_DEFINED__

/* Constants obtained by running the below stream operator for
 * CLSID, IID on the constants in the Windows SDK libraries. */

static const IID   IID_IFileOpenDialog   = {0xd57c7288, 0xd4ad, 0x4768, {0xbe, 0x02, 0x9d, 0x96, 0x95, 0x32, 0xd9, 0x60}};
static const IID   IID_IFileSaveDialog   = {0x84bccd23, 0x5fde, 0x4cdb,{0xae, 0xa4, 0xaf, 0x64, 0xb8, 0x3d, 0x78, 0xab}};
#ifdef __MINGW64_VERSION_MAJOR
static const IID   q_IID_IShellItem      = {0x43826d1e, 0xe718, 0x42ee, {0xbc, 0x55, 0xa1, 0xe2, 0x61, 0xc3, 0x7b, 0xfe}};
#define IID_IShellItem q_IID_IShellItem
#else
static const IID   IID_IShellItem        = {0x43826d1e, 0xe718, 0x42ee, {0xbc, 0x55, 0xa1, 0xe2, 0x61, 0xc3, 0x7b, 0xfe}};
static const IID   IID_IShellItemArray   = {0xb63ea76d, 0x1f85, 0x456f, {0xa1, 0x9c, 0x48, 0x15, 0x9e, 0xfa, 0x85, 0x8b}};
#  define LFF_FORCEFILESYSTEM 1
#endif
static const IID   IID_IFileDialogEvents = {0x973510db, 0x7d7f, 0x452b,{0x89, 0x75, 0x74, 0xa8, 0x58, 0x28, 0xd3, 0x54}};
static const CLSID CLSID_FileOpenDialog  = {0xdc1c5a9c, 0xe88a, 0x4dde, {0xa5, 0xa1, 0x60, 0xf8, 0x2a, 0x20, 0xae, 0xf7}};
static const CLSID CLSID_FileSaveDialog  = {0xc0b4e2f3, 0xba21, 0x4773,{0x8d, 0xba, 0x33, 0x5e, 0xc9, 0x46, 0xeb, 0x8b}};

typedef struct _COMDLG_FILTERSPEC
{
    LPCWSTR pszName;
    LPCWSTR pszSpec;
} COMDLG_FILTERSPEC;


#define FOS_OVERWRITEPROMPT        0x2
#define FOS_STRICTFILETYPES        0x4
#define FOS_NOCHANGEDIR        0x8
#define FOS_PICKFOLDERS        0x20
#define FOS_FORCEFILESYSTEM        0x40
#define FOS_ALLNONSTORAGEITEMS 0x80
#define FOS_NOVALIDATE         0x100
#define FOS_ALLOWMULTISELECT   0x200
#define FOS_PATHMUSTEXIST      0x800
#define FOS_FILEMUSTEXIST      0x1000
#define FOS_CREATEPROMPT       0x2000
#define FOS_SHAREAWARE         0x4000
#define FOS_NOREADONLYRETURN   0x8000
#define FOS_NOTESTFILECREATE   0x10000
#define FOS_HIDEMRUPLACES      0x20000
#define FOS_HIDEPINNEDPLACES   0x40000
#define FOS_NODEREFERENCELINKS 0x100000
#define FOS_DONTADDTORECENT    0x2000000
#define FOS_FORCESHOWHIDDEN    0x10000000
#define FOS_DEFAULTNOMINIMODE  0x20000000
#define FOS_FORCEPREVIEWPANEON 0x40000000

#if __MINGW64_VERSION_MAJOR < 2
typedef int GETPROPERTYSTOREFLAGS;
#define GPS_DEFAULT               0x00000000
#define GPS_HANDLERPROPERTIESONLY 0x00000001
#define GPS_READWRITE             0x00000002
#define GPS_TEMPORARY             0x00000004
#define GPS_FASTPROPERTIESONLY    0x00000008
#define GPS_OPENSLOWITEM          0x00000010
#define GPS_DELAYCREATION         0x00000020
#define GPS_BESTEFFORT            0x00000040
#define GPS_MASK_VALID            0x0000007F
#endif

typedef int (QT_WIN_CALLBACK* BFFCALLBACK)(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData);
// message from browser
#define BFFM_INITIALIZED        1
#define BFFM_SELCHANGED         2
#define BFFM_ENABLEOK           (WM_USER + 101)
// Browsing for directory.
#define BIF_NONEWFOLDERBUTTON  0x0200
#define BIF_NOTRANSLATETARGETS 0x0400
#define BIF_BROWSEFORCOMPUTER  0x1000
#define BIF_BROWSEFORPRINTER   0x2000
#define BIF_BROWSEINCLUDEFILES 0x4000
#define BIF_SHAREABLE          0x8000

//the enums
typedef enum {
    SIATTRIBFLAGS_AND   = 0x1,
    SIATTRIBFLAGS_OR    = 0x2,
    SIATTRIBFLAGS_APPCOMPAT     = 0x3,
    SIATTRIBFLAGS_MASK  = 0x3
}       SIATTRIBFLAGS;
#ifndef __MINGW64_VERSION_MAJOR
typedef enum {
    SIGDN_NORMALDISPLAY = 0x00000000,
    SIGDN_PARENTRELATIVEPARSING = 0x80018001,
    SIGDN_PARENTRELATIVEFORADDRESSBAR = 0x8001c001,
    SIGDN_DESKTOPABSOLUTEPARSING = 0x80028000,
    SIGDN_PARENTRELATIVEEDITING = 0x80031001,
    SIGDN_DESKTOPABSOLUTEEDITING = 0x8004c000,
    SIGDN_FILESYSPATH = 0x80058000,
    SIGDN_URL = 0x80068000
} SIGDN;
#endif
typedef enum {
    FDAP_BOTTOM = 0x00000000,
    FDAP_TOP = 0x00000001
} FDAP;
typedef enum {
    FDESVR_DEFAULT = 0x00000000,
    FDESVR_ACCEPT = 0x00000001,
    FDESVR_REFUSE = 0x00000002
} FDE_SHAREVIOLATION_RESPONSE;
typedef FDE_SHAREVIOLATION_RESPONSE FDE_OVERWRITE_RESPONSE;

//the structs
typedef struct {
    LPCWSTR pszName;
    LPCWSTR pszSpec;
} qt_COMDLG_FILTERSPEC;
typedef struct {
    GUID fmtid;
    DWORD pid;
} qt_PROPERTYKEY;

typedef struct {
    USHORT      cb;
    BYTE        abID[1];
} qt_SHITEMID, *qt_LPSHITEMID;
typedef struct {
    qt_SHITEMID mkid;
} qt_ITEMIDLIST, *qt_LPITEMIDLIST;
typedef const qt_ITEMIDLIST *qt_LPCITEMIDLIST;
typedef struct {
    HWND          hwndOwner;
    qt_LPCITEMIDLIST pidlRoot;
    LPWSTR        pszDisplayName;
    LPCWSTR       lpszTitle;
    UINT          ulFlags;
    BFFCALLBACK   lpfn;
    LPARAM        lParam;
    int           iImage;
} qt_BROWSEINFO;

#endif // __IShellLibrary_FWD_DEFINED__

#ifndef __IFileDialogEvents_FWD_DEFINED__
DECLARE_INTERFACE(IFileDialogEvents);
#endif

#ifndef __IShellItem_INTERFACE_DEFINED__
DECLARE_INTERFACE_(IShellItem, IUnknown)
{
    STDMETHOD(BindToHandler)(THIS_ IBindCtx *pbc, REFGUID bhid, REFIID riid, void **ppv) PURE;
    STDMETHOD(GetParent)(THIS_ IShellItem **ppsi) PURE;
    STDMETHOD(GetDisplayName)(THIS_ SIGDN sigdnName, LPWSTR *ppszName) PURE;
    STDMETHOD(GetAttributes)(THIS_ ULONG sfgaoMask, ULONG *psfgaoAttribs) PURE;
    STDMETHOD(Compare)(THIS_ IShellItem *psi, DWORD hint, int *piOrder) PURE;
};
#endif

#ifndef __IShellItemFilter_INTERFACE_DEFINED__
DECLARE_INTERFACE_(IShellItemFilter, IUnknown)
{
    STDMETHOD(IncludeItem)(THIS_ IShellItem *psi) PURE;
    STDMETHOD(GetEnumFlagsForItem)(THIS_ IShellItem *psi, DWORD *pgrfFlags) PURE;
};
#endif

#ifndef __IEnumShellItems_INTERFACE_DEFINED__
DECLARE_INTERFACE_(IEnumShellItems, IUnknown)
{
    STDMETHOD(Next)(THIS_ ULONG celt, IShellItem **rgelt, ULONG *pceltFetched) PURE;
    STDMETHOD(Skip)(THIS_ ULONG celt) PURE;
    STDMETHOD(Reset)(THIS_) PURE;
    STDMETHOD(Clone)(THIS_ IEnumShellItems **ppenum) PURE;
};
#endif

#ifndef __IShellItemArray_INTERFACE_DEFINED__
DECLARE_INTERFACE_(IShellItemArray, IUnknown)
{
    STDMETHOD(BindToHandler)(THIS_ IBindCtx *pbc, REFGUID rbhid, REFIID riid, void **ppvOut) PURE;
    STDMETHOD(GetPropertyStore)(THIS_ GETPROPERTYSTOREFLAGS flags, REFIID riid, void **ppv) PURE;
    STDMETHOD(GetPropertyDescriptionList)(THIS_ const qt_PROPERTYKEY *keyType, REFIID riid, void **ppv) PURE;
    STDMETHOD(GetAttributes)(THIS_ SIATTRIBFLAGS dwAttribFlags, ULONG sfgaoMask, ULONG *psfgaoAttribs) PURE;
    STDMETHOD(GetCount)(THIS_ DWORD *pdwNumItems) PURE;
    STDMETHOD(GetItemAt)(THIS_ DWORD dwIndex, IShellItem **ppsi) PURE;
    STDMETHOD(EnumItems)(THIS_ IEnumShellItems **ppenumShellItems) PURE;
};
#endif

#ifndef __IShellLibrary_INTERFACE_DEFINED__

enum LIBRARYOPTIONFLAGS {};
enum DEFAULTSAVEFOLDERTYPE { DSFT_DETECT = 1 };
enum LIBRARYSAVEFLAGS {};

DECLARE_INTERFACE_(IShellLibrary, IUnknown)
{
    STDMETHOD(LoadLibraryFromItem)(THIS_ IShellItem *psiLibrary, DWORD grfMode) PURE;
    STDMETHOD(LoadLibraryFromKnownFolder)(THIS_ const GUID &kfidLibrary, DWORD grfMode) PURE;
    STDMETHOD(AddFolder)(THIS_ IShellItem *psiLocation) PURE;
    STDMETHOD(RemoveFolder)(THIS_ IShellItem *psiLocation) PURE;
    STDMETHOD(GetFolders)(THIS_ int lff, REFIID riid, void **ppv) PURE;
    STDMETHOD(ResolveFolder)(THIS_ IShellItem *psiFolderToResolve, DWORD dwTimeout, REFIID riid, void **ppv) PURE;
    STDMETHOD(GetDefaultSaveFolder)(THIS_ DEFAULTSAVEFOLDERTYPE dsft, REFIID riid, void **ppv) PURE;
    STDMETHOD(SetDefaultSaveFolder)(THIS_ DEFAULTSAVEFOLDERTYPE dsft, IShellItem *psi) PURE;
    STDMETHOD(GetOptions)(THIS_ LIBRARYOPTIONFLAGS *plofOptions) PURE;
    STDMETHOD(SetOptions)(THIS_ LIBRARYOPTIONFLAGS lofMask, LIBRARYOPTIONFLAGS lofOptions) PURE;
    STDMETHOD(GetFolderType)(THIS_ GUID *pftid) PURE;
    STDMETHOD(SetFolderType)(THIS_ const GUID &ftid) PURE;
    STDMETHOD(GetIcon)(THIS_ LPWSTR *ppszIcon) PURE;
    STDMETHOD(SetIcon)(THIS_ LPCWSTR pszIcon) PURE;
    STDMETHOD(Commit)(THIS_) PURE;
    STDMETHOD(Save)(THIS_ IShellItem *psiFolderToSaveIn, LPCWSTR pszLibraryName, LIBRARYSAVEFLAGS lsf, IShellItem **ppsiSavedTo) PURE;
    STDMETHOD(SaveInKnownFolder)(THIS_ const GUID &kfidToSaveIn, LPCWSTR pszLibraryName, LIBRARYSAVEFLAGS lsf,IShellItem **ppsiSavedTo) PURE;
};
#endif

#ifndef __IModalWindow_INTERFACE_DEFINED__
DECLARE_INTERFACE_(IModalWindow, IUnknown)
{
    STDMETHOD(Show)(THIS_ HWND hwndParent) PURE;
};
#endif

#ifndef __IFileDialog_INTERFACE_DEFINED__
DECLARE_INTERFACE_(IFileDialog, IModalWindow)
{
    STDMETHOD(SetFileTypes)(THIS_ UINT cFileTypes, const COMDLG_FILTERSPEC *rgFilterSpec) PURE;
    STDMETHOD(SetFileTypeIndex)(THIS_ UINT iFileType) PURE;
    STDMETHOD(GetFileTypeIndex)(THIS_ UINT *piFileType) PURE;
    STDMETHOD(Advise)(THIS_ IFileDialogEvents *pfde, DWORD *pdwCookie) PURE;
    STDMETHOD(Unadvise)(THIS_ DWORD dwCookie) PURE;
    STDMETHOD(SetOptions)(THIS_ DWORD fos) PURE;
    STDMETHOD(GetOptions)(THIS_ DWORD *pfos) PURE;
    STDMETHOD(SetDefaultFolder)(THIS_ IShellItem *psi) PURE;
    STDMETHOD(SetFolder)(THIS_ IShellItem *psi) PURE;
    STDMETHOD(GetFolder)(THIS_ IShellItem **ppsi) PURE;
    STDMETHOD(GetCurrentSelection)(THIS_ IShellItem **ppsi) PURE;
    STDMETHOD(SetFileName)(THIS_ LPCWSTR pszName) PURE;
    STDMETHOD(GetFileName)(THIS_ LPWSTR *pszName) PURE;
    STDMETHOD(SetTitle)(THIS_ LPCWSTR pszTitle) PURE;
    STDMETHOD(SetOkButtonLabel)(THIS_ LPCWSTR pszText) PURE;
    STDMETHOD(SetFileNameLabel)(THIS_ LPCWSTR pszLabel) PURE;
    STDMETHOD(GetResult)(THIS_ IShellItem **ppsi) PURE;
    STDMETHOD(AddPlace)(THIS_ IShellItem *psi, FDAP fdap) PURE;
    STDMETHOD(SetDefaultExtension)(THIS_ LPCWSTR pszDefaultExtension) PURE;
    STDMETHOD(Close)(THIS_ HRESULT hr) PURE;
    STDMETHOD(SetClientGuid)(THIS_ REFGUID guid) PURE;
    STDMETHOD(ClearClientData)(THIS_) PURE;
    STDMETHOD(SetFilter)(THIS_ IShellItemFilter *pFilter) PURE;
};
#endif

#ifndef __IFileDialogEvents_INTERFACE_DEFINED__
DECLARE_INTERFACE_(IFileDialogEvents, IUnknown)
{
    STDMETHOD(OnFileOk)(THIS_ IFileDialog *pfd) PURE;
    STDMETHOD(OnFolderChanging)(THIS_ IFileDialog *pfd, IShellItem *psiFolder) PURE;
    STDMETHOD(OnFolderChange)(THIS_ IFileDialog *pfd) PURE;
    STDMETHOD(OnSelectionChange)(THIS_ IFileDialog *pfd) PURE;
    STDMETHOD(OnShareViolation)(THIS_ IFileDialog *pfd, IShellItem *psi, FDE_SHAREVIOLATION_RESPONSE *pResponse) PURE;
    STDMETHOD(OnTypeChange)(THIS_ IFileDialog *pfd) PURE;
    STDMETHOD(OnOverwrite)(THIS_ IFileDialog *pfd, IShellItem *psi, FDE_OVERWRITE_RESPONSE *pResponse) PURE;
};
#endif

#ifndef __IFileOpenDialog_INTERFACE_DEFINED__
DECLARE_INTERFACE_(IFileOpenDialog, IFileDialog)
{
    STDMETHOD(GetResults)(THIS_ IShellItemArray **ppenum) PURE;
    STDMETHOD(GetSelectedItems)(THIS_ IShellItemArray **ppsai) PURE;
};
#endif

#ifndef __IPropertyStore_FWD_DEFINED__
typedef IUnknown IPropertyStore;
#endif

#ifndef __IFileOperationProgressSink_FWD_DEFINED__
typedef IUnknown IFileOperationProgressSink;
#endif

#ifndef __IFileSaveDialog_INTERFACE_DEFINED__
DECLARE_INTERFACE_(IFileSaveDialog, IFileDialog)
{
public:
    STDMETHOD(SetSaveAsItem)(THIS_ IShellItem *psi) PURE;
    STDMETHOD(SetProperties)(THIS_ IPropertyStore *pStore) PURE;
    STDMETHOD(SetCollectedProperties)(THIS_ IPropertyStore *pStore) PURE;
    STDMETHOD(GetProperties)(THIS_ IPropertyStore **ppStore) PURE;
    STDMETHOD(ApplyProperties)(THIS_ IShellItem *psi, IPropertyStore *pStore, HWND hwnd, IFileOperationProgressSink *pSink) PURE;
};
#endif

#endif // Q_CC_MINGW

QT_BEGIN_NAMESPACE

/* Output UID (IID, CLSID) as C++ constants.
 * The constants are contained in the Windows SDK libs, but not for MinGW. */
static inline QString guidToString(const GUID &g)
{
    QString rc;
    QTextStream str(&rc);
    str.setIntegerBase(16);
    str.setNumberFlags(str.numberFlags() | QTextStream::ShowBase);
    str << '{' << g.Data1 << ", " << g.Data2 << ", " << g.Data3;
    str.setFieldWidth(2);
    str.setFieldAlignment(QTextStream::AlignRight);
    str.setPadChar(QLatin1Char('0'));
    str << ",{" << g.Data4[0] << ", " << g.Data4[1]  << ", " << g.Data4[2]  << ", " << g.Data4[3]
        << ", " << g.Data4[4] << ", " << g.Data4[5]  << ", " << g.Data4[6]  << ", " << g.Data4[7]
        << "}};";
    return rc;
}

inline QDebug operator<<(QDebug d, const GUID &g)
{ d.nospace() << guidToString(g); return d; }

// Return an allocated wchar_t array from a QString, reserve more memory if desired.
static wchar_t *qStringToWCharArray(const QString &s, size_t reserveSize = 0)
{
    const size_t stringSize = s.size();
    wchar_t *result = new wchar_t[qMax(stringSize + 1, reserveSize)];
    s.toWCharArray(result);
    result[stringSize] = 0;
    return result;
}

namespace QWindowsDialogs
{
/*!
    \fn eatMouseMove()

    After closing a windows dialog with a double click (i.e. open a file)
    the message queue still contains a dubious WM_MOUSEMOVE message where
    the left button is reported to be down (wParam != 0).
    remove all those messages (usually 1) and post the last one with a
    reset button state.

    \ingroup qt-lighthouse-win
*/

void eatMouseMove()
{
    MSG msg = {0, 0, 0, 0, 0, {0, 0} };
    while (PeekMessage(&msg, 0, WM_MOUSEMOVE, WM_MOUSEMOVE, PM_REMOVE))
        ;
    if (msg.message == WM_MOUSEMOVE)
        PostMessage(msg.hwnd, msg.message, 0, msg.lParam);
    qCDebug(lcQpaDialogs) << __FUNCTION__ << "triggered=" << (msg.message == WM_MOUSEMOVE);
}

} // namespace QWindowsDialogs

/*!
    \class QWindowsNativeDialogBase
    \brief Base class for Windows native dialogs.

    Base classes for native dialogs (using the CLSID-based
    dialog interfaces "IFileDialog", etc. available from Windows
    Vista on) that mimick the behaviour of their QDialog
    counterparts as close as possible.

    Instances of derived classes are controlled by
    QWindowsDialogHelperBase-derived classes.

    A major difference is that there is only an exec(), which
    is a modal, blocking call; there is no non-blocking show().
    There 2 types of native dialogs:

    \list
    \li Dialogs provided by the Comdlg32 library (ChooseColor,
       ChooseFont). They only provide a modal, blocking
       function call (with idle processing).
    \li File dialogs are classes derived from IFileDialog. They
       inherit IModalWindow and their exec() method (calling
       IModalWindow::Show()) is similarly blocking, but methods
       like close() can be called on them from event handlers.
    \endlist

    \sa QWindowsDialogHelperBase
    \internal
    \ingroup qt-lighthouse-win
*/

class QWindowsNativeDialogBase : public QObject
{
    Q_OBJECT
public:
    virtual void setWindowTitle(const QString &title) = 0;
    bool executed() const { return m_executed; }
    void exec(HWND owner = 0) { doExec(owner); m_executed = true; }

signals:
    void accepted();
    void rejected();

public slots:
    virtual void close() = 0;

protected:
    QWindowsNativeDialogBase() : m_executed(false) {}

private:
    virtual void doExec(HWND owner = 0) = 0;

    bool m_executed;
};

/*!
    \class QWindowsDialogHelperBase
    \brief Helper for native Windows dialogs.

    Provides basic functionality and introduces new virtuals.
    The native dialog is created in setVisible_sys() since
    then modality and the state of DontUseNativeDialog is known.

    Modal dialogs are then run by exec(). Non-modal dialogs are shown using a
    separate thread started in show() should they support it.

    \sa QWindowsDialogThread, QWindowsNativeDialogBase
    \internal
    \ingroup qt-lighthouse-win
*/

template <class BaseClass>
QWindowsDialogHelperBase<BaseClass>::QWindowsDialogHelperBase() :
    m_nativeDialog(0),
    m_ownerWindow(0),
    m_timerId(0),
    m_thread(0)
{
}

template <class BaseClass>
void QWindowsDialogHelperBase<BaseClass>::cleanupThread()
{
    if (m_thread) { // Thread may be running if the dialog failed to close.
        if (m_thread->isRunning())
            m_thread->wait(500);
        if (m_thread->isRunning()) {
            m_thread->terminate();
            m_thread->wait(300);
            if (m_thread->isRunning())
                qCCritical(lcQpaDialogs) <<__FUNCTION__ << "Failed to terminate thread.";
            else
                qCWarning(lcQpaDialogs) << __FUNCTION__ << "Thread terminated.";
        }
        delete m_thread;
        m_thread = 0;
    }
}

template <class BaseClass>
QWindowsNativeDialogBase *QWindowsDialogHelperBase<BaseClass>::nativeDialog() const
{
    if (m_nativeDialog.isNull()) {
         qWarning("%s invoked with no native dialog present.", __FUNCTION__);
         return 0;
    }
    return m_nativeDialog.data();
}

template <class BaseClass>
void QWindowsDialogHelperBase<BaseClass>::timerEvent(QTimerEvent *)
{
    startDialogThread();
}

template <class BaseClass>
QWindowsNativeDialogBase *QWindowsDialogHelperBase<BaseClass>::ensureNativeDialog()
{
    // Create dialog and apply common settings. Check "executed" flag as well
    // since for example IFileDialog::Show() works only once.
    if (m_nativeDialog.isNull() || m_nativeDialog->executed())
        m_nativeDialog = QWindowsNativeDialogBasePtr(createNativeDialog());
    return m_nativeDialog.data();
}

/*!
    \class QWindowsDialogThread
    \brief Run a non-modal native dialog in a separate thread.

    \sa QWindowsDialogHelperBase
    \internal
    \ingroup qt-lighthouse-win
*/

class QWindowsDialogThread : public QThread
{
public:
    typedef QSharedPointer<QWindowsNativeDialogBase> QWindowsNativeDialogBasePtr;

    explicit QWindowsDialogThread(const QWindowsNativeDialogBasePtr &d, HWND owner)
        : m_dialog(d), m_owner(owner) {}
    void run();

private:
    const QWindowsNativeDialogBasePtr m_dialog;
    const HWND m_owner;
};

void QWindowsDialogThread::run()
{
    qCDebug(lcQpaDialogs) << '>' << __FUNCTION__;
    m_dialog->exec(m_owner);
    qCDebug(lcQpaDialogs) << '<' << __FUNCTION__;
}

template <class BaseClass>
bool QWindowsDialogHelperBase<BaseClass>::show(Qt::WindowFlags,
                                                   Qt::WindowModality windowModality,
                                                   QWindow *parent)
{
    const bool modal = (windowModality != Qt::NonModal);
    if (!parent)
        parent = QGuiApplication::focusWindow(); // Need a parent window, else the application loses activation when closed.
    if (parent) {
        m_ownerWindow = QWindowsWindow::handleOf(parent);
    } else {
        m_ownerWindow = 0;
    }
    qCDebug(lcQpaDialogs) << __FUNCTION__ << "modal=" << modal
        << " modal supported? " << supportsNonModalDialog(parent)
        << "native=" << m_nativeDialog.data() << "owner" << m_ownerWindow;
    if (!modal && !supportsNonModalDialog(parent))
        return false; // Was it changed in-between?
    if (!ensureNativeDialog())
        return false;
    // Start a background thread to show the dialog. For modal dialogs,
    // a subsequent call to exec() may follow. So, start an idle timer
    // which will start the dialog thread. If exec() is then called, the
    // timer is stopped and dialog->exec() is called directly.
    cleanupThread();
    if (modal) {
        m_timerId = this->startTimer(0);
    } else {
        startDialogThread();
    }
    return true;
}

template <class BaseClass>
void QWindowsDialogHelperBase<BaseClass>::startDialogThread()
{
    Q_ASSERT(!m_nativeDialog.isNull());
    Q_ASSERT(!m_thread);
    m_thread = new QWindowsDialogThread(m_nativeDialog, m_ownerWindow);
    m_thread->start();
    stopTimer();
}

template <class BaseClass>
void QWindowsDialogHelperBase<BaseClass>::stopTimer()
{
    if (m_timerId) {
        this->killTimer(m_timerId);
        m_timerId = 0;
    }
}

#ifndef Q_OS_WINCE
// Find a file dialog window created by IFileDialog by process id, window
// title and class, which starts with a hash '#'.

struct FindDialogContext
{
    explicit FindDialogContext(const QString &titleIn)
        : title(qStringToWCharArray(titleIn)), processId(GetCurrentProcessId()), hwnd(0) {}

    const QScopedArrayPointer<wchar_t> title;
    const DWORD processId;
    HWND hwnd; // contains the HWND of the window found.
};

static BOOL QT_WIN_CALLBACK findDialogEnumWindowsProc(HWND hwnd, LPARAM lParam)
{
    FindDialogContext *context = reinterpret_cast<FindDialogContext *>(lParam);
    DWORD winPid = 0;
    GetWindowThreadProcessId(hwnd, &winPid);
    if (winPid != context->processId)
        return TRUE;
    wchar_t buf[256];
    if (!RealGetWindowClass(hwnd, buf, sizeof(buf)/sizeof(wchar_t)) || buf[0] != L'#')
        return TRUE;
    if (!GetWindowTextW(hwnd, buf, sizeof(buf)/sizeof(wchar_t)) || wcscmp(buf, context->title.data()))
        return TRUE;
    context->hwnd = hwnd;
    return FALSE;
}

static inline HWND findDialogWindow(const QString &title)
{
    FindDialogContext context(title);
    EnumWindows(findDialogEnumWindowsProc, reinterpret_cast<LPARAM>(&context));
    return context.hwnd;
}
#endif // !Q_OS_WINCE

template <class BaseClass>
void QWindowsDialogHelperBase<BaseClass>::hide()
{
    if (m_nativeDialog)
        m_nativeDialog->close();
    m_ownerWindow = 0;
}

template <class BaseClass>
void QWindowsDialogHelperBase<BaseClass>::exec()
{
    qCDebug(lcQpaDialogs) << __FUNCTION__;
    stopTimer();
    if (QWindowsNativeDialogBase *nd = nativeDialog()) {
         nd->exec(m_ownerWindow);
         m_nativeDialog.clear();
    }
}

/*!
    \class QWindowsFileDialogSharedData
    \brief Explicitly shared file dialog parameters that are not in QFileDialogOptions.

    Contain Parameters that need to be cached while the native dialog does not
    exist yet. In addition, the data are updated by the change notifications of the
    IFileDialogEvent, as querying them after the dialog has closed
    does not reliably work. Provides thread-safe setters (for the non-modal case).

    \internal
    \ingroup qt-lighthouse-win
    \sa QFileDialogOptions
*/

class QWindowsFileDialogSharedData
{
public:
    QWindowsFileDialogSharedData() : m_data(new Data) {}
    void fromOptions(const QSharedPointer<QFileDialogOptions> &o);

    QUrl directory() const;
    void setDirectory(const QUrl &);
    QString selectedNameFilter() const;
    void setSelectedNameFilter(const QString &);
    QList<QUrl> selectedFiles() const;
    void setSelectedFiles(const QList<QUrl> &);
    QString selectedFile() const;
	void setSelectedRemoteContent(const QByteArray &);
	QByteArray selectedRemoteContent() const;

private:
    class Data : public QSharedData {
    public:
        QUrl directory;
        QString selectedNameFilter;
        QList<QUrl> selectedFiles;
		QByteArray selectedRemoteContent;
        QMutex mutex;
    };
    QExplicitlySharedDataPointer<Data> m_data;
};

inline QUrl QWindowsFileDialogSharedData::directory() const
{
    m_data->mutex.lock();
    const QUrl result = m_data->directory;
    m_data->mutex.unlock();
    return result;
}

inline void QWindowsFileDialogSharedData::setDirectory(const QUrl &d)
{
    QMutexLocker (&m_data->mutex);
    m_data->directory = d;
}

inline QString QWindowsFileDialogSharedData::selectedNameFilter() const
{
    m_data->mutex.lock();
    const QString result = m_data->selectedNameFilter;
    m_data->mutex.unlock();
    return result;
}

inline void QWindowsFileDialogSharedData::setSelectedNameFilter(const QString &f)
{
    QMutexLocker (&m_data->mutex);
    m_data->selectedNameFilter = f;
}

inline QList<QUrl> QWindowsFileDialogSharedData::selectedFiles() const
{
    m_data->mutex.lock();
    const QList<QUrl> result = m_data->selectedFiles;
    m_data->mutex.unlock();
    return result;
}

inline QString QWindowsFileDialogSharedData::selectedFile() const
{
    const QList<QUrl> files = selectedFiles();
    return files.isEmpty() ? QString() : files.front().toLocalFile();
}

inline void QWindowsFileDialogSharedData::setSelectedFiles(const QList<QUrl> &urls)
{
    QMutexLocker (&m_data->mutex);
    m_data->selectedFiles = urls;
}

inline QByteArray QWindowsFileDialogSharedData::selectedRemoteContent() const
{
	m_data->mutex.lock();
	const QByteArray result = m_data->selectedRemoteContent;
	m_data->mutex.unlock();
	return result;
}

inline void QWindowsFileDialogSharedData::setSelectedRemoteContent(const QByteArray &c)
{
	QMutexLocker(&m_data->mutex);
	m_data->selectedRemoteContent = c;
}

inline void QWindowsFileDialogSharedData::fromOptions(const QSharedPointer<QFileDialogOptions> &o)
{
    QMutexLocker (&m_data->mutex);
    m_data->directory = o->initialDirectory();
    m_data->selectedFiles = o->initiallySelectedFiles();
    m_data->selectedNameFilter = o->initiallySelectedNameFilter();
}

/*!
    \class QWindowsNativeFileDialogEventHandler
    \brief Listens to IFileDialog events and forwards them to QWindowsNativeFileDialogBase

    Events like 'folder change' that have an equivalent signal
    in QFileDialog are forwarded.

    \sa QWindowsNativeFileDialogBase, QWindowsFileDialogHelper
    \internal
    \ingroup qt-lighthouse-win
*/

class QWindowsNativeFileDialogBase;

class QWindowsNativeFileDialogEventHandler : public IFileDialogEvents
{
public:
    static IFileDialogEvents *create(QWindowsNativeFileDialogBase *nativeFileDialog);

    // IUnknown methods
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv)
    {
        if (riid != IID_IUnknown && riid != IID_IFileDialogEvents) {
            *ppv = NULL;
            return ResultFromScode(E_NOINTERFACE);
        }
        *ppv = this;
        AddRef();
        return NOERROR;
    }

    IFACEMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&m_ref); }

    IFACEMETHODIMP_(ULONG) Release()
    {
        const long ref = InterlockedDecrement(&m_ref);
        if (!ref)
            delete this;
        return ref;
    }

    // IFileDialogEvents methods
    IFACEMETHODIMP OnFileOk(IFileDialog *);
    IFACEMETHODIMP OnFolderChange(IFileDialog *) { return S_OK; }
    IFACEMETHODIMP OnFolderChanging(IFileDialog *, IShellItem *);
    IFACEMETHODIMP OnHelp(IFileDialog *) { return S_OK; }
    IFACEMETHODIMP OnSelectionChange(IFileDialog *);
    IFACEMETHODIMP OnShareViolation(IFileDialog *, IShellItem *, FDE_SHAREVIOLATION_RESPONSE *) { return S_OK; }
    IFACEMETHODIMP OnTypeChange(IFileDialog *);
    IFACEMETHODIMP OnOverwrite(IFileDialog *, IShellItem *, FDE_OVERWRITE_RESPONSE *) { return S_OK; }

    QWindowsNativeFileDialogEventHandler(QWindowsNativeFileDialogBase *nativeFileDialog) :
        m_ref(1), m_nativeFileDialog(nativeFileDialog) {}
    virtual ~QWindowsNativeFileDialogEventHandler() {}

private:
    long m_ref;
    QWindowsNativeFileDialogBase *m_nativeFileDialog;
};

IFileDialogEvents *QWindowsNativeFileDialogEventHandler::create(QWindowsNativeFileDialogBase *nativeFileDialog)
{
    IFileDialogEvents *result;
    QWindowsNativeFileDialogEventHandler *eventHandler = new QWindowsNativeFileDialogEventHandler(nativeFileDialog);
    if (FAILED(eventHandler->QueryInterface(IID_IFileDialogEvents, reinterpret_cast<void **>(&result)))) {
        qErrnoWarning("Unable to obtain IFileDialogEvents");
        return 0;
    }
    eventHandler->Release();
    return result;
}

/*!
    \class QWindowsNativeFileDialogBase
    \brief Windows native file dialog wrapper around IFileOpenDialog, IFileSaveDialog.

    Provides convenience methods.
    Note that only IFileOpenDialog has multi-file functionality.

    \sa QWindowsNativeFileDialogEventHandler, QWindowsFileDialogHelper
    \internal
    \ingroup qt-lighthouse-win
*/

class QWindowsNativeFileDialogBase : public QWindowsNativeDialogBase
{
    Q_OBJECT
    Q_PROPERTY(bool hideFiltersDetails READ hideFiltersDetails WRITE setHideFiltersDetails)
public:
    ~QWindowsNativeFileDialogBase();

    inline static QWindowsNativeFileDialogBase *create(QFileDialogOptions::AcceptMode am, const QWindowsFileDialogSharedData &data);

    virtual void setWindowTitle(const QString &title);
    inline void setMode(QFileDialogOptions::FileMode mode, QFileDialogOptions::AcceptMode acceptMode, QFileDialogOptions::FileDialogOptions options);
    inline void setDirectory(const QString &directory);
    inline void updateDirectory() { setDirectory(m_data.directory().toLocalFile()); }
    inline QString directory() const;
    virtual void doExec(HWND owner = 0);
    virtual void setNameFilters(const QStringList &f);
    inline void selectNameFilter(const QString &filter);
    inline void updateSelectedNameFilter() { selectNameFilter(m_data.selectedNameFilter()); }
    inline QString selectedNameFilter() const;
    void selectFile(const QString &fileName) const;
    bool hideFiltersDetails() const    { return m_hideFiltersDetails; }
    void setHideFiltersDetails(bool h) { m_hideFiltersDetails = h; }
    void setDefaultSuffix(const QString &s);
    inline bool hasDefaultSuffix() const  { return m_hasDefaultSuffix; }
    inline void setLabelText(QFileDialogOptions::DialogLabel l, const QString &text);

    // Return the selected files for tracking in OnSelectionChanged().
    virtual QList<QUrl> selectedFiles() const = 0;
    // Return the result for tracking in OnFileOk(). Differs from selection for
    // example by appended default suffixes, etc.
    virtual QList<QUrl> dialogResult() const = 0;
	virtual QByteArray dialogRemoteContent() const { return QByteArray(); }

    inline void onFolderChange(IShellItem *);
    inline void onSelectionChange();
    inline void onTypeChange();
    inline bool onFileOk();

signals:
    void directoryEntered(const QUrl &directory);
    void currentChanged(const QUrl &file);
    void filterSelected(const QString & filter);

public slots:
    virtual void close();

protected:
    explicit QWindowsNativeFileDialogBase(const QWindowsFileDialogSharedData &data);
    bool init(const CLSID &clsId, const IID &iid);
    void setDefaultSuffixSys(const QString &s);
    inline IFileDialog * fileDialog() const { return m_fileDialog; }
    static QString itemPath(IShellItem *item);
    static QList<QUrl> libraryItemFolders(IShellItem *item);
    static QString libraryItemDefaultSaveFolder(IShellItem *item);
    static int itemPaths(IShellItemArray *items, QList<QUrl> *fileResult = 0);
    static IShellItem *shellItem(const QString &path);

    const QWindowsFileDialogSharedData &data() const { return m_data; }
    QWindowsFileDialogSharedData &data() { return m_data; }

private:
    IFileDialog *m_fileDialog;
    IFileDialogEvents *m_dialogEvents;
    DWORD m_cookie;
    QStringList m_nameFilters;
    bool m_hideFiltersDetails;
    bool m_hasDefaultSuffix;
    QWindowsFileDialogSharedData m_data;
    QString m_title;
};

QWindowsNativeFileDialogBase::QWindowsNativeFileDialogBase(const QWindowsFileDialogSharedData &data) :
    m_fileDialog(0), m_dialogEvents(0), m_cookie(0), m_hideFiltersDetails(false),
    m_hasDefaultSuffix(false), m_data(data)
{
}

QWindowsNativeFileDialogBase::~QWindowsNativeFileDialogBase()
{
    if (m_dialogEvents && m_fileDialog)
        m_fileDialog->Unadvise(m_cookie);
    if (m_dialogEvents)
        m_dialogEvents->Release();
    if (m_fileDialog)
        m_fileDialog->Release();
}

bool QWindowsNativeFileDialogBase::init(const CLSID &clsId, const IID &iid)
{
    HRESULT hr = CoCreateInstance(clsId, NULL, CLSCTX_INPROC_SERVER,
                                  iid, reinterpret_cast<void **>(&m_fileDialog));
    if (FAILED(hr)) {
        qErrnoWarning("CoCreateInstance failed");
        return false;
    }
    m_dialogEvents = QWindowsNativeFileDialogEventHandler::create(this);
    if (!m_dialogEvents)
        return false;
    // Register event handler
    hr = m_fileDialog->Advise(m_dialogEvents, &m_cookie);
    if (FAILED(hr)) {
        qErrnoWarning("IFileDialog::Advise failed");
        return false;
    }
    qCDebug(lcQpaDialogs) << __FUNCTION__ << m_fileDialog << m_dialogEvents <<  m_cookie;

    return true;
}

void QWindowsNativeFileDialogBase::setWindowTitle(const QString &title)
{
    m_title = title;
    m_fileDialog->SetTitle(reinterpret_cast<const wchar_t *>(title.utf16()));
}

IShellItem *QWindowsNativeFileDialogBase::shellItem(const QString &path)
{
#ifndef Q_OS_WINCE
    if (QWindowsContext::shell32dll.sHCreateItemFromParsingName) {
        IShellItem *result = 0;
        const QString native = QDir::toNativeSeparators(path);
        const HRESULT hr =
            QWindowsContext::shell32dll.sHCreateItemFromParsingName(reinterpret_cast<const wchar_t *>(native.utf16()),
                                                                    NULL, IID_IShellItem,
                                                                    reinterpret_cast<void **>(&result));
        if (SUCCEEDED(hr))
            return result;
    }
#endif
    qErrnoWarning("%s: SHCreateItemFromParsingName(%s)) failed", __FUNCTION__, qPrintable(path));
    return 0;
}

void QWindowsNativeFileDialogBase::setDirectory(const QString &directory)
{
    if (!directory.isEmpty()) {
        if (IShellItem *psi = QWindowsNativeFileDialogBase::shellItem(directory)) {
            m_fileDialog->SetFolder(psi);
            psi->Release();
        }
    }
}

QString QWindowsNativeFileDialogBase::directory() const
{
#ifndef Q_OS_WINCE
    IShellItem *item = 0;
    if (m_fileDialog && SUCCEEDED(m_fileDialog->GetFolder(&item)) && item)
        return QWindowsNativeFileDialogBase::itemPath(item);
#endif
    return QString();
}

void QWindowsNativeFileDialogBase::doExec(HWND owner)
{
    qCDebug(lcQpaDialogs) << '>' << __FUNCTION__;
    // Show() blocks until the user closes the dialog, the dialog window
    // gets a WM_CLOSE or the parent window is destroyed.
    const HRESULT hr = m_fileDialog->Show(owner);
    QWindowsDialogs::eatMouseMove();
    qCDebug(lcQpaDialogs) << '<' << __FUNCTION__ << " returns " << hex << hr;
    if (hr == S_OK) {
        emit accepted();
    } else {
        emit rejected();
    }
}

void QWindowsNativeFileDialogBase::setMode(QFileDialogOptions::FileMode mode,
                                           QFileDialogOptions::AcceptMode acceptMode,
                                           QFileDialogOptions::FileDialogOptions options)
{
    DWORD flags = FOS_PATHMUSTEXIST | FOS_FORCESHOWHIDDEN;
    if (options & QFileDialogOptions::DontResolveSymlinks)
        flags |= FOS_NODEREFERENCELINKS;
    switch (mode) {
    case QFileDialogOptions::AnyFile:
        if (acceptMode == QFileDialogOptions::AcceptSave)
            flags |= FOS_NOREADONLYRETURN;
        if (!(options & QFileDialogOptions::DontConfirmOverwrite))
            flags |= FOS_OVERWRITEPROMPT;
        break;
    case QFileDialogOptions::ExistingFile:
        flags |= FOS_FILEMUSTEXIST;
        break;
    case QFileDialogOptions::Directory:
    case QFileDialogOptions::DirectoryOnly:
        flags |= FOS_PICKFOLDERS | FOS_FILEMUSTEXIST;
        break;
    case QFileDialogOptions::ExistingFiles:
        flags |= FOS_FILEMUSTEXIST | FOS_ALLOWMULTISELECT;
        break;
    }
    qCDebug(lcQpaDialogs) << __FUNCTION__ << "mode=" << mode
        << "acceptMode=" << acceptMode << "options=" << options
        << "results in" << showbase << hex << flags;

    if (FAILED(m_fileDialog->SetOptions(flags)))
        qErrnoWarning("%s: SetOptions() failed", __FUNCTION__);
}

#if !defined(Q_OS_WINCE) && defined(__IShellLibrary_INTERFACE_DEFINED__) // Windows SDK 7

// Helper for "Libraries": collections of folders appearing from Windows 7
// on, visible in the file dialogs.

// Load a library from a IShellItem (sanitized copy of the inline function
// SHLoadLibraryFromItem from ShObjIdl.h, which does not exist for MinGW).
static IShellLibrary *sHLoadLibraryFromItem(IShellItem *libraryItem, DWORD mode)
{
    // ID symbols present from Windows 7 on:
    static const CLSID classId_ShellLibrary = {0xd9b3211d, 0xe57f, 0x4426, {0xaa, 0xef, 0x30, 0xa8, 0x6, 0xad, 0xd3, 0x97}};
    static const IID   iId_IShellLibrary    = {0x11a66efa, 0x382e, 0x451a, {0x92, 0x34, 0x1e, 0xe, 0x12, 0xef, 0x30, 0x85}};

    IShellLibrary *helper = 0;
    IShellLibrary *result = 0;
    if (SUCCEEDED(CoCreateInstance(classId_ShellLibrary, NULL, CLSCTX_INPROC_SERVER, iId_IShellLibrary, reinterpret_cast<void **>(&helper))))
        if (SUCCEEDED(helper->LoadLibraryFromItem(libraryItem, mode)))
            helper->QueryInterface(iId_IShellLibrary, reinterpret_cast<void **>(&result));
    if (helper)
        helper->Release();
    return result;
}

// Return all folders of a library-type item.
QList<QUrl> QWindowsNativeFileDialogBase::libraryItemFolders(IShellItem *item)
{
    QList<QUrl> result;
    if (IShellLibrary *library = sHLoadLibraryFromItem(item, STGM_READ | STGM_SHARE_DENY_WRITE)) {
        IShellItemArray *itemArray = 0;
        if (SUCCEEDED(library->GetFolders(LFF_FORCEFILESYSTEM, IID_IShellItemArray, reinterpret_cast<void **>(&itemArray)))) {
            QWindowsNativeFileDialogBase::itemPaths(itemArray, &result);
            itemArray->Release();
        }
        library->Release();
    }
    return result;
}

// Return default save folders of a library-type item.
QString QWindowsNativeFileDialogBase::libraryItemDefaultSaveFolder(IShellItem *item)
{
    QString result;
    if (IShellLibrary *library = sHLoadLibraryFromItem(item, STGM_READ | STGM_SHARE_DENY_WRITE)) {
        IShellItem *item = 0;
        if (SUCCEEDED(library->GetDefaultSaveFolder(DSFT_DETECT, IID_IShellItem, reinterpret_cast<void **>(&item)))) {
            result = QWindowsNativeFileDialogBase::itemPath(item);
            item->Release();
        }
        library->Release();
    }
    return result;
}

#else // !Q_OS_WINCE && __IShellLibrary_INTERFACE_DEFINED__

QList<QUrl> QWindowsNativeFileDialogBase::libraryItemFolders(IShellItem *)
{
    return QList<QUrl>();
}

QString QWindowsNativeFileDialogBase::libraryItemDefaultSaveFolder(IShellItem *)
{
    return QString();
}

#endif // Q_OS_WINCE || !__IShellLibrary_INTERFACE_DEFINED__

QString QWindowsNativeFileDialogBase::itemPath(IShellItem *item)
{
    SFGAOF attributes = 0;
    // Check whether it has a file system representation?
    if (FAILED(item->GetAttributes(SFGAO_FILESYSTEM, &attributes)))
         return QString();
    if (attributes & SFGAO_FILESYSTEM) {
        LPWSTR name = 0;
        QString result;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name))) {
            result = QDir::cleanPath(QString::fromWCharArray(name));
            CoTaskMemFree(name);
        }
        return result;
    }
    // Check for a "Library" item
    if ((QSysInfo::windowsVersion() & QSysInfo::WV_NT_based) && QSysInfo::windowsVersion() >= QSysInfo::WV_WINDOWS7)
        return QWindowsNativeFileDialogBase::libraryItemDefaultSaveFolder(item);
    return QString();
}

int QWindowsNativeFileDialogBase::itemPaths(IShellItemArray *items,
                                            QList<QUrl> *result /* = 0 */)
{
    DWORD itemCount = 0;
    if (result)
        result->clear();
    if (FAILED(items->GetCount(&itemCount)))
        return 0;
    if (result && itemCount) {
        result->reserve(itemCount);
        for (DWORD i = 0; i < itemCount; ++i) {
            IShellItem *item = 0;
            if (SUCCEEDED(items->GetItemAt(i, &item)))
                result->push_back(QUrl::fromLocalFile(QWindowsNativeFileDialogBase::itemPath(item)));
        }
   }
    return itemCount;
}

// Split a list of name filters into description and actual filters
struct FilterSpec
{
    QString description;
    QString filter;
};

static QList<FilterSpec> filterSpecs(const QStringList &filters,
                                     bool hideFilterDetails,
                                     int *totalStringLength)
{
    QList<FilterSpec> result;
    result.reserve(filters.size());
    *totalStringLength = 0;

    const QRegExp filterSeparatorRE(QStringLiteral("[;\\s]+"));
    const QString separator = QStringLiteral(";");
    Q_ASSERT(filterSeparatorRE.isValid());
    // Split filter specification as 'Texts (*.txt[;] *.doc)', '*.txt[;] *.doc'
    // into description and filters specification as '*.txt;*.doc'
    foreach (const QString &filterString, filters) {
        const int openingParenPos = filterString.lastIndexOf(QLatin1Char('('));
        const int closingParenPos = openingParenPos != -1 ?
            filterString.indexOf(QLatin1Char(')'), openingParenPos + 1) : -1;
        FilterSpec filterSpec;
        filterSpec.filter = closingParenPos == -1 ?
            filterString :
            filterString.mid(openingParenPos + 1, closingParenPos - openingParenPos - 1).trimmed();
        if (filterSpec.filter.isEmpty())
            filterSpec.filter += QLatin1Char('*');
        filterSpec.filter.replace(filterSeparatorRE, separator);
        filterSpec.description = filterString;
        if (hideFilterDetails && openingParenPos != -1) { // Do not show pattern in description
            filterSpec.description.truncate(openingParenPos);
            while (filterSpec.description.endsWith(QLatin1Char(' ')))
                filterSpec.description.truncate(filterSpec.description.size() - 1);
        }
        *totalStringLength += filterSpec.filter.size() + filterSpec.description.size();
        result.push_back(filterSpec);
    }
    return result;
}

void QWindowsNativeFileDialogBase::setNameFilters(const QStringList &filters)
{
    /* Populates an array of COMDLG_FILTERSPEC from list of filters,
     * store the strings in a flat, contiguous buffer. */
    m_nameFilters = filters;
    int totalStringLength = 0;
    const QList<FilterSpec> specs = filterSpecs(filters, m_hideFiltersDetails, &totalStringLength);
    const int size = specs.size();

    QScopedArrayPointer<WCHAR> buffer(new WCHAR[totalStringLength + 2 * size]);
    QScopedArrayPointer<COMDLG_FILTERSPEC> comFilterSpec(new COMDLG_FILTERSPEC[size]);

    const QString matchesAll = QStringLiteral(" (*)");
    WCHAR *ptr = buffer.data();
    // Split filter specification as 'Texts (*.txt[;] *.doc)'
    // into description and filters specification as '*.txt;*.doc'

    for (int i = 0; i < size; ++i) {
        // Display glitch (CLSID only): 'All files (*)' shows up as 'All files (*) (*)'
        QString description = specs[i].description;
        if (!m_hideFiltersDetails && description.endsWith(matchesAll))
            description.truncate(description.size() - matchesAll.size());
        // Add to buffer.
        comFilterSpec[i].pszName = ptr;
        ptr += description.toWCharArray(ptr);
        *ptr++ = 0;
        comFilterSpec[i].pszSpec = ptr;
        ptr += specs[i].filter.toWCharArray(ptr);
        *ptr++ = 0;
    }

    m_fileDialog->SetFileTypes(size, comFilterSpec.data());
}

void QWindowsNativeFileDialogBase::setDefaultSuffix(const QString &s)
{
    setDefaultSuffixSys(s);
    m_hasDefaultSuffix = !s.isEmpty();
}

void QWindowsNativeFileDialogBase::setDefaultSuffixSys(const QString &s)
{
    // If this parameter is non-empty, it will be appended by the dialog for the 'Any files'
    // filter ('*'). If this parameter is non-empty and the current filter has a suffix,
    // the dialog will append the filter's suffix.
    wchar_t *wSuffix = const_cast<wchar_t *>(reinterpret_cast<const wchar_t *>(s.utf16()));
    m_fileDialog->SetDefaultExtension(wSuffix);
}

void QWindowsNativeFileDialogBase::setLabelText(QFileDialogOptions::DialogLabel l, const QString &text)
{
    wchar_t *wText = const_cast<wchar_t *>(reinterpret_cast<const wchar_t *>(text.utf16()));
    switch (l) {
        break;
    case QFileDialogOptions::FileName:
        m_fileDialog->SetFileNameLabel(wText);
        break;
    case QFileDialogOptions::Accept:
        m_fileDialog->SetOkButtonLabel(wText);
        break;
    case QFileDialogOptions::LookIn:
    case QFileDialogOptions::Reject:
    case QFileDialogOptions::FileType:
    case QFileDialogOptions::DialogLabelCount:
        break;
    }
}

void QWindowsNativeFileDialogBase::selectFile(const QString &fileName) const
{
	QString file = QDir::toNativeSeparators(fileName);
	int lastBackSlash = file.lastIndexOf(QChar::fromLatin1('\\'));
	if (lastBackSlash >= 0) {
		file = file.mid(lastBackSlash + 1);
	}
	m_fileDialog->SetFileName((wchar_t*)file.utf16());
}

// Return the index of the selected filter, accounting for QFileDialog
// sometimes stripping the filter specification depending on the
// hideFilterDetails setting.
static int indexOfNameFilter(const QStringList &filters, const QString &needle)
{
    const int index = filters.indexOf(needle);
    if (index >= 0)
        return index;
    for (int i = 0; i < filters.size(); ++i)
        if (filters.at(i).startsWith(needle))
            return i;
    return -1;
}

void QWindowsNativeFileDialogBase::selectNameFilter(const QString &filter)
{
    if (filter.isEmpty())
        return;
    const int index = indexOfNameFilter(m_nameFilters, filter);
    if (index < 0) {
        qWarning("%s: Invalid parameter '%s' not found in '%s'.",
                 __FUNCTION__, qPrintable(filter),
                 qPrintable(m_nameFilters.join(QStringLiteral(", "))));
        return;
    }
    m_fileDialog->SetFileTypeIndex(index + 1); // one-based.
}

QString QWindowsNativeFileDialogBase::selectedNameFilter() const
{
    UINT uIndex = 0;
    if (SUCCEEDED(m_fileDialog->GetFileTypeIndex(&uIndex))) {
        const int index = uIndex - 1; // one-based
        if (index < m_nameFilters.size())
            return m_nameFilters.at(index);
    }
    return QString();
}

void QWindowsNativeFileDialogBase::onFolderChange(IShellItem *item)
{
    if (item) {
        const QUrl directory = QUrl::fromLocalFile(QWindowsNativeFileDialogBase::itemPath(item));
        m_data.setDirectory(directory);
        emit directoryEntered(directory);
    }
}

void QWindowsNativeFileDialogBase::onSelectionChange()
{
    const QList<QUrl> current = selectedFiles();
    m_data.setSelectedFiles(current);
    if (current.size() == 1)
        emit currentChanged(current.front());
}

void QWindowsNativeFileDialogBase::onTypeChange()
{
    const QString filter = selectedNameFilter();
    m_data.setSelectedNameFilter(filter);
    emit filterSelected(filter);
}

bool QWindowsNativeFileDialogBase::onFileOk()
{
    // Store selected files as GetResults() returns invalid data after the dialog closes.
    m_data.setSelectedFiles(dialogResult());
	m_data.setSelectedRemoteContent(dialogRemoteContent());
    return true;
}

void QWindowsNativeFileDialogBase::close()
{
    m_fileDialog->Close(S_OK);
#ifndef Q_OS_WINCE
    // IFileDialog::Close() does not work unless invoked from a callback.
    // Try to find the window and send it a WM_CLOSE in addition.
    const HWND hwnd = findDialogWindow(m_title);
    qCDebug(lcQpaDialogs) << __FUNCTION__ << "closing" << hwnd;
    if (hwnd && IsWindowVisible(hwnd))
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
#endif // !Q_OS_WINCE
}

HRESULT QWindowsNativeFileDialogEventHandler::OnFolderChanging(IFileDialog *, IShellItem *item)
{
    m_nativeFileDialog->onFolderChange(item);
    return S_OK;
}

HRESULT QWindowsNativeFileDialogEventHandler::OnSelectionChange(IFileDialog *)
{
    m_nativeFileDialog->onSelectionChange();
    return S_OK;
}

HRESULT QWindowsNativeFileDialogEventHandler::OnTypeChange(IFileDialog *)
{
    m_nativeFileDialog->onTypeChange();
    return S_OK;
}

HRESULT QWindowsNativeFileDialogEventHandler::OnFileOk(IFileDialog *)
{
    return m_nativeFileDialog->onFileOk() ? S_OK : S_FALSE;
}

/*!
    \class QWindowsNativeSaveFileDialog
    \brief Windows native file save dialog wrapper around IFileSaveDialog.

    Implements single-selection methods.

    \internal
    \ingroup qt-lighthouse-win
*/

class QWindowsNativeSaveFileDialog : public QWindowsNativeFileDialogBase
{
    Q_OBJECT
public:
    explicit QWindowsNativeSaveFileDialog(const QWindowsFileDialogSharedData &data)
        : QWindowsNativeFileDialogBase(data) {}
    virtual void setNameFilters(const QStringList &f);
    virtual QList<QUrl> selectedFiles() const;
    virtual QList<QUrl> dialogResult() const;
};

// Return the first suffix from the name filter "Foo files (*.foo;*.bar)" -> "foo".
static inline QString suffixFromFilter(const QString &filter)
{
    int suffixPos = filter.indexOf(QLatin1String("(*."));
    if (suffixPos < 0)
        return QString();
    suffixPos += 3;
    int endPos = filter.indexOf(QLatin1Char(' '), suffixPos + 1);
    if (endPos < 0)
        endPos = filter.indexOf(QLatin1Char(';'), suffixPos + 1);
    if (endPos < 0)
        endPos = filter.indexOf(QLatin1Char(')'), suffixPos + 1);
    return endPos >= 0 ? filter.mid(suffixPos, endPos - suffixPos) : QString();
}

void QWindowsNativeSaveFileDialog::setNameFilters(const QStringList &f)
{
    QWindowsNativeFileDialogBase::setNameFilters(f);
    // QTBUG-31381, QTBUG-30748: IFileDialog will update the suffix of the selected name
    // filter only if a default suffix is set (see docs). Set the first available
    // suffix unless we have a defaultSuffix.
    if (!hasDefaultSuffix()) {
        foreach (const QString &filter, f) {
            const QString suffix = suffixFromFilter(filter);
            if (!suffix.isEmpty()) {
                setDefaultSuffixSys(suffix);
                break;
            }
        }
    } // m_hasDefaultSuffix
}

QList<QUrl> QWindowsNativeSaveFileDialog::dialogResult() const
{
    QList<QUrl> result;
    IShellItem *item = 0;
    if (SUCCEEDED(fileDialog()->GetResult(&item)) && item)
        result.push_back(QUrl::fromLocalFile(QWindowsNativeFileDialogBase::itemPath(item)));
    return result;
}

QList<QUrl> QWindowsNativeSaveFileDialog::selectedFiles() const
{
    QList<QUrl> result;
    IShellItem *item = 0;
    const HRESULT hr = fileDialog()->GetCurrentSelection(&item);
    if (SUCCEEDED(hr) && item)
        result.push_back(QUrl::fromLocalFile(QWindowsNativeSaveFileDialog::itemPath(item)));
    return result;
}

/*!
    \class QWindowsNativeOpenFileDialog
    \brief Windows native file save dialog wrapper around IFileOpenDialog.

    Implements multi-selection methods.

    \internal
    \ingroup qt-lighthouse-win
*/

class QWindowsNativeOpenFileDialog : public QWindowsNativeFileDialogBase
{
public:
    explicit QWindowsNativeOpenFileDialog(const QWindowsFileDialogSharedData &data) :
        QWindowsNativeFileDialogBase(data) {}
    virtual QList<QUrl> selectedFiles() const;
    virtual QList<QUrl> dialogResult() const;
	virtual QByteArray dialogRemoteContent() const;

private:
    inline IFileOpenDialog *openFileDialog() const
        { return static_cast<IFileOpenDialog *>(fileDialog()); }
};

QList<QUrl> QWindowsNativeOpenFileDialog::dialogResult() const
{
    QList<QUrl> result;
    IShellItemArray *items = 0;
    if (SUCCEEDED(openFileDialog()->GetResults(&items)) && items)
        QWindowsNativeFileDialogBase::itemPaths(items, &result);
    return result;
}

QByteArray QWindowsNativeOpenFileDialog::dialogRemoteContent() const {
	QByteArray result;
	IShellItemArray *items = 0;
	if (FAILED(openFileDialog()->GetResults(&items)) || !items)
		return result;
	DWORD itemCount = 0;
	if (FAILED(items->GetCount(&itemCount)) || !itemCount)
		return result;
	for (DWORD i = 0; i < itemCount; ++i) {
		IShellItem *item = 0;
		if (SUCCEEDED(items->GetItemAt(i, &item))) {
			SFGAOF attributes = 0;
			// Check whether it has a file system representation?
			if (FAILED(item->GetAttributes(SFGAO_FILESYSTEM, &attributes)) || (attributes & SFGAO_FILESYSTEM)) {
				LPWSTR name = 0;
				if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name))) {
					CoTaskMemFree(name);
					continue;
				}
			}
			if (FAILED(item->GetAttributes(SFGAO_STREAM, &attributes)) || !(attributes & SFGAO_STREAM))
				continue;

			IBindCtx *bind = 0;
			if (FAILED(CreateBindCtx(0, &bind)))
				continue;

			IStream *stream = 0;
			if (FAILED(item->BindToHandler(bind, BHID_Stream, IID_IStream, reinterpret_cast<void **>(&stream))))
				continue;

			STATSTG stat = { 0 };
			if (FAILED(stream->Stat(&stat, STATFLAG_NONAME)) || !stat.cbSize.QuadPart)
				continue;

			quint64 fullSize = stat.cbSize.QuadPart;
			result.resize(fullSize);
			ULONG read = 0;
			HRESULT r = stream->Read(result.data(), fullSize, &read);
			if (r == S_FALSE || r == S_OK)
				return result;

			result.clear();
		}
	}
	return result;
}

QList<QUrl> QWindowsNativeOpenFileDialog::selectedFiles() const
{
    QList<QUrl> result;
    IShellItemArray *items = 0;
    const HRESULT hr = openFileDialog()->GetSelectedItems(&items);
    if (SUCCEEDED(hr) && items)
        QWindowsNativeFileDialogBase::itemPaths(items, &result);
    return result;
}

/*!
    \brief Factory method for QWindowsNativeFileDialogBase returning
    QWindowsNativeOpenFileDialog or QWindowsNativeSaveFileDialog depending on
    QFileDialog::AcceptMode.
*/

QWindowsNativeFileDialogBase *QWindowsNativeFileDialogBase::create(QFileDialogOptions::AcceptMode am,
                                                                   const QWindowsFileDialogSharedData &data)
{
    QWindowsNativeFileDialogBase *result = 0;
    if (am == QFileDialogOptions::AcceptOpen) {
        result = new QWindowsNativeOpenFileDialog(data);
        if (!result->init(CLSID_FileOpenDialog, IID_IFileOpenDialog)) {
            delete result;
            return 0;
        }
    } else {
        result = new QWindowsNativeSaveFileDialog(data);
        if (!result->init(CLSID_FileSaveDialog, IID_IFileSaveDialog)) {
            delete result;
            return 0;
        }
    }
    return result;
}

static inline bool isQQuickWindow(const QWindow *w = 0)
{
    return w && w->inherits("QQuickWindow");
}

/*!
    \class QWindowsFileDialogHelper
    \brief Helper for native Windows file dialogs

    For Qt 4 compatibility, do not create native non-modal dialogs on widgets,
    but only on QQuickWindows, which do not have a fallback.

    \internal
    \ingroup qt-lighthouse-win
*/

class QWindowsFileDialogHelper : public QWindowsDialogHelperBase<QPlatformFileDialogHelper>
{
public:
    QWindowsFileDialogHelper() {}
    virtual bool supportsNonModalDialog(const QWindow * /* parent */ = 0) const { return false; }
    virtual bool defaultNameFilterDisables() const
        { return false; }
    virtual void setDirectory(const QUrl &directory) Q_DECL_OVERRIDE;
    virtual QUrl directory() const Q_DECL_OVERRIDE;
    virtual void selectFile(const QUrl &filename) Q_DECL_OVERRIDE;
    virtual QList<QUrl> selectedFiles() const Q_DECL_OVERRIDE;
	virtual QByteArray selectedRemoteContent() const Q_DECL_OVERRIDE;
    virtual void setFilter() Q_DECL_OVERRIDE;
    virtual void selectNameFilter(const QString &filter) Q_DECL_OVERRIDE;
    virtual QString selectedNameFilter() const Q_DECL_OVERRIDE;

private:
    virtual QWindowsNativeDialogBase *createNativeDialog();
    inline QWindowsNativeFileDialogBase *nativeFileDialog() const
        { return static_cast<QWindowsNativeFileDialogBase *>(nativeDialog()); }

    // Cache for the case no native dialog is created.
    QWindowsFileDialogSharedData m_data;
};

QWindowsNativeDialogBase *QWindowsFileDialogHelper::createNativeDialog()
{
    QWindowsNativeFileDialogBase *result = QWindowsNativeFileDialogBase::create(options()->acceptMode(), m_data);
    if (!result)
        return 0;
    QObject::connect(result, SIGNAL(accepted()), this, SIGNAL(accept()));
    QObject::connect(result, SIGNAL(rejected()), this, SIGNAL(reject()));
    QObject::connect(result, SIGNAL(directoryEntered(QUrl)),
                     this, SIGNAL(directoryEntered(QUrl)));
    QObject::connect(result, SIGNAL(currentChanged(QUrl)),
                     this, SIGNAL(currentChanged(QUrl)));
    QObject::connect(result, SIGNAL(filterSelected(QString)),
                     this, SIGNAL(filterSelected(QString)));

    // Apply settings.
    const QSharedPointer<QFileDialogOptions> &opts = options();
    m_data.fromOptions(opts);
    const QFileDialogOptions::FileMode mode = opts->fileMode();
    result->setWindowTitle(opts->windowTitle());
    result->setMode(mode, opts->acceptMode(), opts->options());
    result->setHideFiltersDetails(opts->testOption(QFileDialogOptions::HideNameFilterDetails));
    const QStringList nameFilters = opts->nameFilters();
    if (!nameFilters.isEmpty())
        result->setNameFilters(nameFilters);
    if (opts->isLabelExplicitlySet(QFileDialogOptions::FileName))
        result->setLabelText(QFileDialogOptions::FileName, opts->labelText(QFileDialogOptions::FileName));
    if (opts->isLabelExplicitlySet(QFileDialogOptions::Accept))
        result->setLabelText(QFileDialogOptions::Accept, opts->labelText(QFileDialogOptions::Accept));
    result->updateDirectory();
    result->updateSelectedNameFilter();
    const QList<QUrl> initialSelection = opts->initiallySelectedFiles();
    if (initialSelection.size() > 0) {
        const QUrl url = initialSelection.front();
        if (url.isLocalFile()) {
            QFileInfo info(url.toLocalFile());
            if (!info.isDir())
                result->selectFile(info.fileName());
        } else {
            result->selectFile(url.path()); // TODO url.fileName() once it exists
        }
    }
    // No need to select initialNameFilter if mode is Dir
    if (mode != QFileDialogOptions::Directory && mode != QFileDialogOptions::DirectoryOnly) {
        const QString initialNameFilter = opts->initiallySelectedNameFilter();
        if (!initialNameFilter.isEmpty())
            result->selectNameFilter(initialNameFilter);
    }
    const QString defaultSuffix = opts->defaultSuffix();
    if (!defaultSuffix.isEmpty())
        result->setDefaultSuffix(defaultSuffix);
    return result;
}

void QWindowsFileDialogHelper::setDirectory(const QUrl &directory)
{
    qCDebug(lcQpaDialogs) << __FUNCTION__ << directory.toString();

    m_data.setDirectory(directory);
    if (hasNativeDialog())
        nativeFileDialog()->updateDirectory();
}

QUrl QWindowsFileDialogHelper::directory() const
{
    return m_data.directory();
}

void QWindowsFileDialogHelper::selectFile(const QUrl &fileName)
{
    qCDebug(lcQpaDialogs) << __FUNCTION__ << fileName.toString();

    if (hasNativeDialog()) // Might be invoked from the QFileDialog constructor.
        nativeFileDialog()->selectFile(fileName.toLocalFile()); // ## should use QUrl::fileName() once it exists
}

QList<QUrl> QWindowsFileDialogHelper::selectedFiles() const
{
    return m_data.selectedFiles();
}

QByteArray QWindowsFileDialogHelper::selectedRemoteContent() const
{
	return m_data.selectedRemoteContent();
}

void QWindowsFileDialogHelper::setFilter()
{
    qCDebug(lcQpaDialogs) << __FUNCTION__;
}

void QWindowsFileDialogHelper::selectNameFilter(const QString &filter)
{
    m_data.setSelectedNameFilter(filter);
    if (hasNativeDialog())
        nativeFileDialog()->updateSelectedNameFilter();
}

QString QWindowsFileDialogHelper::selectedNameFilter() const
{
    return m_data.selectedNameFilter();
}

#ifndef Q_OS_WINCE

/*!
    \class QWindowsXpNativeFileDialog
    \brief Native Windows directory dialog for Windows XP using SHlib-functions.

    Uses the synchronous GetOpenFileNameW(), GetSaveFileNameW() from ComDlg32
    or SHBrowseForFolder() for directories.

    \internal
    \sa QWindowsXpFileDialogHelper

    \ingroup qt-lighthouse-win
*/

class QWindowsXpNativeFileDialog : public QWindowsNativeDialogBase
{
    Q_OBJECT
public:
    typedef QSharedPointer<QFileDialogOptions> OptionsPtr;

    static QWindowsXpNativeFileDialog *create(const OptionsPtr &options, const QWindowsFileDialogSharedData &data);

    virtual void setWindowTitle(const QString &t) { m_title =  t; }
    virtual void doExec(HWND owner = 0);
    virtual QPlatformDialogHelper::DialogCode result() const { return m_result; }

    int existingDirCallback(HWND hwnd, UINT uMsg, LPARAM lParam);

public slots:
    virtual void close() {}

private:
    typedef BOOL (APIENTRY *PtrGetOpenFileNameW)(LPOPENFILENAMEW);
    typedef BOOL (APIENTRY *PtrGetSaveFileNameW)(LPOPENFILENAMEW);

    explicit QWindowsXpNativeFileDialog(const OptionsPtr &options, const QWindowsFileDialogSharedData &data);
    void populateOpenFileName(OPENFILENAME *ofn, HWND owner) const;
    QList<QUrl> execExistingDir(HWND owner);
    QList<QUrl> execFileNames(HWND owner, int *selectedFilterIndex) const;

    const OptionsPtr m_options;
    QString m_title;
    QPlatformDialogHelper::DialogCode m_result;
    QWindowsFileDialogSharedData m_data;

    static PtrGetOpenFileNameW m_getOpenFileNameW;
    static PtrGetSaveFileNameW m_getSaveFileNameW;
};

QWindowsXpNativeFileDialog::PtrGetOpenFileNameW QWindowsXpNativeFileDialog::m_getOpenFileNameW = 0;
QWindowsXpNativeFileDialog::PtrGetSaveFileNameW QWindowsXpNativeFileDialog::m_getSaveFileNameW = 0;

QWindowsXpNativeFileDialog *QWindowsXpNativeFileDialog::create(const OptionsPtr &options, const QWindowsFileDialogSharedData &data)
{
    // GetOpenFileNameW() GetSaveFileName() are resolved
    // dynamically as not to create a dependency on Comdlg32, which
    // is used on XP only.
    if (!m_getOpenFileNameW) {
        QSystemLibrary library(QStringLiteral("Comdlg32"));
        m_getOpenFileNameW = (PtrGetOpenFileNameW)(library.resolve("GetOpenFileNameW"));
        m_getSaveFileNameW = (PtrGetSaveFileNameW)(library.resolve("GetSaveFileNameW"));
    }
    if (m_getOpenFileNameW && m_getSaveFileNameW)
        return new QWindowsXpNativeFileDialog(options, data);
    return 0;
}

QWindowsXpNativeFileDialog::QWindowsXpNativeFileDialog(const OptionsPtr &options,
                                                       const QWindowsFileDialogSharedData &data) :
    m_options(options), m_result(QPlatformDialogHelper::Rejected), m_data(data)
{
    setWindowTitle(m_options->windowTitle());
}

void QWindowsXpNativeFileDialog::doExec(HWND owner)
{
    int selectedFilterIndex = -1;
    const QList<QUrl> selectedFiles =
        m_options->fileMode() == QFileDialogOptions::DirectoryOnly ?
        execExistingDir(owner) : execFileNames(owner, &selectedFilterIndex);
    m_data.setSelectedFiles(selectedFiles);
    QWindowsDialogs::eatMouseMove();
    if (selectedFiles.isEmpty()) {
        m_result = QPlatformDialogHelper::Rejected;
        emit rejected();
    } else {
        const QStringList nameFilters = m_options->nameFilters();
        if (selectedFilterIndex >= 0 && selectedFilterIndex < nameFilters.size())
            m_data.setSelectedNameFilter(nameFilters.at(selectedFilterIndex));
        QUrl firstFile = selectedFiles.front();
        m_data.setDirectory(firstFile.adjusted(QUrl::RemoveFilename));
        m_result = QPlatformDialogHelper::Accepted;
        emit accepted();
    }
}

// Callback for QWindowsNativeXpFileDialog directory dialog.
// MFC Directory Dialog. Contrib: Steve Williams (minor parts from Scott Powers)

static int QT_WIN_CALLBACK xpFileDialogGetExistingDirCallbackProc(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData)
{
    QWindowsXpNativeFileDialog *dialog = reinterpret_cast<QWindowsXpNativeFileDialog *>(lpData);
    return dialog->existingDirCallback(hwnd, uMsg, lParam);
}

/* The correct declaration of the SHGetPathFromIDList symbol is
 * being used in mingw-w64 as of r6215, which is a v3 snapshot.  */
#if defined(Q_CC_MINGW) && (!defined(__MINGW64_VERSION_MAJOR) || __MINGW64_VERSION_MAJOR < 3)
typedef ITEMIDLIST *qt_LpItemIdList;
#else
typedef PIDLIST_ABSOLUTE qt_LpItemIdList;
#endif

int QWindowsXpNativeFileDialog::existingDirCallback(HWND hwnd, UINT uMsg, LPARAM lParam)
{
    switch (uMsg) {
    case BFFM_INITIALIZED: {
        if (!m_title.isEmpty())
            SetWindowText(hwnd, (wchar_t *)m_title.utf16());
        const QString initialFile = QDir::toNativeSeparators(m_data.directory().toLocalFile());
        if (!initialFile.isEmpty())
            SendMessage(hwnd, BFFM_SETSELECTION, TRUE, LPARAM(initialFile.utf16()));
    }
        break;
    case BFFM_SELCHANGED: {
        wchar_t path[MAX_PATH];
        const bool ok = SHGetPathFromIDList(reinterpret_cast<qt_LpItemIdList>(lParam), path)
                        && path[0];
        SendMessage(hwnd, BFFM_ENABLEOK, ok ? 1 : 0, 1);
    }
        break;
    }
    return 0;
}

QList<QUrl> QWindowsXpNativeFileDialog::execExistingDir(HWND owner)
{
    BROWSEINFO bi;
    wchar_t initPath[MAX_PATH];
    initPath[0] = 0;
    bi.hwndOwner = owner;
    bi.pidlRoot = NULL;
    bi.lpszTitle = 0;
    bi.pszDisplayName = initPath;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_STATUSTEXT | BIF_NEWDIALOGSTYLE;
    bi.lpfn = xpFileDialogGetExistingDirCallbackProc;
    bi.lParam = LPARAM(this);
    QList<QUrl> selectedFiles;
    if (qt_LpItemIdList pItemIDList = SHBrowseForFolder(&bi)) {
        wchar_t path[MAX_PATH];
        path[0] = 0;
        if (SHGetPathFromIDList(pItemIDList, path) && path[0])
            selectedFiles.push_back(QUrl::fromLocalFile(QDir::cleanPath(QString::fromWCharArray(path))));
        IMalloc *pMalloc;
        if (SHGetMalloc(&pMalloc) == NOERROR) {
            pMalloc->Free(pItemIDList);
            pMalloc->Release();
        }
    }
    return selectedFiles;
}

// Open/Save files
void QWindowsXpNativeFileDialog::populateOpenFileName(OPENFILENAME *ofn, HWND owner) const
{
    ZeroMemory(ofn, sizeof(OPENFILENAME));
    ofn->lStructSize = sizeof(OPENFILENAME);
    ofn->hwndOwner = owner;

    // Create a buffer with the filter strings.
    int totalStringLength = 0;
    QList<FilterSpec> specs =
        filterSpecs(m_options->nameFilters(), m_options->options() & QFileDialogOptions::HideNameFilterDetails, &totalStringLength);
    const int size = specs.size();
    wchar_t *ptr = new wchar_t[totalStringLength + 2 * size + 1];
    ofn->lpstrFilter = ptr;
    foreach (const FilterSpec &spec, specs) {
        ptr += spec.description.toWCharArray(ptr);
        *ptr++ = 0;
        ptr += spec.filter.toWCharArray(ptr);
        *ptr++ = 0;
    }
    *ptr = 0;
    const int nameFilterIndex = indexOfNameFilter(m_options->nameFilters(), m_data.selectedNameFilter());
    if (nameFilterIndex >= 0)
        ofn->nFilterIndex = nameFilterIndex + 1; // 1..n based.
    // lpstrFile receives the initial selection and is the buffer
    // for the target. If it contains any invalid character, the dialog
    // will not show.
    ofn->nMaxFile = 65535;
    const QString initiallySelectedFile =
        QDir::toNativeSeparators(m_data.selectedFile()).remove(QLatin1Char('<')).
            remove(QLatin1Char('>')).remove(QLatin1Char('"')).remove(QLatin1Char('|'));
    ofn->lpstrFile = qStringToWCharArray(initiallySelectedFile, ofn->nMaxFile);
    ofn->lpstrInitialDir = qStringToWCharArray(QDir::toNativeSeparators(m_data.directory().toLocalFile()));
    ofn->lpstrTitle = (wchar_t*)m_title.utf16();
    // Determine lpstrDefExt. Note that the current MSDN docs document this
    // member wrong. It should rather be documented as "the default extension
    // if no extension was given and if the current filter does not have an
    // extension (e.g (*)). If the current filter has an extension, use
    // the extension of the current filter".
    if (m_options->acceptMode() == QFileDialogOptions::AcceptSave) {
        QString defaultSuffix = m_options->defaultSuffix();
        if (defaultSuffix.startsWith(QLatin1Char('.')))
            defaultSuffix.remove(0, 1);
        // QTBUG-33156, also create empty strings to trigger the appending mechanism.
        ofn->lpstrDefExt = qStringToWCharArray(defaultSuffix);
    }
    // Flags.
    ofn->Flags = (OFN_NOCHANGEDIR | OFN_HIDEREADONLY | OFN_EXPLORER | OFN_PATHMUSTEXIST);
    if (m_options->fileMode() == QFileDialogOptions::ExistingFile
        || m_options->fileMode() == QFileDialogOptions::ExistingFiles)
        ofn->Flags |= (OFN_FILEMUSTEXIST);
    if (m_options->fileMode() == QFileDialogOptions::ExistingFiles)
        ofn->Flags |= (OFN_ALLOWMULTISELECT);
    if (!(m_options->options() & QFileDialogOptions::DontConfirmOverwrite))
        ofn->Flags |= OFN_OVERWRITEPROMPT;
}

QList<QUrl> QWindowsXpNativeFileDialog::execFileNames(HWND owner, int *selectedFilterIndex) const
{
    *selectedFilterIndex = -1;
    OPENFILENAME ofn;
    populateOpenFileName(&ofn, owner);
    QList<QUrl> result;
    const bool isSave = m_options->acceptMode() == QFileDialogOptions::AcceptSave;
    if (isSave ? m_getSaveFileNameW(&ofn) : m_getOpenFileNameW(&ofn)) {
        *selectedFilterIndex = ofn.nFilterIndex - 1;
        const QString dir = QDir::cleanPath(QString::fromWCharArray(ofn.lpstrFile));
        result.push_back(QUrl::fromLocalFile(dir));
        // For multiselection, the first item is the path followed
        // by "\0<file1>\0<file2>\0\0".
        if (ofn.Flags & (OFN_ALLOWMULTISELECT)) {
            wchar_t *ptr = ofn.lpstrFile + dir.size() + 1;
            if (*ptr) {
                result.pop_front();
                const QString path = dir + QLatin1Char('/');
                while (*ptr) {
                    const QString fileName = QString::fromWCharArray(ptr);
                    result.push_back(QUrl::fromLocalFile(path + fileName));
                    ptr += fileName.size() + 1;
                } // extract multiple files
            } // has multiple files
        } // multiple flag set
    }
    delete [] ofn.lpstrFile;
    delete [] ofn.lpstrInitialDir;
    delete [] ofn.lpstrFilter;
    delete [] ofn.lpstrDefExt;
    return result;
}

/*!
    \class QWindowsXpFileDialogHelper
    \brief Dialog helper using QWindowsXpNativeFileDialog

    \sa QWindowsXpNativeFileDialog
    \internal
    \ingroup qt-lighthouse-win
*/

class QWindowsXpFileDialogHelper : public QWindowsDialogHelperBase<QPlatformFileDialogHelper>
{
public:
    QWindowsXpFileDialogHelper() {}
    virtual bool supportsNonModalDialog(const QWindow * /* parent */ = 0) const { return false; }
    virtual bool defaultNameFilterDisables() const
        { return true; }
    virtual void setDirectory(const QUrl &directory) Q_DECL_OVERRIDE;
    virtual QUrl directory() const Q_DECL_OVERRIDE;
    virtual void selectFile(const QUrl &url) Q_DECL_OVERRIDE;
    virtual QList<QUrl> selectedFiles() const Q_DECL_OVERRIDE;
	virtual QByteArray selectedRemoteContent() const Q_DECL_OVERRIDE;
    virtual void setFilter() Q_DECL_OVERRIDE {}
    virtual void selectNameFilter(const QString &) Q_DECL_OVERRIDE;
    virtual QString selectedNameFilter() const Q_DECL_OVERRIDE;

private:
    virtual QWindowsNativeDialogBase *createNativeDialog();
    inline QWindowsXpNativeFileDialog *nativeFileDialog() const
        { return static_cast<QWindowsXpNativeFileDialog *>(nativeDialog()); }

    QWindowsFileDialogSharedData m_data;
};

QWindowsNativeDialogBase *QWindowsXpFileDialogHelper::createNativeDialog()
{
    m_data.fromOptions(options());
    if (QWindowsXpNativeFileDialog *result = QWindowsXpNativeFileDialog::create(options(), m_data)) {
        QObject::connect(result, SIGNAL(accepted()), this, SIGNAL(accept()));
        QObject::connect(result, SIGNAL(rejected()), this, SIGNAL(reject()));
        return result;
    }
    return 0;
}

void QWindowsXpFileDialogHelper::setDirectory(const QUrl &directory)
{
    m_data.setDirectory(directory); // Dialog cannot be updated at run-time.
}

QUrl QWindowsXpFileDialogHelper::directory() const
{
    return m_data.directory();
}

void QWindowsXpFileDialogHelper::selectFile(const QUrl &url)
{
    m_data.setSelectedFiles(QList<QUrl>() << url); // Dialog cannot be updated at run-time.
}

QList<QUrl> QWindowsXpFileDialogHelper::selectedFiles() const
{
    return m_data.selectedFiles();
}

QByteArray QWindowsXpFileDialogHelper::selectedRemoteContent() const
{
	return m_data.selectedRemoteContent();
}

void QWindowsXpFileDialogHelper::selectNameFilter(const QString &f)
{
    m_data.setSelectedNameFilter(f); // Dialog cannot be updated at run-time.
}

QString QWindowsXpFileDialogHelper::selectedNameFilter() const
{
    return m_data.selectedNameFilter();
}

#endif // Q_OS_WINCE

/*!
    \class QWindowsNativeColorDialog
    \brief Native Windows color dialog.

    Wrapper around Comdlg32's ChooseColor() function.
    Not currently in use as QColorDialog is equivalent.

    \sa QWindowsColorDialogHelper
    \sa #define USE_NATIVE_COLOR_DIALOG
    \internal
    \ingroup qt-lighthouse-win
*/

typedef QSharedPointer<QColor> SharedPointerColor;

#ifdef USE_NATIVE_COLOR_DIALOG
class QWindowsNativeColorDialog : public QWindowsNativeDialogBase
{
    Q_OBJECT
public:
    enum { CustomColorCount = 16 };

    explicit QWindowsNativeColorDialog(const SharedPointerColor &color);

    virtual void setWindowTitle(const QString &) {}
    virtual QPlatformDialogHelper::DialogCode result() const { return m_code; }

public slots:
    virtual void close() {}

private:
    virtual void doExec(HWND owner = 0);

    COLORREF m_customColors[CustomColorCount];
    QPlatformDialogHelper::DialogCode m_code;
    SharedPointerColor m_color;
};

QWindowsNativeColorDialog::QWindowsNativeColorDialog(const SharedPointerColor &color) :
    m_code(QPlatformDialogHelper::Rejected), m_color(color)
{
    std::fill(m_customColors, m_customColors + 16, COLORREF(0));
}

void QWindowsNativeColorDialog::doExec(HWND owner)
{
    typedef BOOL (WINAPI *ChooseColorWType)(LPCHOOSECOLORW);

    CHOOSECOLOR chooseColor;
    ZeroMemory(&chooseColor, sizeof(chooseColor));
    chooseColor.lStructSize = sizeof(chooseColor);
    chooseColor.hwndOwner = owner;
    chooseColor.lpCustColors = m_customColors;
    QRgb *qCustomColors = QColorDialogOptions::customColors();
    const int customColorCount = qMin(QColorDialogOptions::customColorCount(),
                                      int(CustomColorCount));
    for (int c= 0; c < customColorCount; ++c)
        m_customColors[c] = qColorToCOLORREF(QColor(qCustomColors[c]));
    chooseColor.rgbResult = qColorToCOLORREF(*m_color);
    chooseColor.Flags = CC_FULLOPEN | CC_RGBINIT;
    static ChooseColorWType chooseColorW = 0;
    if (!chooseColorW) {
        QSystemLibrary library(QStringLiteral("Comdlg32"));
        chooseColorW = (ChooseColorWType)library.resolve("ChooseColorW");
    }
    if (chooseColorW) {
        m_code = chooseColorW(&chooseColor) ?
            QPlatformDialogHelper::Accepted : QPlatformDialogHelper::Rejected;
        QWindowsDialogs::eatMouseMove();
    } else {
        m_code = QPlatformDialogHelper::Rejected;
    }
    if (m_code == QPlatformDialogHelper::Accepted) {
        *m_color = COLORREFToQColor(chooseColor.rgbResult);
        for (int c= 0; c < customColorCount; ++c)
            qCustomColors[c] = COLORREFToQColor(m_customColors[c]).rgb();
        emit accepted();
    } else {
        emit rejected();
    }
}

/*!
    \class QWindowsColorDialogHelper
    \brief Helper for native Windows color dialogs

    Not currently in use as QColorDialog is equivalent.

    \sa #define USE_NATIVE_COLOR_DIALOG
    \sa QWindowsNativeColorDialog
    \internal
    \ingroup qt-lighthouse-win
*/

class QWindowsColorDialogHelper : public QWindowsDialogHelperBase<QPlatformColorDialogHelper>
{
public:
    QWindowsColorDialogHelper() : m_currentColor(new QColor) {}

    virtual bool supportsNonModalDialog()
        { return false; }

    virtual QColor currentColor() const { return *m_currentColor; }
    virtual void setCurrentColor(const QColor &c) { *m_currentColor = c; }

private:
    inline QWindowsNativeColorDialog *nativeFileDialog() const
        { return static_cast<QWindowsNativeColorDialog *>(nativeDialog()); }
    virtual QWindowsNativeDialogBase *createNativeDialog();

    SharedPointerColor m_currentColor;
};

QWindowsNativeDialogBase *QWindowsColorDialogHelper::createNativeDialog()
{
    QWindowsNativeColorDialog *nativeDialog = new QWindowsNativeColorDialog(m_currentColor);
    nativeDialog->setWindowTitle(options()->windowTitle());
    connect(nativeDialog, SIGNAL(accepted()), this, SIGNAL(accept()));
    connect(nativeDialog, SIGNAL(rejected()), this, SIGNAL(reject()));
    return nativeDialog;
}
#endif // USE_NATIVE_COLOR_DIALOG

namespace QWindowsDialogs {

// QWindowsDialogHelperBase creation functions
bool useHelper(QPlatformTheme::DialogType type)
{
    if (QWindowsIntegration::instance()->options() & QWindowsIntegration::NoNativeDialogs)
        return false;
    switch (type) {
    case QPlatformTheme::FileDialog:
        return QSysInfo::windowsVersion() >= QSysInfo::WV_XP;
    case QPlatformTheme::ColorDialog:
#ifdef USE_NATIVE_COLOR_DIALOG
        return true;
#else
        break;
#endif
    case QPlatformTheme::FontDialog:
    case QPlatformTheme::MessageDialog:
        break;
    default:
        break;
    }
    return false;
}

QPlatformDialogHelper *createHelper(QPlatformTheme::DialogType type)
{
    if (QWindowsIntegration::instance()->options() & QWindowsIntegration::NoNativeDialogs)
        return 0;
    switch (type) {
    case QPlatformTheme::FileDialog:
#ifndef Q_OS_WINCE // Note: "Windows XP Professional x64 Edition has version number WV_5_2 (WV_2003).
        if (QWindowsIntegration::instance()->options() & QWindowsIntegration::XpNativeDialogs
            || QSysInfo::windowsVersion() <= QSysInfo::WV_2003) {
            return new QWindowsXpFileDialogHelper();
        }
        if (QSysInfo::windowsVersion() > QSysInfo::WV_2003)
            return new QWindowsFileDialogHelper();
#else
        return new QWindowsFileDialogHelper();
#endif // Q_OS_WINCE
    case QPlatformTheme::ColorDialog:
#ifdef USE_NATIVE_COLOR_DIALOG
        return new QWindowsColorDialogHelper();
#else
        break;
#endif
    case QPlatformTheme::FontDialog:
    case QPlatformTheme::MessageDialog:
        break;
    default:
        break;
    }
    return 0;
}

} // namespace QWindowsDialogs
QT_END_NAMESPACE

#include "qwindowsdialoghelpers.moc"
