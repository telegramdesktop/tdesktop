/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_information.h"

#include "settings/settings_common.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/box_content_divider.h"
#include "ui/special_buttons.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "boxes/add_contact_box.h"
#include "boxes/confirm_box.h"
#include "boxes/change_phone_box.h"
#include "boxes/photo_crop_box.h"
#include "boxes/username_box.h"
#include "data/data_user.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "apiwrap.h"
#include "core/file_utilities.h"
#include "base/call_delayed.h"
#include "app.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

namespace Settings {
namespace {

constexpr auto kSaveBioTimeout = 1000;

void SetupPhoto(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		not_null<UserData*> self) {
	const auto wrap = container->add(object_ptr<Ui::BoxContentDivider>(
		container,
		st::settingsInfoPhotoHeight));
	const auto photo = Ui::CreateChild<Ui::UserpicButton>(
		wrap,
		controller,
		self,
		Ui::UserpicButton::Role::OpenPhoto,
		st::settingsInfoPhoto);
	const auto upload = Ui::CreateChild<Ui::RoundButton>(
		wrap,
		tr::lng_settings_upload(),
		st::settingsInfoPhotoSet);
	upload->setFullRadius(true);
	upload->addClickHandler([=] {
		const auto filter = FileDialog::ImagesOrAllFilter();
		const auto callback = [=](const FileDialog::OpenResult &result) {
			if (result.paths.isEmpty() && result.remoteContent.isEmpty()) {
				return;
			}

			const auto image = result.remoteContent.isEmpty()
				? App::readImage(result.paths.front())
				: App::readImage(result.remoteContent);
			if (image.isNull()
				|| image.width() > 10 * image.height()
				|| image.height() > 10 * image.width()) {
				Ui::show(Box<InformBox>(tr::lng_bad_photo(tr::now)));
				return;
			}

			const auto box = Ui::show(
				Box<PhotoCropBox>(image, tr::lng_settings_crop_profile(tr::now)));
			box->ready(
			) | rpl::start_with_next([=](QImage &&image) {
				self->session().api().uploadPeerPhoto(
					self,
					std::move(image));
			}, box->lifetime());
		};
		FileDialog::GetOpenPath(
			upload,
			tr::lng_choose_image(tr::now),
			filter,
			crl::guard(upload, callback));
	});
	rpl::combine(
		wrap->widthValue(),
		photo->widthValue(),
		upload->widthValue()
	) | rpl::start_with_next([=](int max, int photoWidth, int uploadWidth) {
		photo->moveToLeft(
			(max - photoWidth) / 2,
			st::settingsInfoPhotoTop);
		upload->moveToLeft(
			(max - uploadWidth) / 2,
			(st::settingsInfoPhotoTop
				+ photo->height()
				+ st::settingsInfoPhotoSkip));
	}, photo->lifetime());
}

void ShowMenu(
		QWidget *parent,
		const QString &copyButton,
		const QString &text) {
	const auto menu = new Ui::PopupMenu(parent);

	menu->addAction(copyButton, [=] {
		QGuiApplication::clipboard()->setText(text);
	});
	menu->popup(QCursor::pos());
}

void AddRow(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> label,
		rpl::producer<TextWithEntities> value,
		const QString &copyButton,
		Fn<void()> edit,
		const style::icon &icon) {
	const auto wrap = AddButton(
		container,
		rpl::single(QString()),
		st::settingsInfoRow,
		&icon);
	const auto forcopy = Ui::CreateChild<QString>(wrap.get());
	wrap->setAcceptBoth();
	wrap->clicks(
	) | rpl::filter([=] {
		return !wrap->isDisabled();
	}) | rpl::start_with_next([=](Qt::MouseButton button) {
		if (button == Qt::LeftButton) {
			edit();
		} else if (!forcopy->isEmpty()) {
			ShowMenu(wrap, copyButton, *forcopy);
		}
	}, wrap->lifetime());

	auto existing = base::duplicate(
		value
	) | rpl::map([](const TextWithEntities &text) {
		return text.entities.isEmpty();
	});
	base::duplicate(
		value
	) | rpl::filter([](const TextWithEntities &text) {
		return text.entities.isEmpty();
	}) | rpl::start_with_next([=](const TextWithEntities &text) {
		*forcopy = text.text;
	}, wrap->lifetime());
	const auto text = Ui::CreateChild<Ui::FlatLabel>(
		wrap.get(),
		std::move(value),
		st::settingsInfoValue);
	text->setClickHandlerFilter([=](auto&&...) {
		edit();
		return false;
	});
	base::duplicate(
		existing
	) | rpl::start_with_next([=](bool existing) {
		wrap->setDisabled(!existing);
		text->setAttribute(Qt::WA_TransparentForMouseEvents, existing);
		text->setSelectable(existing);
		text->setDoubleClickSelectsParagraph(existing);
	}, text->lifetime());

	const auto about = Ui::CreateChild<Ui::FlatLabel>(
		wrap.get(),
		std::move(label),
		st::settingsInfoAbout);
	about->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto button = Ui::CreateChild<Ui::RpWidget>(wrap.get());
	button->resize(st::settingsInfoEditIconOver.size());
	button->setAttribute(Qt::WA_TransparentForMouseEvents);
	button->paintRequest(
	) | rpl::filter([=] {
		return (wrap->isOver() || wrap->isDown()) && !wrap->isDisabled();
	}) | rpl::start_with_next([=](QRect clip) {
		Painter p(button);
		st::settingsInfoEditIconOver.paint(p, QPoint(), button->width());
	}, button->lifetime());

	wrap->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		const auto width = size.width();
		text->resizeToWidth(width
			- st::settingsInfoValuePosition.x()
			- st::settingsInfoRightSkip);
		text->moveToLeft(
			st::settingsInfoValuePosition.x(),
			st::settingsInfoValuePosition.y(),
			width);
		about->resizeToWidth(width
			- st::settingsInfoAboutPosition.x()
			- st::settingsInfoRightSkip);
		about->moveToLeft(
			st::settingsInfoAboutPosition.x(),
			st::settingsInfoAboutPosition.y(),
			width);
		button->moveToRight(
			st::settingsInfoEditRight,
			(size.height() - button->height()) / 2,
			width);
	}, wrap->lifetime());
}

void SetupRows(
		not_null<Ui::VerticalLayout*> container,
		not_null<UserData*> self) {
	const auto session = &self->session();

	AddSkip(container);

	AddRow(
		container,
		tr::lng_settings_name_label(),
		Info::Profile::NameValue(self),
		tr::lng_profile_copy_fullname(tr::now),
		[=] { Ui::show(Box<EditNameBox>(self)); },
		st::settingsInfoName);

	AddRow(
		container,
		tr::lng_settings_phone_label(),
		Info::Profile::PhoneValue(self),
		tr::lng_profile_copy_phone(tr::now),
		[=] { Ui::show(Box<ChangePhoneBox>(session)); },
		st::settingsInfoPhone);

	auto username = Info::Profile::UsernameValue(self);
	auto empty = base::duplicate(
		username
	) | rpl::map([](const TextWithEntities &username) {
		return username.text.isEmpty();
	});
	auto label = rpl::combine(
		tr::lng_settings_username_label(),
		std::move(empty)
	) | rpl::map([](const QString &label, bool empty) {
		return empty ? "t.me/username" : label;
	});
	auto value = rpl::combine(
		std::move(username),
		tr::lng_settings_username_add()
	) | rpl::map([](const TextWithEntities &username, const QString &add) {
		if (!username.text.isEmpty()) {
			return username;
		}
		auto result = TextWithEntities{ add };
		result.entities.push_back({
			EntityType::CustomUrl,
			0,
			add.size(),
			"internal:edit_username" });
		return result;
	});
	AddRow(
		container,
		std::move(label),
		std::move(value),
		tr::lng_context_copy_mention(tr::now),
		[=] { Ui::show(Box<UsernameBox>(session)); },
		st::settingsInfoUsername);

	AddSkip(container, st::settingsInfoAfterSkip);
}

struct BioManager {
	rpl::producer<bool> canSave;
	Fn<void(FnMut<void()> done)> save;
};

BioManager SetupBio(
		not_null<Ui::VerticalLayout*> container,
		not_null<UserData*> self) {
	AddDivider(container);
	AddSkip(container);

	const auto bioStyle = [] {
		auto result = st::settingsBio;
		result.textMargins.setRight(
			st::boxTextFont->spacew
			+ st::boxTextFont->width(QString::number(kMaxBioLength)));
		return result;
	};
	const auto style = Ui::AttachAsChild(container, bioStyle());
	const auto current = Ui::AttachAsChild(container, self->about());
	const auto changed = Ui::CreateChild<rpl::event_stream<bool>>(
		container.get());
	const auto bio = container->add(
		object_ptr<Ui::InputField>(
			container,
			*style,
			Ui::InputField::Mode::MultiLine,
			tr::lng_bio_placeholder(),
			*current),
		st::settingsBioMargins);

	const auto countdown = Ui::CreateChild<Ui::FlatLabel>(
		container.get(),
		QString(),
		st::settingsBioCountdown);

	rpl::combine(
		bio->geometryValue(),
		countdown->widthValue()
	) | rpl::start_with_next([=](QRect geometry, int width) {
		countdown->move(
			geometry.x() + geometry.width() - width,
			geometry.y() + style->textMargins.top());
	}, countdown->lifetime());

	const auto assign = [=](QString text) {
		auto position = bio->textCursor().position();
		bio->setText(text.replace('\n', ' '));
		auto cursor = bio->textCursor();
		cursor.setPosition(position);
		bio->setTextCursor(cursor);
	};
	const auto updated = [=] {
		auto text = bio->getLastText();
		if (text.indexOf('\n') >= 0) {
			assign(text);
			text = bio->getLastText();
		}
		changed->fire(*current != text);
		const auto countLeft = qMax(kMaxBioLength - text.size(), 0);
		countdown->setText(QString::number(countLeft));
	};
	const auto save = [=](FnMut<void()> done) {
		self->session().api().saveSelfBio(
			TextUtilities::PrepareForSending(bio->getLastText()),
			std::move(done));
	};

	Info::Profile::AboutValue(
		self
	) | rpl::start_with_next([=](const TextWithEntities &text) {
		const auto wasChanged = (*current != bio->getLastText());
		*current = text.text;
		if (wasChanged) {
			changed->fire(*current != bio->getLastText());
		} else {
			assign(text.text);
			*current = bio->getLastText();
		}
	}, bio->lifetime());

	const auto generation = Ui::CreateChild<int>(bio);
	changed->events(
	) | rpl::start_with_next([=](bool changed) {
		if (changed) {
			const auto saved = *generation = std::abs(*generation) + 1;
			base::call_delayed(kSaveBioTimeout, bio, [=] {
				if (*generation == saved) {
					save(nullptr);
					*generation = 0;
				}
			});
		} else if (*generation > 0) {
			*generation = -*generation;
		}
	}, bio->lifetime());

	// We need 'bio' to still exist here as InputField, so we add this
	// to 'container' lifetime, not to the 'bio' lifetime.
	container->lifetime().add([=] {
		if (*generation > 0) {
			save(nullptr);
		}
	});

	bio->setMaxLength(kMaxBioLength);
	bio->setSubmitSettings(Ui::InputField::SubmitSettings::Both);
	auto cursor = bio->textCursor();
	cursor.setPosition(bio->getLastText().size());
	bio->setTextCursor(cursor);
	QObject::connect(bio, &Ui::InputField::submitted, [=] {
		save(nullptr);
	});
	QObject::connect(bio, &Ui::InputField::changed, updated);
	bio->setInstantReplaces(Ui::InstantReplaces::Default());
	bio->setInstantReplacesEnabled(
		Core::App().settings().replaceEmojiValue());
	Ui::Emoji::SuggestionsController::Init(
		container->window(),
		bio,
		&self->session());
	updated();

	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			tr::lng_settings_about_bio(),
			st::boxDividerLabel),
		st::settingsBioLabelPadding);

	AddSkip(container);

	return BioManager{
		changed->events() | rpl::distinct_until_changed(),
		save
	};
}

} // namespace

Information::Information(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent) {
	setupContent(controller);
}

//rpl::producer<bool> Information::sectionCanSaveChanges() {
//	return _canSaveChanges.value();
//}
//
//void Information::sectionSaveChanges(FnMut<void()> done) {
//	_save(std::move(done));
//}

void Information::setupContent(
		not_null<Window::SessionController*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	const auto self = controller->session().user();
	SetupPhoto(content, controller, self);
	SetupRows(content, self);
	SetupBio(content, self);
	//auto manager = SetupBio(content, self);
	//_canSaveChanges = std::move(manager.canSave);
	//_save = std::move(manager.save);

	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
