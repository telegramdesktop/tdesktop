/****************************************************************************
**
** Copyright (C) 2014 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the plugins of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL21$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia. For licensing terms and
** conditions see http://qt.digia.com/licensing. For further information
** use the contact form at http://qt.digia.com/contact-us.
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
** In addition, as a special exception, Digia gives you certain additional
** rights. These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qfontconfigdatabase_p.h"
#include "qfontenginemultifontconfig_p.h"

#include <QtCore/QList>
#include <QtCore/QElapsedTimer>

#include <qpa/qplatformnativeinterface.h>
#include <qpa/qplatformscreen.h>
#include <qpa/qplatformintegration.h>
#include <qpa/qplatformservices.h>

#include <QtGui/private/qfontengine_ft_p.h>
#include <QtGui/private/qguiapplication_p.h>

#include <QtGui/qguiapplication.h>

#include <fontconfig/fontconfig.h>
#if FC_VERSION >= 20402
#include <fontconfig/fcfreetype.h>
#endif

QT_BEGIN_NAMESPACE

static inline bool requiresOpenType(int writingSystem)
{
    return ((writingSystem >= QFontDatabase::Syriac && writingSystem <= QFontDatabase::Sinhala)
            || writingSystem == QFontDatabase::Khmer || writingSystem == QFontDatabase::Nko);
}

static inline int weightFromFcWeight(int fcweight)
{
    // Font Config uses weights from 0 to 215 (the highest enum value) while QFont ranges from
    // 0 to 99. The spacing between the values for the enums are uneven so a linear mapping from
    // Font Config values to Qt would give surprising results.  So, we do a piecewise linear
    // mapping.  This ensures that where there is a corresponding enum on both sides (for example
    // FC_WEIGHT_DEMIBOLD and QFont::DemiBold) we map one to the other but other values map
    // to intermediate Qt weights.
    const int maxWeight = 99;
    int qtweight;
    if (fcweight < 0)
        qtweight = 0;
    else if (fcweight <= FC_WEIGHT_LIGHT)
        qtweight = (fcweight * QFont::Light) / FC_WEIGHT_LIGHT;
    else if (fcweight <= FC_WEIGHT_NORMAL)
        qtweight = QFont::Light + ((fcweight - FC_WEIGHT_LIGHT) * (QFont::Normal - QFont::Light)) / (FC_WEIGHT_NORMAL - FC_WEIGHT_LIGHT);
    else if (fcweight <= FC_WEIGHT_DEMIBOLD)
        qtweight = QFont::Normal + ((fcweight - FC_WEIGHT_NORMAL) * (QFont::DemiBold - QFont::Normal)) / (FC_WEIGHT_DEMIBOLD - FC_WEIGHT_NORMAL);
    else if (fcweight <= FC_WEIGHT_BOLD)
        qtweight = QFont::DemiBold + ((fcweight - FC_WEIGHT_DEMIBOLD) * (QFont::Bold - QFont::DemiBold)) / (FC_WEIGHT_BOLD - FC_WEIGHT_DEMIBOLD);
    else if (fcweight <= FC_WEIGHT_BLACK)
        qtweight = QFont::Bold + ((fcweight - FC_WEIGHT_BOLD) * (QFont::Black - QFont::Bold)) / (FC_WEIGHT_BLACK - FC_WEIGHT_BOLD);
    else if (fcweight <= FC_WEIGHT_ULTRABLACK)
        qtweight = QFont::Black + ((fcweight - FC_WEIGHT_BLACK) * (maxWeight - QFont::Black)) / (FC_WEIGHT_ULTRABLACK - FC_WEIGHT_BLACK);
    else
        qtweight = maxWeight;

    return qtweight;
}

static inline int stretchFromFcWidth(int fcwidth)
{
    // Font Config enums for width match pretty closely with those used by Qt so just use
    // Font Config values directly while enforcing the same limits imposed by QFont.
    const int maxStretch = 4000;
    int qtstretch;
    if (fcwidth < 1)
        qtstretch = 1;
    else if (fcwidth > maxStretch)
        qtstretch = maxStretch;
    else
        qtstretch = fcwidth;

    return qtstretch;
}

static const char *specialLanguages[] = {
    "", // Unknown
    "", // Inherited
    "", // Common
    "en", // Latin
    "el", // Greek
    "ru", // Cyrillic
    "hy", // Armenian
    "he", // Hebrew
    "ar", // Arabic
    "syr", // Syriac
    "dv", // Thaana
    "hi", // Devanagari
    "bn", // Bengali
    "pa", // Gurmukhi
    "gu", // Gujarati
    "or", // Oriya
    "ta", // Tamil
    "te", // Telugu
    "kn", // Kannada
    "ml", // Malayalam
    "si", // Sinhala
    "th", // Thai
    "lo", // Lao
    "bo", // Tibetan
    "my", // Myanmar
    "ka", // Georgian
    "ko", // Hangul
    "am", // Ethiopic
    "chr", // Cherokee
    "cr", // CanadianAboriginal
    "sga", // Ogham
    "non", // Runic
    "km", // Khmer
    "mn", // Mongolian
    "ja", // Hiragana
    "ja", // Katakana
    "zh-TW", // Bopomofo
    "", // Han
    "ii", // Yi
    "ett", // OldItalic
    "got", // Gothic
    "en", // Deseret
    "fil", // Tagalog
    "hnn", // Hanunoo
    "bku", // Buhid
    "tbw", // Tagbanwa
    "cop", // Coptic
    "lif", // Limbu
    "tdd", // TaiLe
    "grc", // LinearB
    "uga", // Ugaritic
    "en", // Shavian
    "so", // Osmanya
    "grc", // Cypriot
    "", // Braille
    "bug", // Buginese
    "khb", // NewTaiLue
    "cu", // Glagolitic
    "shi", // Tifinagh
    "syl", // SylotiNagri
    "peo", // OldPersian
    "pra", // Kharoshthi
    "ban", // Balinese
    "akk", // Cuneiform
    "phn", // Phoenician
    "lzh", // PhagsPa
    "man", // Nko
    "su", // Sundanese
    "lep", // Lepcha
    "sat", // OlChiki
    "vai", // Vai
    "saz", // Saurashtra
    "eky", // KayahLi
    "rej", // Rejang
    "xlc", // Lycian
    "xcr", // Carian
    "xld", // Lydian
    "cjm", // Cham
    "nod", // TaiTham
    "blt", // TaiViet
    "ae", // Avestan
    "egy", // EgyptianHieroglyphs
    "smp", // Samaritan
    "lis", // Lisu
    "bax", // Bamum
    "jv", // Javanese
    "mni", // MeeteiMayek
    "arc", // ImperialAramaic
    "xsa", // OldSouthArabian
    "xpr", // InscriptionalParthian
    "pal", // InscriptionalPahlavi
    "otk", // OldTurkic
    "bh", // Kaithi
    "bbc", // Batak
    "pra", // Brahmi
    "myz", // Mandaic
    "ccp", // Chakma
    "xmr", // MeroiticCursive
    "xmr", // MeroiticHieroglyphs
    "hmd", // Miao
    "sa", // Sharada
    "srb", // SoraSompeng
    "doi"  // Takri
};
Q_STATIC_ASSERT(sizeof(specialLanguages) / sizeof(const char *) == QChar::ScriptCount);

// this could become a list of all languages used for each writing
// system, instead of using the single most common language.
static const char *languageForWritingSystem[] = {
    0,     // Any
    "en",  // Latin
    "el",  // Greek
    "ru",  // Cyrillic
    "hy",  // Armenian
    "he",  // Hebrew
    "ar",  // Arabic
    "syr", // Syriac
    "div", // Thaana
    "hi",  // Devanagari
    "bn",  // Bengali
    "pa",  // Gurmukhi
    "gu",  // Gujarati
    "or",  // Oriya
    "ta",  // Tamil
    "te",  // Telugu
    "kn",  // Kannada
    "ml",  // Malayalam
    "si",  // Sinhala
    "th",  // Thai
    "lo",  // Lao
    "bo",  // Tibetan
    "my",  // Myanmar
    "ka",  // Georgian
    "km",  // Khmer
    "zh-cn", // SimplifiedChinese
    "zh-tw", // TraditionalChinese
    "ja",  // Japanese
    "ko",  // Korean
    "vi",  // Vietnamese
    0, // Symbol
    "sga", // Ogham
    "non", // Runic
    "man" // N'Ko
};
Q_STATIC_ASSERT(sizeof(languageForWritingSystem) / sizeof(const char *) == QFontDatabase::WritingSystemsCount);

#if FC_VERSION >= 20297
// Newer FontConfig let's us sort out fonts that contain certain glyphs, but no
// open type tables for is directly. Do this so we don't pick some strange
// pseudo unicode font
static const char *openType[] = {
    0,     // Any
    0,  // Latin
    0,  // Greek
    0,  // Cyrillic
    0,  // Armenian
    0,  // Hebrew
    0,  // Arabic
    "syrc",  // Syriac
    "thaa",  // Thaana
    "deva",  // Devanagari
    "beng",  // Bengali
    "guru",  // Gurmukhi
    "gurj",  // Gujarati
    "orya",  // Oriya
    "taml",  // Tamil
    "telu",  // Telugu
    "knda",  // Kannada
    "mlym",  // Malayalam
    "sinh",  // Sinhala
    0,  // Thai
    0,  // Lao
    "tibt",  // Tibetan
    "mymr",  // Myanmar
    0,  // Georgian
    "khmr",  // Khmer
    0, // SimplifiedChinese
    0, // TraditionalChinese
    0,  // Japanese
    0,  // Korean
    0,  // Vietnamese
    0, // Symbol
    0, // Ogham
    0, // Runic
    "nko " // N'Ko
};
Q_STATIC_ASSERT(sizeof(openType) / sizeof(const char *) == QFontDatabase::WritingSystemsCount);
#endif

static const char *getFcFamilyForStyleHint(const QFont::StyleHint style)
{
    const char *stylehint = 0;
    switch (style) {
    case QFont::SansSerif:
        stylehint = "sans-serif";
        break;
    case QFont::Serif:
        stylehint = "serif";
        break;
    case QFont::TypeWriter:
    case QFont::Monospace:
        stylehint = "monospace";
        break;
    case QFont::Cursive:
        stylehint = "cursive";
        break;
    case QFont::Fantasy:
        stylehint = "fantasy";
        break;
    default:
        break;
    }
    return stylehint;
}

static void populateFromPattern(FcPattern *pattern)
{
    QString familyName;
    FcChar8 *value = 0;
    int weight_value;
    int slant_value;
    int spacing_value;
    int width_value;
    FcChar8 *file_value;
    int indexValue;
    FcChar8 *foundry_value;
    FcChar8 *style_value;
    FcBool scalable;
    FcBool antialias;

    if (FcPatternGetString(pattern, FC_FAMILY, 0, &value) != FcResultMatch)
        return;

    familyName = QString::fromUtf8((const char *)value);
    if (familyName == QLatin1String("Open Sans")) {
        FcChar8 *styl = 0;
        if (FcPatternGetString(pattern, FC_STYLE, 0, &styl) == FcResultMatch) {
            QString style = QString::fromUtf8(reinterpret_cast<const char *>(styl));
            if (style == QLatin1String("Semibold")) {
                familyName.append(QChar(QChar::Space)).append(style);
            }
        }
    }

    slant_value = FC_SLANT_ROMAN;
    weight_value = FC_WEIGHT_REGULAR;
    spacing_value = FC_PROPORTIONAL;
    file_value = 0;
    indexValue = 0;
    scalable = FcTrue;


    if (FcPatternGetInteger(pattern, FC_SLANT, 0, &slant_value) != FcResultMatch)
        slant_value = FC_SLANT_ROMAN;
    if (FcPatternGetInteger(pattern, FC_WEIGHT, 0, &weight_value) != FcResultMatch)
        weight_value = FC_WEIGHT_REGULAR;
    if (FcPatternGetInteger(pattern, FC_WIDTH, 0, &width_value) != FcResultMatch)
        width_value = FC_WIDTH_NORMAL;
    if (FcPatternGetInteger(pattern, FC_SPACING, 0, &spacing_value) != FcResultMatch)
        spacing_value = FC_PROPORTIONAL;
    if (FcPatternGetString(pattern, FC_FILE, 0, &file_value) != FcResultMatch)
        file_value = 0;
    if (FcPatternGetInteger(pattern, FC_INDEX, 0, &indexValue) != FcResultMatch)
        indexValue = 0;
    if (FcPatternGetBool(pattern, FC_SCALABLE, 0, &scalable) != FcResultMatch)
        scalable = FcTrue;
    if (FcPatternGetString(pattern, FC_FOUNDRY, 0, &foundry_value) != FcResultMatch)
        foundry_value = 0;
    if (FcPatternGetString(pattern, FC_STYLE, 0, &style_value) != FcResultMatch)
        style_value = 0;
    if (FcPatternGetBool(pattern,FC_ANTIALIAS,0,&antialias) != FcResultMatch)
        antialias = true;

    QSupportedWritingSystems writingSystems;
    FcLangSet *langset = 0;
    FcResult res = FcPatternGetLangSet(pattern, FC_LANG, 0, &langset);
    if (res == FcResultMatch) {
        bool hasLang = false;
        for (int j = 1; j < QFontDatabase::WritingSystemsCount; ++j) {
            const FcChar8 *lang = (const FcChar8*) languageForWritingSystem[j];
            if (lang) {
                FcLangResult langRes = FcLangSetHasLang(langset, lang);
                if (langRes != FcLangDifferentLang) {
                    writingSystems.setSupported(QFontDatabase::WritingSystem(j));
                    hasLang = true;
                }
            }
        }
        if (!hasLang)
            // none of our known languages, add it to the other set
            writingSystems.setSupported(QFontDatabase::Other);
    } else {
        // we set Other to supported for symbol fonts. It makes no
        // sense to merge these with other ones, as they are
        // special in a way.
        writingSystems.setSupported(QFontDatabase::Other);
    }

#if FC_VERSION >= 20297
    for (int j = 1; j < QFontDatabase::WritingSystemsCount; ++j) {
        if (writingSystems.supported(QFontDatabase::WritingSystem(j))
            && requiresOpenType(j) && openType[j]) {
            FcChar8 *cap;
            res = FcPatternGetString (pattern, FC_CAPABILITY, 0, &cap);
            if (res != FcResultMatch || !strstr((const char *)cap, openType[j]))
                writingSystems.setSupported(QFontDatabase::WritingSystem(j),false);
        }
    }
#endif

    FontFile *fontFile = new FontFile;
    fontFile->fileName = QString::fromLocal8Bit((const char *)file_value);
    fontFile->indexValue = indexValue;

    QFont::Style style = (slant_value == FC_SLANT_ITALIC)
                     ? QFont::StyleItalic
                     : ((slant_value == FC_SLANT_OBLIQUE)
                        ? QFont::StyleOblique
                        : QFont::StyleNormal);
    // Note: weight should really be an int but registerFont incorrectly uses an enum
    QFont::Weight weight = QFont::Weight(weightFromFcWeight(weight_value));

    double pixel_size = 0;
    if (!scalable)
        FcPatternGetDouble (pattern, FC_PIXEL_SIZE, 0, &pixel_size);

    bool fixedPitch = spacing_value >= FC_MONO;
    // Note: stretch should really be an int but registerFont incorrectly uses an enum
    QFont::Stretch stretch = QFont::Stretch(stretchFromFcWidth(width_value));
    QString styleName = style_value ? QString::fromUtf8((const char *) style_value) : QString();
    QPlatformFontDatabase::registerFont(familyName,styleName,QLatin1String((const char *)foundry_value),weight,style,stretch,antialias,scalable,pixel_size,fixedPitch,writingSystems,fontFile);
//        qDebug() << familyName << (const char *)foundry_value << weight << style << &writingSystems << scalable << true << pixel_size;

    for (int k = 1; FcPatternGetString(pattern, FC_FAMILY, k, &value) == FcResultMatch; ++k)
        QPlatformFontDatabase::registerAliasToFontFamily(familyName, QString::fromUtf8((const char *)value));

}

void QFontconfigDatabase::populateFontDatabase()
{
    FcInitReinitialize();
    FcFontSet  *fonts;

    {
        FcObjectSet *os = FcObjectSetCreate();
        FcPattern *pattern = FcPatternCreate();
        const char *properties [] = {
            FC_FAMILY, FC_STYLE, FC_WEIGHT, FC_SLANT,
            FC_SPACING, FC_FILE, FC_INDEX,
            FC_LANG, FC_CHARSET, FC_FOUNDRY, FC_SCALABLE, FC_PIXEL_SIZE,
            FC_WIDTH,
#if FC_VERSION >= 20297
            FC_CAPABILITY,
#endif
            (const char *)0
        };
        const char **p = properties;
        while (*p) {
            FcObjectSetAdd(os, *p);
            ++p;
        }
        fonts = FcFontList(0, pattern, os);
        FcObjectSetDestroy(os);
        FcPatternDestroy(pattern);
    }

    for (int i = 0; i < fonts->nfont; i++)
        populateFromPattern(fonts->fonts[i]);

    FcFontSetDestroy (fonts);

    struct FcDefaultFont {
        const char *qtname;
        const char *rawname;
        bool fixed;
    };
    const FcDefaultFont defaults[] = {
        { "Serif", "serif", false },
        { "Sans Serif", "sans-serif", false },
        { "Monospace", "monospace", true },
        { 0, 0, false }
    };
    const FcDefaultFont *f = defaults;
    // aliases only make sense for 'common', not for any of the specials
    QSupportedWritingSystems ws;
    ws.setSupported(QFontDatabase::Latin);

    while (f->qtname) {
        QString familyQtName = QString::fromLatin1(f->qtname);
        registerFont(familyQtName,QString(),QString(),QFont::Normal,QFont::StyleNormal,QFont::Unstretched,true,true,0,f->fixed,ws,0);
        registerFont(familyQtName,QString(),QString(),QFont::Normal,QFont::StyleItalic,QFont::Unstretched,true,true,0,f->fixed,ws,0);
        registerFont(familyQtName,QString(),QString(),QFont::Normal,QFont::StyleOblique,QFont::Unstretched,true,true,0,f->fixed,ws,0);
        ++f;
    }

    //Lighthouse has very lazy population of the font db. We want it to be initialized when
    //QApplication is constructed, so that the population procedure can do something like this to
    //set the default font
//    const FcDefaultFont *s = defaults;
//    QFont font("Sans Serif");
//    font.setPointSize(9);
//    QApplication::setFont(font);
}

QFontEngineMulti *QFontconfigDatabase::fontEngineMulti(QFontEngine *fontEngine, QChar::Script script)
{
    return new QFontEngineMultiFontConfig(fontEngine, script);
}

namespace {
QFontEngine::HintStyle defaultHintStyleFromMatch(QFont::HintingPreference hintingPreference, FcPattern *match, bool useXftConf)
{
    switch (hintingPreference) {
    case QFont::PreferNoHinting:
        return QFontEngine::HintNone;
    case QFont::PreferVerticalHinting:
        return QFontEngine::HintLight;
    case QFont::PreferFullHinting:
        return QFontEngine::HintFull;
    case QFont::PreferDefaultHinting:
        break;
    }

    if (useXftConf) {
        void *hintStyleResource =
                QGuiApplication::platformNativeInterface()->nativeResourceForScreen("hintstyle",
                                                                                    QGuiApplication::primaryScreen());
        int hintStyle = int(reinterpret_cast<qintptr>(hintStyleResource));
        if (hintStyle > 0)
            return QFontEngine::HintStyle(hintStyle - 1);
    }

    int hint_style = 0;
    if (FcPatternGetInteger (match, FC_HINT_STYLE, 0, &hint_style) == FcResultNoMatch)
        hint_style = FC_HINT_FULL;
    switch (hint_style) {
    case FC_HINT_NONE:
        return QFontEngine::HintNone;
    case FC_HINT_SLIGHT:
        return QFontEngine::HintLight;
    case FC_HINT_MEDIUM:
        return QFontEngine::HintMedium;
    case FC_HINT_FULL:
        return QFontEngine::HintFull;
    default:
        Q_UNREACHABLE();
        break;
    }
    return QFontEngine::HintFull;
}

QFontEngine::SubpixelAntialiasingType subpixelTypeFromMatch(FcPattern *match, bool useXftConf)
{
    if (useXftConf) {
        void *subpixelTypeResource =
                QGuiApplication::platformNativeInterface()->nativeResourceForScreen("subpixeltype",
                                                                                    QGuiApplication::primaryScreen());
        int subpixelType = int(reinterpret_cast<qintptr>(subpixelTypeResource));
        if (subpixelType > 0)
            return QFontEngine::SubpixelAntialiasingType(subpixelType - 1);
    }

    int subpixel = FC_RGBA_UNKNOWN;
    FcPatternGetInteger(match, FC_RGBA, 0, &subpixel);

    switch (subpixel) {
    case FC_RGBA_UNKNOWN:
    case FC_RGBA_NONE:
        return QFontEngine::Subpixel_None;
    case FC_RGBA_RGB:
        return QFontEngine::Subpixel_RGB;
    case FC_RGBA_BGR:
        return QFontEngine::Subpixel_BGR;
    case FC_RGBA_VRGB:
        return QFontEngine::Subpixel_VRGB;
    case FC_RGBA_VBGR:
        return QFontEngine::Subpixel_VBGR;
    default:
        Q_UNREACHABLE();
        break;
    }
    return QFontEngine::Subpixel_None;
}
} // namespace

QFontEngine *QFontconfigDatabase::fontEngine(const QFontDef &f, void *usrPtr)
{
    if (!usrPtr)
        return 0;

    FontFile *fontfile = static_cast<FontFile *> (usrPtr);
    QFontEngine::FaceId fid;
    fid.filename = QFile::encodeName(fontfile->fileName);
    fid.index = fontfile->indexValue;

    QFontEngineFT *engine = new QFontEngineFT(f);
    engine->face_id = fid;

    setupFontEngine(engine, f);

    if (!engine->init(fid, engine->antialias, engine->defaultFormat) || engine->invalid()) {
        delete engine;
        engine = 0;
    }

    return engine;
}

QFontEngine *QFontconfigDatabase::fontEngine(const QByteArray &fontData, qreal pixelSize, QFont::HintingPreference hintingPreference)
{
    QFontEngineFT *engine = static_cast<QFontEngineFT*>(QBasicFontDatabase::fontEngine(fontData, pixelSize, hintingPreference));
    if (engine == 0)
        return 0;

    setupFontEngine(engine, engine->fontDef);

    return engine;
}

QStringList QFontconfigDatabase::fallbacksForFamily(const QString &family, QFont::Style style, QFont::StyleHint styleHint, QChar::Script script) const
{
    QStringList fallbackFamilies;
    FcPattern *pattern = FcPatternCreate();
    if (!pattern)
        return fallbackFamilies;

    FcValue value;
    value.type = FcTypeString;
    QByteArray cs = family.toUtf8();
    value.u.s = (const FcChar8 *)cs.data();
    FcPatternAdd(pattern,FC_FAMILY,value,true);

    int slant_value = FC_SLANT_ROMAN;
    if (style == QFont::StyleItalic)
        slant_value = FC_SLANT_ITALIC;
    else if (style == QFont::StyleOblique)
        slant_value = FC_SLANT_OBLIQUE;
    FcPatternAddInteger(pattern, FC_SLANT, slant_value);

    Q_ASSERT(uint(script) < QChar::ScriptCount);
    if (*specialLanguages[script] != '\0') {
        FcLangSet *ls = FcLangSetCreate();
        FcLangSetAdd(ls, (const FcChar8*)specialLanguages[script]);
        FcPatternAddLangSet(pattern, FC_LANG, ls);
        FcLangSetDestroy(ls);
    } else if (!family.isEmpty()) {
        // If script is Common or Han, then it may include languages like CJK,
        // we should attach system default language set to the pattern
        // to obtain correct font fallback list (i.e. if LANG=zh_CN
        // then we normally want to use a Chinese font for CJK text;
        // while a Japanese font should be used for that if LANG=ja)
        FcPattern *dummy = FcPatternCreate();
        FcDefaultSubstitute(dummy);
        FcChar8 *lang = 0;
        FcResult res = FcPatternGetString(dummy, FC_LANG, 0, &lang);
        if (res == FcResultMatch)
            FcPatternAddString(pattern, FC_LANG, lang);
        FcPatternDestroy(dummy);
    }

    const char *stylehint = getFcFamilyForStyleHint(styleHint);
    if (stylehint) {
        value.u.s = (const FcChar8 *)stylehint;
        FcPatternAddWeak(pattern, FC_FAMILY, value, FcTrue);
    }

    FcConfigSubstitute(0, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);

    FcResult result = FcResultMatch;
    FcFontSet *fontSet = FcFontSort(0,pattern,FcFalse,0,&result);
    FcPatternDestroy(pattern);

    if (fontSet) {
        for (int i = 0; i < fontSet->nfont; i++) {
            FcChar8 *value = 0;
            if (FcPatternGetString(fontSet->fonts[i], FC_FAMILY, 0, &value) != FcResultMatch)
                continue;
            //         capitalize(value);
            QString familyName = QString::fromUtf8((const char *)value);
            if (!fallbackFamilies.contains(familyName,Qt::CaseInsensitive) &&
                familyName.compare(family, Qt::CaseInsensitive)) {
                fallbackFamilies << familyName;
            }
        }
        FcFontSetDestroy(fontSet);
    }
//    qDebug() << "fallbackFamilies for:" << family << style << styleHint << script << fallbackFamilies;

    return fallbackFamilies;
}

static FcPattern *queryFont(const FcChar8 *file, const QByteArray &data, int id, FcBlanks *blanks, int *count)
{
#if FC_VERSION < 20402
    Q_UNUSED(data)
    return FcFreeTypeQuery(file, id, blanks, count);
#else
    if (data.isEmpty())
        return FcFreeTypeQuery(file, id, blanks, count);

    FT_Library lib = qt_getFreetype();

    FcPattern *pattern = 0;

    FT_Face face;
    if (!FT_New_Memory_Face(lib, (const FT_Byte *)data.constData(), data.size(), id, &face)) {
        *count = face->num_faces;

        pattern = FcFreeTypeQueryFace(face, file, id, blanks);

        FT_Done_Face(face);
    }

    return pattern;
#endif
}

QStringList QFontconfigDatabase::addApplicationFont(const QByteArray &fontData, const QString &fileName)
{
    QStringList families;

    FcFontSet *set = FcConfigGetFonts(0, FcSetApplication);
    if (!set) {
        FcConfigAppFontAddFile(0, (const FcChar8 *)":/non-existent");
        set = FcConfigGetFonts(0, FcSetApplication); // try again
        if (!set)
            return families;
    }

    int id = 0;
    FcBlanks *blanks = FcConfigGetBlanks(0);
    int count = 0;

    FcPattern *pattern;
    do {
        pattern = queryFont((const FcChar8 *)QFile::encodeName(fileName).constData(),
                            fontData, id, blanks, &count);
        if (!pattern)
            return families;

        FcChar8 *fam = 0;
        if (FcPatternGetString(pattern, FC_FAMILY, 0, &fam) == FcResultMatch) {
            QString family = QString::fromUtf8(reinterpret_cast<const char *>(fam));
            if (family == QLatin1String("Open Sans")) {
                FcChar8 *styl = 0;
                if (FcPatternGetString(pattern, FC_STYLE, 0, &styl) == FcResultMatch) {
                    QString style = QString::fromUtf8(reinterpret_cast<const char *>(styl));
                    if (style == QLatin1String("Semibold")) {
                        family.append(QChar(QChar::Space)).append(style);
                    }
                }
            }
            families << family;
        }
        populateFromPattern(pattern);

        FcFontSetAdd(set, pattern);

        ++id;
    } while (id < count);

    return families;
}

QString QFontconfigDatabase::resolveFontFamilyAlias(const QString &family) const
{
    QString resolved = QBasicFontDatabase::resolveFontFamilyAlias(family);
    if (!resolved.isEmpty() && resolved != family)
        return resolved;
    FcPattern *pattern = FcPatternCreate();
    if (!pattern)
        return family;

    if (!family.isEmpty()) {
        QByteArray cs = family.toUtf8();
        FcPatternAddString(pattern, FC_FAMILY, (const FcChar8 *) cs.constData());
    }
    FcConfigSubstitute(0, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);

    FcChar8 *familyAfterSubstitution = 0;
    FcPatternGetString(pattern, FC_FAMILY, 0, &familyAfterSubstitution);
    resolved = QString::fromUtf8((const char *) familyAfterSubstitution);
    FcPatternDestroy(pattern);

    return resolved;
}

QFont QFontconfigDatabase::defaultFont() const
{
    // Hack to get system default language until FcGetDefaultLangs()
    // is exported (https://bugs.freedesktop.org/show_bug.cgi?id=32853)
    // or https://bugs.freedesktop.org/show_bug.cgi?id=35482 is fixed
    FcPattern *dummy = FcPatternCreate();
    FcDefaultSubstitute(dummy);
    FcChar8 *lang = 0;
    FcResult res = FcPatternGetString(dummy, FC_LANG, 0, &lang);

    FcPattern *pattern = FcPatternCreate();
    if (res == FcResultMatch) {
        // Make defaultFont pattern matching locale language aware, because
        // certain FC_LANG based custom rules may happen in FcConfigSubstitute()
        FcPatternAddString(pattern, FC_LANG, lang);
    }
    FcConfigSubstitute(0, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);

    FcChar8 *familyAfterSubstitution = 0;
    FcPatternGetString(pattern, FC_FAMILY, 0, &familyAfterSubstitution);
    QString resolved = QString::fromUtf8((const char *) familyAfterSubstitution);
    FcPatternDestroy(pattern);
    FcPatternDestroy(dummy);

    return QFont(resolved);
}

void QFontconfigDatabase::setupFontEngine(QFontEngineFT *engine, const QFontDef &fontDef) const
{
    bool antialias = !(fontDef.styleStrategy & QFont::NoAntialias);
    bool forcedAntialiasSetting = !antialias;

    const QPlatformServices *services = QGuiApplicationPrivate::platformIntegration()->services();
    bool useXftConf = (services && (services->desktopEnvironment() == "GNOME" || services->desktopEnvironment() == "UNITY"));
    if (useXftConf) {
        void *antialiasResource =
                QGuiApplication::platformNativeInterface()->nativeResourceForScreen("antialiasingEnabled",
                                                                                    QGuiApplication::primaryScreen());
        int antialiasingEnabled = int(reinterpret_cast<qintptr>(antialiasResource));
        if (antialiasingEnabled > 0) {
            antialias = antialiasingEnabled - 1;
            forcedAntialiasSetting = true;
        }
    }

    QFontEngine::GlyphFormat format;
    // try and get the pattern
    FcPattern *pattern = FcPatternCreate();

    FcValue value;
    value.type = FcTypeString;
    QByteArray cs = fontDef.family.toUtf8();
    value.u.s = (const FcChar8 *)cs.data();
    FcPatternAdd(pattern,FC_FAMILY,value,true);

    QFontEngine::FaceId fid = engine->faceId();

    if (!fid.filename.isEmpty()) {
        value.u.s = (const FcChar8 *)fid.filename.data();
        FcPatternAdd(pattern,FC_FILE,value,true);

        value.type = FcTypeInteger;
        value.u.i = fid.index;
        FcPatternAdd(pattern,FC_INDEX,value,true);
    }

    if (fontDef.pixelSize > 0.1)
        FcPatternAddDouble(pattern, FC_PIXEL_SIZE, fontDef.pixelSize);

    FcResult result;

    FcConfigSubstitute(0, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);

    FcPattern *match = FcFontMatch(0, pattern, &result);
    if (match) {
        engine->setDefaultHintStyle(defaultHintStyleFromMatch((QFont::HintingPreference)fontDef.hintingPreference, match, useXftConf));

        FcBool fc_autohint;
        if (FcPatternGetBool(match, FC_AUTOHINT,0, &fc_autohint) == FcResultMatch)
            engine->forceAutoHint = fc_autohint;

#if defined(FT_LCD_FILTER_H)
        int lcdFilter;
        if (FcPatternGetInteger(match, FC_LCD_FILTER, 0, &lcdFilter) == FcResultMatch)
            engine->lcdFilterType = lcdFilter;
#endif

        if (!forcedAntialiasSetting) {
            FcBool fc_antialias;
            if (FcPatternGetBool(match, FC_ANTIALIAS,0, &fc_antialias) == FcResultMatch)
                antialias = fc_antialias;
        }

        if (antialias) {
            QFontEngine::SubpixelAntialiasingType subpixelType = QFontEngine::Subpixel_None;
            if (!(fontDef.styleStrategy & QFont::NoSubpixelAntialias))
                subpixelType = subpixelTypeFromMatch(match, useXftConf);
            engine->subpixelType = subpixelType;

            format = (subpixelType == QFontEngine::Subpixel_None)
                    ? QFontEngine::Format_A8
                    : QFontEngine::Format_A32;
        } else
            format = QFontEngine::Format_Mono;

        FcPatternDestroy(match);
    } else
        format = antialias ? QFontEngine::Format_A8 : QFontEngine::Format_Mono;

    FcPatternDestroy(pattern);

    engine->antialias = antialias;
    engine->defaultFormat = format;
    engine->glyphFormat = format;
}

QT_END_NAMESPACE
