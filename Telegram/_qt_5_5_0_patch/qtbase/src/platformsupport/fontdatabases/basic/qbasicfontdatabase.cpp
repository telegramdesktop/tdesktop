/****************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the plugins of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL21$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see http://www.qt.io/terms-conditions. For further
** information use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 or version 3 as published by the Free
** Software Foundation and appearing in the file LICENSE.LGPLv21 and
** LICENSE.LGPLv3 included in the packaging of this file. Please review the
** following information to ensure the GNU Lesser General Public License
** requirements will be met: https://www.gnu.org/licenses/lgpl.html and
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** As a special exception, The Qt Company gives you certain additional
** rights. These rights are described in The Qt Company LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
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

void QBasicFontDatabase::populateFontDatabase()
{
    QString fontpath = fontDir();

    if(!QFile::exists(fontpath)) {
        qWarning("QFontDatabase: Cannot find font directory %s - is Qt installed correctly?",
               qPrintable(fontpath));
        return;
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

inline static void setHintingPreference(QFontEngine *engine, QFont::HintingPreference hintingPreference)
{
    switch (hintingPreference) {
    case QFont::PreferNoHinting:
        engine->setDefaultHintStyle(QFontEngineFT::HintNone);
        break;
    case QFont::PreferFullHinting:
        engine->setDefaultHintStyle(QFontEngineFT::HintFull);
        break;
    case QFont::PreferVerticalHinting:
        engine->setDefaultHintStyle(QFontEngineFT::HintLight);
        break;
    case QFont::PreferDefaultHinting:
        // Leave it as it is
        break;
    }
}

QFontEngine *QBasicFontDatabase::fontEngine(const QFontDef &fontDef, void *usrPtr)
{
    FontFile *fontfile = static_cast<FontFile *> (usrPtr);
    QFontEngine::FaceId fid;
    fid.filename = QFile::encodeName(fontfile->fileName);
    fid.index = fontfile->indexValue;

    bool antialias = !(fontDef.styleStrategy & QFont::NoAntialias);
    QFontEngineFT *engine = new QFontEngineFT(fontDef);
    QFontEngineFT::GlyphFormat format = QFontEngineFT::Format_Mono;
    if (antialias) {
        QFontEngine::SubpixelAntialiasingType subpixelType = subpixelAntialiasingTypeHint();
        if (subpixelType == QFontEngine::Subpixel_None || (fontDef.styleStrategy & QFont::NoSubpixelAntialias)) {
            format = QFontEngineFT::Format_A8;
            engine->subpixelType = QFontEngine::Subpixel_None;
        } else {
            format = QFontEngineFT::Format_A32;
            engine->subpixelType = subpixelType;
        }
    }

    if (!engine->init(fid, antialias, format) || engine->invalid()) {
        delete engine;
        engine = 0;
    } else {
        setHintingPreference(engine, static_cast<QFont::HintingPreference>(fontDef.hintingPreference));
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
    setHintingPreference(fe, hintingPreference);

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
	if (!pathname)
		return FT_Err_Invalid_Argument;

	FT_Parameter params[2];
	params[0].tag = FT_PARAM_TAG_IGNORE_PREFERRED_FAMILY;
	params[0].data = 0;
	params[1].tag = FT_PARAM_TAG_IGNORE_PREFERRED_SUBFAMILY;
	params[1].data = 0;
	args.flags = FT_OPEN_PATHNAME | FT_OPEN_PARAMS;
	args.pathname = (char*)pathname;
	args.stream = NULL;
	args.num_params = 2;
	args.params = params;

	return FT_Open_Face(library, &args, face_index, aface);
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
	if (!file_base)
		return FT_Err_Invalid_Argument;

	FT_Parameter params[2];
	params[0].tag = FT_PARAM_TAG_IGNORE_PREFERRED_FAMILY;
	params[0].data = 0;
	params[1].tag = FT_PARAM_TAG_IGNORE_PREFERRED_SUBFAMILY;
	params[1].data = 0;
	args.flags = FT_OPEN_MEMORY | FT_OPEN_PARAMS;
	args.memory_base = file_base;
	args.memory_size = file_size;
	args.stream = NULL;
	args.num_params = 2;
	args.params = params;

	return FT_Open_Face(library, &args, face_index, aface);
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

            if (os2->usWeightClass) {
                weight = QPlatformFontDatabase::weightFromInteger(os2->usWeightClass);
            } else if (os2->panose[2]) {
                int w = os2->panose[2];
                if (w <= 1)
                    weight = QFont::Thin;
                else if (w <= 2)
                    weight = QFont::ExtraLight;
                else if (w <= 3)
                    weight = QFont::Light;
                else if (w <= 5)
                    weight = QFont::Normal;
                else if (w <= 6)
                    weight = QFont::Medium;
                else if (w <= 7)
                    weight = QFont::DemiBold;
                else if (w <= 8)
                    weight = QFont::Bold;
                else if (w <= 9)
                    weight = QFont::ExtraBold;
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

QT_END_NAMESPACE
