/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_document_resolver.h"

#include "base/options.h"
#include "base/platform/base_platform_info.h"
#include "ui/boxes/confirm_box.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/mime_type.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_click_handler.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "history/view/media/history_view_gif.h"
#include "history/history.h"
#include "history/history_item.h"
#include "media/player/media_player_instance.h"
#include "platform/platform_file_utilities.h"
#include "ui/chat/chat_theme.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/checkbox.h"
#include "window/window_session_controller.h"
#include "boxes/abstract_box.h" // Ui::show().
#include "styles/style_layers.h"

#include <QtCore/QBuffer>
#include <QtCore/QMimeType>
#include <QtCore/QMimeDatabase>

namespace Data {
namespace {

base::options::toggle OptionExternalVideoPlayer({
	.id = kOptionExternalVideoPlayer,
	.name = "External video player",
});

void ConfirmDontWarnBox(
		not_null<Ui::GenericBox*> box,
		rpl::producer<TextWithEntities> &&text,
		rpl::producer<QString> &&confirm,
		Fn<void(bool)> callback) {
	auto checkbox = object_ptr<Ui::Checkbox>(
		box.get(),
		tr::lng_launch_exe_dont_ask(),
		false,
		st::defaultBoxCheckbox);
	const auto weak = Ui::MakeWeak(checkbox.data());
	auto confirmed = crl::guard(weak, [=, callback = std::move(callback)] {
		const auto checked = weak->checked();
		box->closeBox();
		callback(checked);
	});
	Ui::ConfirmBox(box, {
		.text = std::move(text),
		.confirmed = std::move(confirmed),
		.confirmText = std::move(confirm),
	});
	auto padding = st::boxPadding;
	padding.setTop(padding.bottom());
	box->addRow(std::move(checkbox), std::move(padding));
}

void LaunchWithWarning(
		// not_null<Window::Controller*> controller,
		const QString &name,
		HistoryItem *item) {
	const auto isExecutable = Data::IsExecutableName(name);
	const auto isIpReveal = Data::IsIpRevealingName(name);
	auto &app = Core::App();
	const auto warn = [&] {
		if (item && item->history()->peer->isVerified()) {
			return false;
		}
		return (isExecutable && app.settings().exeLaunchWarning())
			|| (isIpReveal && app.settings().ipRevealWarning());
	}();
	const auto extension = '.' + Data::FileExtension(name);
	if (Platform::IsWindows() && extension == u"."_q) {
		// If you launch a file without extension, like "test", in case
		// there is an executable file with the same name in this folder,
		// like "test.bat", the executable file will be launched.
		//
		// Now we always force an Open With dialog box for such files.
		crl::on_main([=] {
			Platform::File::UnsafeShowOpenWith(name);
		});
		return;
	} else if (!warn) {
		File::Launch(name);
		return;
	}
	const auto callback = [=, &app](bool checked) {
		if (checked) {
			if (isExecutable) {
				app.settings().setExeLaunchWarning(false);
			} else if (isIpReveal) {
				app.settings().setIpRevealWarning(false);
			}
			app.saveSettingsDelayed();
		}
		File::Launch(name);
	};
	auto text = isExecutable
		? tr::lng_launch_exe_warning(
			lt_extension,
			rpl::single(Ui::Text::Bold(extension)),
			Ui::Text::WithEntities)
		: tr::lng_launch_svg_warning(Ui::Text::WithEntities);
	Ui::show(Box(
		ConfirmDontWarnBox,
		std::move(text),
		(isExecutable ? tr::lng_launch_exe_sure : tr::lng_continue)(),
		callback));
}

} // namespace

const char kOptionExternalVideoPlayer[] = "external-video-player";

QString FileExtension(const QString &filepath) {
	const auto reversed = ranges::views::reverse(filepath);
	const auto last = ranges::find_first_of(reversed, ".\\/");
	if (last == reversed.end() || *last != '.') {
		return QString();
	}
	return QString(last.base(), last - reversed.begin());
}

#if 0
bool IsValidMediaFile(const QString &filepath) {
	static const auto kExtensions = [] {
		const auto list = qsl("\
16svx 2sf 3g2 3gp 8svx aac aaf aif aifc aiff amr amv ape asf ast au aup \
avchd avi brstm bwf cam cdda cust dat divx drc dsh dsf dts dtshd dtsma \
dvr-ms dwd evo f4a f4b f4p f4v fla flac flr flv gif gifv gsf gsm gym iff \
ifo it jam la ly m1v m2p m2ts m2v m4a m4p m4v mcf mid mk3d mka mks mkv mng \
mov mp1 mp2 mp3 mp4 minipsf mod mpc mpe mpeg mpg mpv mscz mt2 mus mxf mxl \
niff nsf nsv off ofr ofs ogg ogv opus ots pac ps psf psf2 psflib ptb qsf \
qt ra raw rka rm rmj rmvb roq s3m shn sib sid smi smp sol spc spx ssf svi \
swa swf tak ts tta txm usf vgm vob voc vox vqf wav webm wma wmv wrap wtv \
wv xm xml ym yuv").split(' ');
		return base::flat_set<QString>(list.begin(), list.end());
	}();

	return ranges::binary_search(
		kExtensions,
		FileExtension(filepath).toLower());
}
#endif

bool IsExecutableName(const QString &filepath) {
	static const auto kExtensions = [] {
		const auto joined =
#ifdef Q_OS_WIN
			u"\
ad ade adp app application appref-ms asp asx bas bat bin cab cdxml cer cfg \
chi chm cmd cnt com cpl crt csh der diagcab dll drv eml exe fon fxp gadget \
grp hlp hpj hta htt inf ini ins inx isp isu its jar jnlp job js jse key ksh \
lnk local lua mad maf mag mam manifest maq mar mas mat mau mav maw mcf mda \
mdb mde mdt mdw mdz mht mhtml mjs mmc mof msc msg msh msh1 msh2 msh1xml \
msh2xml mshxml msi msp mst ops osd paf pcd phar php php3 php4 php5 php7 phps \
php-s pht phtml pif pl plg pm pod prf prg ps1 ps2 ps1xml ps2xml psc1 psc2 \
psd1 psm1 pssc pst py py3 pyc pyd pyi pyo pyw pywz pyz rb reg rgs scf scr \
sct search-ms settingcontent-ms sh shb shs slk sys t tmp u3p url vb vbe vbp \
vbs vbscript vdx vsmacros vsd vsdm vsdx vss vssm vssx vst vstm vstx vsw vsx \
vtx website ws wsc wsf wsh xbap xll xnk xs"_q;
#elif defined Q_OS_MAC // Q_OS_MAC
			u"\
applescript action app bin command csh osx workflow terminal url caction \
mpkg pkg scpt scptd xhtm webarchive"_q;
#else // Q_OS_WIN || Q_OS_MAC
			u"bin csh deb desktop ksh out pet pkg pup rpm run sh shar \
slp zsh"_q;
#endif // !Q_OS_WIN && !Q_OS_MAC
		const auto list = joined.split(' ');
		return base::flat_set<QString>(list.begin(), list.end());
	}();

	return ranges::binary_search(
		kExtensions,
		FileExtension(filepath).toLower());
}

bool IsIpRevealingName(const QString &filepath) {
	static const auto kExtensions = [] {
		const auto joined = u"htm html svg m4v m3u8"_q;
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

base::binary_guard ReadBackgroundImageAsync(
		not_null<Data::DocumentMedia*> media,
		FnMut<QImage(QImage)> postprocess,
		FnMut<void(QImage&&)> done) {
	auto result = base::binary_guard();
	const auto gzipSvg = media->owner()->isPatternWallPaperSVG();
	crl::async([
		gzipSvg,
		bytes = media->bytes(),
		path = media->owner()->filepath(),
		postprocess = std::move(postprocess),
		guard = result.make_guard(),
		callback = std::move(done)
	]() mutable {
		auto image = Ui::ReadBackgroundImage(path, bytes, gzipSvg);
		if (postprocess) {
			image = postprocess(std::move(image));
		}
		crl::on_main(std::move(guard), [
			image = std::move(image),
			callback = std::move(callback)
		]() mutable {
			callback(std::move(image));
		});
	});
	return result;
}

void ResolveDocument(
		Window::SessionController *controller,
		not_null<DocumentData*> document,
		HistoryItem *item,
		MsgId topicRootId) {
	if (document->isNull()) {
		return;
	}
	const auto msgId = item ? item->fullId() : FullMsgId();

	const auto showDocument = [&] {
		if (OptionExternalVideoPlayer.value()
			&& document->isVideoFile()
			&& !document->filepath().isEmpty()) {
			File::Launch(document->location(false).fname);
		} else if (controller) {
			controller->openDocument(
				document,
				true,
				{ msgId, topicRootId });
		}
	};

	const auto media = document->createMediaView();
	const auto openImageInApp = [&] {
		if (document->size >= Images::kReadBytesLimit) {
			return false;
		}
		const auto &location = document->location(true);
		if (!location.isEmpty() && location.accessEnable()) {
			const auto guard = gsl::finally([&] {
				location.accessDisable();
			});
			const auto path = location.name();
			if (Core::MimeTypeForFile(QFileInfo(path)).name().startsWith("image/")
				&& QImageReader(path).canRead()) {
				showDocument();
				return true;
			}
		} else if (document->mimeString().startsWith("image/")
			&& !media->bytes().isEmpty()) {
			auto bytes = media->bytes();
			auto buffer = QBuffer(&bytes);
			if (QImageReader(&buffer).canRead()) {
				showDocument();
				return true;
			}
		}
		return false;
	};
	const auto &location = document->location(true);
	if (document->isTheme() && media->loaded(true)) {
		showDocument();
		location.accessDisable();
	} else if (media->canBePlayed(item)) {
		if (document->isAudioFile()
			|| document->isVoiceMessage()
			|| document->isVideoMessage()) {
			::Media::Player::instance()->playPause({ document, msgId });
		} else {
			showDocument();
		}
	} else {
		document->saveFromDataSilent();
		if (!openImageInApp()) {
			if (!document->filepath(true).isEmpty()) {
				LaunchWithWarning(location.name(), item);
			} else if (document->status == FileReady
				|| document->status == FileDownloadFailed) {
				DocumentSaveClickHandler::Save(
					item ? item->fullId() : Data::FileOrigin(),
					document);
			}
		}
	}
}

} // namespace Data
