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

enum LangKey : int;

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

class EditPrivacyBox : public BoxContent, private MTP::Sender {
public:
	using Value = ApiWrap::Privacy;
	using Option = Value::Option;
	enum class Exception {
		Always,
		Never,
	};

	class Controller {
	public:
		using Key = ApiWrap::Privacy::Key;

		virtual Key key() = 0;
		virtual MTPInputPrivacyKey apiKey() = 0;

		virtual QString title() = 0;
		virtual bool hasOption(Option option) {
			return true;
		}
		virtual LangKey optionsTitleKey() = 0;
		virtual LangKey optionLabelKey(Option option);
		virtual rpl::producer<QString> warning() {
			return rpl::never<QString>();
		}
		virtual LangKey exceptionButtonTextKey(Exception exception) = 0;
		virtual QString exceptionBoxTitle(Exception exception) = 0;
		virtual rpl::producer<QString> exceptionsDescription() = 0;

		virtual void confirmSave(
				bool someAreDisallowed,
				FnMut<void()> saveCallback) {
			saveCallback();
		}

		virtual ~Controller() = default;

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

	EditPrivacyBox(
		QWidget*,
		std::unique_ptr<Controller> controller,
		const Value &value);

protected:
	void prepare() override;

private:
	bool showExceptionLink(Exception exception) const;
	void setupContent();
	QVector<MTPInputPrivacyRule> collectResult();

	Ui::Radioenum<Option> *addOption(
		not_null<Ui::VerticalLayout*> container,
		const std::shared_ptr<Ui::RadioenumGroup<Option>> &group,
		Option option);
	Ui::FlatLabel *addLabel(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> text);

	void editExceptionUsers(Exception exception, Fn<void()> done);
	std::vector<not_null<UserData*>> &exceptionUsers(Exception exception);

	std::unique_ptr<Controller> _controller;
	Value _value;

};
