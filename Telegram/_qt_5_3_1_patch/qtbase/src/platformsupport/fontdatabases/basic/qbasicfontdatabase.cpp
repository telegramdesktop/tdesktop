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

#include "qbasicfontdatabase_p.h"

#include <QtGui/private/qguiapplication_p.h>
#include <qpa/qplatformscreen.h>

#include <QtCore/QFile>
#include <QtCore/QLibraryInfo>
#include <QtCore/QDir>
#include <QtCore/QUuid>
#include <QtCore/QtEndian>

#undef QT_NO_FREETYPE
#include <QtGui/private/qfontengine_ft_p.h>
#include <QtGui/private/qfontengine_p.h>

#include <ft2build.h>
#include FT_TRUETYPE_TABLES_H
#include FT_ERRORS_H

QT_BEGIN_NAMESPACE

typedef struct {
    quint16 majorVersion;
    quint16 minorVersion;
    quint16 numTables;
    quint16 searchRange;
    quint16 entrySelector;
    quint16 rangeShift;
} OFFSET_TABLE;

typedef struct {
    quint32 tag;
    quint32 checkSum;
    quint32 offset;
    quint32 length;
} TABLE_DIRECTORY;

typedef struct {
    quint16 fontSelector;
    quint16 nrCount;
    quint16 storageOffset;
} NAME_TABLE_HEADER;

typedef struct {
    quint16 platformID;
    quint16 encodingID;
    quint16 languageID;
    quint16 nameID;
    quint16 stringLength;
    quint16 stringOffset;
} NAME_RECORD;

void QBasicFontDatabase::populateFontDatabase()
{
    QString fontpath = fontDir();

    if(!QFile::exists(fontpath)) {
        qFatal("QFontDatabase: Cannot find font directory %s - is Qt installed correctly?",
               qPrintable(fontpath));
    }

    QDir dir(fontpath);
    dir.setNameFilters(QStringList() << QLatin1String("*.ttf")
                       << QLatin1String("*.ttc") << QLatin1String("*.pfa")
                       << QLatin1String("*.pfb")
                       << QLatin1String("*.otf"));
    dir.refresh();
    for (int i = 0; i < int(dir.count()); ++i) {
        const QByteArray file = QFile::encodeName(dir.absoluteFilePath(dir[i]));
//        qDebug() << "looking at" << file;
        addTTFile(QByteArray(), file);
    }
}

QFontEngine *QBasicFontDatabase::fontEngine(const QFontDef &fontDef, void *usrPtr)
{
    FontFile *fontfile = static_cast<FontFile *> (usrPtr);
    QFontEngine::FaceId fid;
    fid.filename = QFile::encodeName(fontfile->fileName);
    fid.index = fontfile->indexValue;

    bool antialias = !(fontDef.styleStrategy & QFont::NoAntialias);
    QFontEngineFT::GlyphFormat format = antialias? QFontEngineFT::Format_A8 : QFontEngineFT::Format_Mono;

    QFontEngineFT *engine = new QFontEngineFT(fontDef);
    if (!engine->init(fid, antialias, format) || engine->invalid()) {
        delete engine;
        engine = 0;
    }

    return engine;
}

namespace {

    class QFontEngineFTRawData: public QFontEngineFT
    {
    public:
        QFontEngineFTRawData(const QFontDef &fontDef) : QFontEngineFT(fontDef)
        {
        }

        void updateFamilyNameAndStyle()
        {
            fontDef.family = QString::fromLatin1(freetype->face->family_name);

            if (freetype->face->style_flags & FT_STYLE_FLAG_ITALIC)
                fontDef.style = QFont::StyleItalic;

            if (freetype->face->style_flags & FT_STYLE_FLAG_BOLD)
                fontDef.weight = QFont::Bold;
        }

        bool initFromData(const QByteArray &fontData)
        {
            FaceId faceId;
            faceId.filename = "";
            faceId.index = 0;
            faceId.uuid = QUuid::createUuid().toByteArray();

            return init(faceId, true, Format_None, fontData);
        }
    };

}

QFontEngine *QBasicFontDatabase::fontEngine(const QByteArray &fontData, qreal pixelSize,
                                                QFont::HintingPreference hintingPreference)
{
    QFontDef fontDef;
    fontDef.pixelSize = pixelSize;
    fontDef.hintingPreference = hintingPreference;

    QFontEngineFTRawData *fe = new QFontEngineFTRawData(fontDef);
    if (!fe->initFromData(fontData)) {
        delete fe;
        return 0;
    }

    fe->updateFamilyNameAndStyle();

    switch (hintingPreference) {
    case QFont::PreferNoHinting:
        fe->setDefaultHintStyle(QFontEngineFT::HintNone);
        break;
    case QFont::PreferFullHinting:
        fe->setDefaultHintStyle(QFontEngineFT::HintFull);
        break;
    case QFont::PreferVerticalHinting:
        fe->setDefaultHintStyle(QFontEngineFT::HintLight);
        break;
    default:
        // Leave it as it is
        break;
    }

    return fe;
}

QStringList QBasicFontDatabase::fallbacksForFamily(const QString &family, QFont::Style style, QFont::StyleHint styleHint, QChar::Script script) const
{
    Q_UNUSED(family);
    Q_UNUSED(style);
    Q_UNUSED(script);
    Q_UNUSED(styleHint);
    return QStringList();
}

QStringList QBasicFontDatabase::addApplicationFont(const QByteArray &fontData, const QString &fileName)
{
    return addTTFile(fontData,fileName.toLocal8Bit());
}

void QBasicFontDatabase::releaseHandle(void *handle)
{
    FontFile *file = static_cast<FontFile *>(handle);
    delete file;
}

extern FT_Library qt_getFreetype();

// copied from freetype with some modifications

#ifndef FT_PARAM_TAG_IGNORE_PREFERRED_FAMILY
#define FT_PARAM_TAG_IGNORE_PREFERRED_FAMILY FT_MAKE_TAG('i', 'g', 'p', 'f')
#endif

#ifndef FT_PARAM_TAG_IGNORE_PREFERRED_SUBFAMILY
#define FT_PARAM_TAG_IGNORE_PREFERRED_SUBFAMILY FT_MAKE_TAG('i', 'g', 'p', 's')
#endif

/* there's a Mac-specific extended implementation of FT_New_Face() */
/* in src/base/ftmac.c                                             */

#if !defined( FT_MACINTOSH ) || defined( DARWIN_NO_CARBON )

/* documentation is in freetype.h */

FT_Error __ft_New_Face(FT_Library library, const char* pathname, FT_Long face_index, FT_Face *aface) {
  FT_Open_Args args;

  /* test for valid `library' and `aface' delayed to FT_Open_Face() */
  if ( !pathname )
    return FT_Err_Invalid_Argument;

  FT_Parameter params[2];
  params[0].tag   = FT_PARAM_TAG_IGNORE_PREFERRED_FAMILY;
  params[0].data  = 0;
  params[1].tag   = FT_PARAM_TAG_IGNORE_PREFERRED_SUBFAMILY;
  params[1].data  = 0;
  args.flags      = FT_OPEN_PATHNAME | FT_OPEN_PARAMS;
  args.pathname   = (char*)pathname;
  args.stream     = NULL;
  args.num_params = 2;
  args.params     = params;

  return FT_Open_Face( library, &args, face_index, aface );
}

#else

FT_Error __ft_New_Face(FT_Library library, const char* pathname, FT_Long face_index, FT_Face *aface) {
    return FT_New_Face(library, pathname, face_index, aface);
}

#endif  /* defined( FT_MACINTOSH ) && !defined( DARWIN_NO_CARBON ) */

/* documentation is in freetype.h */

FT_Error __ft_New_Memory_Face(FT_Library library, const FT_Byte* file_base, FT_Long file_size, FT_Long face_index, FT_Face *aface) {
  FT_Open_Args  args;

  /* test for valid `library' and `face' delayed to FT_Open_Face() */
  if ( !file_base )
    return FT_Err_Invalid_Argument;

  FT_Parameter params[2];
  params[0].tag    = FT_PARAM_TAG_IGNORE_PREFERRED_FAMILY;
  params[0].data   = 0;
  params[1].tag    = FT_PARAM_TAG_IGNORE_PREFERRED_SUBFAMILY;
  params[1].data   = 0;
  args.flags       = FT_OPEN_MEMORY | FT_OPEN_PARAMS;
  args.memory_base = file_base;
  args.memory_size = file_size;
  args.stream      = NULL;
  args.num_params  = 2;
  args.params      = params;

  return FT_Open_Face( library, &args, face_index, aface );
}

// end

QStringList QBasicFontDatabase::addTTFile(const QByteArray &fontData, const QByteArray &file, QSupportedWritingSystems *supportedWritingSystems)
{
    FT_Library library = qt_getFreetype();

    int index = 0;
    int numFaces = 0;
    QStringList families;
    do {
        FT_Face face;
        FT_Error error;
        if (!fontData.isEmpty()) {
            error = __ft_New_Memory_Face(library, (const FT_Byte *)fontData.constData(), fontData.size(), index, &face);
        } else {
            error = __ft_New_Face(library, file.constData(), index, &face);
        }
        if (error != FT_Err_Ok) {
            qDebug() << "FT_New_Face failed with index" << index << ":" << hex << error;
            break;
        }
        numFaces = face->num_faces;

        QFont::Weight weight = QFont::Normal;

        QFont::Style style = QFont::StyleNormal;
        if (face->style_flags & FT_STYLE_FLAG_ITALIC)
            style = QFont::StyleItalic;

        if (face->style_flags & FT_STYLE_FLAG_BOLD)
            weight = QFont::Bold;

        bool fixedPitch = (face->face_flags & FT_FACE_FLAG_FIXED_WIDTH);

        QSupportedWritingSystems writingSystems;
        // detect symbol fonts
        for (int i = 0; i < face->num_charmaps; ++i) {
            FT_CharMap cm = face->charmaps[i];
            if (cm->encoding == FT_ENCODING_ADOBE_CUSTOM
                    || cm->encoding == FT_ENCODING_MS_SYMBOL) {
                writingSystems.setSupported(QFontDatabase::Symbol);
                if (supportedWritingSystems)
                    supportedWritingSystems->setSupported(QFontDatabase::Symbol);
                break;
            }
        }

        TT_OS2 *os2 = (TT_OS2 *)FT_Get_Sfnt_Table(face, ft_sfnt_os2);
        if (os2) {
            quint32 unicodeRange[4] = {
                quint32(os2->ulUnicodeRange1),
                quint32(os2->ulUnicodeRange2),
                quint32(os2->ulUnicodeRange3),
                quint32(os2->ulUnicodeRange4)
            };
            quint32 codePageRange[2] = {
                quint32(os2->ulCodePageRange1),
                quint32(os2->ulCodePageRange2)
            };

            writingSystems = QPlatformFontDatabase::writingSystemsFromTrueTypeBits(unicodeRange, codePageRange);
            if (supportedWritingSystems)
                *supportedWritingSystems = writingSystems;

            if (os2->usWeightClass == 0)
                ;
            else if (os2->usWeightClass < 350)
                weight = QFont::Light;
            else if (os2->usWeightClass < 450)
                weight = QFont::Normal;
            else if (os2->usWeightClass < 650)
                weight = QFont::DemiBold;
            else if (os2->usWeightClass < 750)
                weight = QFont::Bold;
            else if (os2->usWeightClass < 1000)
                weight = QFont::Black;

            if (os2->panose[2] >= 2) {
                int w = os2->panose[2];
                if (w <= 3)
                    weight = QFont::Light;
                else if (w <= 5)
                    weight = QFont::Normal;
                else if (w <= 7)
                    weight = QFont::DemiBold;
                else if (w <= 8)
                    weight = QFont::Bold;
                else if (w <= 10)
                    weight = QFont::Black;
            }
        }

        QString family = QString::fromLatin1(face->family_name);
        FontFile *fontFile = new FontFile;
        fontFile->fileName = QFile::decodeName(file);
        fontFile->indexValue = index;

        QFont::Stretch stretch = QFont::Unstretched;

        registerFont(family,QString::fromLatin1(face->style_name),QString(),weight,style,stretch,true,true,0,fixedPitch,writingSystems,fontFile);

        families.append(family);

        FT_Done_Face(face);
        ++index;
    } while (index < numFaces);
    return families;
}

QString QBasicFontDatabase::fontNameFromTTFile(const QString &filename)
{
    QFile f(filename);
    QString retVal;
    qint64 bytesRead;
    qint64 bytesToRead;

    if (f.open(QIODevice::ReadOnly)) {
        OFFSET_TABLE ttOffsetTable;
        bytesToRead = sizeof(OFFSET_TABLE);
        bytesRead = f.read((char*)&ttOffsetTable, bytesToRead);
        if (bytesToRead != bytesRead)
            return retVal;
        ttOffsetTable.numTables = qFromBigEndian(ttOffsetTable.numTables);
        ttOffsetTable.majorVersion = qFromBigEndian(ttOffsetTable.majorVersion);
        ttOffsetTable.minorVersion = qFromBigEndian(ttOffsetTable.minorVersion);

        if (ttOffsetTable.majorVersion != 1 || ttOffsetTable.minorVersion != 0)
            return retVal;

        TABLE_DIRECTORY tblDir;
        bool found = false;

        for (int i = 0; i < ttOffsetTable.numTables; i++) {
            bytesToRead = sizeof(TABLE_DIRECTORY);
            bytesRead = f.read((char*)&tblDir, bytesToRead);
            if (bytesToRead != bytesRead)
                return retVal;
            if (qFromBigEndian(tblDir.tag) == MAKE_TAG('n', 'a', 'm', 'e')) {
                found = true;
                tblDir.length = qFromBigEndian(tblDir.length);
                tblDir.offset = qFromBigEndian(tblDir.offset);
                break;
            }
        }

        if (found) {
            f.seek(tblDir.offset);
            NAME_TABLE_HEADER ttNTHeader;
            bytesToRead = sizeof(NAME_TABLE_HEADER);
            bytesRead = f.read((char*)&ttNTHeader, bytesToRead);
            if (bytesToRead != bytesRead)
                return retVal;
            ttNTHeader.nrCount = qFromBigEndian(ttNTHeader.nrCount);
            ttNTHeader.storageOffset = qFromBigEndian(ttNTHeader.storageOffset);
            NAME_RECORD ttRecord;
            found = false;

            for (int i = 0; i < ttNTHeader.nrCount; i++) {
                bytesToRead = sizeof(NAME_RECORD);
                bytesRead = f.read((char*)&ttRecord, bytesToRead);
                if (bytesToRead != bytesRead)
                    return retVal;
                ttRecord.nameID = qFromBigEndian(ttRecord.nameID);
                if (ttRecord.nameID == 1) {
                    ttRecord.stringLength = qFromBigEndian(ttRecord.stringLength);
                    ttRecord.stringOffset = qFromBigEndian(ttRecord.stringOffset);
                    int nPos = f.pos();
                    f.seek(tblDir.offset + ttRecord.stringOffset + ttNTHeader.storageOffset);

                    QByteArray nameByteArray = f.read(ttRecord.stringLength);
                    if (!nameByteArray.isEmpty()) {
                        if (ttRecord.encodingID == 256 || ttRecord.encodingID == 768) {
                            //This is UTF-16 in big endian
                            int stringLength = ttRecord.stringLength / 2;
                            retVal.resize(stringLength);
                            QChar *data = retVal.data();
                            const ushort *srcData = (const ushort *)nameByteArray.data();
                            for (int i = 0; i < stringLength; ++i)
                                data[i] = qFromBigEndian(srcData[i]);
                            return retVal;
                        } else if (ttRecord.encodingID == 0) {
                            //This is Latin1
                            retVal = QString::fromLatin1(nameByteArray);
                        } else {
                            qWarning("Could not retrieve Font name from file: %s", qPrintable(QDir::toNativeSeparators(filename)));
                        }
                        break;
                    }
                    f.seek(nPos);
                }
            }
        }
        f.close();
    }
    return retVal;
}

QT_END_NAMESPACE
