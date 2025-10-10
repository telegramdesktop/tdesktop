/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_contact_box.h"

#include "api/api_peer_photo.h"
#include "api/api_text_entities.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "boxes/peers/edit_peer_common.h"
#include "boxes/premium_preview_box.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "core/ui_integration.h"
#include "data/data_changes.h"
#include "data/data_document.h"
#include "data/data_premium_limits.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/stickers/data_stickers.h"
#include "editor/photo_editor_common.h"
#include "editor/photo_editor_layer_widget.h"
#include "history/view/controls/history_view_characters_limit.h"
#include "info/profile/info_profile_cover.h"
#include "info/userpic/info_userpic_emoji_builder_common.h"
#include "info/userpic/info_userpic_emoji_builder_menu_item.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_common.h"
#include "lottie/lottie_frame_generator.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/animated_icon.h"
#include "ui/controls/emoji_button_factory.h"
#include "ui/controls/emoji_button.h"
#include "ui/controls/userpic_button.h"
#include "ui/boxes/confirm_box.h"
#include "ui/text/format_values.h" // Ui::FormatPhone
#include "ui/text/text_entity.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/vertical_list.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/painter.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

namespace {

constexpr auto kAnimationStartFrame = 0;
constexpr auto kAnimationEndFrame = 21;

QString UserPhone(not_null<UserData*> user) {
	const auto phone = user->phone();
	return phone.isEmpty()
		? user->owner().findContactPhone(peerToUser(user->id))
		: phone;
}

void SendRequest(
		base::weak_qptr<Ui::GenericBox> box,
		not_null<UserData*> user,
		bool sharePhone,
		const QString &first,
		const QString &last,
		const QString &phone,
		const TextWithEntities &note,
		Fn<void()> done) {
	const auto wasContact = user->isContact();
	using Flag = MTPcontacts_AddContact::Flag;
	user->session().api().request(MTPcontacts_AddContact(
		MTP_flags(Flag::f_note
			| (sharePhone ? Flag::f_add_phone_privacy_exception : Flag(0))),
		user->inputUser,
		MTP_string(first),
		MTP_string(last),
		MTP_string(phone),
		note.text.isEmpty()
			? MTPTextWithEntities()
			: MTP_textWithEntities(
				MTP_string(note.text),
				Api::EntitiesToMTP(&user->session(), note.entities))
	)).done([=](const MTPUpdates &result) {
		user->setName(
			first,
			last,
			user->nameOrPhone,
			user->username());
		user->session().api().applyUpdates(result);
		if (const auto settings = user->barSettings()) {
			const auto flags = PeerBarSetting::AddContact
				| PeerBarSetting::BlockContact
				| PeerBarSetting::ReportSpam;
			user->setBarSettings(*settings & ~flags);
		}
		if (box) {
			if (!wasContact) {
				box->showToast(
					tr::lng_new_contact_add_done(tr::now, lt_user, first));
			}
			box->closeBox();
		}
		done();
	}).send();
}

class Controller {
public:
	Controller(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> window,
		not_null<UserData*> user,
		bool focusOnNotes = false);

	void prepare();

private:
	void setupContent();
	void setupCover();
	void setupNameFields();
	void setupNotesField();
	void setupPhotoButtons();
	void setupDeleteContactButton();
	void setupWarning();
	void setupSharePhoneNumber();
	void initNameFields(
		not_null<Ui::InputField*> first,
		not_null<Ui::InputField*> last,
		bool inverted);
	void showPhotoMenu(bool suggest);
	void choosePhotoFile(bool suggest);
	void processChosenPhoto(QImage &&image, bool suggest);
	void processChosenPhotoWithMarkup(
		UserpicBuilder::Result &&data,
		bool suggest);
	void executeWithDelay(
		Fn<void()> callback,
		bool suggest,
		bool startAnimation = true);
	void finishIconAnimation(bool suggest);

	not_null<Ui::GenericBox*> _box;
	not_null<Window::SessionController*> _window;
	not_null<UserData*> _user;
	bool _focusOnNotes = false;
	Ui::Checkbox *_sharePhone = nullptr;
	Ui::InputField *_notesField = nullptr;
	Ui::InputField *_firstNameField = nullptr;
	base::unique_qptr<ChatHelpers::TabbedPanel> _emojiPanel;
	base::unique_qptr<Ui::PopupMenu> _photoMenu;
	std::unique_ptr<Ui::AnimatedIcon> _suggestIcon;
	std::unique_ptr<Ui::AnimatedIcon> _cameraIcon;
	Ui::RpWidget *_suggestIconWidget = nullptr;
	Ui::RpWidget *_cameraIconWidget = nullptr;
	QString _phone;
	Fn<void()> _focus;
	Fn<void()> _save;
	Fn<std::optional<QImage>()> _updatedPersonalPhoto;

};

Controller::Controller(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> window,
	not_null<UserData*> user,
	bool focusOnNotes)
: _box(box)
, _window(window)
, _user(user)
, _focusOnNotes(focusOnNotes)
, _phone(UserPhone(user)) {
}

void Controller::prepare() {
	setupContent();

	_box->setTitle(_user->isContact()
		? tr::lng_edit_contact_title()
		: tr::lng_enter_contact_data());

	_box->addButton(tr::lng_box_done(), _save);
	_box->addButton(tr::lng_cancel(), [=] { _box->closeBox(); });
	_box->setFocusCallback(_focus);
}

void Controller::setupContent() {
	setupCover();
	setupNameFields();
	setupNotesField();
	setupPhotoButtons();
	setupDeleteContactButton();
	setupWarning();
	setupSharePhoneNumber();
}

void Controller::setupCover() {
	const auto cover = _box->addRow(
		object_ptr<Info::Profile::Cover>(
			_box,
			_window,
			_user,
			Info::Profile::Cover::Role::EditContact,
			(_phone.isEmpty()
				? tr::lng_contact_mobile_hidden()
				: rpl::single(Ui::FormatPhone(_phone)))),
		style::margins());
	_updatedPersonalPhoto = [=] { return cover->updatedPersonalPhoto(); };
}

void Controller::setupNameFields() {
	const auto inverted = langFirstNameGoesSecond();
	_firstNameField = _box->addRow(
		object_ptr<Ui::InputField>(
			_box,
			st::defaultInputField,
			tr::lng_signup_firstname(),
			_user->firstName),
		st::addContactFieldMargin);
	const auto first = _firstNameField;
	auto preparedLast = object_ptr<Ui::InputField>(
		_box,
		st::defaultInputField,
		tr::lng_signup_lastname(),
		_user->lastName);
	const auto last = inverted
		? _box->insertRow(
			_box->rowsCount() - 1,
			std::move(preparedLast),
			st::addContactFieldMargin)
		: _box->addRow(std::move(preparedLast), st::addContactFieldMargin);

	initNameFields(first, last, inverted);
}

void Controller::initNameFields(
		not_null<Ui::InputField*> first,
		not_null<Ui::InputField*> last,
		bool inverted) {
	const auto getValue = [](not_null<Ui::InputField*> field) {
		return TextUtilities::SingleLine(field->getLastText()).trimmed();
	};

	if (inverted) {
		_box->setTabOrder(last, first);
	}
	_focus = [=] {
		if (_focusOnNotes && _notesField) {
			_notesField->setFocusFast();
			_notesField->setCursorPosition(_notesField->getLastText().size());
			return;
		}
		const auto firstValue = getValue(first);
		const auto lastValue = getValue(last);
		const auto empty = firstValue.isEmpty() && lastValue.isEmpty();
		const auto focusFirst = (inverted != empty);
		(focusFirst ? first : last)->setFocusFast();
	};
	_save = [=] {
		const auto firstValue = getValue(first);
		const auto lastValue = getValue(last);
		const auto empty = firstValue.isEmpty() && lastValue.isEmpty();
		if (empty) {
			_focus();
			(inverted ? last : first)->showError();
			return;
		}

		if (_notesField) {
			const auto limit = Data::PremiumLimits(
				&_user->session()).contactNoteLengthCurrent();
			const auto remove = Ui::ComputeFieldCharacterCount(_notesField)
				- limit;
			if (remove > 0) {
				_box->showToast(tr::lng_contact_notes_limit_reached(
					tr::now,
					lt_count,
					remove));
				_notesField->setFocus();
				return;
			}
		}

		const auto user = _user;
		const auto personal = _updatedPersonalPhoto
			? _updatedPersonalPhoto()
			: std::nullopt;
		const auto done = [=] {
			if (personal) {
				if (personal->isNull()) {
					user->session().api().peerPhoto().clearPersonal(user);
				} else {
					user->session().api().peerPhoto().upload(
						user,
						{ base::duplicate(*personal) });
				}
			}
		};
		const auto noteValue = _notesField
			? [&] {
				auto textWithTags = _notesField->getTextWithAppliedMarkdown();
				return TextWithEntities{
					base::take(textWithTags.text),
					TextUtilities::ConvertTextTagsToEntities(
						base::take(textWithTags.tags)),
				};
			}()
			: TextWithEntities();
		SendRequest(
			base::make_weak(_box),
			user,
			_sharePhone && _sharePhone->checked(),
			firstValue,
			lastValue,
			_phone,
			noteValue,
			done);
	};
	const auto submit = [=] {
		const auto firstValue = first->getLastText().trimmed();
		const auto lastValue = last->getLastText().trimmed();
		const auto empty = firstValue.isEmpty() && lastValue.isEmpty();
		if (inverted ? last->hasFocus() : empty) {
			first->setFocus();
		} else if (inverted ? empty : first->hasFocus()) {
			last->setFocus();
		} else {
			_save();
		}
	};
	first->submits() | rpl::start_with_next(submit, first->lifetime());
	last->submits() | rpl::start_with_next(submit, last->lifetime());
	first->setMaxLength(Ui::EditPeer::kMaxUserFirstLastName);
	first->setMaxLength(Ui::EditPeer::kMaxUserFirstLastName);
}

void Controller::setupWarning() {
	if (_user->isContact() || !_phone.isEmpty()) {
		return;
	}
	_box->addRow(
		object_ptr<Ui::FlatLabel>(
			_box,
			tr::lng_contact_phone_after(tr::now, lt_user, _user->shortName()),
			st::changePhoneLabel),
		st::addContactWarningMargin);
}

void Controller::setupNotesField() {
	Ui::AddSkip(_box->verticalLayout());
	Ui::AddDivider(_box->verticalLayout());
	Ui::AddSkip(_box->verticalLayout());
	_notesField = _box->addRow(
		object_ptr<Ui::InputField>(
			_box,
			st::notesFieldWithEmoji,
			Ui::InputField::Mode::MultiLine,
			tr::lng_contact_add_notes(),
			QString()),
		st::addContactFieldMargin);
	_notesField->setMarkdownSet(Ui::MarkdownSet::Notes);
	_notesField->setCustomTextContext(Core::TextContext({
		.session = &_user->session()
	}));
	_notesField->setTextWithTags({
		_user->note().text,
		TextUtilities::ConvertEntitiesToTextTags(_user->note().entities)
	});

	_notesField->setMarkdownReplacesEnabled(rpl::single(
		Ui::MarkdownEnabledState{
			Ui::MarkdownEnabled{
				{
					Ui::InputField::kTagBold,
					Ui::InputField::kTagItalic,
					Ui::InputField::kTagUnderline,
					Ui::InputField::kTagStrikeOut,
					Ui::InputField::kTagSpoiler
				}
			}
		}
	));

	const auto container = _box->getDelegate()->outerContainer();
	using Selector = ChatHelpers::TabbedSelector;
	_emojiPanel = base::make_unique_q<ChatHelpers::TabbedPanel>(
		container,
		_window,
		object_ptr<Selector>(
			nullptr,
			_window->uiShow(),
			Window::GifPauseReason::Layer,
			Selector::Mode::EmojiOnly));
	_emojiPanel->setDesiredHeightValues(
		1.,
		st::emojiPanMinHeight / 2,
		st::emojiPanMinHeight);
	_emojiPanel->hide();
	_emojiPanel->selector()->setCurrentPeer(_window->session().user());
	_emojiPanel->selector()->emojiChosen(
	) | rpl::start_with_next([=](ChatHelpers::EmojiChosen data) {
		Ui::InsertEmojiAtCursor(_notesField->textCursor(), data.emoji);
	}, _notesField->lifetime());
	_emojiPanel->selector()->customEmojiChosen(
	) | rpl::start_with_next([=](ChatHelpers::FileChosen data) {
		const auto info = data.document->sticker();
		if (info
			&& info->setType == Data::StickersType::Emoji
			&& !_window->session().premium()) {
			ShowPremiumPreviewBox(
				_window,
				PremiumFeature::AnimatedEmoji);
		} else {
			Data::InsertCustomEmoji(_notesField, data.document);
		}
	}, _notesField->lifetime());

	const auto emojiButton = Ui::AddEmojiToggleToField(
		_notesField,
		_box,
		_window,
		_emojiPanel.get(),
		st::sendGifWithCaptionEmojiPosition);
	emojiButton->show();

	using Limit = HistoryView::Controls::CharactersLimitLabel;
	struct LimitState {
		base::unique_qptr<Limit> charsLimitation;
	};
	const auto limitState = _notesField->lifetime().make_state<LimitState>();

	const auto checkCharsLimitation = [=] {
		const auto limit = Data::PremiumLimits(
			&_user->session()).contactNoteLengthCurrent();
		const auto remove = Ui::ComputeFieldCharacterCount(_notesField)
			- limit;
		if (!limitState->charsLimitation) {
			limitState->charsLimitation = base::make_unique_q<Limit>(
				_box->verticalLayout(),
				emojiButton,
				style::al_top,
				QMargins{ 0, -st::lineWidth, 0, 0 });
			_notesField->heightValue(
			) | rpl::start_with_next([=](int height) {
				const auto &st = _notesField->st();
				const auto hasMultipleLines = height >
					(st.textMargins.top()
						+ st.style.font->height
						+ st.textMargins.bottom() * 2);
				limitState->charsLimitation->setVisible(hasMultipleLines);
				limitState->charsLimitation->raise();
			}, limitState->charsLimitation->lifetime());
		}
		limitState->charsLimitation->setLeft(remove);
	};

	_notesField->changes() | rpl::start_with_next([=] {
		checkCharsLimitation();
	}, _notesField->lifetime());

	Ui::AddDividerText(
		_box->verticalLayout(),
		tr::lng_contact_add_notes_about());
}

void Controller::setupPhotoButtons() {
	const auto iconSize = st::restoreUserpicIcon.size;
	auto nameValue = _firstNameField
		? rpl::merge(
			rpl::single(_firstNameField->getLastText().trimmed()),
			_firstNameField->changes() | rpl::map([=] {
				return _firstNameField->getLastText().trimmed();
		})) | rpl::map([=](const QString &text) {
			return text.isEmpty() ? Ui::kQEllipsis : text;
		})
		: rpl::single(_user->shortName()) | rpl::type_erased();
	const auto inner = _box->verticalLayout();
	Ui::AddSkip(inner);

	const auto suggestBirthdayWrap = inner->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			inner,
			object_ptr<Ui::VerticalLayout>(inner)));

	const auto suggestBirthdayButton = Settings::AddButtonWithIcon(
		suggestBirthdayWrap->entity(),
		tr::lng_suggest_birthday(),
		st::settingsButtonLight,
		{ &st::editContactSuggestBirthday });
	suggestBirthdayButton->setClickedCallback([=] {
		Core::App().openInternalUrl(
			u"internal:edit_birthday:suggest:%1"_q.arg(
				peerToUser(_user->id).bare),
			QVariant::fromValue(ClickHandlerContext{
				.sessionWindow = base::make_weak(_window),
			}));
	});
	suggestBirthdayWrap->toggleOn(rpl::single(!_user->birthday().valid()));

	_suggestIcon = Ui::MakeAnimatedIcon({
		.generator = [] {
			return std::make_unique<Lottie::FrameGenerator>(
				Lottie::ReadContent(
					QByteArray(),
					u":/animations/photo_suggest_icon.tgs"_q));
		},
		.sizeOverride = iconSize * style::DevicePixelRatio(),
		.colorized = true,
	});

	_cameraIcon = Ui::MakeAnimatedIcon({
		.generator = [] {
			return std::make_unique<Lottie::FrameGenerator>(
				Lottie::ReadContent(
					QByteArray(),
					u":/animations/camera_outline.tgs"_q));
		},
		.sizeOverride = iconSize * style::DevicePixelRatio(),
		.colorized = true,
	});

	const auto suggestButton = Settings::AddButtonWithIcon(
		inner,
		tr::lng_suggest_photo_for(lt_user, rpl::duplicate(nameValue)),
		st::settingsButtonLight,
		{ nullptr });

	_suggestIconWidget = Ui::CreateChild<Ui::RpWidget>(suggestButton);
	_suggestIconWidget->resize(iconSize * style::DevicePixelRatio());
	_suggestIconWidget->paintRequest() | rpl::start_with_next([=] {
		if (_suggestIcon && _suggestIcon->valid()) {
			auto p = QPainter(_suggestIconWidget);
			const auto frame = _suggestIcon->frame(st::lightButtonFg->c);
			const auto rect = _suggestIconWidget->rect();
			p.drawImage(rect, frame);
		}
	}, _suggestIconWidget->lifetime());

	suggestButton->sizeValue() | rpl::start_with_next([=](QSize size) {
		_suggestIconWidget->move(
			st::settingsButtonLight.iconLeft - iconSize.width() / 2,
			(size.height() - _suggestIconWidget->height()) / 2);
	}, _suggestIconWidget->lifetime());

	suggestButton->setClickedCallback([=] {
		if (_suggestIcon && _suggestIcon->valid()) {
			_suggestIcon->setCustomStartFrame(kAnimationStartFrame);
			_suggestIcon->setCustomEndFrame(kAnimationEndFrame);
			_suggestIcon->jumpToStart([=] { _suggestIconWidget->update(); });
			_suggestIcon->animate([=] { _suggestIconWidget->update(); });
		}
		showPhotoMenu(true);
	});

	const auto setButton = Settings::AddButtonWithIcon(
		inner,
		tr::lng_set_photo_for_user(lt_user, rpl::duplicate(nameValue)),
		st::settingsButtonLight,
		{ nullptr });

	_cameraIconWidget = Ui::CreateChild<Ui::RpWidget>(setButton);
	_cameraIconWidget->resize(iconSize * style::DevicePixelRatio());
	_cameraIconWidget->paintRequest() | rpl::start_with_next([=] {
		if (_cameraIcon && _cameraIcon->valid()) {
			auto p = QPainter(_cameraIconWidget);
			const auto frame = _cameraIcon->frame(st::lightButtonFg->c);
			const auto rect = _cameraIconWidget->rect();
			p.drawImage(rect, frame);
		}
	}, _cameraIconWidget->lifetime());

	setButton->sizeValue() | rpl::start_with_next([=](QSize size) {
		_cameraIconWidget->move(
			st::settingsButtonLight.iconLeft - iconSize.width() / 2,
			(size.height() - _cameraIconWidget->height()) / 2);
	}, _cameraIconWidget->lifetime());

	setButton->setClickedCallback([=] {
		if (_cameraIcon && _cameraIcon->valid()) {
			_cameraIcon->setCustomStartFrame(kAnimationStartFrame);
			_cameraIcon->setCustomEndFrame(kAnimationEndFrame);
			_cameraIcon->jumpToStart([=] { _cameraIconWidget->update(); });
			_cameraIcon->animate([=] { _cameraIconWidget->update(); });
		}
		showPhotoMenu(false);
	});

	const auto resetButtonWrap = inner->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			inner,
			object_ptr<Ui::VerticalLayout>(inner)));

	const auto resetButton = Settings::AddButtonWithIcon(
		resetButtonWrap->entity(),
		tr::lng_profile_photo_reset(),
		st::settingsButtonLight,
		{ nullptr });

	const auto userpicButton = Ui::CreateChild<Ui::UserpicButton>(
		resetButton,
		_window,
		_user,
		Ui::UserpicButton::Role::Custom,
		Ui::UserpicButton::Source::NonPersonalIfHasPersonal,
		st::restoreUserpicIcon);
	userpicButton->setAttribute(Qt::WA_TransparentForMouseEvents);

	resetButton->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		userpicButton->move(
			st::settingsButtonLight.iconLeft,
			(size.height() - userpicButton->height()) / 2);
	}, userpicButton->lifetime());
	resetButtonWrap->toggleOn(
		_user->session().changes().peerFlagsValue(
			_user,
			Data::PeerUpdate::Flag::FullInfo
		) | rpl::map([=] {
			return _user->hasPersonalPhoto();
		}) | rpl::distinct_until_changed());

	resetButton->setClickedCallback([=] {
		_window->show(Ui::MakeConfirmBox({
			.text = tr::lng_profile_photo_reset_sure(
				tr::now,
				lt_user,
				_user->shortName()),
			.confirmed = [=] {
				_window->session().api().peerPhoto().clearPersonal(_user);
			},
			.confirmText = tr::lng_profile_photo_reset(tr::now),
		}));
	});

	Ui::AddSkip(inner);

	Ui::AddDividerText(
		inner,
		tr::lng_contact_photo_replace_info(lt_user, std::move(nameValue)));
	Ui::AddSkip(inner);
}

void Controller::setupDeleteContactButton() {
	if (!_user->isContact()) {
		return;
	}
	const auto inner = _box->verticalLayout();
	const auto deleteButton = Settings::AddButtonWithIcon(
		inner,
		tr::lng_info_delete_contact(),
		st::settingsAttentionButton,
		{ nullptr });
	deleteButton->setClickedCallback([=] {
		const auto text = tr::lng_sure_delete_contact(
			tr::now,
			lt_contact,
			_user->name());
		const auto deleteSure = [=](Fn<void()> &&close) {
			close();
			_user->session().api().request(MTPcontacts_DeleteContacts(
				MTP_vector<MTPInputUser>(1, _user->inputUser)
			)).done([=](const MTPUpdates &result) {
				_user->session().api().applyUpdates(result);
				_box->closeBox();
			}).send();
		};
		_window->show(Ui::MakeConfirmBox({
			.text = text,
			.confirmed = deleteSure,
			.confirmText = tr::lng_box_delete(),
			.confirmStyle = &st::attentionBoxButton,
		}));
	});
	Ui::AddSkip(inner);
}

void Controller::setupSharePhoneNumber() {
	const auto settings = _user->barSettings();
	if (!settings
		|| !((*settings) & PeerBarSetting::NeedContactsException)) {
		return;
	}
	_sharePhone = _box->addRow(
		object_ptr<Ui::Checkbox>(
			_box,
			tr::lng_contact_share_phone(tr::now),
			true,
			st::defaultBoxCheckbox),
		st::addContactWarningMargin);
	_box->addRow(
		object_ptr<Ui::FlatLabel>(
			_box,
			tr::lng_contact_phone_will_be_shared(tr::now, lt_user, _user->shortName()),
			st::changePhoneLabel),
		st::addContactWarningMargin);

}

void Controller::showPhotoMenu(bool suggest) {
	_photoMenu = base::make_unique_q<Ui::PopupMenu>(
		_box,
		st::popupMenuWithIcons);

	QObject::connect(_photoMenu.get(), &QObject::destroyed, [=] {
		finishIconAnimation(suggest);
	});

	_photoMenu->addAction(
		tr::lng_attach_photo(tr::now),
		[=] { executeWithDelay([=] { choosePhotoFile(suggest); }, suggest); },
		&st::menuIconPhoto);

	if (const auto data = QGuiApplication::clipboard()->mimeData()) {
		if (data->hasImage()) {
			auto callback = [=] {
				Editor::PrepareProfilePhoto(
					_box,
					&_window->window(),
					Editor::EditorData{
						.about = (suggest
							? tr::lng_profile_suggest_sure(
								tr::now,
								lt_user,
								Ui::Text::Bold(_user->shortName()),
								Ui::Text::WithEntities)
							: tr::lng_profile_set_personal_sure(
								tr::now,
								lt_user,
								Ui::Text::Bold(_user->shortName()),
								Ui::Text::WithEntities)),
						.confirm = (suggest
							? tr::lng_profile_suggest_button(tr::now)
							: tr::lng_profile_set_photo_button(tr::now)),
						.cropType = Editor::EditorData::CropType::Ellipse,
						.keepAspectRatio = true,
					},
					[=](QImage &&editedImage) {
						processChosenPhoto(std::move(editedImage), suggest);
					},
					qvariant_cast<QImage>(data->imageData()));
			};
			_photoMenu->addAction(
				tr::lng_profile_photo_from_clipboard(tr::now),
				[=] { executeWithDelay(callback, suggest); },
				&st::menuIconPhoto);
		}
	}

	UserpicBuilder::AddEmojiBuilderAction(
		_window,
		_photoMenu.get(),
		_window->session().api().peerPhoto().emojiListValue(
			Api::PeerPhoto::EmojiListType::Profile),
		[=](UserpicBuilder::Result data) {
			processChosenPhotoWithMarkup(std::move(data), suggest);
		},
		false);

	_photoMenu->popup(QCursor::pos());
}

void Controller::choosePhotoFile(bool suggest) {
	Editor::PrepareProfilePhotoFromFile(
		_box,
		&_window->window(),
		Editor::EditorData{
			.about = (suggest
				? tr::lng_profile_suggest_sure(
					tr::now,
					lt_user,
					Ui::Text::Bold(_user->shortName()),
					Ui::Text::WithEntities)
				: tr::lng_profile_set_personal_sure(
					tr::now,
					lt_user,
					Ui::Text::Bold(_user->shortName()),
					Ui::Text::WithEntities)),
			.confirm = (suggest
				? tr::lng_profile_suggest_button(tr::now)
				: tr::lng_profile_set_photo_button(tr::now)),
			.cropType = Editor::EditorData::CropType::Ellipse,
			.keepAspectRatio = true,
		},
		[=](QImage &&image) {
			processChosenPhoto(std::move(image), suggest);
		});
}

void Controller::processChosenPhoto(QImage &&image, bool suggest) {
	Api::PeerPhoto::UserPhoto photo{
		.image = base::duplicate(image),
	};
	if (suggest && _suggestIcon && _suggestIcon->valid()) {
		_suggestIcon->animate([=] { _suggestIconWidget->update(); });
	} else if (!suggest && _cameraIcon && _cameraIcon->valid()) {
		_cameraIcon->animate([=] { _cameraIconWidget->update(); });
	}
	if (suggest) {
		_window->session().api().peerPhoto().suggest(_user, std::move(photo));
		_window->showPeerHistory(_user->id);
	} else {
		_window->session().api().peerPhoto().upload(_user, std::move(photo));
	}
}

void Controller::processChosenPhotoWithMarkup(
		UserpicBuilder::Result &&data,
		bool suggest) {
	Api::PeerPhoto::UserPhoto photo{
		.image = std::move(data.image),
		.markupDocumentId = data.id,
		.markupColors = std::move(data.colors),
	};
	if (suggest && _suggestIcon && _suggestIcon->valid()) {
		_suggestIcon->animate([=] { _suggestIconWidget->update(); });
	} else if (!suggest && _cameraIcon && _cameraIcon->valid()) {
		_cameraIcon->animate([=] { _cameraIconWidget->update(); });
	}
	if (suggest) {
		_window->session().api().peerPhoto().suggest(_user, std::move(photo));
		_window->showPeerHistory(_user->id);
	} else {
		_window->session().api().peerPhoto().upload(_user, std::move(photo));
	}
}

void Controller::finishIconAnimation(bool suggest) {
	const auto icon = suggest ? _suggestIcon.get() : _cameraIcon.get();
	const auto widget = suggest ? _suggestIconWidget : _cameraIconWidget;
	if (icon && icon->valid()) {
		icon->setCustomStartFrame(icon->frameIndex());
		icon->setCustomEndFrame(-1);
		icon->animate([=] { widget->update(); });
	}
}

void Controller::executeWithDelay(
		Fn<void()> callback,
		bool suggest,
		bool startAnimation) {
	const auto icon = suggest ? _suggestIcon.get() : _cameraIcon.get();
	const auto widget = suggest ? _suggestIconWidget : _cameraIconWidget;

	if (startAnimation && icon && icon->valid()) {
		icon->setCustomStartFrame(icon->frameIndex());
		icon->setCustomEndFrame(-1);
		icon->animate([=] { widget->update(); });
	}

	if (icon && icon->valid() && icon->animating()) {
		base::call_delayed(50, [=] {
			executeWithDelay(callback, suggest, false);
		});
	} else {
		callback();
	}
}

} // namespace

void EditContactBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> window,
		not_null<UserData*> user) {
	box->setWidth(st::boxWideWidth);
	box->lifetime().make_state<Controller>(box, window, user)->prepare();
}

void EditContactNoteBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> window,
		not_null<UserData*> user) {
	box->setWidth(st::boxWideWidth);
	box->lifetime().make_state<Controller>(
		box,
		window,
		user,
		true)->prepare();
}
