/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/create_poll_box.h"

#include "poll/poll_media_upload.h"
#include "base/call_delayed.h"
#include "base/qt/qt_key_modifiers.h"
#include "base/unixtime.h"
#include "boxes/premium_limits_box.h"
#include "base/event_filter.h"
#include "base/random.h"
#include "base/unique_qptr.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "core/file_utilities.h"
#include "core/mime_type.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/shortcuts.h"
#include "core/ui_integration.h"
#include "ui/power_saving.h"
#include "data/data_cloud_file.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "data/data_location.h"
#include "data/data_poll.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "history/view/media/menu/history_view_poll_menu.h"
#include "history/view/history_view_schedule_box.h"
#include "lang/lang_keys.h"
#include "layout/layout_document_generic_preview.h"
#include "main/main_app_config.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "platform/platform_file_utilities.h"
#include "main/main_session.h"
#include "menu/menu_send.h"
#include "settings/detailed_settings_button.h"
#include "settings/settings_common.h"
#include "storage/file_upload.h"
#include "storage/localimageloader.h"
#include "storage/storage_account.h"
#include "storage/storage_media_prepare.h"
#include "ui/controls/emoji_button.h"
#include "ui/controls/emoji_button_factory.h"
#include "ui/controls/location_picker.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/dynamic_image.h"
#include "ui/dynamic_thumbnails.h"
#include "ui/effects/radial_animation.h"
#include "ui/effects/ripple_animation.h"
#include "ui/effects/ttl_icon.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/boxes/choose_date_time.h"
#include "ui/text/format_values.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/vertical_layout_reorder.h"
#include "ui/ui_utility.h"
#include "window/section_widget.h"
#include "window/window_session_controller.h"
#include "apiwrap.h"
#include "styles/style_boxes.h"
#include "styles/style_dialogs.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h" // defaultComposeFiles.
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_overview.h"
#include "styles/style_polls.h"
#include "styles/style_settings.h"

#include <QtCore/QBuffer>
#include <QtCore/QMimeData>

namespace {

constexpr auto kQuestionLimit = 255;
constexpr auto kMaxOptionsCount = PollData::kMaxOptions;
constexpr auto kOptionLimit = 100;
constexpr auto kWarnQuestionLimit = 80;
constexpr auto kWarnOptionLimit = 30;
constexpr auto kSolutionLimit = 200;
constexpr auto kWarnSolutionLimit = 60;
constexpr auto kErrorLimit = 99;
constexpr auto kMediaUploadMaxAge = 45 * 60 * crl::time(1000);

using PollMediaState = PollMediaUpload::PollMediaState;
using PollMediaButton = PollMediaUpload::PollMediaButton;
using PollMediaUploader = PollMediaUpload::PollMediaUploader;

using PollMediaUpload::FileListFromMimeData;
using PollMediaUpload::GenerateDocumentFilePreview;
using PollMediaUpload::LocalImageThumbnail;
using PollMediaUpload::PreparePollMediaTask;
using PollMediaUpload::UploadContext;
using PollMediaUpload::ValidateFileDragData;

class Options {
public:
	using AttachCallback = Fn<void(
		not_null<Ui::RpWidget*>,
		std::shared_ptr<PollMediaState>)>;
	using FieldDropCallback = Fn<void(
		not_null<Ui::InputField*>,
		std::shared_ptr<PollMediaState>)>;
	using WidgetDropCallback = Fn<void(
		not_null<QWidget*>,
		std::shared_ptr<PollMediaState>)>;

	Options(
		not_null<Ui::BoxContent*> box,
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		ChatHelpers::TabbedPanel *emojiPanel,
		bool chooseCorrectEnabled,
		AttachCallback attachCallback,
		FieldDropCallback fieldDropCallback,
		WidgetDropCallback widgetDropCallback);

	[[nodiscard]] bool hasOptions() const;
	[[nodiscard]] bool isValid() const;
	[[nodiscard]] bool hasCorrect() const;
	[[nodiscard]] bool hasUploadingMedia() const;
	bool refreshStaleMedia(crl::time threshold);
	[[nodiscard]] std::vector<PollAnswer> toPollAnswers() const;
	void focusFirst();

	void enableChooseCorrect(bool enabled, bool multiCorrect = false);

	[[nodiscard]] not_null<Ui::RpWidget*> layoutWidget() const;
	[[nodiscard]] rpl::producer<int> usedCount() const;
	[[nodiscard]] rpl::producer<not_null<QWidget*>> scrollToWidget() const;
	[[nodiscard]] rpl::producer<> backspaceInFront() const;
	[[nodiscard]] rpl::producer<> tabbed() const;

private:
	class Option {
	public:
		Option(
			not_null<QWidget*> outer,
			not_null<Ui::VerticalLayout*> container,
			not_null<Main::Session*> session,
			int position,
			std::shared_ptr<Ui::RadiobuttonGroup> group,
			AttachCallback attachCallback,
			FieldDropCallback fieldDropCallback,
			WidgetDropCallback widgetDropCallback);

		Option(const Option &other) = delete;
		Option &operator=(const Option &other) = delete;

		void enableChooseCorrect(
			std::shared_ptr<Ui::RadiobuttonGroup> group,
			bool multiCorrect = false,
			Fn<void()> checkboxChanged = nullptr);

		void show(anim::type animated);
		void destroy(FnMut<void()> done);

		[[nodiscard]] bool hasShadow() const;
		void createShadow();
		void destroyShadow();

		[[nodiscard]] bool isEmpty() const;
		[[nodiscard]] bool isGood() const;
		[[nodiscard]] bool isTooLong() const;
		[[nodiscard]] bool isCorrect() const;
		[[nodiscard]] bool uploadingMedia() const;
		bool refreshMediaIfStale(crl::time threshold);
		[[nodiscard]] bool hasFocus() const;
		void setFocus() const;

		void setPlaceholder() const;
		void removePlaceholder() const;
		void showAddIcon(bool show);

		[[nodiscard]] not_null<Ui::InputField*> field() const;

		[[nodiscard]] PollAnswer toPollAnswer(int index) const;

		[[nodiscard]] Ui::RpWidget *handleWidget() const;

	private:
		void createAttach();
		void createWarning();
		void createHandle();
		void updateFieldGeometry();

		base::unique_qptr<Ui::SlideWrap<Ui::RpWidget>> _wrap;
		not_null<Ui::RpWidget*> _content;
		base::unique_qptr<Ui::FadeWrapScaled<Ui::Checkbox>> _correct;
		base::unique_qptr<Ui::FadeWrapScaled<Ui::RpWidget>> _handle;
		bool _hasCorrect = false;
		Ui::InputField *_field = nullptr;
		base::unique_qptr<Ui::PlainShadow> _shadow;
		base::unique_qptr<PollMediaButton> _attach;
		AttachCallback _attachCallback;
		FieldDropCallback _fieldDropCallback;
		WidgetDropCallback _widgetDropCallback;
		std::shared_ptr<PollMediaState> _media;
		Ui::FadeWrapScaled<Ui::RpWidget> *_addIcon = nullptr;

	};

	[[nodiscard]] bool full() const;
	[[nodiscard]] bool correctShadows() const;
	void fixShadows();
	void removeEmptyTail();
	void addEmptyOption();
	void checkLastOption();
	void validateState();
	void fixAfterErase();
	void destroy(std::unique_ptr<Option> option);
	void removeDestroyed(not_null<Option*> field);
	int findField(not_null<Ui::InputField*> field) const;
	[[nodiscard]] auto createChooseCorrectGroup()
		-> std::shared_ptr<Ui::RadiobuttonGroup>;
	void setupReorder();
	void restartReorder();

	not_null<Ui::BoxContent*> _box;
	not_null<Ui::VerticalLayout*> _container;
	const not_null<Window::SessionController*> _controller;
	ChatHelpers::TabbedPanel * const _emojiPanel;
	const AttachCallback _attachCallback;
	const FieldDropCallback _fieldDropCallback;
	const WidgetDropCallback _widgetDropCallback;
	std::shared_ptr<Ui::RadiobuttonGroup> _chooseCorrectGroup;
	bool _multiCorrect = false;
	Fn<void()> _multiCorrectChanged;
	Ui::VerticalLayout *_optionsLayout = nullptr;
	std::unique_ptr<Ui::VerticalLayoutReorder> _reorder;
	int _reordering = 0;
	std::vector<std::unique_ptr<Option>> _list;
	std::vector<std::unique_ptr<Option>> _destroyed;
	rpl::variable<int> _usedCount = 0;
	bool _hasOptions = false;
	bool _isValid = false;
	bool _hasCorrect = false;
	rpl::event_stream<not_null<QWidget*>> _scrollToWidget;
	rpl::event_stream<> _backspaceInFront;
	rpl::event_stream<> _tabbed;
	rpl::lifetime _emojiPanelLifetime;

};

void InitField(
		not_null<QWidget*> container,
		not_null<Ui::InputField*> field,
		not_null<Main::Session*> session,
		std::shared_ptr<Main::SessionShow> show = nullptr,
		base::flat_set<QString> markdownTags = {}) {
	InitMessageFieldHandlers({
		.session = session,
		.show = std::move(show),
		.field = field,
		.allowMarkdownTags = std::move(markdownTags),
	});
	auto options = Ui::Emoji::SuggestionsController::Options();
	options.suggestExactFirstWord = false;
	Ui::Emoji::SuggestionsController::Init(
		container,
		field,
		session,
		options);
}

not_null<Ui::FlatLabel*> CreateWarningLabel(
		not_null<QWidget*> parent,
		not_null<Ui::InputField*> field,
		int valueLimit,
		int warnLimit) {
	const auto result = Ui::CreateChild<Ui::FlatLabel>(
		parent.get(),
		QString(),
		st::createPollWarning);
	result->setAttribute(Qt::WA_TransparentForMouseEvents);
	field->changes(
	) | rpl::on_next([=] {
		Ui::PostponeCall(crl::guard(field, [=] {
			const auto length = field->getLastText().size();
			const auto value = valueLimit - length;
			const auto shown = (value < warnLimit)
				&& (field->height() > st::createPollOptionField.heightMin);
			if (value >= 0) {
				result->setText(QString::number(value));
			} else {
				constexpr auto kMinus = QChar(0x2212);
				result->setMarkedText(Ui::Text::Colorized(
					kMinus + QString::number(std::abs(value))));
			}
			result->setVisible(shown);
		}));
	}, field->lifetime());
	return result;
}

void FocusAtEnd(not_null<Ui::InputField*> field) {
	field->setFocus();
	field->setCursorPosition(field->getLastText().size());
	field->ensureCursorVisible();
}

not_null<DetailedSettingsButton*> AddPollToggleButton(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> title,
		rpl::producer<QString> description,
		Settings::IconDescriptor icon,
		rpl::producer<bool> toggled,
		const style::DetailedSettingsButtonStyle &rowStyle) {
	return AddDetailedSettingsButton(
		container,
		std::move(title),
		std::move(description),
		std::move(icon),
		std::move(toggled),
		rowStyle);
}

Options::Option::Option(
	not_null<QWidget*> outer,
	not_null<Ui::VerticalLayout*> container,
	not_null<Main::Session*> session,
	int position,
	std::shared_ptr<Ui::RadiobuttonGroup> group,
	AttachCallback attachCallback,
	FieldDropCallback fieldDropCallback,
	WidgetDropCallback widgetDropCallback)
: _wrap(container->insert(
	position,
	object_ptr<Ui::SlideWrap<Ui::RpWidget>>(
		container,
		object_ptr<Ui::RpWidget>(container))))
, _content(_wrap->entity())
, _field(
	Ui::CreateChild<Ui::InputField>(
		_content.get(),
		st::createPollOptionFieldPremium,
		Ui::InputField::Mode::NoNewlines,
		tr::lng_polls_create_option_add()))
, _attachCallback(std::move(attachCallback))
, _fieldDropCallback(std::move(fieldDropCallback))
, _widgetDropCallback(std::move(widgetDropCallback))
, _media(std::make_shared<PollMediaState>()) {
	InitField(outer, _field, session);
	_field->setMaxLength(kOptionLimit + kErrorLimit);
	_field->show();
	if (_fieldDropCallback) {
		_fieldDropCallback(_field, _media);
	}

	_wrap->hide(anim::type::instant);

	_content->paintRequest(
	) | rpl::on_next([content = _content.get()] {
		auto p = QPainter(content);
		p.fillRect(content->rect(), st::boxBg);
	}, _content->lifetime());

	_content->widthValue(
	) | rpl::on_next([=] {
		updateFieldGeometry();
	}, _field->lifetime());

	_field->heightValue(
	) | rpl::on_next([=](int height) {
		_content->resize(_content->width(), height);
	}, _field->lifetime());

	_field->changes(
	) | rpl::on_next([=] {
		Ui::PostponeCall(crl::guard(_field, [=] {
			if (_hasCorrect) {
				_correct->toggle(isGood(), anim::type::normal);
			} else if (_handle) {
				_handle->toggle(isGood(), anim::type::normal);
			}
		}));
	}, _field->lifetime());

	createShadow();
	createAttach();
	createWarning();
	createHandle();
	enableChooseCorrect(group);
	if (_correct) {
		_correct->finishAnimating();
	}
	if (_handle) {
		_handle->finishAnimating();
	}
	updateFieldGeometry();
}

bool Options::Option::hasShadow() const {
	return (_shadow != nullptr);
}

void Options::Option::createShadow() {
	Expects(_content != nullptr);

	if (_shadow) {
		return;
	}
	_shadow.reset(Ui::CreateChild<Ui::PlainShadow>(field().get()));
	_shadow->show();
	field()->sizeValue(
	) | rpl::on_next([=](QSize size) {
		const auto left = st::createPollFieldPadding.left();
		_shadow->setGeometry(
			left,
			size.height() - st::lineWidth,
			size.width() - left,
			st::lineWidth);
	}, _shadow->lifetime());
}

void Options::Option::destroyShadow() {
	_shadow = nullptr;
}

void Options::Option::createAttach() {
	const auto field = Option::field();
	const auto attach = Ui::CreateChild<PollMediaButton>(
		field.get(),
		st::pollAttach,
		_media);
	attach->show();
	field->sizeValue(
	) | rpl::on_next([=](QSize size) {
		attach->moveToRight(
			st::createPollOptionRemovePosition.x(),
			st::createPollOptionRemovePosition.y() - st::lineWidth * 2,
			size.width());
	}, attach->lifetime());
	attach->clicks(
	) | rpl::on_next([=](Qt::MouseButton button) {
		if (button != Qt::LeftButton) {
			return;
		}
		if (_attachCallback) {
			_attachCallback(not_null<Ui::RpWidget*>(attach), _media);
		}
	}, attach->lifetime());
	if (_widgetDropCallback) {
		_widgetDropCallback(attach, _media);
	}
	_attach.reset(attach);
}

void Options::Option::createWarning() {
	using namespace rpl::mappers;

	const auto field = this->field();
	const auto warning = CreateWarningLabel(
		field,
		field,
		kOptionLimit,
		kWarnOptionLimit);
	rpl::combine(
		field->sizeValue(),
		warning->sizeValue()
	) | rpl::on_next([=](QSize size, QSize label) {
		warning->moveToLeft(
			(size.width()
				- label.width()
				- st::createPollWarningPosition.x()),
			(size.height()
				- label.height()
				- st::createPollWarningPosition.y()),
			size.width());
	}, warning->lifetime());
}

void Options::Option::createHandle() {
	auto widget = object_ptr<Ui::RpWidget>(_content.get());
	const auto raw = widget.data();
	const auto &icon = st::pollBoxMenuPollOrderIcon;
	raw->resize(icon.width(), icon.height());
	raw->setCursor(Qt::SizeVerCursor);
	raw->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(raw);
		icon.paint(p, 0, 0, raw->width());
	}, raw->lifetime());

	const auto wrap = Ui::CreateChild<Ui::FadeWrapScaled<Ui::RpWidget>>(
		_content.get(),
		std::move(widget));
	wrap->hide(anim::type::instant);

	_content->sizeValue(
	) | rpl::on_next([=](QSize size) {
		const auto left = st::createPollFieldPadding.left();
		wrap->moveToLeft(
			left,
			(size.height() - wrap->heightNoMargins()) / 2);
	}, wrap->lifetime());

	_handle.reset(wrap);
}

Ui::RpWidget *Options::Option::handleWidget() const {
	return _handle ? _handle->entity() : nullptr;
}

bool Options::Option::isEmpty() const {
	return field()->getLastText().trimmed().isEmpty();
}

bool Options::Option::isGood() const {
	return !field()->getLastText().trimmed().isEmpty() && !isTooLong();
}

bool Options::Option::isTooLong() const {
	return (field()->getLastText().size() > kOptionLimit);
}

bool Options::Option::isCorrect() const {
	return isGood() && _correct && _correct->entity()->Checkbox::checked();
}

bool Options::Option::uploadingMedia() const {
	return _media->uploading;
}

bool Options::Option::refreshMediaIfStale(crl::time threshold) {
	if (_media->media
		&& _media->uploadedAt > 0
		&& (!threshold
			|| (crl::now() - _media->uploadedAt > threshold))
		&& _media->reupload) {
		_media->reupload();
		return true;
	}
	return false;
}

bool Options::Option::hasFocus() const {
	return field()->hasFocus();
}

void Options::Option::setFocus() const {
	FocusAtEnd(field());
}

void Options::Option::setPlaceholder() const {
	field()->setPlaceholder(tr::lng_polls_create_option_add());
}

void Options::Option::enableChooseCorrect(
		std::shared_ptr<Ui::RadiobuttonGroup> group,
		bool multiCorrect,
		Fn<void()> checkboxChanged) {
	if (!group && !multiCorrect) {
		_hasCorrect = false;
		if (_correct) {
			_correct->hide(anim::type::normal);
		}
		if (_handle) {
			_handle->toggle(isGood(), anim::type::normal);
		}
		return;
	}
	static auto Index = 0;
	auto checkbox = multiCorrect
		? object_ptr<Ui::Checkbox>(
			_content.get(),
			QString(),
			false,
			st::defaultCheckbox,
			st::defaultCheck)
		: object_ptr<Ui::Checkbox>(object_ptr<Ui::Radiobutton>(
			_content.get(),
			group,
			++Index,
			QString(),
			st::defaultCheckbox));
	const auto button = Ui::CreateChild<Ui::FadeWrapScaled<Ui::Checkbox>>(
		_content.get(),
		std::move(checkbox));
	button->entity()->resize(
		button->entity()->height(),
		button->entity()->height());
	button->hide(anim::type::instant);
	_content->sizeValue(
	) | rpl::on_next([=](QSize size) {
		const auto left = st::createPollFieldPadding.left();
		button->moveToLeft(
			left,
			(size.height() - button->heightNoMargins()) / 2);
	}, button->lifetime());
	_correct.reset(button);
	_hasCorrect = true;
	if (multiCorrect && checkboxChanged) {
		button->entity()->checkedChanges(
		) | rpl::on_next([=](bool) {
			checkboxChanged();
		}, button->lifetime());
	}
	if (isGood()) {
		_correct->show(anim::type::normal);
	} else {
		_correct->hide(anim::type::instant);
	}
	if (_handle) {
		_handle->hide(anim::type::normal);
	}
}

void Options::Option::updateFieldGeometry() {
	const auto skip = st::defaultRadio.diameter
		+ st::defaultCheckbox.textPosition.x();
	_field->resizeToWidth(_content->width() - skip);
	_field->moveToLeft(skip, 0);
}

not_null<Ui::InputField*> Options::Option::field() const {
	return _field;
}

void Options::Option::removePlaceholder() const {
	field()->setPlaceholder(rpl::single(QString()));
}

void Options::Option::showAddIcon(bool show) {
	if (show && !_addIcon) {
		auto icon = Settings::Icon(Settings::IconDescriptor{
			&st::settingsIconAdd,
			Settings::IconType::Round,
			&st::windowBgActive,
		});
		const auto iconSize = icon.size();
		auto widget = object_ptr<Ui::RpWidget>(_content.get());
		const auto raw = widget.data();
		raw->resize(iconSize);
		const auto iconPtr = std::make_shared<Settings::Icon>(
			std::move(icon));
		raw->paintOn([=](QPainter &p) {
			iconPtr->paint(p, 0, 0);
		});

		const auto wrap =
			Ui::CreateChild<Ui::FadeWrapScaled<Ui::RpWidget>>(
				_content.get(),
				std::move(widget));
		wrap->hide(anim::type::instant);

		_content->sizeValue(
		) | rpl::on_next([=](QSize size) {
			const auto &handleIcon = st::pollBoxMenuPollOrderIcon;
			const auto left = st::createPollFieldPadding.left()
				+ (handleIcon.width() - iconSize.width()) / 2;
			wrap->moveToLeft(
				left,
				(size.height() - wrap->heightNoMargins()) / 2);
		}, wrap->lifetime());

		_addIcon = wrap;
	}
	if (_addIcon) {
		if (show) {
			_addIcon->show(anim::type::normal);
		} else {
			_addIcon->hide(anim::type::normal);
		}
	}
}

PollAnswer Options::Option::toPollAnswer(int index) const {
	Expects(index >= 0 && index < kMaxOptionsCount);

	const auto text = field()->getTextWithAppliedMarkdown();

	auto result = PollAnswer{
		TextWithEntities{
			.text = text.text,
			.entities = TextUtilities::ConvertTextTagsToEntities(text.tags),
		},
		QByteArray(1, ('0' + index)),
	};
	result.media = _media->media;
	TextUtilities::Trim(result.text);
	result.correct = _correct ? _correct->entity()->Checkbox::checked() : false;
	return result;
}

Options::Options(
	not_null<Ui::BoxContent*> box,
	not_null<Ui::VerticalLayout*> container,
	not_null<Window::SessionController*> controller,
	ChatHelpers::TabbedPanel *emojiPanel,
	bool chooseCorrectEnabled,
	AttachCallback attachCallback,
	FieldDropCallback fieldDropCallback,
	WidgetDropCallback widgetDropCallback)
: _box(box)
, _container(container)
, _controller(controller)
, _emojiPanel(emojiPanel)
, _attachCallback(std::move(attachCallback))
, _fieldDropCallback(std::move(fieldDropCallback))
, _widgetDropCallback(std::move(widgetDropCallback))
, _chooseCorrectGroup(chooseCorrectEnabled
	? createChooseCorrectGroup()
	: nullptr) {
	auto optionsObj = object_ptr<Ui::VerticalLayout>(container);
	_optionsLayout = optionsObj.data();
	container->add(std::move(optionsObj));
	setupReorder();
	checkLastOption();
}

bool Options::full() const {
	const auto limit = _controller->session().appConfig().pollOptionsLimit();
	return (_list.size() >= limit);
}

bool Options::hasOptions() const {
	return _hasOptions;
}

bool Options::isValid() const {
	return _isValid;
}

bool Options::hasCorrect() const {
	return _hasCorrect;
}

bool Options::hasUploadingMedia() const {
	return ranges::any_of(_list, &Option::uploadingMedia);
}

bool Options::refreshStaleMedia(crl::time threshold) {
	auto refreshed = false;
	for (const auto &option : _list) {
		if (option->refreshMediaIfStale(threshold)) {
			refreshed = true;
		}
	}
	return refreshed;
}

not_null<Ui::RpWidget*> Options::layoutWidget() const {
	return _optionsLayout;
}

rpl::producer<int> Options::usedCount() const {
	return _usedCount.value();
}

rpl::producer<not_null<QWidget*>> Options::scrollToWidget() const {
	return _scrollToWidget.events();
}

rpl::producer<> Options::backspaceInFront() const {
	return _backspaceInFront.events();
}

rpl::producer<> Options::tabbed() const {
	return _tabbed.events();
}

void Options::Option::show(anim::type animated) {
	_wrap->show(animated);
}

void Options::Option::destroy(FnMut<void()> done) {
	if (anim::Disabled() || _wrap->isHidden()) {
		Ui::PostponeCall(std::move(done));
		return;
	}
	_wrap->hide(anim::type::normal);
	base::call_delayed(
		st::slideWrapDuration * 2,
		_content.get(),
		std::move(done));
}

std::vector<PollAnswer> Options::toPollAnswers() const {
	auto result = std::vector<PollAnswer>();
	result.reserve(_list.size());
	auto counter = int(0);
	const auto makeAnswer = [&](const std::unique_ptr<Option> &option) {
		return option->toPollAnswer(counter++);
	};
	ranges::copy(
		_list
		| ranges::views::filter(&Option::isGood)
		| ranges::views::transform(makeAnswer),
		ranges::back_inserter(result));
	return result;
}

void Options::focusFirst() {
	Expects(!_list.empty());

	_list.front()->setFocus();
}

std::shared_ptr<Ui::RadiobuttonGroup> Options::createChooseCorrectGroup() {
	auto result = std::make_shared<Ui::RadiobuttonGroup>(0);
	result->setChangedCallback([=](int) {
		validateState();
	});
	return result;
}

void Options::enableChooseCorrect(bool enabled, bool multiCorrect) {
	_multiCorrect = enabled && multiCorrect;
	if (_multiCorrect) {
		_chooseCorrectGroup = nullptr;
		_multiCorrectChanged = [=] { validateState(); };
		for (auto &option : _list) {
			option->enableChooseCorrect(
				nullptr,
				true,
				_multiCorrectChanged);
		}
	} else {
		_multiCorrectChanged = nullptr;
		_chooseCorrectGroup = enabled
			? createChooseCorrectGroup()
			: nullptr;
		for (auto &option : _list) {
			option->enableChooseCorrect(_chooseCorrectGroup);
		}
	}
	validateState();
	restartReorder();
}

bool Options::correctShadows() const {
	// Last one should be without shadow.
	const auto noShadow = ranges::find(
		_list,
		true,
		ranges::not_fn(&Option::hasShadow));
	return (noShadow == end(_list) - 1);
}

void Options::fixShadows() {
	if (correctShadows()) {
		return;
	}
	for (auto &option : _list) {
		option->createShadow();
	}
	_list.back()->destroyShadow();
}

void Options::removeEmptyTail() {
	// Only one option at the end of options list can be empty.
	// Remove all other trailing empty options.
	// Only last empty and previous option have non-empty placeholders.
	const auto focused = ranges::find_if(
		_list,
		&Option::hasFocus);
	const auto end = _list.end();
	const auto reversed = ranges::views::reverse(_list);
	const auto emptyItem = ranges::find_if(
		reversed,
		ranges::not_fn(&Option::isEmpty)).base();
	const auto focusLast = (focused > emptyItem) && (focused < end);
	if (emptyItem == end) {
		return;
	}
	if (focusLast) {
		(*emptyItem)->setFocus();
	}
	for (auto i = emptyItem + 1; i != end; ++i) {
		destroy(std::move(*i));
	}
	_list.erase(emptyItem + 1, end);
	fixAfterErase();
}

void Options::destroy(std::unique_ptr<Option> option) {
	if (_reorder) {
		_reorder->cancel();
	}
	const auto value = option.get();
	option->destroy([=] { removeDestroyed(value); });
	_destroyed.push_back(std::move(option));
}

void Options::fixAfterErase() {
	Expects(!_list.empty());

	const auto last = _list.end() - 1;
	(*last)->setPlaceholder();
	(*last)->showAddIcon(true);
	if (last != begin(_list)) {
		(*(last - 1))->setPlaceholder();
		(*(last - 1))->showAddIcon(false);
	}
	fixShadows();
}

void Options::addEmptyOption() {
	if (full()) {
		return;
	} else if (!_list.empty() && _list.back()->isEmpty()) {
		return;
	}
	if (!_list.empty()) {
		_list.back()->showAddIcon(false);
	}
	if (_list.size() > 1) {
		(*(_list.end() - 2))->removePlaceholder();
	}
	_list.push_back(std::make_unique<Option>(
		_box,
		_optionsLayout,
		&_controller->session(),
		_optionsLayout->count(),
		_chooseCorrectGroup,
		_attachCallback,
		_fieldDropCallback,
		_widgetDropCallback));
	if (_multiCorrect) {
		_list.back()->enableChooseCorrect(
			nullptr,
			true,
			_multiCorrectChanged);
	}
	const auto field = _list.back()->field();
	if (const auto emojiPanel = _emojiPanel) {
		const auto isPremium = _controller->session().user()->isPremium();
		const auto emojiToggle = Ui::AddEmojiToggleToField(
			field,
			_box,
			_controller,
			emojiPanel,
			QPoint(
				-st::createPollOptionFieldPremium.textMargins.right(),
				st::createPollOptionEmojiPositionSkip));
		emojiToggle->shownValue() | rpl::on_next([=](bool shown) {
			if (!shown) {
				return;
			}
			_emojiPanelLifetime.destroy();
			emojiPanel->selector()->emojiChosen(
			) | rpl::on_next([=](ChatHelpers::EmojiChosen data) {
				if (field->hasFocus()) {
					Ui::InsertEmojiAtCursor(field->textCursor(), data.emoji);
				}
			}, _emojiPanelLifetime);
			if (isPremium) {
				emojiPanel->selector()->customEmojiChosen(
				) | rpl::on_next([=](ChatHelpers::FileChosen data) {
					if (field->hasFocus()) {
						Data::InsertCustomEmoji(field, data.document);
					}
				}, _emojiPanelLifetime);
			}
		}, emojiToggle->lifetime());
	}
	field->submits(
	) | rpl::on_next([=] {
		const auto index = findField(field);
		if (_list[index]->isGood() && index + 1 < _list.size()) {
			_list[index + 1]->setFocus();
		}
	}, field->lifetime());
	field->changes(
	) | rpl::on_next([=] {
		Ui::PostponeCall(crl::guard(field, [=] {
			validateState();
		}));
	}, field->lifetime());
	field->focusedChanges(
	) | rpl::filter(rpl::mappers::_1) | rpl::on_next([=] {
		_scrollToWidget.fire_copy(field);
	}, field->lifetime());
	field->tabbed(
	) | rpl::on_next([=](not_null<bool*> handled) {
		const auto index = findField(field);
		if (index + 1 < _list.size()) {
			_list[index + 1]->setFocus();
		} else {
			_tabbed.fire({});
		}
		*handled = true;
	}, field->lifetime());
	base::install_event_filter(field, [=](not_null<QEvent*> event) {
		if (event->type() != QEvent::KeyPress
			|| !field->getLastText().isEmpty()) {
			return base::EventFilterResult::Continue;
		}
		const auto key = static_cast<QKeyEvent*>(event.get())->key();
		if (key != Qt::Key_Backspace) {
			return base::EventFilterResult::Continue;
		}

		const auto index = findField(field);
		if (index > 0) {
			_list[index - 1]->setFocus();
		} else {
			_backspaceInFront.fire({});
		}
		return base::EventFilterResult::Cancel;
	});

	_list.back()->showAddIcon(true);
	_list.back()->show((_list.size() == 1)
		? anim::type::instant
		: anim::type::normal);
	fixShadows();
	restartReorder();
}

void Options::removeDestroyed(not_null<Option*> option) {
	const auto i = ranges::find(
		_destroyed,
		option.get(),
		&std::unique_ptr<Option>::get);
	Assert(i != end(_destroyed));
	_destroyed.erase(i);
	restartReorder();
}

void Options::validateState() {
	checkLastOption();
	_hasOptions = (ranges::count_if(_list, &Option::isGood) > 0);
	_isValid = _hasOptions && ranges::none_of(_list, &Option::isTooLong);
	_hasCorrect = ranges::any_of(_list, &Option::isCorrect);

	const auto lastEmpty = !_list.empty() && _list.back()->isEmpty();
	_usedCount = _list.size() - (lastEmpty ? 1 : 0);
}

int Options::findField(not_null<Ui::InputField*> field) const {
	const auto result = ranges::find(
		_list,
		field,
		&Option::field) - begin(_list);

	Ensures(result >= 0 && result < _list.size());
	return result;
}

void Options::checkLastOption() {
	removeEmptyTail();
	addEmptyOption();
}

void Options::setupReorder() {
	_reorder = std::make_unique<Ui::VerticalLayoutReorder>(
		_optionsLayout);
	_reorder->setMouseEventProxy([=](int i)
			-> not_null<Ui::RpWidget*> {
		if (i < int(_list.size())) {
			if (const auto handle = _list[i]->handleWidget()) {
				return handle;
			}
		}
		return _optionsLayout->widgetAt(i);
	});
	_reorder->updates(
	) | rpl::on_next([=](Ui::VerticalLayoutReorder::Single data) {
		using State = Ui::VerticalLayoutReorder::State;
		if (data.state == State::Started) {
			++_reordering;
		} else {
			Ui::PostponeCall(_optionsLayout, [=] {
				--_reordering;
			});
			if (data.state == State::Applied) {
				base::reorder(
					_list,
					data.oldPosition,
					data.newPosition);
				fixShadows();
			}
		}
	}, _box->lifetime());
}

void Options::restartReorder() {
	if (!_reorder) {
		return;
	}
	_reorder->cancel();

	if (!_destroyed.empty()) {
		return;
	}
	if (_chooseCorrectGroup || _multiCorrect) {
		return;
	}

	_reorder->clearPinnedIntervals();

	const auto count = int(_list.size());
	if (count < 2) {
		return;
	}
	if (_list.back()->isEmpty()) {
		_reorder->addPinnedInterval(count - 1, 1);
	}
	_reorder->start();
}

class DurationIconAction final : public Ui::Menu::Action {
public:
	DurationIconAction(
		not_null<Ui::Menu::Menu*> parent,
		const style::Menu &st,
		not_null<QAction*> action,
		const QString &tinyText);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	QString _tinyText;

};

DurationIconAction::DurationIconAction(
	not_null<Ui::Menu::Menu*> parent,
	const style::Menu &st,
	not_null<QAction*> action,
	const QString &tinyText)
: Ui::Menu::Action(parent, st, action, nullptr, nullptr)
, _tinyText(tinyText) {
}

void DurationIconAction::paintEvent(QPaintEvent *e) {
	Ui::Menu::Action::paintEvent(e);

	const auto &st = this->st();
	const auto iconPos = st.itemIconPosition;
	const auto size = st::createPollDurationIconSize;
	const auto &pos = st::createPollDurationIconPosition;
	const auto rect = QRect(
		iconPos.x() + pos.x(),
		iconPos.y() + pos.y(),
		size,
		size);
	const auto innerRect = rect - st::createPollDurationIconMargins;

	Painter p(this);
	PainterHighQualityEnabler hq(p);

	Ui::PaintTimerIcon(p, innerRect, _tinyText, st::menuIconColor->c);
}

void ShowMediaUploadingToast() {
	Ui::Toast::Show({
		.title = tr::lng_polls_media_uploading_toast_title(tr::now),
		.text = tr::lng_polls_media_uploading_toast(tr::now, tr::marked),
		.iconLottie = u"uploading"_q,
		.iconLottieSize = st::pollToastUploadingIconSize,
		.duration = crl::time(3000),
	});
}

} // namespace

CreatePollBox::CreatePollBox(
	QWidget*,
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	PollData::Flags chosen,
	PollData::Flags disabled,
	rpl::producer<int> starsRequired,
	Api::SendType sendType,
	SendMenu::Details sendMenuDetails)
: _controller(controller)
, _peer(peer)
, _chosen(chosen)
, _disabled(disabled)
, _sendType(sendType)
, _sendMenuDetails([result = sendMenuDetails] { return result; })
, _starsRequired(std::move(starsRequired)) {
}

rpl::producer<CreatePollBox::Result> CreatePollBox::submitRequests() const {
	return _submitRequests.events();
}

void CreatePollBox::setInnerFocus() {
	_setInnerFocus();
}

void CreatePollBox::submitFailed(const QString &error) {
	showToast(error);
}

void CreatePollBox::submitMediaExpired() {
	if (_refreshExpiredMedia) {
		_refreshExpiredMedia();
		ShowMediaUploadingToast();
	}
}

not_null<Ui::InputField*> CreatePollBox::setupQuestion(
		not_null<Ui::VerticalLayout*> container) {
	using namespace Settings;

	const auto session = &_controller->session();
	const auto isPremium = session->user()->isPremium();
	Ui::AddSubsectionTitle(container, tr::lng_polls_create_question());

	const auto question = container->add(
		object_ptr<Ui::InputField>(
			container,
			st::createPollField,
			Ui::InputField::Mode::MultiLine,
			tr::lng_polls_create_question_placeholder()),
		st::createPollFieldPadding
			+ QMargins(0, 0, st::defaultComposeFiles.emoji.inner.width, 0));
	InitField(
		getDelegate()->outerContainer(),
		question,
		session,
		_controller->uiShow());
	question->setMaxLength(kQuestionLimit + kErrorLimit);
	question->setSubmitSettings(Ui::InputField::SubmitSettings::Both);

	{
		using Selector = ChatHelpers::TabbedSelector;
		const auto outer = getDelegate()->outerContainer();
		_emojiPanel = base::make_unique_q<ChatHelpers::TabbedPanel>(
			outer,
			_controller,
			object_ptr<Selector>(
				nullptr,
				_controller->uiShow(),
				Window::GifPauseReason::Layer,
				Selector::Mode::EmojiOnly));
		const auto emojiPanel = _emojiPanel.get();
		emojiPanel->setDesiredHeightValues(
			1.,
			st::emojiPanMinHeight / 2,
			st::emojiPanMinHeight);
		emojiPanel->hide();
		emojiPanel->selector()->setCurrentPeer(session->user());

		const auto emojiToggle = Ui::AddEmojiToggleToField(
			question,
			this,
			_controller,
			emojiPanel,
			st::createPollOptionFieldPremiumEmojiPosition);
		emojiToggle->show();
		emojiPanel->selector()->emojiChosen(
		) | rpl::on_next([=](ChatHelpers::EmojiChosen data) {
			if (question->hasFocus()) {
				Ui::InsertEmojiAtCursor(question->textCursor(), data.emoji);
			}
		}, emojiToggle->lifetime());
		if (isPremium) {
			emojiPanel->selector()->customEmojiChosen(
			) | rpl::on_next([=](ChatHelpers::FileChosen data) {
				if (question->hasFocus()) {
					Data::InsertCustomEmoji(question, data.document);
				}
			}, emojiToggle->lifetime());
		}
	}

	const auto warning = CreateWarningLabel(
		container,
		question,
		kQuestionLimit,
		kWarnQuestionLimit);
	rpl::combine(
		question->geometryValue(),
		warning->sizeValue()
	) | rpl::on_next([=](QRect geometry, QSize label) {
		warning->moveToLeft(
			(container->width()
				- label.width()
				- st::createPollWarningPosition.x()),
			(geometry.y()
				- st::createPollFieldPadding.top()
				- st::defaultSubsectionTitlePadding.bottom()
				- st::defaultSubsectionTitle.style.font->height
				+ st::defaultSubsectionTitle.style.font->ascent
				- st::createPollWarning.style.font->ascent),
			geometry.width());
	}, warning->lifetime());

	return question;
}

not_null<Ui::InputField*> CreatePollBox::setupDescription(
		not_null<Ui::VerticalLayout*> container) {
	const auto session = &_controller->session();
	const auto isPremium = session->user()->isPremium();
	const auto description = container->add(
		object_ptr<Ui::InputField>(
			container,
			st::pollDescriptionField,
			Ui::InputField::Mode::MultiLine,
			tr::lng_polls_create_description_placeholder()),
		st::pollDescriptionFieldPadding);
	InitField(
		getDelegate()->outerContainer(),
		description,
		session,
		_controller->uiShow());
	description->setSubmitSettings(Ui::InputField::SubmitSettings::Both);

	if (const auto emojiPanel = _emojiPanel.get()) {
		const auto emojiToggle = Ui::AddEmojiToggleToField(
			description,
			this,
			_controller,
			emojiPanel,
			QPoint(
				-st::pollDescriptionField.textMargins.right(),
				-st::lineWidth));
		emojiToggle->shownValue() | rpl::on_next([=](bool shown) {
			if (!shown) {
				return;
			}
			emojiPanel->selector()->emojiChosen(
			) | rpl::on_next([=](ChatHelpers::EmojiChosen data) {
				if (description->hasFocus()) {
					Ui::InsertEmojiAtCursor(
						description->textCursor(),
						data.emoji);
				}
			}, emojiToggle->lifetime());
			if (isPremium) {
				emojiPanel->selector()->customEmojiChosen(
				) | rpl::on_next([=](ChatHelpers::FileChosen data) {
					if (description->hasFocus()) {
						Data::InsertCustomEmoji(description, data.document);
					}
				}, emojiToggle->lifetime());
			}
		}, emojiToggle->lifetime());
	}

	return description;
}

not_null<Ui::InputField*> CreatePollBox::setupSolution(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<bool> shown) {
	using namespace Settings;

	const auto outer = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container))
	)->toggleOn(std::move(shown));
	const auto inner = outer->entity();

	const auto session = &_controller->session();
	Ui::AddSkip(inner);
	Ui::AddSubsectionTitle(inner, tr::lng_polls_solution_title());
	const auto solution = inner->add(
		object_ptr<Ui::InputField>(
			inner,
			st::pollMediaField,
			Ui::InputField::Mode::MultiLine,
			tr::lng_polls_solution_placeholder()),
		st::createPollFieldPadding);
	InitField(
		getDelegate()->outerContainer(),
		solution,
		session,
		_controller->uiShow(),
		{
			Ui::InputField::kTagBold,
			Ui::InputField::kTagItalic,
			Ui::InputField::kTagUnderline,
			Ui::InputField::kTagStrikeOut,
			Ui::InputField::kTagCode,
			Ui::InputField::kTagSpoiler,
		});
	solution->setMaxLength(kSolutionLimit + kErrorLimit);

	const auto warning = CreateWarningLabel(
		inner,
		solution,
		kSolutionLimit,
		kWarnSolutionLimit);
	rpl::combine(
		solution->geometryValue(),
		warning->sizeValue()
	) | rpl::on_next([=](QRect geometry, QSize label) {
		warning->moveToLeft(
			(inner->width()
				- label.width()
				- st::createPollWarningPosition.x()),
			(geometry.y()
				- st::createPollFieldPadding.top()
				- st::defaultSubsectionTitlePadding.bottom()
				- st::defaultSubsectionTitle.style.font->height
				+ st::defaultSubsectionTitle.style.font->ascent
				- st::createPollWarning.style.font->ascent),
			geometry.width());
	}, warning->lifetime());

	Ui::AddDividerText(
		inner,
		tr::lng_polls_solution_about());

	return solution;
}

object_ptr<Ui::RpWidget> CreatePollBox::setupContent() {
	using namespace Settings;

	const auto id = base::RandomValue<uint64>();
	struct UploadContext {
		std::weak_ptr<PollMediaState> media;
		uint64 token = 0;
		QString filename;
		QString filemime;
		QVector<MTPDocumentAttribute> attributes;
		bool forceFile = true;
	};
	using StartUploadFn = Fn<void(
		std::shared_ptr<PollMediaState>,
		Ui::PreparedFile)>;
	struct State final {
		Errors error = Error::Question;
		std::unique_ptr<Options> options;
		rpl::event_stream<bool> multipleForceOff;
		rpl::event_stream<bool> addOptionsForceOff;
		rpl::event_stream<bool> revotingForceOff;
		rpl::event_stream<bool> quizForceOff;
		rpl::event_stream<bool> showWhoVotedForceOn;
		rpl::variable<int> closePeriod = 0;
		rpl::variable<TimeId> closeDate = TimeId(0);
		std::shared_ptr<PollMediaState> descriptionMedia
			= std::make_shared<PollMediaState>();
		std::shared_ptr<PollMediaState> solutionMedia
			= std::make_shared<PollMediaState>();
		std::weak_ptr<PollMediaState> stickerTarget;
		base::flat_map<FullMsgId, UploadContext> uploads;
		base::unique_qptr<Ui::PopupMenu> mediaMenu;
		base::unique_qptr<Ui::PopupMenu> durationMenu;
		base::unique_qptr<ChatHelpers::TabbedPanel> stickerPanel;
		std::unique_ptr<TaskQueue> prepareQueue;
		StartUploadFn startPhotoUpload;
		StartUploadFn startDocumentUpload;
		StartUploadFn startVideoUpload;
	};
	const auto state = lifetime().make_state<State>();
	state->prepareQueue = std::make_unique<TaskQueue>();

	auto result = object_ptr<Ui::VerticalLayout>(this);
	const auto container = result.data();

	const auto updateMedia = [=](
			const std::shared_ptr<PollMediaState> &media) {
		if (media->update) {
			media->update();
		}
	};
	const auto setMedia = [=](
			const std::shared_ptr<PollMediaState> &media,
			PollMedia value,
			std::shared_ptr<Ui::DynamicImage> thumbnail,
			bool rounded) {
		const auto wasUploading = media->uploading;
		media->token++;
		media->media = value;
		media->thumbnail = std::move(thumbnail);
		media->rounded = rounded;
		media->progress = (media->uploading && media->media)
			? 1.
			: 0.;
		media->uploadDataId = 0;
		media->uploading = false;
		if (wasUploading && value) {
			media->uploadedAt = crl::now();
		} else {
			media->uploadedAt = 0;
			media->reupload = nullptr;
		}
		updateMedia(media);
	};
	struct UploadedMedia final {
		PollMedia input;
		std::shared_ptr<Ui::DynamicImage> thumbnail;
	};
	const auto parseUploaded = [=](
			const MTPMessageMedia &result,
			FullMsgId fullId) {
		auto parsed = UploadedMedia();
		auto &owner = _controller->session().data();
		result.match([&](const MTPDmessageMediaPhoto &media) {
			if (const auto photo = media.vphoto()) {
				photo->match([&](const MTPDphoto &) {
					parsed.input.photo = owner.processPhoto(*photo);
					parsed.thumbnail = Ui::MakePhotoThumbnail(
						parsed.input.photo,
						fullId);
				}, [](const auto &) {
				});
			}
		}, [&](const MTPDmessageMediaDocument &media) {
			if (const auto document = media.vdocument()) {
				document->match([&](const MTPDdocument &) {
					parsed.input.document = owner.processDocument(
						*document);
					parsed.thumbnail
						= Ui::MakeDocumentFilePreviewThumbnail(
							parsed.input.document,
							fullId);
				}, [](const auto &) {
				});
			}
		}, [](const auto &) {
		});
		return parsed;
	};
	const auto applyUploaded = [=](
			const std::shared_ptr<PollMediaState> &media,
			uint64 token,
			FullMsgId fullId,
			const MTPInputFile &file) {
		const auto uploaded = MTP_inputMediaUploadedPhoto(
			MTP_flags(0),
			file,
			MTP_vector<MTPInputDocument>(QVector<MTPInputDocument>()),
			MTPint(),
			MTPInputDocument());
		_controller->session().api().request(MTPmessages_UploadMedia(
			MTP_flags(0),
			MTPstring(),
			_peer->input(),
			uploaded
		)).done([=](const MTPMessageMedia &result) {
			if (media->token != token) {
				return;
			}
			auto parsed = parseUploaded(result, fullId);
			if (!parsed.input) {
				setMedia(media, PollMedia(), nullptr, false);
				showToast(tr::lng_attach_failed(tr::now));
				return;
			}
			setMedia(
				media,
				parsed.input,
				media->thumbnail
					? media->thumbnail
					: std::move(parsed.thumbnail),
				true);
		}).fail([=](const MTP::Error &) {
			if (media->token != token) {
				return;
			}
			setMedia(media, PollMedia(), nullptr, false);
			showToast(tr::lng_attach_failed(tr::now));
		}).send();
	};
	const auto applyUploadedDocument = [=](
			const std::shared_ptr<PollMediaState> &media,
			uint64 token,
			FullMsgId fullId,
			const Api::RemoteFileInfo &info,
			const UploadContext &context) {
		using Flag = MTPDinputMediaUploadedDocument::Flag;
		const auto flags = (context.forceFile ? Flag::f_force_file : Flag())
			| (info.thumb ? Flag::f_thumb : Flag());
		auto attributes = !context.attributes.isEmpty()
			? context.attributes
			: QVector<MTPDocumentAttribute>{
				MTP_documentAttributeFilename(
					MTP_string(context.filename)),
			};
		const auto uploaded = MTP_inputMediaUploadedDocument(
			MTP_flags(flags),
			info.file,
			info.thumb.value_or(MTPInputFile()),
			MTP_string(context.filemime),
			MTP_vector<MTPDocumentAttribute>(std::move(attributes)),
			MTP_vector<MTPInputDocument>(),
			MTPInputPhoto(),
			MTP_int(0),
			MTP_int(0));
		_controller->session().api().request(MTPmessages_UploadMedia(
			MTP_flags(0),
			MTPstring(),
			_peer->input(),
			uploaded
		)).done([=](const MTPMessageMedia &result) {
			if (media->token != token) {
				return;
			}
			auto parsed = parseUploaded(result, fullId);
			if (!parsed.input) {
				setMedia(media, PollMedia(), nullptr, false);
				showToast(tr::lng_attach_failed(tr::now));
				return;
			}
			const auto isVideo = parsed.input.document
				&& parsed.input.document->isVideoFile();
			setMedia(
				media,
				parsed.input,
				isVideo
					? (media->thumbnail
						? media->thumbnail
						: std::move(parsed.thumbnail))
					: std::move(parsed.thumbnail),
				isVideo);
		}).fail([=](const MTP::Error &) {
			if (media->token != token) {
				return;
			}
			setMedia(media, PollMedia(), nullptr, false);
			showToast(tr::lng_attach_failed(tr::now));
		}).send();
	};
	_controller->session().uploader().photoReady(
	) | rpl::on_next([=](const Storage::UploadedMedia &data) {
		const auto context = state->uploads.take(data.fullId);
		if (!context) {
			return;
		}
		const auto media = context->media.lock();
		if (!media || (media->token != context->token)) {
			return;
		}
		applyUploaded(media, context->token, data.fullId, data.info.file);
	}, lifetime());
	_controller->session().uploader().photoProgress(
	) | rpl::on_next([=](const FullMsgId &id) {
		const auto i = state->uploads.find(id);
		if (i == state->uploads.end()) {
			return;
		}
		const auto &context = i->second;
		const auto media = context.media.lock();
		if (!media
			|| (media->token != context.token)
			|| !media->uploadDataId) {
			return;
		}
		media->progress = _controller->session().data().photo(
			media->uploadDataId)->progress();
		updateMedia(media);
	}, lifetime());
	_controller->session().uploader().photoFailed(
	) | rpl::on_next([=](const FullMsgId &id) {
		const auto context = state->uploads.take(id);
		if (!context) {
			return;
		}
		const auto media = context->media.lock();
		if (!media || (media->token != context->token)) {
			return;
		}
		setMedia(media, PollMedia(), nullptr, false);
		showToast(tr::lng_attach_failed(tr::now));
	}, lifetime());
	_controller->session().uploader().documentReady(
	) | rpl::on_next([=](const Storage::UploadedMedia &data) {
		const auto context = state->uploads.take(data.fullId);
		if (!context) {
			return;
		}
		const auto media = context->media.lock();
		if (!media || (media->token != context->token)) {
			return;
		}
		applyUploadedDocument(
			media,
			context->token,
			data.fullId,
			data.info,
			*context);
	}, lifetime());
	_controller->session().uploader().documentProgress(
	) | rpl::on_next([=](const FullMsgId &id) {
		const auto i = state->uploads.find(id);
		if (i == state->uploads.end()) {
			return;
		}
		const auto &context = i->second;
		const auto media = context.media.lock();
		if (!media
			|| (media->token != context.token)
			|| !media->uploadDataId) {
			return;
		}
		media->progress = _controller->session().data().document(
			media->uploadDataId)->progress();
		updateMedia(media);
	}, lifetime());
	_controller->session().uploader().documentFailed(
	) | rpl::on_next([=](const FullMsgId &id) {
		const auto context = state->uploads.take(id);
		if (!context) {
			return;
		}
		const auto media = context->media.lock();
		if (!media || (media->token != context->token)) {
			return;
		}
		setMedia(media, PollMedia(), nullptr, false);
		showToast(tr::lng_attach_failed(tr::now));
	}, lifetime());
	const auto emojiPaused = [=] {
		using namespace Window;
		return _controller->isGifPausedAtLeastFor(GifPauseReason::Any);
	};
	const auto updateStickerPanelGeometry = [=] {
		if (!state->stickerPanel) {
			return;
		}
		const auto panel = state->stickerPanel.get();
		const auto parent = panel->parentWidget();
		const auto left = std::max(
			(parent->width() - panel->width()) / 2,
			0);
		const auto top = std::max(
			(parent->height() - panel->height()) / 2,
			0);
		panel->moveTopRight(top, left + panel->width());
	};
	const auto showStickerPanel = [=](
			not_null<Ui::RpWidget*>,
			std::shared_ptr<PollMediaState> media) {
		if (!state->stickerPanel) {
			const auto body = getDelegate()->outerContainer();
			state->stickerPanel = HistoryView::CreatePollStickerPanel(
				body,
				_controller);
			state->stickerPanel->setDropDown(true);
			base::install_event_filter(
				body,
				[=](not_null<QEvent*> event) {
					const auto type = event->type();
					if (type == QEvent::Move || type == QEvent::Resize) {
						crl::on_main(this, updateStickerPanelGeometry);
					}
			return base::EventFilterResult::Continue;
		},
		lifetime());
			state->stickerPanel->selector()->fileChosen(
			) | rpl::on_next([=](ChatHelpers::FileChosen data) {
				if (Window::ShowSendPremiumError(
						_controller,
						data.document)) {
					return;
				}
				const auto target = state->stickerTarget.lock();
				if (!target) {
					return;
				}
				setMedia(
					target,
					PollMedia{ .document = data.document },
					Ui::MakeEmojiThumbnail(
						&_controller->session().data(),
						Data::SerializeCustomEmojiId(data.document),
						emojiPaused),
					false);
				state->stickerPanel->hideAnimated();
			}, state->stickerPanel->lifetime());
		}
		state->stickerTarget = media;
		const auto panel = state->stickerPanel.get();
		updateStickerPanelGeometry();
		panel->toggleAnimated();
	};
	const auto asyncReupload = [=](
			std::shared_ptr<PollMediaState> media,
			Fn<Ui::PreparedList()> prepare,
			Fn<bool(const Ui::PreparedList&)> validate,
			const QString &name,
			const StartUploadFn &startUpload) {
		const auto reuploadToken = ++media->token;
		media->media = PollMedia();
		media->uploading = true;
		media->progress = 0.;
		media->uploadDataId = 0;
		updateMedia(media);
		const auto weak = QPointer<CreatePollBox>(this);
		crl::async([=, prepare = std::move(prepare)] {
			auto list = prepare();
			crl::on_main([=, list = std::move(list)]() mutable {
				if (!weak || media->token != reuploadToken) {
					return;
				}
				if (list.error != Ui::PreparedList::Error::None
					|| list.files.empty()
					|| (validate && !validate(list))) {
					setMedia(media, PollMedia(), nullptr, false);
					showToast(tr::lng_attach_failed(tr::now));
					return;
				}
				auto &f = list.files.front();
				if (!name.isEmpty()) {
					f.displayName = name;
				}
				startUpload(media, std::move(f));
			});
		});
	};
	const auto setFileReupload = [=](
			std::shared_ptr<PollMediaState> media,
			const QString &path,
			const QString &name,
			const StartUploadFn &startUpload) {
		media->reupload = crl::guard(this, [=,
				weak = std::weak_ptr(media)] {
			const auto strong = weak.lock();
			if (!strong) {
				return;
			}
			if (path.isEmpty()) {
				setMedia(strong, PollMedia(), nullptr, false);
				showToast(tr::lng_attach_failed(tr::now));
				return;
			}
			const auto premium = _controller->session().premium();
			asyncReupload(
				strong,
				[=] {
					return Storage::PrepareMediaList(
						QStringList{ path },
						st::sendMediaPreviewSize,
						premium);
				},
				nullptr,
				name,
				startUpload);
		});
	};
	const auto startPreparedPhotoUpload = [=](
			std::shared_ptr<PollMediaState> media,
			Ui::PreparedFile file) {
		const auto sourceImage = (file.path.isEmpty()
			&& file.content.isEmpty()
			&& file.information)
			? [&]() -> QImage {
				const auto img = std::get_if<
					Ui::PreparedFileInformation::Image>(
						&file.information->media);
				return (img && !img->data.isNull())
					? img->data
					: QImage();
			}()
			: QImage();
		media->reupload = crl::guard(this, [=,
				weak = std::weak_ptr(media),
				path = file.path,
				content = file.content,
				name = file.displayName] {
			const auto strong = weak.lock();
			if (!strong) {
				return;
			}
			const auto premium = _controller->session().premium();
			asyncReupload(
				strong,
				[=] {
					if (!path.isEmpty()) {
						return Storage::PrepareMediaList(
							QStringList{ path },
							st::sendMediaPreviewSize,
							premium);
					}
					if (!content.isEmpty()) {
						auto image = QImage::fromData(content);
						if (!image.isNull()) {
							return Storage::PrepareMediaFromImage(
								std::move(image),
								QByteArray(content),
								st::sendMediaPreviewSize);
						}
					} else if (!sourceImage.isNull()) {
						auto bytes = QByteArray();
						auto buffer = QBuffer(&bytes);
						buffer.open(QIODevice::WriteOnly);
						sourceImage.save(&buffer, "PNG");
						return Storage::PrepareMediaFromImage(
							QImage(sourceImage),
							std::move(bytes),
							st::sendMediaPreviewSize);
					}
					return Ui::PreparedList(
						Ui::PreparedList::Error::EmptyFile,
						QString());
				},
				[](const Ui::PreparedList &list) {
					return list.files.front().type
						== Ui::PreparedFile::Type::Photo;
				},
				name,
				state->startPhotoUpload);
		});
		const auto token = ++media->token;
		media->media = PollMedia();
		media->thumbnail = std::make_shared<LocalImageThumbnail>(
			std::move(file.preview));
		media->rounded = true;
		media->uploading = true;
		media->progress = 0.;
		media->uploadDataId = 0;
		updateMedia(media);
		using PreparePoll = PreparePollMediaTask;
		state->prepareQueue->addTask(std::make_unique<PreparePoll>(
			FileLoadTask::Args{
				.session = &_controller->session(),
				.filepath = file.path,
				.content = file.content,
				.information = std::move(file.information),
				.videoCover = nullptr,
				.type = SendMediaType::Photo,
				.to = FileLoadTo(
					_peer->id,
					Api::SendOptions(),
					FullReplyTo(),
					MsgId()),
				.caption = TextWithTags(),
				.spoiler = false,
				.album = nullptr,
				.forceFile = false,
				.idOverride = 0,
				.displayName = file.displayName,
			},
			[=](std::shared_ptr<FilePrepareResult> prepared) {
				if ((media->token != token)
					|| !prepared
					|| (prepared->type != SendMediaType::Photo)) {
					if (media->token == token) {
						setMedia(media, PollMedia(), nullptr, false);
						showToast(tr::lng_attach_failed(tr::now));
					}
					return;
				}
				const auto uploadId = FullMsgId(
					_peer->id,
					_controller->session().data().nextLocalMessageId());
				state->uploads.emplace(uploadId, UploadContext{
					.media = media,
					.token = token,
				});
				media->uploadDataId = prepared->id;
				_controller->session().uploader().upload(
					uploadId,
					prepared);
			}));
	};
	const auto startPreparedDocumentUpload = [=](
			std::shared_ptr<PollMediaState> media,
			Ui::PreparedFile file) {
		const auto displayName = file.displayName.isEmpty()
			? QFileInfo(file.path).fileName()
			: file.displayName;
		setFileReupload(
			media,
			file.path,
			displayName,
			state->startDocumentUpload);
		auto audioAttributes = PollMediaUpload::ExtractAudioAttributes(file);
		const auto isAudio = !audioAttributes.isEmpty();
		const auto token = ++media->token;
		media->media = PollMedia();
		media->thumbnail = std::make_shared<LocalImageThumbnail>(
			GenerateDocumentFilePreview(
				displayName,
				st::pollAttach.rippleAreaSize));
		media->rounded = false;
		media->uploading = true;
		media->progress = 0.;
		media->uploadDataId = 0;
		updateMedia(media);
		using PreparePoll = PreparePollMediaTask;
		state->prepareQueue->addTask(std::make_unique<PreparePoll>(
			FileLoadTask::Args{
				.session = &_controller->session(),
				.filepath = file.path,
				.content = file.content,
				.information = std::move(file.information),
				.videoCover = nullptr,
				.type = SendMediaType::File,
				.to = FileLoadTo(
					_peer->id,
					Api::SendOptions(),
					FullReplyTo(),
					MsgId()),
				.caption = TextWithTags(),
				.spoiler = false,
				.album = nullptr,
				.forceFile = !isAudio,
				.idOverride = 0,
				.displayName = displayName,
			},
			[=, attributes = std::move(audioAttributes)](
					std::shared_ptr<FilePrepareResult> prepared) {
				if ((media->token != token) || !prepared) {
					if (media->token == token) {
						setMedia(media, PollMedia(), nullptr, false);
						showToast(tr::lng_attach_failed(tr::now));
					}
					return;
				}
				const auto uploadId = FullMsgId(
					_peer->id,
					_controller->session().data().nextLocalMessageId());
				state->uploads.emplace(uploadId, UploadContext{
					.media = media,
					.token = token,
					.filename = prepared->filename,
					.filemime = prepared->filemime,
					.attributes = attributes,
					.forceFile = !isAudio,
				});
				media->uploadDataId = prepared->id;
				_controller->session().uploader().upload(
					uploadId,
					prepared);
			}));
	};
	const auto startPreparedVideoUpload = [=](
			std::shared_ptr<PollMediaState> media,
			Ui::PreparedFile file) {
		setFileReupload(
			media,
			file.path,
			file.displayName,
			state->startVideoUpload);
		const auto token = ++media->token;
		media->media = PollMedia();
		media->thumbnail = std::make_shared<LocalImageThumbnail>(
			std::move(file.preview));
		media->rounded = true;
		media->uploading = true;
		media->progress = 0.;
		media->uploadDataId = 0;
		updateMedia(media);
		using PreparePoll = PreparePollMediaTask;
		state->prepareQueue->addTask(std::make_unique<PreparePoll>(
			FileLoadTask::Args{
				.session = &_controller->session(),
				.filepath = file.path,
				.content = file.content,
				.information = std::move(file.information),
				.videoCover = nullptr,
				.type = SendMediaType::File,
				.to = FileLoadTo(
					_peer->id,
					Api::SendOptions(),
					FullReplyTo(),
					MsgId()),
				.caption = TextWithTags(),
				.spoiler = false,
				.album = nullptr,
				.forceFile = false,
				.idOverride = 0,
				.displayName = file.displayName,
			},
			[=](std::shared_ptr<FilePrepareResult> prepared) {
				if ((media->token != token) || !prepared) {
					if (media->token == token) {
						setMedia(media, PollMedia(), nullptr, false);
						showToast(tr::lng_attach_failed(tr::now));
					}
					return;
				}
				auto attributes = QVector<MTPDocumentAttribute>();
				prepared->document.match([&](const MTPDdocument &data) {
					attributes = data.vattributes().v;
				}, [](const auto &) {
				});
				const auto uploadId = FullMsgId(
					_peer->id,
					_controller->session().data().nextLocalMessageId());
				state->uploads.emplace(uploadId, UploadContext{
					.media = media,
					.token = token,
					.filename = prepared->filename,
					.filemime = prepared->filemime,
					.attributes = std::move(attributes),
					.forceFile = false,
				});
				media->uploadDataId = prepared->id;
				_controller->session().uploader().upload(
					uploadId,
					prepared);
			}));
	};
	state->startPhotoUpload = startPreparedPhotoUpload;
	state->startDocumentUpload = startPreparedDocumentUpload;
	state->startVideoUpload = startPreparedVideoUpload;
	const auto applyPreparedPhotoList = [=](
			std::shared_ptr<PollMediaState> media,
			Ui::PreparedList &&list) {
		if (list.error != Ui::PreparedList::Error::None
			|| (list.files.size() != 1)
			|| (list.files.front().type != Ui::PreparedFile::Type::Photo)) {
			return false;
		}
		startPreparedPhotoUpload(media, std::move(list.files.front()));
		return true;
	};
	using ValidateFn = Fn<bool(not_null<const QMimeData*>)>;
	using ApplyDropFn = Fn<bool(
		std::shared_ptr<PollMediaState>,
		not_null<const QMimeData*>)>;
	const auto installDropToWidget = [=](
			not_null<QWidget*> widget,
			std::shared_ptr<PollMediaState> media,
			ValidateFn validate,
			ApplyDropFn apply) {
		widget->setAcceptDrops(true);
		base::install_event_filter(widget, [=](not_null<QEvent*> event) {
			const auto type = event->type();
			if (type != QEvent::DragEnter
				&& type != QEvent::DragMove
				&& type != QEvent::Drop) {
				return base::EventFilterResult::Continue;
			}
			const auto drop = static_cast<QDropEvent*>(event.get());
			const auto data = drop->mimeData();
			if (!data || !validate(data)) {
				return base::EventFilterResult::Continue;
			}
			if (type == QEvent::Drop && !apply(media, data)) {
				return base::EventFilterResult::Continue;
			}
			drop->acceptProposedAction();
			return base::EventFilterResult::Cancel;
		});
	};
	const auto installDropToField = [=](
			not_null<Ui::InputField*> field,
			std::shared_ptr<PollMediaState> media,
			ValidateFn validate,
			ApplyDropFn apply) {
		field->setMimeDataHook([=](
				not_null<const QMimeData*> data,
				Ui::InputField::MimeAction action) {
			if (action == Ui::InputField::MimeAction::Check) {
				return validate(data);
			} else if (action == Ui::InputField::MimeAction::Insert) {
				return apply(media, data);
			}
			Unexpected("Polls: action in MimeData hook.");
		});
	};
	const auto applyPhotoOrVideoDrop = ApplyDropFn([=](
			std::shared_ptr<PollMediaState> media,
			not_null<const QMimeData*> data) {
		auto list = FileListFromMimeData(
			data,
			_controller->session().premium());
		if (list.error != Ui::PreparedList::Error::None
			|| list.files.empty()) {
			return false;
		}
		auto &file = list.files.front();
		if (file.type == Ui::PreparedFile::Type::Photo) {
			startPreparedPhotoUpload(media, std::move(file));
			return true;
		} else if (file.type == Ui::PreparedFile::Type::Video) {
			startPreparedVideoUpload(media, std::move(file));
			return true;
		}
		return false;
	});
	const auto validatePhotoOrVideo = ValidateFn([](
			not_null<const QMimeData*> data) {
		if (data->hasImage()) {
			return true;
		}
		const auto urls = Core::ReadMimeUrls(data);
		if (urls.size() != 1 || !urls.front().isLocalFile()) {
			return false;
		}
		const auto file = Platform::File::UrlToLocal(urls.front());
		const auto mime = Core::MimeTypeForFile(QFileInfo(file)).name();
		return Core::IsMimeAcceptedForPhotoVideoAlbum(mime);
	});
	const auto installPhotoDropToWidget = [=](
			not_null<QWidget*> widget,
			std::shared_ptr<PollMediaState> media) {
		installDropToWidget(
			widget,
			media,
			validatePhotoOrVideo,
			applyPhotoOrVideoDrop);
	};
	const auto installPhotoDropToField = [=](
			not_null<Ui::InputField*> field,
			std::shared_ptr<PollMediaState> media) {
		installDropToField(
			field,
			media,
			validatePhotoOrVideo,
			applyPhotoOrVideoDrop);
	};
	const auto applyFileDrop = ApplyDropFn([=](
			std::shared_ptr<PollMediaState> media,
			not_null<const QMimeData*> data) {
		auto list = FileListFromMimeData(
			data,
			_controller->session().premium());
		if (list.error == Ui::PreparedList::Error::TooLargeFile) {
			const auto fileSize = list.files.empty()
				? 0
				: list.files.front().size;
			_controller->show(Box(
				FileSizeLimitBox,
				&_controller->session(),
				fileSize,
				nullptr));
			return false;
		}
		if (list.error != Ui::PreparedList::Error::None
			|| list.files.empty()) {
			return false;
		}
		auto &file = list.files.front();
		if (file.type == Ui::PreparedFile::Type::Photo) {
			startPreparedPhotoUpload(media, std::move(file));
		} else if (file.type == Ui::PreparedFile::Type::Video) {
			startPreparedVideoUpload(media, std::move(file));
		} else {
			startPreparedDocumentUpload(media, std::move(file));
		}
		return true;
	});
	const auto validateFile = ValidateFn(ValidateFileDragData);
	const auto choosePhotoOrVideo = [=](
			std::shared_ptr<PollMediaState> media) {
		const auto callback = crl::guard(this, [=](
				FileDialog::OpenResult &&result) {
			const auto checkResult = [&](const Ui::PreparedList &list) {
				using namespace Ui;
				if (list.files.size() != 1) {
					return false;
				}
				const auto type = list.files.front().type;
				return (type == PreparedFile::Type::Photo)
					|| (type == PreparedFile::Type::Video);
			};
			const auto showError = [=](tr::phrase<> text) {
				showToast(text(tr::now));
			};
			auto list = Storage::PreparedFileFromFilesDialog(
				std::move(result),
				checkResult,
				showError,
				st::sendMediaPreviewSize,
				_controller->session().premium());
			if (!list) {
				return;
			}
			auto &file = list->files.front();
			if (file.type == Ui::PreparedFile::Type::Photo) {
				applyPreparedPhotoList(media, std::move(*list));
			} else {
				startPreparedVideoUpload(media, std::move(file));
			}
		});
		FileDialog::GetOpenPath(
			this,
			tr::lng_attach_photo_or_video(tr::now),
			FileDialog::PhotoVideoFilesFilter(),
			callback);
	};
	const auto chooseDocument = [=](std::shared_ptr<PollMediaState> media) {
		const auto callback = crl::guard(this, [=](
				FileDialog::OpenResult &&result) {
			if (result.paths.isEmpty()) {
				return;
			}
			auto list = Storage::PrepareMediaList(
				result.paths.mid(0, 1),
				st::sendMediaPreviewSize,
				_controller->session().premium());
			if (list.error == Ui::PreparedList::Error::TooLargeFile) {
				const auto fileSize = list.files.empty()
					? 0
					: list.files.front().size;
				_controller->show(Box(
					FileSizeLimitBox,
					&_controller->session(),
					fileSize,
					nullptr));
				return;
			} else if (list.error != Ui::PreparedList::Error::None
				|| list.files.empty()) {
				return;
			}
			startPreparedDocumentUpload(
				media,
				std::move(list.files.front()));
		});
		FileDialog::GetOpenPath(
			this,
			tr::lng_attach_file(tr::now),
			FileDialog::AllFilesFilter(),
			callback);
	};
	const auto clearMedia = [=](std::shared_ptr<PollMediaState> media) {
		auto toCancel = std::vector<FullMsgId>();
		for (auto i = state->uploads.begin(); i != state->uploads.end();) {
			if (i->second.media.lock() == media) {
				toCancel.push_back(i->first);
				i = state->uploads.erase(i);
			} else {
				++i;
			}
		}
		for (const auto &id : toCancel) {
			_controller->session().uploader().cancel(id);
		}
		setMedia(media, PollMedia(), nullptr, false);
	};
	const auto chooseLocation = [=](
			std::shared_ptr<PollMediaState> media) {
		const auto session = &_controller->session();
		const auto &appConfig = session->appConfig();
		auto map = appConfig.get<base::flat_map<QString, QString>>(
			u"tdesktop_config_map"_q,
			base::flat_map<QString, QString>());
		const auto config = Ui::LocationPickerConfig{
			.mapsToken = map[u"maps"_q],
			.geoToken = map[u"geo"_q],
		};
		const auto applyGeo = [=](float64 lat, float64 lon) {
			const auto point = Data::LocationPoint(
				lat,
				lon,
				Data::LocationPoint::NoAccessHash);
			auto pollMedia = PollMedia();
			pollMedia.geo = point;
			const auto cloudImage = session->data().location(point);
			auto thumbnail = Ui::MakeGeoThumbnailWithPin(
				cloudImage,
				session,
				Data::FileOrigin());
			setMedia(media, pollMedia, std::move(thumbnail), true);
		};
		if (base::IsCtrlPressed()) {
			const auto lat = 48.8566 + base::RandomValue<uint32>()
				/ float64(std::numeric_limits<uint32>::max()) * 0.02 - 0.01;
			const auto lon = 2.3522 + base::RandomValue<uint32>()
				/ float64(std::numeric_limits<uint32>::max()) * 0.02 - 0.01;
			applyGeo(lat, lon);
			return;
		}
		if (!Ui::LocationPicker::Available(config)) {
			return;
		}
		Ui::LocationPicker::Show({
			.parent = _controller->widget().get(),
			.config = config,
			.chooseLabel = tr::lng_maps_point_send(),
			.session = session,
			.callback = crl::guard(this, [=](Data::InputVenue venue) {
				applyGeo(venue.lat, venue.lon);
			}),
			.quit = [] { Shortcuts::Launch(Shortcuts::Command::Quit); },
			.storageId = session->local().resolveStorageIdBots(),
			.closeRequests = _controller->content()->death(),
		});
	};
	const auto showMediaMenu = [=](
			not_null<Ui::RpWidget*> button,
			std::shared_ptr<PollMediaState> media,
			bool allowDocuments = false,
			bool allowStickers = true) {
		if (HistoryView::ShowPollMediaPreview(_controller, media, {
			.choosePhotoOrVideo = [=] { choosePhotoOrVideo(media); },
			.chooseDocument = [=] { chooseDocument(media); },
			.chooseSticker = [=] {
				showStickerPanel(button, media);
			},
			.editPhoto = crl::guard(this, [=](Ui::PreparedList list) {
				applyPreparedPhotoList(media, std::move(list));
			}),
			.remove = [=] { clearMedia(media); },
		})) {
			return;
		}
		state->mediaMenu = base::make_unique_q<Ui::PopupMenu>(
			button,
			st::popupMenuWithIcons);
		state->mediaMenu->setForcedOrigin(
			Ui::PanelAnimation::Origin::TopRight);
		state->mediaMenu->addAction(
			tr::lng_attach_photo_or_video(tr::now),
			[=] { choosePhotoOrVideo(media); },
			&st::menuIconPhoto);
		if (allowDocuments) {
			state->mediaMenu->addAction(
				tr::lng_attach_file(tr::now),
				[=] { chooseDocument(media); },
				&st::menuIconFile);
		}
		{
			const auto &appConfig = _controller->session().appConfig();
			auto map = appConfig.get<base::flat_map<QString, QString>>(
				u"tdesktop_config_map"_q,
				base::flat_map<QString, QString>());
			const auto config = Ui::LocationPickerConfig{
				.mapsToken = map[u"maps"_q],
				.geoToken = map[u"geo"_q],
			};
			if (Ui::LocationPicker::Available(config)) {
				state->mediaMenu->addAction(
					tr::lng_maps_point(tr::now),
					[=] { chooseLocation(media); },
					&st::menuIconAddress);
			}
		}
		if (allowStickers) {
			state->mediaMenu->addAction(
				tr::lng_chat_intro_choose_sticker(tr::now),
				[=] { showStickerPanel(button, media); },
				&st::menuIconStickers);
		}
		if (media->media || media->uploading) {
			state->mediaMenu->addAction(
				tr::lng_box_remove(tr::now),
				[=] { clearMedia(media); },
				&st::menuIconDelete);
		}
		state->mediaMenu->popup(QCursor::pos());
	};
	const auto addMediaButton = [=](
			not_null<Ui::InputField*> field,
			std::shared_ptr<PollMediaState> media) {
		const auto button = Ui::CreateChild<PollMediaButton>(
			field,
			st::pollAttach,
			media);
		button->show();
		installDropToField(field, media, validateFile, applyFileDrop);
		installDropToWidget(button, media, validateFile, applyFileDrop);
		field->sizeValue(
		) | rpl::on_next([=](QSize size) {
			button->moveToRight(
				st::createPollAttachPosition.x(),
				st::createPollAttachPosition.y(),
				size.width());
		}, button->lifetime());
		button->clicks(
		) | rpl::on_next([=](Qt::MouseButton buttonType) {
			if (buttonType != Qt::LeftButton) {
				return;
			}
			showMediaMenu(button, media, true, false);
		}, button->lifetime());
	};

	const auto question = setupQuestion(container);
	const auto description = setupDescription(container);
	addMediaButton(description, state->descriptionMedia);
	Ui::AddDivider(container);
	Ui::AddSkip(container);
	container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			tr::lng_polls_create_options(),
			st::defaultSubsectionTitle),
		st::createPollFieldTitlePadding);
	state->options = std::make_unique<Options>(
		this,
		container,
		_controller,
		_emojiPanel ? _emojiPanel.get() : nullptr,
		(_chosen & PollData::Flag::Quiz),
		showMediaMenu,
		installPhotoDropToField,
		installPhotoDropToWidget);
	const auto options = state->options.get();
	auto limit = options->usedCount() | rpl::after_next([=](int count) {
		setCloseByEscape(!count);
		setCloseByOutsideClick(!count);
	}) | rpl::map([=](int count) {
		const auto appConfig = &_controller->session().appConfig();
		const auto max = appConfig->pollOptionsLimit();
		return (count < max)
			? tr::lng_polls_create_limit(tr::now, lt_count, max - count)
			: tr::lng_polls_create_maximum(tr::now);
	}) | rpl::after_next([=] {
		container->resizeToWidth(container->widthNoMargins());
	});
	container->add(
		object_ptr<Ui::DividerLabel>(
			container,
			object_ptr<Ui::FlatLabel>(
				container,
				std::move(limit),
				st::boxDividerLabel),
			st::createPollLimitPadding));

	question->tabbed(
	) | rpl::on_next([=](not_null<bool*> handled) {
		description->setFocus();
		*handled = true;
	}, question->lifetime());

	description->tabbed(
	) | rpl::on_next([=](not_null<bool*> handled) {
		options->focusFirst();
		*handled = true;
	}, description->lifetime());

	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, tr::lng_polls_create_settings());

	const auto showWhoVoted = (!(_disabled & PollData::Flag::PublicVotes))
		? AddPollToggleButton(
			container,
			tr::lng_polls_create_show_who_voted(),
			tr::lng_polls_create_show_who_voted_about(),
			{
				.icon = &st::pollBoxFilledPollViewIcon,
				.background = &st::settingsIconBg4,
			},
			rpl::single(!!(_chosen & PollData::Flag::PublicVotes))
				| rpl::then(state->showWhoVotedForceOn.events()),
			st::detailedSettingsButtonStyle).get()
		: nullptr;
	const auto multiple = AddPollToggleButton(
		container,
		tr::lng_polls_create_allow_multiple_answers(),
		tr::lng_polls_create_allow_multiple_answers_about(),
		{
			.icon = &st::pollBoxFilledPollMultipleIcon,
			.background = &st::settingsIconBg3,
		},
		rpl::single(!!(_chosen & PollData::Flag::MultiChoice))
			| rpl::then(state->multipleForceOff.events()),
		st::detailedSettingsButtonStyle);
	const auto addOptions = (!(_disabled & PollData::Flag::OpenAnswers))
		? AddPollToggleButton(
			container,
			tr::lng_polls_create_allow_adding_options(),
			tr::lng_polls_create_allow_adding_options_about(),
			{
				.icon = &st::pollBoxFilledPollAddIcon,
				.background = &st::settingsIconBg4,
			},
			rpl::single(!!(_chosen & PollData::Flag::OpenAnswers))
				| rpl::then(state->addOptionsForceOff.events()),
			st::detailedSettingsButtonStyle).get()
		: nullptr;
	const auto revoting = AddPollToggleButton(
		container,
		tr::lng_polls_create_allow_revoting(),
		tr::lng_polls_create_allow_revoting_about(),
		{
			.icon = &st::pollBoxFilledPollRevoteIcon,
			.background = &st::settingsIconBg6,
		},
		rpl::single(!(_chosen & PollData::Flag::RevotingDisabled))
			| rpl::then(state->revotingForceOff.events()),
		st::detailedSettingsButtonStyle);
	const auto shuffle = AddPollToggleButton(
		container,
		tr::lng_polls_create_shuffle_options(),
		tr::lng_polls_create_shuffle_options_about(),
		{
			.icon = &st::pollBoxFilledPollShuffleIcon,
			.background = &st::settingsIconBg8,
		},
		rpl::single(!!(_chosen & PollData::Flag::ShuffleAnswers)),
		st::detailedSettingsButtonStyle);
	const auto quiz = AddPollToggleButton(
		container,
		tr::lng_polls_create_set_correct_answer(),
		rpl::single(multiple->toggled()) | rpl::then(
			multiple->toggledChanges()
		) | rpl::map([](bool multi) {
			return multi
				? tr::lng_polls_create_set_correct_answer_about_multi(
					tr::now)
				: tr::lng_polls_create_set_correct_answer_about(tr::now);
		}),
		{
			.icon = &st::pollBoxFilledPollCorrectIcon,
			.background = &st::settingsIconBg2,
		},
		rpl::single(!!(_chosen & PollData::Flag::Quiz))
			| rpl::then(state->quizForceOff.events()),
		st::detailedSettingsButtonStyle);

	const auto duration = AddPollToggleButton(
		container,
		tr::lng_polls_create_limit_duration(),
		tr::lng_polls_create_limit_duration_about(),
		{
			.icon = &st::pollBoxFilledPollDeadlineIcon,
			.background = &st::settingsIconBg1,
		},
		rpl::single(false),
		st::detailedSettingsButtonStyle);

	const auto durationWrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container))
	)->toggleOn(
		rpl::single(duration->toggled())
			| rpl::then(duration->toggledChanges()));
	const auto durationInner = durationWrap->entity();

	auto pollEndsLabelText = state->closePeriod.value(
	) | rpl::map([=](int) {
		const auto date = state->closeDate.current();
		if (date > 0) {
			return langDateTime(base::unixtime::parse(date));
		}
		const auto period = state->closePeriod.current();
		if (period > 0) {
			const auto target = base::unixtime::now() + period;
			return langDateTime(base::unixtime::parse(target));
		}
		return QString();
	});
	state->closeDate.value(
	) | rpl::on_next([=](TimeId) {
		state->closePeriod.force_assign(state->closePeriod.current());
	}, durationInner->lifetime());

	const auto pollEndsLabel = AddButtonWithLabel(
		durationInner,
		tr::lng_polls_create_poll_ends(),
		std::move(pollEndsLabelText),
		st::settingsButtonNoIcon);

	const auto show = uiShow();
	pollEndsLabel->setClickedCallback([=] {
		state->durationMenu = base::make_unique_q<Ui::PopupMenu>(
			pollEndsLabel,
			st::popupMenuWithIcons);
		const auto &menuSt = state->durationMenu->st().menu;
		const auto presets = {
			3600,
			3 * 3600,
			8 * 3600,
			24 * 3600,
			72 * 3600,
		};
		for (const auto seconds : presets) {
			const auto text = Ui::FormatMuteFor(seconds);
			auto item = base::make_unique_q<DurationIconAction>(
				state->durationMenu->menu(),
				menuSt,
				Ui::Menu::CreateAction(
					state->durationMenu->menu().get(),
					text,
					[=] {
						state->closePeriod = seconds;
						state->closeDate = TimeId(0);
					}),
				Ui::FormatTTLTiny(seconds));
			state->durationMenu->addAction(std::move(item));
		}
		state->durationMenu->addAction(
			tr::lng_polls_create_duration_custom(tr::now),
			[=] {
				const auto now = base::unixtime::now();
				const auto current = (state->closeDate.current() > now)
					? state->closeDate.current()
					: (state->closePeriod.current() > 0)
					? (now + state->closePeriod.current())
					: (now + 24 * 3600);
				show->show(Box([=](not_null<Ui::GenericBox*> box) {
					Ui::ChooseDateTimeBox(box, {
						.title = tr::lng_polls_create_deadline_title(),
						.submit = tr::lng_polls_create_deadline_button(),
						.done = [=](TimeId time) {
							state->closeDate = time;
							state->closePeriod = 0;
							box->closeBox();
						},
						.min = [=] { return base::unixtime::now() + 60; },
						.time = current,
						.max = [=] {
							return base::unixtime::now() + 365 * 24 * 3600;
						},
					});
				}));
			},
			&st::menuIconCustomize);
		state->durationMenu->popup(QCursor::pos());
	});

	duration->toggledChanges(
	) | rpl::on_next([=](bool checked) {
		if (checked && state->closePeriod.current() == 0
				&& state->closeDate.current() == 0) {
			state->closePeriod = 24 * 3600;
		}
	}, duration->lifetime());

	const auto hideResults = durationInner->add(
		object_ptr<Ui::SettingsButton>(
			durationInner,
			tr::lng_polls_create_hide_results(),
			st::settingsButtonNoIcon)
	)->toggleOn(rpl::single(false));

	Ui::AddSkip(durationInner);
	Ui::AddDividerText(
		durationInner,
		tr::lng_polls_create_hide_results_about());

	const auto solution = setupSolution(
		container,
		rpl::single(quiz->toggled()) | rpl::then(quiz->toggledChanges()));
	addMediaButton(solution, state->solutionMedia);

	options->tabbed(
	) | rpl::on_next([=] {
		if (quiz->toggled()) {
			solution->setFocus();
		} else {
			question->setFocus();
		}
	}, question->lifetime());

	solution->tabbed(
	) | rpl::on_next([=](not_null<bool*> handled) {
		question->setFocus();
		*handled = true;
	}, solution->lifetime());

	const auto updateAddOptionsLocked = [=] {
		if (addOptions) {
			const auto locked = (_disabled & PollData::Flag::OpenAnswers)
				|| quiz->toggled()
				|| (showWhoVoted && !showWhoVoted->toggled());
			addOptions->setToggleLocked(locked);
			if (locked) {
				state->addOptionsForceOff.fire(false);
			}
		}
	};
	const auto updateQuizDependentLocks = [=](bool checked) {
		updateAddOptionsLocked();
		revoting->setToggleLocked(
			_disabled & PollData::Flag::RevotingDisabled);
	};
	quiz->setToggleLocked(_disabled & PollData::Flag::Quiz);
	shuffle->setToggleLocked(_disabled & PollData::Flag::ShuffleAnswers);
	updateQuizDependentLocks(quiz->toggled());

	using namespace rpl::mappers;
	quiz->toggledChanges(
	) | rpl::on_next([=](bool checked) {
		if (checked && (_disabled & PollData::Flag::Quiz)) {
			state->quizForceOff.fire(false);
			return;
		}
		if (checked) {
			state->addOptionsForceOff.fire(false);
			state->revotingForceOff.fire(false);
			solution->setFocus();
		}
		updateQuizDependentLocks(checked);
		options->enableChooseCorrect(checked, multiple->toggled());
	}, quiz->lifetime());

	multiple->toggledChanges(
	) | rpl::on_next([=](bool checked) {
		if (quiz->toggled()) {
			options->enableChooseCorrect(true, checked);
		}
	}, multiple->lifetime());

	if (addOptions && showWhoVoted) {
		updateAddOptionsLocked();
		showWhoVoted->toggledChanges(
		) | rpl::on_next([=](bool) {
			updateAddOptionsLocked();
		}, showWhoVoted->lifetime());
	}

	const auto isValidQuestion = [=] {
		const auto text = question->getLastText().trimmed();
		return !text.isEmpty() && (text.size() <= kQuestionLimit);
	};
	question->submits(
	) | rpl::on_next([=] {
		if (isValidQuestion()) {
			description->setFocus();
		}
	}, question->lifetime());

	description->submits(
	) | rpl::on_next([=] {
		options->focusFirst();
	}, description->lifetime());

	_setInnerFocus = [=] {
		question->setFocusFast();
	};

	const auto collectResult = [=] {
		const auto textWithTags = question->getTextWithAppliedMarkdown();
		const auto descriptionWithTags = description->getTextWithTags();
		using Flag = PollData::Flag;
		auto result = PollData(&_controller->session().data(), id);
		result.question.text = textWithTags.text;
		result.question.entities = TextUtilities::ConvertTextTagsToEntities(
			textWithTags.tags);
		TextUtilities::Trim(result.question);
		result.answers = options->toPollAnswers();
		const auto solutionWithTags = quiz->toggled()
			? solution->getTextWithAppliedMarkdown()
			: TextWithTags();
		result.solution = TextWithEntities{
			solutionWithTags.text,
			TextUtilities::ConvertTextTagsToEntities(solutionWithTags.tags)
		};
		result.attachedMedia = state->descriptionMedia->media;
		if (quiz->toggled()) {
			result.solutionMedia = state->solutionMedia->media;
		}
		if (duration->toggled()) {
			const auto closeDate = state->closeDate.current();
			const auto closePeriod = state->closePeriod.current();
			if (closeDate > 0) {
				result.closeDate = closeDate;
				result.closePeriod = closeDate - base::unixtime::now();
			} else if (closePeriod > 0) {
				result.closePeriod = closePeriod;
				result.closeDate = base::unixtime::now() + closePeriod;
			}
		}
		const auto publicVotes = (showWhoVoted && showWhoVoted->toggled());
		const auto multiChoice = multiple->toggled();
		const auto hideResultsEnabled = duration->toggled()
			&& hideResults->toggled();
		result.setFlags(Flag(0)
			| (publicVotes ? Flag::PublicVotes : Flag(0))
			| (multiChoice ? Flag::MultiChoice : Flag(0))
			| ((addOptions && addOptions->toggled()) ? Flag::OpenAnswers : Flag(0))
			| (!revoting->toggled() ? Flag::RevotingDisabled : Flag(0))
			| (shuffle->toggled() ? Flag::ShuffleAnswers : Flag(0))
			| (quiz->toggled() ? Flag::Quiz : Flag(0))
			| (hideResultsEnabled
				? Flag::HideResultsUntilClose
				: Flag(0)));
		auto text = TextWithEntities{
			descriptionWithTags.text,
			TextUtilities::ConvertTextTagsToEntities(
				descriptionWithTags.tags),
		};
		TextUtilities::Trim(text);
		return Result{
			std::move(result),
			std::move(text),
			Api::SendOptions(),
		};
	};
	const auto collectError = [=] {
		if (isValidQuestion()) {
			state->error &= ~Error::Question;
		} else {
			state->error |= Error::Question;
		}
		if (!options->hasOptions()) {
			state->error |= Error::Options;
		} else if (!options->isValid()) {
			state->error |= Error::Other;
		} else {
			state->error &= ~(Error::Options | Error::Other);
		}
		if (quiz->toggled() && !options->hasCorrect()) {
			state->error |= Error::Correct;
		} else {
			state->error &= ~Error::Correct;
		}
		if (quiz->toggled()
			&& solution->getLastText().trimmed().size() > kSolutionLimit) {
			state->error |= Error::Solution;
		} else {
			state->error &= ~Error::Solution;
		}
		if (state->descriptionMedia->uploading
			|| (quiz->toggled() && state->solutionMedia->uploading)
			|| options->hasUploadingMedia()) {
			state->error |= Error::Media;
		} else {
			state->error &= ~Error::Media;
		}
		if (duration->toggled()) {
			const auto now = base::unixtime::now();
			const auto closeDate = state->closeDate.current();
			const auto closePeriod = state->closePeriod.current();
			const auto deadline = (closeDate > 0)
				? closeDate
				: (closePeriod > 0)
				? (now + closePeriod)
				: 0;
			if (deadline > 0 && deadline <= now) {
				state->error |= Error::Deadline;
			} else {
				state->error &= ~Error::Deadline;
			}
		} else {
			state->error &= ~Error::Deadline;
		}
	};
	const auto showError = [show = uiShow()](
			tr::phrase<> text) {
		show->showToast(text(tr::now));
	};
	_refreshExpiredMedia = [=] {
		const auto forceRefresh = [](
				const std::shared_ptr<PollMediaState> &m) {
			if (m->media && m->reupload) {
				m->reupload();
			}
		};
		forceRefresh(state->descriptionMedia);
		forceRefresh(state->solutionMedia);
		options->refreshStaleMedia(0);
	};
	const auto send = [=](Api::SendOptions sendOptions) {
		const auto kStaleTimeout = kMediaUploadMaxAge;
		auto refreshedAny = false;
		const auto tryRefresh = [&](
				const std::shared_ptr<PollMediaState> &m) {
			if (m->media
				&& m->uploadedAt > 0
				&& (crl::now() - m->uploadedAt > kStaleTimeout)
				&& m->reupload) {
				m->reupload();
				refreshedAny = true;
			}
		};
		tryRefresh(state->descriptionMedia);
		if (quiz->toggled()) {
			tryRefresh(state->solutionMedia);
		}
		if (options->refreshStaleMedia(kStaleTimeout)) {
			refreshedAny = true;
		}
		if (refreshedAny) {
			collectError();
			if (state->error & Error::Media) {
				ShowMediaUploadingToast();
			}
			return;
		}
		collectError();
		if (state->error & Error::Question) {
			showError(tr::lng_polls_choose_question);
			question->setFocus();
		} else if (state->error & Error::Options) {
			showError(tr::lng_polls_choose_answers);
			options->focusFirst();
		} else if (state->error & Error::Correct) {
			showError(tr::lng_polls_choose_correct);
			scrollToWidget(options->layoutWidget());
		} else if (state->error & Error::Solution) {
			solution->showError();
		} else if (state->error & Error::Media) {
			ShowMediaUploadingToast();
		} else if (state->error & Error::Deadline) {
			showError(tr::lng_polls_create_deadline_expired);
		} else if (!state->error) {
			auto result = collectResult();
			result.options = sendOptions;
			_submitRequests.fire(std::move(result));
		}
	};
	const auto sendAction = SendMenu::DefaultCallback(
		_controller->uiShow(),
		crl::guard(this, send));

	options->scrollToWidget(
	) | rpl::on_next([=](not_null<QWidget*> widget) {
		scrollToWidget(widget);
	}, lifetime());

	options->backspaceInFront(
	) | rpl::on_next([=] {
		FocusAtEnd(description);
	}, lifetime());

	const auto isNormal = (_sendType == Api::SendType::Normal);
	const auto schedule = [=] {
		sendAction(
			{ .type = SendMenu::ActionType::Schedule },
			_sendMenuDetails());
	};
	const auto submit = addButton(
		tr::lng_polls_create_button(),
		[=] { isNormal ? send({}) : schedule(); });
	submit->setText(PaidSendButtonText(_starsRequired.value(), isNormal
		? tr::lng_polls_create_button()
		: tr::lng_schedule_button()));
	const auto sendMenuDetails = [=] {
		collectError();
		return (state->error) ? SendMenu::Details() : _sendMenuDetails();
	};
	SendMenu::SetupMenuAndShortcuts(
		submit.data(),
		_controller->uiShow(),
		sendMenuDetails,
		sendAction);
	addButton(tr::lng_cancel(), [=] { closeBox(); });

	if (showWhoVoted) {
		showWhoVoted->finishAnimating();
	}
	multiple->finishAnimating();
	if (addOptions) {
		addOptions->finishAnimating();
	}
	revoting->finishAnimating();
	shuffle->finishAnimating();
	quiz->finishAnimating();
	duration->finishAnimating();
	durationWrap->finishAnimating();
	hideResults->finishAnimating();

	return result;
}

void CreatePollBox::prepare() {
	setTitle(tr::lng_polls_create_title());

	const auto inner = setInnerWidget(setupContent());

	setDimensionsToContent(st::boxWideWidth, inner);

	Ui::SetStickyBottomScroll(this, inner);
}
