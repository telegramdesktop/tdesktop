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
struct Environment;

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
		OtherData,
		LeftChannels,
		Dialogs,
	};
	enum class FileType {
		None,
		Photo,
		Video,
		VoiceMessage,
		VideoMessage,
		Sticker,
		GIF,
		File,
	};
	enum class EntityType {
		Chat,
		SavedMessages,
		Other,
	};

	Step step = Step::Initializing;

	int substepsPassed = 0;
	int substepsNow = 0;
	int substepsTotal = 0;

	EntityType entityType = EntityType::Other;
	QString entityName;
	int entityIndex = 0;
	int entityCount = 0;

	int itemIndex = 0;
	int itemCount = 0;

	FileType bytesType = FileType::None;
	QString bytesName;
	int bytesLoaded = 0;
	int bytesCount = 0;
};

struct ApiErrorState {
	RPCError data;
};

struct OutputErrorState {
	QString path;
};

struct CancelledState {
};

struct FinishedState {
	QString path;
	int filesCount = 0;
	int64 bytesCount = 0;
};

using State = base::optional_variant<
	PasswordCheckState,
	ProcessingState,
	ApiErrorState,
	OutputErrorState,
	CancelledState,
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
	void startExport(
		const Settings &settings,
		const Environment &environment);
	void cancelExportFast();

	rpl::lifetime &lifetime();

	~ControllerWrap();

private:
	crl::object_on_queue<Controller> _wrapped;
	rpl::lifetime _lifetime;

};

} // namespace Export
