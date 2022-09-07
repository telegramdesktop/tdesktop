/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/box_content.h"
#include "mtproto/sender.h"
#include "api/api_user_privacy.h"

namespace Ui {
class VerticalLayout;
class FlatLabel;
class LinkButton;
template <typename Enum>
class RadioenumGroup;
template <typename Enum>
class Radioenum;
template <typename Widget>
class SlideWrap;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

class EditPrivacyBox;

class EditPrivacyController {
public:
	using Key = Api::UserPrivacy::Key;
	using Option = Api::UserPrivacy::Option;
	enum class Exception {
		Always,
		Never,
	};

	[[nodiscard]] virtual Key key() const = 0;

	[[nodiscard]] virtual rpl::producer<QString> title() const = 0;
	[[nodiscard]] virtual bool hasOption(Option option) const {
		return true;
	}
	[[nodiscard]] virtual rpl::producer<QString> optionsTitleKey() const = 0;
	[[nodiscard]] virtual QString optionLabel(Option option) const;
	[[nodiscard]] virtual rpl::producer<TextWithEntities> warning() const {
		return nullptr;
	}
	virtual void prepareWarningLabel(not_null<Ui::FlatLabel*> warning) const {
	}
	[[nodiscard]] virtual rpl::producer<QString> exceptionButtonTextKey(
		Exception exception) const = 0;
	[[nodiscard]] virtual rpl::producer<QString> exceptionBoxTitle(
		Exception exception) const = 0;
	[[nodiscard]] virtual auto exceptionsDescription()
		const -> rpl::producer<QString> = 0;

	[[nodiscard]] virtual object_ptr<Ui::RpWidget> setupAboveWidget(
			not_null<QWidget*> parent,
			rpl::producer<Option> option,
			not_null<QWidget*> outerContainer) {
		return { nullptr };
	}
	[[nodiscard]] virtual object_ptr<Ui::RpWidget> setupMiddleWidget(
			not_null<Window::SessionController*> controller,
			not_null<QWidget*> parent,
			rpl::producer<Option> option) {
		return { nullptr };
	}
	[[nodiscard]] virtual object_ptr<Ui::RpWidget> setupBelowWidget(
			not_null<Window::SessionController*> controller,
			not_null<QWidget*> parent) const {
		return { nullptr };
	}

	virtual void confirmSave(
			bool someAreDisallowed,
			Fn<void()> saveCallback) {
		saveCallback();
	}
	virtual void saveAdditional() {
	}

	virtual ~EditPrivacyController() = default;

protected:
	[[nodiscard]] EditPrivacyBox *view() const {
		return _view;
	}

private:
	void setView(EditPrivacyBox *box) {
		_view = box;
	}

	EditPrivacyBox *_view = nullptr;

	friend class EditPrivacyBox;

};

class EditPrivacyBox final : public Ui::BoxContent {
public:
	using Value = Api::UserPrivacy::Rule;
	using Option = Api::UserPrivacy::Option;
	using Exception = EditPrivacyController::Exception;

	EditPrivacyBox(
		QWidget*,
		not_null<Window::SessionController*> window,
		std::unique_ptr<EditPrivacyController> controller,
		const Value &value);

	static Ui::Radioenum<Option> *AddOption(
		not_null<Ui::VerticalLayout*> container,
		not_null<EditPrivacyController*> controller,
		const std::shared_ptr<Ui::RadioenumGroup<Option>> &group,
		Option option);

protected:
	void prepare() override;

private:
	bool showExceptionLink(Exception exception) const;
	void setupContent();

	Ui::FlatLabel *addLabel(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<TextWithEntities> text,
		int topSkip);
	Ui::FlatLabel *addLabelOrDivider(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<TextWithEntities> text,
		int topSkip);

	void editExceptions(Exception exception, Fn<void()> done);
	std::vector<not_null<PeerData*>> &exceptions(Exception exception);

	const not_null<Window::SessionController*> _window;
	std::unique_ptr<EditPrivacyController> _controller;
	Value _value;

};
