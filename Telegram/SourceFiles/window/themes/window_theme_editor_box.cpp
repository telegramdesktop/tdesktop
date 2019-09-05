/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/themes/window_theme_editor_box.h"

#include "window/themes/window_theme.h"
#include "window/themes/window_theme_editor.h"
#include "window/window_controller.h"
#include "boxes/confirm_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/image/image_prepare.h"
#include "ui/toast/toast.h"
#include "info/profile/info_profile_button.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "storage/localstorage.h"
#include "core/file_utilities.h"
#include "core/application.h"
#include "core/event_filter.h"
#include "lang/lang_keys.h"
#include "base/zlib_help.h"
#include "base/unixtime.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/data_cloud_themes.h"
#include "storage/file_upload.h"
#include "mainwindow.h"
#include "layout.h"
#include "apiwrap.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"
#include "styles/style_settings.h"
#include "styles/style_boxes.h"

#include <QtCore/QBuffer>

namespace Window {
namespace Theme {
namespace {

constexpr auto kRandomSlugSize = 16;
constexpr auto kMinSlugSize = 5;
constexpr auto kMaxSlugSize = 64;

enum class SaveErrorType {
	Other,
	Name,
	Link,
};

struct PreparedBackground {
	QByteArray content;
	bool tile = false;
	bool isPng = false;
};

class BackgroundSelector : public Ui::RpWidget {
public:
	BackgroundSelector(
		QWidget *parent,
		const QImage &background,
		const PreparedBackground &data);

	[[nodiscard]] PreparedBackground result() const;

	int resizeGetHeight(int newWidth) override;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void updateThumbnail();
	void chooseBackgroundFromFile();

	object_ptr<Ui::LinkButton> _chooseFromFile;
	object_ptr<Ui::Checkbox> _tileBackground;

	QImage _background;
	QByteArray _backgroundContent;
	bool _isPng = false;
	QString _imageText;
	QPixmap _thumbnail;

};

BackgroundSelector::BackgroundSelector(
	QWidget *parent,
	const QImage &background,
	const PreparedBackground &data)
: RpWidget(parent)
, _chooseFromFile(
	this,
	tr::lng_settings_bg_from_file(tr::now),
	st::boxLinkButton)
, _tileBackground(
	this,
	tr::lng_settings_bg_tile(tr::now),
	data.tile,
	st::defaultBoxCheckbox)
, _background(background)
, _backgroundContent(data.content) {
	_imageText = tr::lng_theme_editor_saved_to_jpg(
		tr::now,
		lt_size,
		formatSizeText(_backgroundContent.size()));
	_chooseFromFile->setClickedCallback([=] { chooseBackgroundFromFile(); });

	const auto height = st::boxTextFont->height
		+ st::themesSmallSkip
		+ _chooseFromFile->heightNoMargins()
		+ st::themesSmallSkip
		+ _tileBackground->heightNoMargins();
	resize(width(), height);

	updateThumbnail();
}

void BackgroundSelector::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto left = height() + st::themesSmallSkip;

	p.setPen(st::boxTextFg);
	p.setFont(st::boxTextFont);
	p.drawTextLeft(left, 0, width(), _imageText);

	p.drawPixmapLeft(0, 0, width(), _thumbnail);
}

int BackgroundSelector::resizeGetHeight(int newWidth) {
	const auto left = height() + st::themesSmallSkip;
	_chooseFromFile->moveToLeft(left, st::boxTextFont->height + st::themesSmallSkip);
	_tileBackground->moveToLeft(left, st::boxTextFont->height + st::themesSmallSkip + _chooseFromFile->height() + st::themesSmallSkip);
	return height();
}

void BackgroundSelector::updateThumbnail() {
	const auto size = height();
	auto back = QImage(
		QSize(size, size) * cIntRetinaFactor(),
		QImage::Format_ARGB32_Premultiplied);
	back.setDevicePixelRatio(cRetinaFactor());
	{
		Painter p(&back);
		PainterHighQualityEnabler hq(p);

		auto &pix = _background;
		int sx = (pix.width() > pix.height()) ? ((pix.width() - pix.height()) / 2) : 0;
		int sy = (pix.height() > pix.width()) ? ((pix.height() - pix.width()) / 2) : 0;
		int s = (pix.width() > pix.height()) ? pix.height() : pix.width();
		p.drawImage(QRect(0, 0, size, size), pix, QRect(sx, sy, s, s));
	}
	Images::prepareRound(back, ImageRoundRadius::Small);
	_thumbnail = App::pixmapFromImageInPlace(std::move(back));
	_thumbnail.setDevicePixelRatio(cRetinaFactor());
	update();
}

void BackgroundSelector::chooseBackgroundFromFile() {
	const auto callback = [=](const FileDialog::OpenResult &result) {
		auto content = result.remoteContent;
		if (!result.paths.isEmpty()) {
			QFile f(result.paths.front());
			if (f.open(QIODevice::ReadOnly)) {
				content = f.readAll();
				f.close();
			}
		}
		if (!content.isEmpty()) {
			auto format = QByteArray();
			auto image = App::readImage(content, &format);
			if (!image.isNull()
				&& (format == "jpeg"
					|| format == "jpg"
					|| format == "png")) {
				_background = image;
				_backgroundContent = content;
				_isPng = (format == "png");
				const auto phrase = _isPng
					? tr::lng_theme_editor_read_from_png
					: tr::lng_theme_editor_read_from_jpg;
				_imageText = phrase(
					tr::now,
					lt_size,
					formatSizeText(_backgroundContent.size()));
				_tileBackground->setChecked(false);
				updateThumbnail();
			}
		}
	};
	FileDialog::GetOpenPath(
		this,
		tr::lng_theme_editor_choose_image(tr::now),
		"Image files (*.jpeg *.jpg *.png)",
		crl::guard(this, callback));
}

PreparedBackground BackgroundSelector::result() const {
	return {
		_backgroundContent,
		_tileBackground->checked(),
		_isPng,
	};
}

void ImportFromFile(
		not_null<Main::Session*> session,
		not_null<QWidget*> parent) {
	const auto &imgExtensions = cImgExtensions();
	auto filters = QStringList(
		qsl("Theme files (*.tdesktop-theme *.tdesktop-palette)"));
	filters.push_back(FileDialog::AllFilesFilter());
	const auto callback = crl::guard(session, [=](
		const FileDialog::OpenResult &result) {
		const auto path = result.paths.isEmpty()
			? QString()
			: result.paths.front();
		if (!path.isEmpty()) {
			Window::Theme::Apply(path);
		}
	});
	FileDialog::GetOpenPath(
		parent.get(),
		tr::lng_choose_image(tr::now),
		filters.join(qsl(";;")),
		crl::guard(parent, callback));
}

QString BytesToUTF8(QLatin1String string) {
	return QString::fromUtf8(string.data(), string.size());
}

bool WriteDefaultPalette(const QString &path) {
	QFile f(path);
	if (!f.open(QIODevice::WriteOnly)) {
		LOG(("Theme Error: could not open '%1' for writing.").arg(path));
		return false;
	}

	QTextStream stream(&f);
	stream.setCodec("UTF-8");

	auto rows = style::main_palette::data();
	for (const auto &row : std::as_const(rows)) {
		stream
			<< BytesToUTF8(row.name)
			<< ": "
			<< BytesToUTF8(row.value)
			<< "; // "
			<< BytesToUTF8(
				row.description
			).replace(
				'\n',
				' '
			).replace(
				'\r',
				' ')
			<< "\n";
	}
	return true;
}

[[nodiscard]] QString GenerateSlug() {
	const auto letters = uint8('Z' + 1 - 'A');
	const auto digits = uint8('9' + 1 - '0');
	const auto values = uint8(2 * letters + digits);

	auto result = QString();
	result.reserve(kRandomSlugSize);
	for (auto i = 0; i != kRandomSlugSize; ++i) {
		const auto value = rand_value<uint8>() % values;
		if (value < letters) {
			result.append(char('A' + value));
		} else if (value < 2 * letters) {
			result.append(char('a' + (value - letters)));
		} else {
			result.append(char('0' + (value - 2 * letters)));
		}
	}
	return result;
}

[[nodiscard]] QByteArray PrepareTheme(
		const QByteArray &palette,
		const PreparedBackground &background) {
	zlib::FileToWrite zip;

	zip_fileinfo zfi = { { 0, 0, 0, 0, 0, 0 }, 0, 0, 0 };
	const auto back = std::string(background.tile ? "tiled" : "background")
		+ (background.isPng ? ".png" : ".jpg");
	zip.openNewFile(
		back.c_str(),
		&zfi,
		nullptr,
		0,
		nullptr,
		0,
		nullptr,
		Z_DEFLATED,
		Z_DEFAULT_COMPRESSION);
	zip.writeInFile(
		background.content.constData(),
		background.content.size());
	zip.closeFile();
	const auto scheme = "colors.tdesktop-theme";
	zip.openNewFile(
		scheme,
		&zfi,
		nullptr,
		0,
		nullptr,
		0,
		nullptr,
		Z_DEFLATED,
		Z_DEFAULT_COMPRESSION);
	zip.writeInFile(palette.constData(), palette.size());
	zip.closeFile();
	zip.close();

	if (zip.error() != ZIP_OK) {
		LOG(("Theme Error: could not export zip-ed theme, status: %1"
			).arg(zip.error()));
		return QByteArray();
	}
	return zip.result();
}

[[nodiscard]] bool IsGoodSlug(const QString &slug) {
	if (slug.size() < kMinSlugSize || slug.size() > kMaxSlugSize) {
		return false;
	}
	const auto i = ranges::find_if(slug, [](QChar ch) {
		return (ch < 'A' || ch > 'Z')
			&& (ch < 'a' || ch > 'z')
			&& (ch < '0' || ch > '9')
			&& (ch != '_');
	});
	return (i == slug.end());
}

SendMediaReady PrepareThemeMedia(
		const QString &name,
		const QByteArray &content) {
	PreparedPhotoThumbs thumbnails;
	QVector<MTPPhotoSize> sizes;

	//const auto push = [&](const char *type, QImage &&image) {
	//	sizes.push_back(MTP_photoSize(
	//		MTP_string(type),
	//		MTP_fileLocationToBeDeprecated(MTP_long(0), MTP_int(0)),
	//		MTP_int(image.width()),
	//		MTP_int(image.height()), MTP_int(0)));
	//	thumbnails.emplace(type[0], std::move(image));
	//};
	//push("s", scaled(320));

	const auto filename = File::NameFromUserString(name)
		+ qsl(".tdesktop-theme"); // #TODO themes
	auto attributes = QVector<MTPDocumentAttribute>(
		1,
		MTP_documentAttributeFilename(MTP_string(filename)));
	const auto id = rand_value<DocumentId>();
	const auto document = MTP_document(
		MTP_flags(0),
		MTP_long(id),
		MTP_long(0),
		MTP_bytes(),
		MTP_int(base::unixtime::now()),
		MTP_string("application/x-tgtheme-tdesktop"),
		MTP_int(content.size()),
		MTP_vector<MTPPhotoSize>(sizes),
		MTP_int(MTP::maindc()),
		MTP_vector<MTPDocumentAttribute>(attributes));

	return SendMediaReady(
		SendMediaType::ThemeFile,
		QString(), // filepath
		filename,
		content.size(),
		content,
		id,
		0,
		QString(),
		PeerId(),
		MTP_photoEmpty(MTP_long(0)),
		thumbnails,
		document,
		QByteArray(),
		0);
}

Fn<void()> SaveTheme(
		not_null<Window::Controller*> window,
		const QByteArray &palette,
		const PreparedBackground &background,
		const QString &name,
		const QString &link,
		Fn<void()> done,
		Fn<void(SaveErrorType,QString)> fail) {
	using Storage::UploadedDocument;
	struct State {
		FullMsgId id;
		bool generating = false;
		mtpRequestId requestId = 0;
		rpl::lifetime lifetime;
	};

	if (name.isEmpty()) {
		fail(SaveErrorType::Name, {});
		return nullptr;
	} else if (!IsGoodSlug(link)) {
		fail(SaveErrorType::Link, {});
		return nullptr;
	}
	const auto session = &window->account().session();
	const auto api = &session->api();
	const auto state = std::make_shared<State>();
	state->id = FullMsgId(
		0,
		session->data().nextLocalMessageId());

	const auto createTheme = [=](const MTPDocument &data) {
		const auto document = session->data().processDocument(data);
		state->requestId = api->request(MTPaccount_CreateTheme(
			MTP_string(link),
			MTP_string(name),
			document->mtpInput()
		)).done([=](const MTPTheme &result) {
			done();
		}).fail([=](const RPCError &error) {
			fail(SaveErrorType::Other, error.type());
		}).send();
	};

	const auto uploadTheme = [=](const UploadedDocument &data) {
		state->requestId = api->request(MTPaccount_UploadTheme(
			MTP_flags(0),
			data.file,
			MTPInputFile(), // thumb
			MTP_string(name + ".tdesktop-theme"), // #TODO themes
			MTP_string("application/x-tgtheme-tdesktop")
		)).done([=](const MTPDocument &result) {
			createTheme(result);
		}).fail([=](const RPCError &error) {
			fail(SaveErrorType::Other, error.type());
		}).send();
	};

	const auto uploadFile = [=](const QByteArray &theme) {
		session->uploader().documentReady(
		) | rpl::filter([=](const UploadedDocument &data) {
			return data.fullId == state->id;
		}) | rpl::start_with_next([=](const UploadedDocument &data) {
			uploadTheme(data);
		}, state->lifetime);

		session->uploader().uploadMedia(
			state->id,
			PrepareThemeMedia(name, theme));
	};

	state->generating = true;
	crl::async([=] {
		crl::on_main([=, ready = PrepareTheme(palette, background)]{
			if (!state->generating) {
				return;
			}
			state->generating = false;
			uploadFile(ready);
		});
	});

	return [=] {
		if (state->generating) {
			state->generating = false;
		} else {
			api->request(base::take(state->requestId)).cancel();
			session->uploader().cancel(state->id);
			state->lifetime.destroy();
		}
	};
}

} // namespace

void StartEditor(
		not_null<Window::Controller*> window,
		const Data::CloudTheme &cloud) {
	const auto path = EditingPalettePath();
	if (!Local::copyThemeColorsToPalette(path)
		&& !WriteDefaultPalette(path)) {
		window->show(Box<InformBox>(tr::lng_theme_editor_error(tr::now)));
		return;
	}
	Background()->setIsEditingTheme(true);
	window->showRightColumn(Box<Editor>(window, cloud));
}

void CreateBox(
		not_null<GenericBox*> box,
		not_null<Window::Controller*> window) {
	CreateForExistingBox(box, window, Data::CloudTheme());
}

void CreateForExistingBox(
		not_null<GenericBox*> box,
		not_null<Window::Controller*> window,
		const Data::CloudTheme &cloud) {
	const auto userId = window->account().sessionExists()
		? window->account().session().userId()
		: UserId(-1);
	const auto amCreator = window->account().sessionExists()
		&& (window->account().session().userId() == cloud.createdBy);
	box->setTitle(amCreator
		? (rpl::single(cloud.title) | Ui::Text::ToWithEntities())
		: tr::lng_theme_editor_create_title(Ui::Text::WithEntities));

	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		(amCreator
			? tr::lng_theme_editor_attach_description
			: tr::lng_theme_editor_create_description)(),
		st::boxDividerLabel));

	box->addRow(
		object_ptr<Info::Profile::Button>(
			box,
			tr::lng_theme_editor_import_existing() | Ui::Text::ToUpper(),
			st::createThemeImportButton),
		style::margins(
			0,
			st::boxRowPadding.left(),
			0,
			0)
	)->addClickHandler([=] {
		ImportFromFile(&window->account().session(), box);
	});

	const auto done = [=] {
		box->closeBox();
		StartEditor(window, cloud);
	};
	Core::InstallEventFilter(box, box, [=](not_null<QEvent*> event) {
		if (event->type() == QEvent::KeyPress) {
			const auto key = static_cast<QKeyEvent*>(event.get())->key();
			if (key == Qt::Key_Enter || key == Qt::Key_Return) {
				done();
				return Core::EventFilter::Result::Cancel;
			}
		}
		return Core::EventFilter::Result::Continue;
	});
	box->addButton(tr::lng_theme_editor_create(), done);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void SaveThemeBox(
		not_null<GenericBox*> box,
		not_null<Window::Controller*> window,
		const Data::CloudTheme &cloud,
		const QByteArray &palette) {
	Expects(window->account().sessionExists());

	const auto background = Background()->createCurrentImage();
	auto backgroundContent = QByteArray();
	const auto tiled = Background()->tile();
	{
		QBuffer buffer(&backgroundContent);
		background.save(&buffer, "JPG", 87);
	}

	box->setTitle(tr::lng_theme_editor_save_title(Ui::Text::WithEntities));

	const auto name = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		tr::lng_theme_editor_name(),
		cloud.title));
	const auto linkWrap = box->addRow(
		object_ptr<Ui::RpWidget>(box),
		style::margins(
			st::boxRowPadding.left(),
			st::themesSmallSkip,
			st::boxRowPadding.right(),
			st::boxRowPadding.bottom()));
	const auto link = Ui::CreateChild<Ui::UsernameInput>(
		linkWrap,
		st::createThemeLink,
		rpl::single(qsl("link")),
		cloud.slug,
		true);
	linkWrap->widthValue(
	) | rpl::start_with_next([=](int width) {
		link->resize(width, link->height());
		link->moveToLeft(0, 0, width);
	}, link->lifetime());
	link->heightValue(
	) | rpl::start_with_next([=](int height) {
		linkWrap->resize(linkWrap->width(), height);
	}, link->lifetime());
	link->setLinkPlaceholder(
		Core::App().createInternalLink(qsl("addtheme/")));
	link->setPlaceholderHidden(false);
	link->setMaxLength(kMaxSlugSize);

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_theme_editor_link_about(),
			st::boxDividerLabel),
		style::margins(
			st::boxRowPadding.left(),
			st::themesSmallSkip,
			st::boxRowPadding.right(),
			st::boxRowPadding.bottom()));

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_theme_editor_background_image(),
			st::settingsSubsectionTitle),
		st::settingsSubsectionTitlePadding);
	const auto back = box->addRow(
		object_ptr<BackgroundSelector>(
			box,
			background,
			PreparedBackground{ backgroundContent, tiled }),
		style::margins(
			st::boxRowPadding.left(),
			st::themesSmallSkip,
			st::boxRowPadding.right(),
			st::boxRowPadding.bottom()));

	box->setFocusCallback([=] { name->setFocusFast(); });

	box->setWidth(st::boxWideWidth);

	const auto saving = box->lifetime().make_state<bool>();
	const auto cancel = std::make_shared<Fn<void()>>(nullptr);
	box->lifetime().add([=] { if (*cancel) (*cancel)(); });
	box->addButton(tr::lng_settings_save(), [=] {
		if (*saving) {
			return;
		}
		*saving = true;
		const auto done = crl::guard(box, [=] {
			box->closeBox();
			window->showRightColumn(nullptr);
		});
		const auto fail = crl::guard(box, [=](
				SaveErrorType type,
				const QString &text) {
			if (!text.isEmpty()) {
				Ui::Toast::Show(text);
			}
			if (type == SaveErrorType::Name) {
				name->showError();
			} else if (type == SaveErrorType::Link) {
				link->showError();
			}
		});
		*cancel = SaveTheme(
			window,
			palette,
			back->result(),
			name->getLastText().trimmed(),
			link->getLastText().trimmed(),
			done,
			fail);
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

} // namespace Theme
} // namespace Window
