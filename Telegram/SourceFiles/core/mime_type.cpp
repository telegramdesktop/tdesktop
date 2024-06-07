/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/mime_type.h"

#include "core/utils.h"
#include "ui/image/image_prepare.h"

#include <QtCore/QMimeDatabase>
#include <QtCore/QMimeData>

#include <kurlmimedata.h>

namespace Core {
namespace {

[[nodiscard]] bool IsImageFromFirefox(not_null<const QMimeData*> data) {
	// See https://bugs.telegram.org/c/6765/public
	// See https://github.com/telegramdesktop/tdesktop/issues/10564
	//
	// Usually we prefer pasting from URLs list instead of pasting from
	// image data, because sometimes a file is copied together with an
	// image data of its File Explorer thumbnail or smth like that. In
	// that case you end up sending this thumbnail instead of the file.
	//
	// But in case of "Copy Image" from Firefox on Windows we get both
	// URLs list with a file path to some Temp folder in the list and
	// the image data that was copied. The file is read slower + it may
	// have incorrect content in case the URL can't be accessed without
	// authorization. So in that case we want only image data and we
	// check for a special Firefox mime type to check for that case.
	return data->hasFormat(u"application/x-moz-nativeimage"_q)
		&& data->hasImage();
}

[[nodiscard]] base::flat_set<QString> SplitExtensions(
		const QString &joined) {
	const auto list = joined.split(' ');
	return base::flat_set<QString>(list.begin(), list.end());
}

} // namespace

MimeType::MimeType(const QMimeType &type) : _typeStruct(type) {
}

MimeType::MimeType(Known type) : _type(type) {
}

QStringList MimeType::globPatterns() const {
	switch (_type) {
	case Known::WebP: return QStringList(u"*.webp"_q);
	case Known::Tgs: return QStringList(u"*.tgs"_q);
	case Known::Tgv: return QStringList(u"*.tgv"_q);
	case Known::TDesktopTheme: return QStringList(u"*.tdesktop-theme"_q);
	case Known::TDesktopPalette: return QStringList(u"*.tdesktop-palette"_q);
	default: break;
	}
	return _typeStruct.globPatterns();
}

QString MimeType::filterString() const {
	switch (_type) {
	case Known::WebP: return u"WebP image (*.webp)"_q;
	case Known::Tgs: return u"Telegram sticker (*.tgs)"_q;
	case Known::Tgv: return u"Wallpaper pattern (*.tgv)"_q;
	case Known::TDesktopTheme: return u"Theme files (*.tdesktop-theme)"_q;
	case Known::TDesktopPalette: return u"Palette files (*.tdesktop-palette)"_q;
	default: break;
	}
	return _typeStruct.filterString();
}

QString MimeType::name() const {
	switch (_type) {
	case Known::WebP: return u"image/webp"_q;
	case Known::Tgs: return u"application/x-tgsticker"_q;
	case Known::Tgv: return u"application/x-tgwallpattern"_q;
	case Known::TDesktopTheme: return u"application/x-tdesktop-theme"_q;
	case Known::TDesktopPalette: return u"application/x-tdesktop-palette"_q;
	default: break;
	}
	return _typeStruct.name();
}

MimeType MimeTypeForName(const QString &mime) {
	if (mime == u"image/webp"_q) {
		return MimeType(MimeType::Known::WebP);
	} else if (mime == u"application/x-tgsticker"_q) {
		return MimeType(MimeType::Known::Tgs);
	} else if (mime == u"application/x-tgwallpattern"_q) {
		return MimeType(MimeType::Known::Tgv);
	} else if (mime == u"application/x-tdesktop-theme"_q
		|| mime == u"application/x-tgtheme-tdesktop"_q) {
		return MimeType(MimeType::Known::TDesktopTheme);
	} else if (mime == u"application/x-tdesktop-palette"_q) {
		return MimeType(MimeType::Known::TDesktopPalette);
	} else if (mime == u"audio/mpeg3"_q) {
		return MimeType(QMimeDatabase().mimeTypeForName("audio/mp3"));
	}
	return MimeType(QMimeDatabase().mimeTypeForName(mime));
}

MimeType MimeTypeForFile(const QFileInfo &file) {
	QString path = file.absoluteFilePath();
	if (path.endsWith(u".webp"_q, Qt::CaseInsensitive)) {
		return MimeType(MimeType::Known::WebP);
	} else if (path.endsWith(u".tgs"_q, Qt::CaseInsensitive)) {
		return MimeType(MimeType::Known::Tgs);
	} else if (path.endsWith(u".tgv"_q)) {
		return MimeType(MimeType::Known::Tgv);
	} else if (path.endsWith(u".tdesktop-theme"_q, Qt::CaseInsensitive)) {
		return MimeType(MimeType::Known::TDesktopTheme);
	} else if (path.endsWith(u".tdesktop-palette"_q, Qt::CaseInsensitive)) {
		return MimeType(MimeType::Known::TDesktopPalette);
	}

	{
		QFile f(path);
		if (f.open(QIODevice::ReadOnly)) {
			QByteArray magic = f.read(12);
			if (magic.size() >= 12) {
				if (!memcmp(magic.constData(), "RIFF", 4) && !memcmp(magic.constData() + 8, "WEBP", 4)) {
					return MimeType(MimeType::Known::WebP);
				}
			}
			f.close();
		}
	}
	return MimeType(QMimeDatabase().mimeTypeForFile(file));
}

MimeType MimeTypeForData(const QByteArray &data) {
	if (data.size() >= 12) {
		if (!memcmp(data.constData(), "RIFF", 4) && !memcmp(data.constData() + 8, "WEBP", 4)) {
			return MimeType(MimeType::Known::WebP);
		}
	}
	return MimeType(QMimeDatabase().mimeTypeForData(data));
}

bool IsMimeStickerLottie(const QString &mime) {
	return (mime == u"application/x-tgsticker"_q);
}

bool IsMimeStickerWebm(const QString &mime) {
	return (mime == u"video/webm"_q);
}

bool IsMimeStickerAnimated(const QString &mime) {
	return (mime == u"application/x-tgsticker"_q);
}

bool IsMimeSticker(const QString &mime) {
	return (mime == u"image/webp"_q)
		|| IsMimeStickerAnimated(mime);
}

bool IsMimeAcceptedForPhotoVideoAlbum(const QString &mime) {
	return (mime == u"image/jpeg"_q)
		|| (mime == u"image/png"_q)
		|| (mime == u"video/mp4"_q)
		|| (mime == u"video/quicktime"_q);
}

bool FileIsImage(const QString &name, const QString &mime) {
	return name.isEmpty()
		? mime.toLower().startsWith(u"image/"_q)
		: (DetectNameType(name) == NameType::Image);
}

std::shared_ptr<QMimeData> ShareMimeMediaData(
		not_null<const QMimeData*> original) {
	auto result = std::make_shared<QMimeData>();
	if (original->hasFormat(u"application/x-td-forward"_q)) {
		result->setData(u"application/x-td-forward"_q, "1");
	}
	if (original->hasImage()) {
		result->setImageData(original->imageData());
	}
	if (original->hasFormat(u"application/x-td-use-jpeg"_q)
		&& original->hasFormat(u"image/jpeg"_q)) {
		result->setData(u"application/x-td-use-jpeg"_q, "1");
		result->setData(u"image/jpeg"_q, original->data(u"image/jpeg"_q));
	}
	if (auto list = ReadMimeUrls(original); !list.isEmpty()) {
		result->setUrls(std::move(list));
	}
	result->setText(ReadMimeText(original));
	return result;
}

MimeImageData ReadMimeImage(not_null<const QMimeData*> data) {
	if (data->hasFormat(u"application/x-td-use-jpeg"_q)) {
		auto bytes = data->data(u"image/jpeg"_q);
		auto read = Images::Read({ .content = bytes });
		if (read.format == "jpeg" && !read.image.isNull()) {
			return {
				.image = std::move(read.image),
				.content = std::move(bytes),
			};
		}
	} else if (data->hasImage()) {
		return { .image = qvariant_cast<QImage>(data->imageData()) };
	}
	return {};
}

QString ReadMimeText(not_null<const QMimeData*> data) {
	return IsImageFromFirefox(data) ? QString() : data->text();
}

QList<QUrl> ReadMimeUrls(not_null<const QMimeData*> data) {
	return (data->hasUrls() && !IsImageFromFirefox(data))
		? KUrlMimeData::urlsFromMimeData(
			data,
			KUrlMimeData::PreferLocalUrls)
		: QList<QUrl>();
}

bool CanSendFiles(not_null<const QMimeData*> data) {
	if (data->hasImage()) {
		return true;
	} else if (const auto urls = ReadMimeUrls(data); !urls.empty()) {
		if (ranges::all_of(urls, &QUrl::isLocalFile)) {
			return true;
		}
	}
	return false;
}

QString FileExtension(const QString &filepath) {
	const auto reversed = ranges::views::reverse(filepath);
	const auto last = ranges::find_first_of(reversed, ".\\/");
	if (last == reversed.end() || *last != '.') {
		return QString();
	}
	return QString(last.base(), last - reversed.begin());
}

NameType DetectNameType(const QString &filepath) {
	static const auto kImage = SplitExtensions(u"\
afdesign ai avif bmp dng gif heic icns ico jfif jpeg jpg jpg-large jxl nef \
png png-large psd qoi raw sketch svg tga tif tiff webp"_q);
	static const auto kVideo = SplitExtensions(u"\
3g2 3gp 3gpp aep avi flv h264 m4s m4v mkv mov mp4 mpeg mpg ogv srt tgs tgv \
vob webm wmv"_q);
	static const auto kAudio = SplitExtensions(u"\
aac ac3 aif amr caf cda cue flac m4a m4b mid midi mp3 ogg opus wav wma"_q);
	static const auto kDocument = SplitExtensions(u"\
pdf doc docx ppt pptx pps ppsx xls xlsx txt rtf odt ods odp csv text log tl \
tex xspf xml djvu diag ps ost kml pub epub mobi cbr cbz fb2 prc ris pem p7b \
m3u m3u8 wpd wpl htm html xhtml key"_q);
	static const auto kArchive = SplitExtensions(u"\
7z arj bz2 gz rar tar xz z zip zst"_q);
	static const auto kThemeFile = SplitExtensions(u"\
tdesktop-theme tdesktop-palette tgios-theme attheme"_q);
	static const auto kOtherBenign = SplitExtensions(u"\
c cc cpp cxx h m mm swift cs ts class java css ninja cmake patch diff plist \
gyp gitignore strings asoundrc torrent csr json xaml md keylayout sql \
sln xib mk \
\
dmg img iso vcd \
\
pdb eot ics ips ipa core mem pcap ovpn part pcapng dmp pkpass dat zxp crash \
file bak gbr plain dlc fon fnt otf ttc ttf gpx db rss cur \
\
tdesktop-endpoints"_q);

	static const auto kExecutable = SplitExtensions(
#ifdef Q_OS_WIN
		u"\
ad ade adp ahk app application appref-ms asp aspx asx bas bat bin cab cdxml \
cer cfg cgi chi chm cmd cnt com conf cpl crt csh der diagcab dll drv eml \
exe fon fxp gadget grp hlp hpj hta htt inf ini ins inx isp isu its jar jnlp \
job js jse jsp key ksh lexe library-ms lnk local lua mad maf mag mam \
manifest maq mar mas mat mau mav maw mcf mda mdb mde mdt mdw mdz mht mhtml \
mjs mmc mof msc msg msh msh1 msh2 msh1xml msh2xml mshxml msi msp mst ops \
osd paf pcd phar php php3 php4 php5 php7 phps php-s pht phtml pif pl plg pm \
pod prf prg ps1 ps2 ps1xml ps2xml psc1 psc2 psd1 psm1 pssc pst py py3 pyc \
pyd pyi pyo pyw pyzw pyz rb reg rgs scf scr sct search-ms settingcontent-ms \
sh shb shs slk sys swf t tmp u3p url vb vbe vbp vbs vbscript vdx vsmacros \
vsd vsdm vsdx vss vssm vssx vst vstm vstx vsw vsx vtx website wlua ws wsc \
wsf wsh xbap xll xlsm xnk xs"_q
#elif defined Q_OS_MAC // Q_OS_MAC
		u"\
applescript action app bin command csh osx workflow terminal url caction \
mpkg pkg scpt scptd xhtm xhtml webarchive"_q
#else // Q_OS_WIN || Q_OS_MAC
		u"bin csh deb desktop ksh out pet pkg pup rpm run sh shar slp zsh"_q
#endif // !Q_OS_WIN && !Q_OS_MAC
	);

	const auto extension = FileExtension(filepath).toLower();
	if (kExecutable.contains(extension)) {
		return NameType::Executable;
	} else if (kImage.contains(extension)) {
		return NameType::Image;
	} else if (kVideo.contains(extension)) {
		return NameType::Video;
	} else if (kAudio.contains(extension)) {
		return NameType::Audio;
	} else if (kDocument.contains(extension)) {
		return NameType::Document;
	} else if (kArchive.contains(extension)) {
		return NameType::Archive;
	} else if (kThemeFile.contains(extension)) {
		return NameType::ThemeFile;
	} else if (kOtherBenign.contains(extension)) {
		return NameType::OtherBenign;
	}
	return NameType::Unknown;
}

bool NameTypeAllowsThumbnail(NameType type) {
	return type == NameType::Image
		|| type == NameType::Video
		|| type == NameType::Audio
		|| type == NameType::Document
		|| type == NameType::ThemeFile;
}

bool IsIpRevealingPath(const QString &filepath) {
	static const auto kExtensions = [] {
		const auto joined = u"htm html svg m4v m3u8 xhtml"_q;
		const auto list = joined.split(' ');
		return base::flat_set<QString>(list.begin(), list.end());
	}();
	static const auto kMimeTypes = [] {
		const auto joined = u"text/html image/svg+xml"_q;
		const auto list = joined.split(' ');
		return base::flat_set<QString>(list.begin(), list.end());
	}();

	return ranges::binary_search(
		kExtensions,
		FileExtension(filepath).toLower()
	) || ranges::binary_search(
		kMimeTypes,
		QMimeDatabase().mimeTypeForFile(QFileInfo(filepath)).name()
	);
}

} // namespace Core
