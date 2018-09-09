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
#include "boxes/add_contact_box.h"
#include "boxes/change_phone_box.h"
#include "boxes/username_box.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "auth_session.h"
#include "apiwrap.h"
#include "styles/style_settings.h"
#include "styles/style_old_settings.h"

namespace Settings {
namespace {

void AddRow(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> label,
		rpl::producer<TextWithEntities> value,
		const QString &copyText,
		const style::IconButton &editSt,
		Fn<void()> edit,
		const style::icon &icon) {
	const auto wrap = container->add(object_ptr<Ui::FixedHeightWidget>(
		container,
		st::settingsInfoRowHeight));

	wrap->paintRequest(
	) | rpl::start_with_next([=, &icon] {
		Painter p(wrap);
		icon.paint(p, st::settingsInfoIconPosition, wrap->width());
	}, wrap->lifetime());

	auto existing = base::duplicate(
		value
	) | rpl::map([](const TextWithEntities &text) {
		return text.entities.isEmpty();
	});
	const auto text = Ui::CreateChild<Ui::FlatLabel>(
		wrap,
		std::move(value),
		st::settingsInfoValue);
	text->setClickHandlerFilter([=](auto&&...) {
		edit();
		return false;
	});
	base::duplicate(
		existing
	) | rpl::start_with_next([=](bool existing) {
		text->setSelectable(existing);
		text->setDoubleClickSelectsParagraph(existing);
		text->setContextCopyText(existing ? copyText : QString());
	}, text->lifetime());

	const auto about = Ui::CreateChild<Ui::FlatLabel>(
		wrap,
		std::move(label),
		st::settingsInfoAbout);

	const auto button = Ui::CreateChild<Ui::IconButton>(
		wrap,
		editSt);
	button->addClickHandler(edit);
	button->showOn(std::move(existing));

	wrap->widthValue(
	) | rpl::start_with_next([=](int width) {
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
			st::settingsInfoEditPosition.x(),
			st::settingsInfoEditPosition.y(),
			width);
	}, wrap->lifetime());
}

void SetupRows(
		not_null<Ui::VerticalLayout*> container,
		not_null<UserData*> self) {
	AddDivider(container);
	AddSkip(container);

	AddRow(
		container,
		Lang::Viewer(lng_settings_name_label),
		Info::Profile::NameValue(self),
		lang(lng_profile_copy_fullname),
		st::settingsEditButton,
		[=] { Ui::show(Box<EditNameBox>(self)); },
		st::settingsEditButton.icon);

	AddRow(
		container,
		Lang::Viewer(lng_settings_phone_label),
		Info::Profile::PhoneValue(self),
		lang(lng_profile_copy_phone),
		st::settingsEditButton,
		[] { Ui::show(Box<ChangePhoneBox>()); },
		st::settingsEditButton.icon);

	auto username = Info::Profile::UsernameValue(self);
	auto empty = base::duplicate(
		username
	) | rpl::map([](const TextWithEntities &username) {
		return username.text.isEmpty();
	});
	auto label = rpl::combine(
		Lang::Viewer(lng_settings_username_label),
		std::move(empty)
	) | rpl::map([](const QString &label, bool empty) {
		return empty ? "t.me/username" : label;
	});
	auto value = rpl::combine(
		std::move(username),
		Lang::Viewer(lng_settings_username_add)
	) | rpl::map([](const TextWithEntities &username, const QString &add) {
		if (!username.text.isEmpty()) {
			return username;
		}
		auto result = TextWithEntities{ add };
		result.entities.push_back(EntityInText(
			EntityInTextCustomUrl,
			0,
			add.size(),
			"internal:edit_username"));
		return result;
	});
	AddRow(
		container,
		std::move(label),
		std::move(value),
		lang(lng_context_copy_mention),
		st::settingsEditButton,
		[=] { Ui::show(Box<UsernameBox>()); },
		st::settingsEditButton.icon);

	AddSkip(container);
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
		auto result = CreateBioFieldStyle();
		return result;
	};
	const auto style = Ui::AttachAsChild(container, bioStyle());
	const auto current = Ui::AttachAsChild(container, self->about());
	const auto changed = Ui::AttachAsChild(
		container,
		rpl::event_stream<bool>());
	const auto bio = container->add(
		object_ptr<Ui::InputField>(
			container,
			*style,
			Ui::InputField::Mode::MultiLine,
			langFactory(lng_bio_placeholder),
			*current),
		st::settingsBioMargins);

	const auto countdown = Ui::CreateChild<Ui::FlatLabel>(
		container.get(),
		QString(),
		Ui::FlatLabel::InitType::Simple,
		st::settingsBioCountdown);

	rpl::combine(
		bio->geometryValue(),
		countdown->widthValue()
	) | rpl::start_with_next([=](QRect geometry, int width) {
		countdown->move(
			geometry.x() + geometry.width() - width,
			geometry.y() + style->textMargins.top());
	}, countdown->lifetime());

	const auto updated = [=] {
		auto text = bio->getLastText();
		if (text.indexOf('\n') >= 0) {
			auto position = bio->textCursor().position();
			bio->setText(text.replace('\n', ' '));
			auto cursor = bio->textCursor();
			cursor.setPosition(position);
			bio->setTextCursor(cursor);
		}
		changed->fire(*current != text);
		const auto countLeft = qMax(kMaxBioLength - text.size(), 0);
		countdown->setText(QString::number(countLeft));
	};
	const auto save = [=](FnMut<void()> done) {
		Auth().api().saveSelfBio(
			TextUtilities::PrepareForSending(bio->getLastText()),
			std::move(done));
	};

	Info::Profile::BioValue(
		self
	) | rpl::start_with_next([=](const TextWithEntities &text) {
		*current = text.text;
		changed->fire(*current != bio->getLastText());
	}, bio->lifetime());

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
	bio->setInstantReplacesEnabled(Global::ReplaceEmojiValue());
	updated();

	AddSkip(container);
	AddDividerText(container, Lang::Viewer(lng_settings_about_bio));

	return BioManager{
		changed->events() | rpl::distinct_until_changed(),
		save
	};
}

} // namespace

Information::Information(QWidget *parent, not_null<UserData*> self)
: Section(parent)
, _self(self) {
	setupContent();
}

rpl::producer<bool> Information::sectionCanSaveChanges() {
	return _canSaveChanges.value();
}

void Information::sectionSaveChanges(FnMut<void()> done) {
	_save(std::move(done));
}

void Information::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	SetupRows(content, _self);
	auto manager = SetupBio(content, _self);
	_canSaveChanges = std::move(manager.canSave);
	_save = std::move(manager.save);

	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
