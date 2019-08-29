/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "mtproto/sender.h"
#include "apiwrap.h"

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

class EditPrivacyBox;

class EditPrivacyController {
public:
	using Key = ApiWrap::Privacy::Key;
	using Option = ApiWrap::Privacy::Option;
	enum class Exception {
		Always,
		Never,
	};

	[[nodiscard]] virtual Key key() = 0;
	[[nodiscard]] virtual MTPInputPrivacyKey apiKey() = 0;

	[[nodiscard]] virtual rpl::producer<QString> title() = 0;
	[[nodiscard]] virtual bool hasOption(Option option) {
		return true;
	}
	[[nodiscard]] virtual rpl::producer<QString> optionsTitleKey() = 0;
	[[nodiscard]] virtual QString optionLabel(Option option);
	[[nodiscard]] virtual rpl::producer<QString> warning() {
		return nullptr;
	}
	[[nodiscard]] virtual rpl::producer<QString> exceptionButtonTextKey(
		Exception exception) = 0;
	[[nodiscard]] virtual rpl::producer<QString> exceptionBoxTitle(
		Exception exception) = 0;
	[[nodiscard]] virtual auto exceptionsDescription()
		-> rpl::producer<QString> = 0;

	[[nodiscard]] virtual object_ptr<Ui::RpWidget> setupAboveWidget(
			not_null<QWidget*> parent,
			rpl::producer<Option> option) {
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
			not_null<QWidget*> parent) {
		return { nullptr };
	}

	virtual void confirmSave(
			bool someAreDisallowed,
			FnMut<void()> saveCallback) {
		saveCallback();
	}
	virtual void saveAdditional() {
	}

	virtual ~EditPrivacyController() = default;

protected:
	EditPrivacyBox *view() const {
		return _view;
	}

private:
	void setView(EditPrivacyBox *box) {
		_view = box;
	}

	EditPrivacyBox *_view = nullptr;

	friend class EditPrivacyBox;

};

class EditPrivacyBox : public BoxContent, private MTP::Sender {
public:
	using Value = ApiWrap::Privacy;
	using Option = Value::Option;
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
	QVector<MTPInputPrivacyRule> collectResult();

	Ui::FlatLabel *addLabel(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> text);

	void editExceptions(Exception exception, Fn<void()> done);
	std::vector<not_null<PeerData*>> &exceptions(Exception exception);

	const not_null<Window::SessionController*> _window;
	std::unique_ptr<EditPrivacyController> _controller;
	Value _value;

};
