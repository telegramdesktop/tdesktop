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

#include "qglobal.h"

#if defined(Q_OS_MACX)
#import <Cocoa/Cocoa.h>
#import <IOKit/graphics/IOGraphicsLib.h>
#elif defined(Q_OS_IOS)
#import <UIKit/UIFont.h>
#endif

#include "qcoretextfontdatabase_p.h"
#include "qfontengine_coretext_p.h"
#include <QtCore/QSettings>
#include <QtGui/QGuiApplication>

QT_BEGIN_NAMESPACE

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
    "zh-Hans", // SimplifiedChinese
    "zh-Hant", // TraditionalChinese
    "ja",  // Japanese
    "ko",  // Korean
    "vi",  // Vietnamese
    0, // Symbol
    "sga", // Ogham
    "non", // Runic
    "man" // N'Ko
};
enum { LanguageCount = sizeof(languageForWritingSystem) / sizeof(const char *) };

#ifdef Q_OS_OSX
static NSInteger languageMapSort(id obj1, id obj2, void *context)
{
    NSArray *map1 = (NSArray *) obj1;
    NSArray *map2 = (NSArray *) obj2;
    NSArray *languages = (NSArray *) context;

    NSString *lang1 = [map1 objectAtIndex: 0];
    NSString *lang2 = [map2 objectAtIndex: 0];

    return [languages indexOfObject: lang1] - [languages indexOfObject: lang2];
}
#endif

QCoreTextFontDatabase::QCoreTextFontDatabase()
{
#ifdef Q_OS_MACX
    QSettings appleSettings(QLatin1String("apple.com"));
    QVariant appleValue = appleSettings.value(QLatin1String("AppleAntiAliasingThreshold"));
    if (appleValue.isValid())
        QCoreTextFontEngine::antialiasingThreshold = appleValue.toInt();

    /*
        font_smoothing = 0 means no smoothing, while 1-3 means subpixel
        antialiasing with different hinting styles (but we don't care about the
        exact value, only if subpixel rendering is available or not)
    */
    int font_smoothing = 0;
    appleValue = appleSettings.value(QLatin1String("AppleFontSmoothing"));
    if (appleValue.isValid()) {
        font_smoothing = appleValue.toInt();
    } else {
        // non-Apple displays do not provide enough information about subpixel rendering so
        // draw text with cocoa and compare pixel colors to see if subpixel rendering is enabled
        int w = 10;
        int h = 10;
        NSRect rect = NSMakeRect(0.0, 0.0, w, h);
        NSImage *fontImage = [[NSImage alloc] initWithSize:NSMakeSize(w, h)];

        [fontImage lockFocus];

        [[NSColor whiteColor] setFill];
        NSRectFill(rect);

        NSString *str = @"X\\";
        NSFont *font = [NSFont fontWithName:@"Helvetica" size:10.0];
        NSMutableDictionary *attrs = [NSMutableDictionary dictionary];
        [attrs setObject:font forKey:NSFontAttributeName];
        [attrs setObject:[NSColor blackColor] forKey:NSForegroundColorAttributeName];

        [str drawInRect:rect withAttributes:attrs];

        NSBitmapImageRep *nsBitmapImage = [[NSBitmapImageRep alloc] initWithFocusedViewRect:rect];

        [fontImage unlockFocus];

        float red, green, blue;
        for (int x = 0; x < w; x++) {
            for (int y = 0; y < h; y++) {
                NSColor *pixelColor = [nsBitmapImage colorAtX:x y:y];
                red = [pixelColor redComponent];
                green = [pixelColor greenComponent];
                blue = [pixelColor blueComponent];
                if (red != green || red != blue)
                    font_smoothing = 1;
            }
        }

        [nsBitmapImage release];
        [fontImage release];
    }
    QCoreTextFontEngine::defaultGlyphFormat = (font_smoothing > 0
                                               ? QFontEngine::Format_A32
                                               : QFontEngine::Format_A8);
#else
    QCoreTextFontEngine::defaultGlyphFormat = QFontEngine::Format_A8;
#endif
}

QCoreTextFontDatabase::~QCoreTextFontDatabase()
{
}

static CFArrayRef availableFamilyNames()
{
#if defined(Q_OS_OSX)
    return CTFontManagerCopyAvailableFontFamilyNames();
#elif defined(Q_OS_IOS)
    return (CFArrayRef) [[UIFont familyNames] retain];
#endif
}

void QCoreTextFontDatabase::populateFontDatabase()
{
    // The caller (QFontDB) expects the db to be populate only with system fonts, so we need
    // to make sure that any previously registered app fonts become invisible.
    removeApplicationFonts();

    QCFType<CFArrayRef> familyNames = availableFamilyNames();
    const int numberOfFamilies = CFArrayGetCount(familyNames);
    for (int i = 0; i < numberOfFamilies; ++i) {
        CFStringRef familyNameRef = (CFStringRef) CFArrayGetValueAtIndex(familyNames, i);
        QString familyName = QCFString::toQString(familyNameRef);

        // Don't populate internal fonts
        if (familyName.startsWith(QLatin1Char('.')) || familyName == QStringLiteral("LastResort"))
            continue;

        QPlatformFontDatabase::registerFontFamily(familyName);

#if defined(Q_OS_OSX)
        QString localizedFamilyName = QString::fromNSString([[NSFontManager sharedFontManager] localizedNameForFamily:(NSString*)familyNameRef face:nil]);
        if (familyName != localizedFamilyName)
            QPlatformFontDatabase::registerAliasToFontFamily(familyName, localizedFamilyName);
#endif
    }

    // Force creating the theme fonts to get the descriptors in m_systemFontDescriptors
    if (m_themeFonts.isEmpty())
        (void)themeFonts();

    Q_FOREACH (CTFontDescriptorRef fontDesc, m_systemFontDescriptors)
        populateFromDescriptor(fontDesc);
}

void QCoreTextFontDatabase::populateFamily(const QString &familyName)
{
    CFMutableDictionaryRef attributes = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionaryAddValue(attributes, kCTFontFamilyNameAttribute, QCFString(familyName));
    CTFontDescriptorRef nameOnlyDescriptor = CTFontDescriptorCreateWithAttributes(attributes);

    // A single family might match several different fonts with different styles eg.
    QCFType<CFArrayRef> matchingFonts = (CFArrayRef) CTFontDescriptorCreateMatchingFontDescriptors(nameOnlyDescriptor, 0);
    if (!matchingFonts) {
        qWarning() << "QCoreTextFontDatabase: Found no matching fonts for family" << familyName;
        return;
    }

    const int numFonts = CFArrayGetCount(matchingFonts);
    for (int i = 0; i < numFonts; ++i)
        populateFromDescriptor(CTFontDescriptorRef(CFArrayGetValueAtIndex(matchingFonts, i)));
}

struct FontDescription {
    QCFString familyName;
    QCFString styleName;
    QString foundryName;
    QFont::Weight weight;
    QFont::Style style;
    QFont::Stretch stretch;
    int pixelSize;
    bool fixedPitch;
    QSupportedWritingSystems writingSystems;
};

static void getFontDescription(CTFontDescriptorRef font, FontDescription *fd)
{
    QCFType<CFDictionaryRef> styles = (CFDictionaryRef) CTFontDescriptorCopyAttribute(font, kCTFontTraitsAttribute);

    fd->foundryName = QStringLiteral("CoreText");
    fd->familyName = (CFStringRef) CTFontDescriptorCopyAttribute(font, kCTFontFamilyNameAttribute);
	QCFString _displayName = (CFStringRef) CTFontDescriptorCopyAttribute(font, kCTFontDisplayNameAttribute);
	if (_displayName == QStringLiteral("Open Sans Semibold")) {
		fd->familyName = _displayName;
	}
    fd->styleName = (CFStringRef)CTFontDescriptorCopyAttribute(font, kCTFontStyleNameAttribute);
    fd->weight = QFont::Normal;
    fd->style = QFont::StyleNormal;
    fd->stretch = QFont::Unstretched;
    fd->fixedPitch = false;

    if (styles) {
        if (CFNumberRef weightValue = (CFNumberRef) CFDictionaryGetValue(styles, kCTFontWeightTrait)) {
            double normalizedWeight;
            if (CFNumberGetValue(weightValue, kCFNumberDoubleType, &normalizedWeight)) {
                if (normalizedWeight >= 0.62)
                    fd->weight = QFont::Black;
                else if (normalizedWeight >= 0.4)
                    fd->weight = QFont::Bold;
                else if (normalizedWeight >= 0.3)
                    fd->weight = QFont::DemiBold;
                else if (normalizedWeight == 0.0)
                    fd->weight = QFont::Normal;
                else if (normalizedWeight <= -0.4)
                    fd->weight = QFont::Light;
            }
        }
        if (CFNumberRef italic = (CFNumberRef) CFDictionaryGetValue(styles, kCTFontSlantTrait)) {
            double d;
            if (CFNumberGetValue(italic, kCFNumberDoubleType, &d)) {
                if (d > 0.0)
                    fd->style = QFont::StyleItalic;
            }
        }
        if (CFNumberRef symbolic = (CFNumberRef) CFDictionaryGetValue(styles, kCTFontSymbolicTrait)) {
            int d;
            if (CFNumberGetValue(symbolic, kCFNumberSInt32Type, &d)) {
                if (d & kCTFontMonoSpaceTrait)
                    fd->fixedPitch = true;
                if (d & kCTFontExpandedTrait)
                    fd->stretch = QFont::Expanded;
                else if (d & kCTFontCondensedTrait)
                    fd->stretch = QFont::Condensed;
            }
        }
    }

    if (QCFType<CFNumberRef> size = (CFNumberRef) CTFontDescriptorCopyAttribute(font, kCTFontSizeAttribute)) {
        if (CFNumberIsFloatType(size)) {
            double d;
            CFNumberGetValue(size, kCFNumberDoubleType, &d);
            fd->pixelSize = d;
        } else {
            CFNumberGetValue(size, kCFNumberIntType, &fd->pixelSize);
        }
    }

    if (QCFType<CFArrayRef> languages = (CFArrayRef) CTFontDescriptorCopyAttribute(font, kCTFontLanguagesAttribute)) {
        CFIndex length = CFArrayGetCount(languages);
        for (int i = 1; i < LanguageCount; ++i) {
            if (!languageForWritingSystem[i])
                continue;
            QCFString lang = CFStringCreateWithCString(NULL, languageForWritingSystem[i], kCFStringEncodingASCII);
            if (CFArrayContainsValue(languages, CFRangeMake(0, length), lang))
                fd->writingSystems.setSupported(QFontDatabase::WritingSystem(i));
        }
    }
}

void QCoreTextFontDatabase::populateFromDescriptor(CTFontDescriptorRef font)
{
    FontDescription fd;
    getFontDescription(font, &fd);
    populateFromFontDescription(font, fd);
}

void QCoreTextFontDatabase::populateFromFontDescription(CTFontDescriptorRef font, const FontDescription &fd)
{
    CFRetain(font);
    QPlatformFontDatabase::registerFont(fd.familyName, fd.styleName, fd.foundryName, fd.weight, fd.style, fd.stretch,
            true /* antialiased */, true /* scalable */,
            fd.pixelSize, fd.fixedPitch, fd.writingSystems, (void *) font);
}

void QCoreTextFontDatabase::releaseHandle(void *handle)
{
    CFRelease(CTFontDescriptorRef(handle));
}

QFontEngine *QCoreTextFontDatabase::fontEngine(const QFontDef &f, void *usrPtr)
{
    qreal scaledPointSize = f.pixelSize;

    // When 96 DPI is forced, the Mac plugin will use DPI 72 for some
    // fonts (hardcoded in qcocoaintegration.mm) and 96 for others. This
    // discrepancy makes it impossible to find the correct point size
    // here without having the DPI used for the font. Until a proper
    // solution (requiring API change) can be made, we simply fall back
    // to passing in the point size to retain old behavior.
    if (QGuiApplication::testAttribute(Qt::AA_Use96Dpi))
        scaledPointSize = f.pointSize;

    CTFontDescriptorRef descriptor = (CTFontDescriptorRef) usrPtr;
    CTFontRef font = CTFontCreateWithFontDescriptor(descriptor, scaledPointSize, NULL);
    if (font) {
        QFontEngine *engine = new QCoreTextFontEngine(font, f);
        engine->fontDef = f;
        CFRelease(font);
        return engine;
    }

    return NULL;
}

static void releaseFontData(void* info, const void* data, size_t size)
{
    Q_UNUSED(data);
    Q_UNUSED(size);
    delete (QByteArray*)info;
}

QFontEngine *QCoreTextFontDatabase::fontEngine(const QByteArray &fontData, qreal pixelSize, QFont::HintingPreference hintingPreference)
{
    Q_UNUSED(hintingPreference);

    QByteArray* fontDataCopy = new QByteArray(fontData);
    QCFType<CGDataProviderRef> dataProvider = CGDataProviderCreateWithData(fontDataCopy,
            fontDataCopy->constData(), fontDataCopy->size(), releaseFontData);

    CGFontRef cgFont = CGFontCreateWithDataProvider(dataProvider);

    QFontEngine *fontEngine = NULL;
    if (cgFont == NULL) {
        qWarning("QRawFont::platformLoadFromData: CGFontCreateWithDataProvider failed");
    } else {
        QFontDef def;
        def.pixelSize = pixelSize;
        def.pointSize = pixelSize * 72.0 / qt_defaultDpi();
        fontEngine = new QCoreTextFontEngine(cgFont, def);
        CFRelease(cgFont);
    }

    return fontEngine;
}

QFont::StyleHint styleHintFromNSString(NSString *style)
{
    if ([style isEqual: @"sans-serif"])
        return QFont::SansSerif;
    else if ([style isEqual: @"monospace"])
        return QFont::Monospace;
    else if ([style isEqual: @"cursive"])
        return QFont::Cursive;
    else if ([style isEqual: @"serif"])
        return QFont::Serif;
    else if ([style isEqual: @"fantasy"])
        return QFont::Fantasy;
    else // if ([style isEqual: @"default"])
        return QFont::AnyStyle;
}

#ifdef Q_OS_OSX
static QString familyNameFromPostScriptName(NSString *psName)
{
    QCFType<CTFontDescriptorRef> fontDescriptor = (CTFontDescriptorRef) CTFontDescriptorCreateWithNameAndSize((CFStringRef)psName, 12.0);
    QCFString familyName = (CFStringRef) CTFontDescriptorCopyAttribute(fontDescriptor, kCTFontFamilyNameAttribute);
    QString name = QCFString::toQString(familyName);
    if (name.isEmpty())
        qWarning() << "QCoreTextFontDatabase: Failed to resolve family name for PostScript name " << QCFString::toQString((CFStringRef)psName);

    return name;
}
#endif

QStringList QCoreTextFontDatabase::fallbacksForFamily(const QString &family, QFont::Style style, QFont::StyleHint styleHint, QChar::Script script) const
{
    Q_UNUSED(style);
    Q_UNUSED(script);

    static QHash<QString, QStringList> fallbackLists;

    if (!family.isEmpty()) {
#if QT_MAC_PLATFORM_SDK_EQUAL_OR_ABOVE(__MAC_10_8, __IPHONE_6_0)
      // CTFontCopyDefaultCascadeListForLanguages is available in the SDK
  #if QT_MAC_DEPLOYMENT_TARGET_BELOW(__MAC_10_8, __IPHONE_6_0)
        // But we have to feature check at runtime
        if (&CTFontCopyDefaultCascadeListForLanguages)
  #endif
        {
            if (fallbackLists.contains(family))
                return fallbackLists.value(family);

            QCFType<CFMutableDictionaryRef> attributes = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
            CFDictionaryAddValue(attributes, kCTFontFamilyNameAttribute, QCFString(family));
            if (QCFType<CTFontDescriptorRef> fontDescriptor = CTFontDescriptorCreateWithAttributes(attributes)) {
                if (QCFType<CTFontRef> font = CTFontCreateWithFontDescriptor(fontDescriptor, 12.0, 0)) {
                    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
                    NSArray *languages = [defaults stringArrayForKey: @"AppleLanguages"];

                    QCFType<CFArrayRef> cascadeList = (CFArrayRef) CTFontCopyDefaultCascadeListForLanguages(font, (CFArrayRef) languages);
                    if (cascadeList) {
                        QStringList fallbackList;
                        const int numCascades = CFArrayGetCount(cascadeList);
                        for (int i = 0; i < numCascades; ++i) {
                            CTFontDescriptorRef fontFallback = (CTFontDescriptorRef) CFArrayGetValueAtIndex(cascadeList, i);
                            QCFString fallbackFamilyName = (CFStringRef) CTFontDescriptorCopyAttribute(fontFallback, kCTFontFamilyNameAttribute);
                            fallbackList.append(QCFString::toQString(fallbackFamilyName));
                        }
                        fallbackLists[family] = fallbackList;
                    }
                }

                if (fallbackLists.contains(family))
                    return fallbackLists.value(family);
            }
        }
#endif
    }

    // We were not able to find a fallback for the specific family,
    // so we fall back to the stylehint.

    static const QString styleLookupKey = QString::fromLatin1(".QFontStyleHint_%1");

    static bool didPopulateStyleFallbacks = false;
    if (!didPopulateStyleFallbacks) {
#if defined(Q_OS_MACX)
        NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
        NSArray *languages = [defaults stringArrayForKey: @"AppleLanguages"];

        NSDictionary *fallbackDict = [NSDictionary dictionaryWithContentsOfFile: @"/System/Library/Frameworks/ApplicationServices.framework/Frameworks/CoreText.framework/Resources/DefaultFontFallbacks.plist"];

        for (NSString *style in [fallbackDict allKeys]) {
            NSArray *list = [fallbackDict valueForKey: style];
            QFont::StyleHint fallbackStyleHint = styleHintFromNSString(style);
            QStringList fallbackList;
            for (id item in list) {
                // sort the array based on system language preferences
                if ([item isKindOfClass: [NSArray class]]) {
                    NSArray *langs = [(NSArray *) item sortedArrayUsingFunction: languageMapSort
                                                                        context: languages];
                    for (NSArray *map in langs)
                        fallbackList.append(familyNameFromPostScriptName([map objectAtIndex: 1]));
                }
                else if ([item isKindOfClass: [NSString class]])
                    fallbackList.append(familyNameFromPostScriptName(item));
            }

            if (QCoreTextFontEngine::supportsColorGlyphs())
                fallbackList.append(QLatin1String("Apple Color Emoji"));

            fallbackLists[styleLookupKey.arg(fallbackStyleHint)] = fallbackList;
        }
#else
        QStringList staticFallbackList;
        staticFallbackList << QString::fromLatin1("Helvetica,Apple Color Emoji,Geeza Pro,Arial Hebrew,Thonburi,Kailasa"
            "Hiragino Kaku Gothic ProN,.Heiti J,Apple SD Gothic Neo,.Heiti K,Heiti SC,Heiti TC"
            "Bangla Sangam MN,Devanagari Sangam MN,Gujarati Sangam MN,Gurmukhi MN,Kannada Sangam MN"
            "Malayalam Sangam MN,Oriya Sangam MN,Sinhala Sangam MN,Tamil Sangam MN,Telugu Sangam MN"
            "Euphemia UCAS,.PhoneFallback").split(QLatin1String(","));

        for (int i = QFont::Helvetica; i <= QFont::Fantasy; ++i)
            fallbackLists[styleLookupKey.arg(i)] = staticFallbackList;
#endif

        didPopulateStyleFallbacks = true;
    }

    Q_ASSERT(!fallbackLists.isEmpty());
    return fallbackLists[styleLookupKey.arg(styleHint)];
}

#if HAVE_CORETEXT
static CFArrayRef createDescriptorArrayForFont(CTFontRef font)
{
    CFMutableArrayRef array = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
    CFArrayAppendValue(array, QCFType<CTFontDescriptorRef>(CTFontCopyFontDescriptor(font)));
    return array;
}
#endif

QStringList QCoreTextFontDatabase::addApplicationFont(const QByteArray &fontData, const QString &fileName)
{
    QCFType<CFArrayRef> fonts;
    QStringList families;

#if HAVE_CORETEXT
    if (&CTFontManagerRegisterGraphicsFont) {
        CFErrorRef error = 0;
        if (!fontData.isEmpty()) {
            QByteArray* fontDataCopy = new QByteArray(fontData);
            QCFType<CGDataProviderRef> dataProvider = CGDataProviderCreateWithData(fontDataCopy,
                    fontDataCopy->constData(), fontDataCopy->size(), releaseFontData);
            QCFType<CGFontRef> cgFont = CGFontCreateWithDataProvider(dataProvider);
            if (cgFont) {
                if (CTFontManagerRegisterGraphicsFont(cgFont, &error)) {
                    QCFType<CTFontRef> font = CTFontCreateWithGraphicsFont(cgFont, 0.0, NULL, NULL);
                    fonts = createDescriptorArrayForFont(font);
                    m_applicationFonts.append(QVariant::fromValue(QCFType<CGFontRef>::constructFromGet(cgFont)));
                }
            }
        } else {
            QCFType<CFURLRef> fontURL = CFURLCreateWithFileSystemPath(NULL, QCFString(fileName), kCFURLPOSIXPathStyle, false);
            if (CTFontManagerRegisterFontsForURL(fontURL, kCTFontManagerScopeProcess, &error)) {
#if QT_MAC_PLATFORM_SDK_EQUAL_OR_ABOVE(__MAC_10_6, __IPHONE_7_0)
                if (&CTFontManagerCreateFontDescriptorsFromURL)
                    fonts = CTFontManagerCreateFontDescriptorsFromURL(fontURL);
                else
#endif
                {
                    // We're limited to a single font per file, unless we dive into the font tables
                    QCFType<CFMutableDictionaryRef> attributes = CFDictionaryCreateMutable(kCFAllocatorDefault, 1,
                        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
                    CFDictionaryAddValue(attributes, kCTFontURLAttribute, fontURL);
                    QCFType<CTFontDescriptorRef> descriptor = CTFontDescriptorCreateWithAttributes(attributes);
                    QCFType<CTFontRef> font = CTFontCreateWithFontDescriptor(descriptor, 0.0, NULL);
                    fonts = createDescriptorArrayForFont(font);
                }

                m_applicationFonts.append(QVariant::fromValue(QCFType<CFURLRef>::constructFromGet(fontURL)));
            }
        }

        if (error) {
            NSLog(@"Unable to register font: %@", error);
            CFRelease(error);
        }
    }
#endif
#if HAVE_CORETEXT && HAVE_ATS
    else
#endif
#if HAVE_ATS
    {
        ATSFontContainerRef fontContainer;
        OSStatus e;

        if (!fontData.isEmpty()) {
            e = ATSFontActivateFromMemory((void *) fontData.constData(), fontData.size(),
                                          kATSFontContextLocal, kATSFontFormatUnspecified, NULL,
                                          kATSOptionFlagsDefault, &fontContainer);
        } else {
            FSRef ref;
            OSErr qt_mac_create_fsref(const QString &file, FSRef *fsref);
            if (qt_mac_create_fsref(fileName, &ref) != noErr)
                return QStringList();
            e = ATSFontActivateFromFileReference(&ref, kATSFontContextLocal, kATSFontFormatUnspecified, 0,
                                                 kATSOptionFlagsDefault, &fontContainer);
        }

        if (e == noErr) {
            ItemCount fontCount = 0;
            e = ATSFontFindFromContainer(fontContainer, kATSOptionFlagsDefault, 0, 0, &fontCount);
            if (e != noErr)
                return QStringList();

            QVarLengthArray<ATSFontRef> containedFonts(fontCount);
            e = ATSFontFindFromContainer(fontContainer, kATSOptionFlagsDefault, fontCount, containedFonts.data(), &fontCount);
            if (e != noErr)
                return QStringList();

            CFMutableArrayRef fontsArray = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
            for (int i = 0; i < containedFonts.size(); ++i) {
                QCFType<CTFontRef> font = CTFontCreateWithPlatformFont(containedFonts[i], 12.0, NULL, NULL);
                CFArrayAppendValue(fontsArray, QCFType<CTFontDescriptorRef>(CTFontCopyFontDescriptor(font)));
            }

            fonts = fontsArray;

            m_applicationFonts.append(QVariant::fromValue(fontContainer));
        }
    }
#endif

    if (fonts) {
        const int numFonts = CFArrayGetCount(fonts);
        for (int i = 0; i < numFonts; ++i) {
            CTFontDescriptorRef fontDescriptor = CTFontDescriptorRef(CFArrayGetValueAtIndex(fonts, i));
            populateFromDescriptor(fontDescriptor);
            QCFType<CFStringRef> familyName = CFStringRef(CTFontDescriptorCopyAttribute(fontDescriptor, kCTFontFamilyNameAttribute));
            families.append(QCFString(familyName));
        }
    }

    return families;
}

bool QCoreTextFontDatabase::isPrivateFontFamily(const QString &family) const
{
    if (family.startsWith(QLatin1Char('.')))
        return true;

    return QPlatformFontDatabase::isPrivateFontFamily(family);
}

static CTFontUIFontType fontTypeFromTheme(QPlatformTheme::Font f)
{
    switch (f) {
    case QPlatformTheme::SystemFont:
        return kCTFontSystemFontType;

    case QPlatformTheme::MenuFont:
    case QPlatformTheme::MenuBarFont:
    case QPlatformTheme::MenuItemFont:
        return kCTFontMenuItemFontType;

    case QPlatformTheme::MessageBoxFont:
        return kCTFontEmphasizedSystemFontType;

    case QPlatformTheme::LabelFont:
        return kCTFontSystemFontType;

    case QPlatformTheme::TipLabelFont:
        return kCTFontToolTipFontType;

    case QPlatformTheme::StatusBarFont:
        return kCTFontSystemFontType;

    case QPlatformTheme::TitleBarFont:
        return kCTFontWindowTitleFontType;

    case QPlatformTheme::MdiSubWindowTitleFont:
    case QPlatformTheme::DockWidgetTitleFont:
        return kCTFontSystemFontType;

    case QPlatformTheme::PushButtonFont:
        return kCTFontPushButtonFontType;

    case QPlatformTheme::CheckBoxFont:
    case QPlatformTheme::RadioButtonFont:
        return kCTFontSystemFontType;

    case QPlatformTheme::ToolButtonFont:
        return kCTFontSmallToolbarFontType;

    case QPlatformTheme::ItemViewFont:
        return kCTFontSystemFontType;

    case QPlatformTheme::ListViewFont:
        return kCTFontViewsFontType;

    case QPlatformTheme::HeaderViewFont:
        return kCTFontSmallSystemFontType;

    case QPlatformTheme::ListBoxFont:
        return kCTFontViewsFontType;

    case QPlatformTheme::ComboMenuItemFont:
        return kCTFontSystemFontType;

    case QPlatformTheme::ComboLineEditFont:
        return kCTFontViewsFontType;

    case QPlatformTheme::SmallFont:
        return kCTFontSmallSystemFontType;

    case QPlatformTheme::MiniFont:
        return kCTFontMiniSystemFontType;

    case QPlatformTheme::FixedFont:
        return kCTFontUserFixedPitchFontType;

    default:
        return kCTFontSystemFontType;
    }
}

const QHash<QPlatformTheme::Font, QFont *> &QCoreTextFontDatabase::themeFonts() const
{
    if (m_themeFonts.isEmpty()) {
        for (long f = QPlatformTheme::SystemFont; f < QPlatformTheme::NFonts; f++) {
            QPlatformTheme::Font ft = static_cast<QPlatformTheme::Font>(f);
            m_themeFonts.insert(ft, themeFont(ft));
        }
    }

    return m_themeFonts;
}

QFont *QCoreTextFontDatabase::themeFont(QPlatformTheme::Font f) const
{
    CTFontUIFontType fontType = fontTypeFromTheme(f);

    QCFType<CTFontRef> ctFont = CTFontCreateUIFontForLanguage(fontType, 0.0, NULL);
    CTFontDescriptorRef fontDesc = CTFontCopyFontDescriptor(ctFont);

    FontDescription fd;
    getFontDescription(fontDesc, &fd);
    m_systemFontDescriptors.insert(fontDesc);

    QFont *font = new QFont(fd.familyName, fd.pixelSize, fd.weight, fd.style == QFont::StyleItalic);
    return font;
}

QFont QCoreTextFontDatabase::defaultFont() const
{
    if (defaultFontName.isEmpty()) {
        QCFType<CTFontRef> font = CTFontCreateUIFontForLanguage(kCTFontSystemFontType, 12.0, NULL);
        defaultFontName = (QString) QCFString(CTFontCopyFullName(font));
    }

    return QFont(defaultFontName);
}

QList<int> QCoreTextFontDatabase::standardSizes() const
{
    QList<int> ret;
    static const unsigned short standard[] =
        { 9, 10, 11, 12, 13, 14, 18, 24, 36, 48, 64, 72, 96, 144, 288, 0 };
    ret.reserve(int(sizeof(standard) / sizeof(standard[0])));
    const unsigned short *sizes = standard;
    while (*sizes) ret << *sizes++;
    return ret;
}

void QCoreTextFontDatabase::removeApplicationFonts()
{
    foreach (const QVariant &font, m_applicationFonts) {
#if HAVE_CORETEXT
        if (&CTFontManagerUnregisterGraphicsFont && &CTFontManagerUnregisterFontsForURL) {
            CFErrorRef error;
            if (font.canConvert(qMetaTypeId<QCFType<CGFontRef> >())) {
                CTFontManagerUnregisterGraphicsFont(font.value<QCFType<CGFontRef> >(), &error);
            } else if (font.canConvert(qMetaTypeId<QCFType<CFURLRef> >())) {
                CTFontManagerUnregisterFontsForURL(font.value<QCFType<CFURLRef> >(), kCTFontManagerScopeProcess, &error);
            }
        }
#endif
#if HAVE_CORETEXT && HAVE_ATS
        else
#endif
#if HAVE_ATS
        if (font.canConvert(qMetaTypeId<ATSFontContainerRef>())) {
            ATSFontDeactivate(font.value<ATSFontContainerRef>(), 0, kATSOptionFlagsDoNotNotify);
        }
#endif
    }

    m_applicationFonts.clear();

#if HAVE_ATS
    ATSFontNotify(kATSFontNotifyActionFontsChanged, 0);
#endif
}

QT_END_NAMESPACE

