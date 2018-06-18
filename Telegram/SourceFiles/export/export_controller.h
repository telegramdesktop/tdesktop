/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <crl/crl_object_on_queue.h>
#include "base/variant.h"
#include "mtproto/rpc_sender.h"

namespace Export {

class Controller;
struct Settings;

struct PasswordCheckState {
	QString hint;
	QString unconfirmedPattern;
	bool requesting = true;
	bool hasPassword = false;
	bool checked = false;

};

struct ProcessingState {
	enum class Step {
		Initializing,
		LeftChannelsList,
		DialogsList,
		PersonalInfo,
		Userpics,
		Contacts,
		Sessions,
		LeftChannels,
		Dialogs,
	};
	enum class Item {
		Other,
		Photo,
		Video,
		VoiceMessage,
		VideoMessage,
		Sticker,
		GIF,
		File,
	};

	Step step = Step::Initializing;

	std::vector<int> substepsInStep;

	int entityIndex = 0;
	int entityCount = 1;
	QString entityName;

	int itemIndex = 0;
	int itemCount = 0;
	Item itemType = Item::Other;
	QString itemName;
	QString itemId;

	int bytesLoaded = 0;
	int bytesCount = 0;
	QString objectId;

};

struct ErrorState {
	enum class Type {
		Unknown,
		API,
		IO,
	};
	Type type = Type::Unknown;
	base::optional<RPCError> apiError;
	base::optional<QString> ioErrorPath;

};

struct FinishedState {
	QString path;

};

using State = base::optional_variant<
	PasswordCheckState,
	ProcessingState,
	ErrorState,
	FinishedState>;

//struct PasswordUpdate {
//	enum class Type {
//		CheckSucceed,
//		WrongPassword,
//		FloodLimit,
//		RecoverUnavailable,
//	};
//	Type type = Type::WrongPassword;
//
//};

class ControllerWrap {
public:
	ControllerWrap();

	rpl::producer<State> state() const;

	// Password step.
	//void submitPassword(const QString &password);
	//void requestPasswordRecover();
	//rpl::producer<PasswordUpdate> passwordUpdate() const;
	//void reloadPasswordState();
	//void cancelUnconfirmedPassword();

	// Processing step.
	void startExport(const Settings &settings);

	rpl::lifetime &lifetime();

	~ControllerWrap();

private:
	crl::object_on_queue<Controller> _wrapped;
	rpl::lifetime _lifetime;

};

} // namespace Export
