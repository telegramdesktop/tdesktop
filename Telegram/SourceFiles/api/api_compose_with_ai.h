/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"
#include "ui/text/text_entity.h"

#include <optional>
#include <vector>

class ApiWrap;

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class InputField;
class Show;
} // namespace Ui

namespace Api {

class ComposeWithAi final {
public:
	struct ToneRef {
		QString defaultTone;
		uint64 id = 0;
		uint64 accessHash = 0;
	};

	struct Request {
		TextWithEntities text;
		QString translateToLang;
		std::optional<ToneRef> tone;
		bool proofread = false;
		bool emojify = false;

		void setDefaultTone(const QString &type) {
			tone = ToneRef{ .defaultTone = type };
		}
		void setCustomTone(uint64 id, uint64 accessHash) {
			tone = ToneRef{ .id = id, .accessHash = accessHash };
		}
	};

	struct DiffEntity {
		enum class Type {
			Insert,
			Replace,
			Delete,
		};

		Type type = Type::Insert;
		int offset = 0;
		int length = 0;
		QString oldText;
	};

	struct Diff {
		TextWithEntities text;
		std::vector<DiffEntity> entities;
	};

	struct Result {
		TextWithEntities resultText;
		std::optional<Diff> diffText;
	};

	explicit ComposeWithAi(not_null<ApiWrap*> api);

	[[nodiscard]] mtpRequestId request(
		Request request,
		Fn<void(Result &&)> done,
		Fn<void(const MTP::Error &)> fail = nullptr);
	void cancel(mtpRequestId requestId);

private:
	[[nodiscard]] static Diff ParseDiff(
		not_null<Main::Session*> session,
		const MTPTextWithEntities &text);

	const not_null<Main::Session*> _session;
	MTP::Sender _api;

};

void ApplyAiInPlaceBySlug(
	not_null<Main::Session*> session,
	TextWithEntities text,
	QString slug,
	Fn<void(TextWithEntities)> done,
	Fn<void(const MTP::Error &)> fail = nullptr);

[[nodiscard]] QString AiApplyBoundSlug();
void SetAiApplyBoundSlug(const QString &slug);
void ClearAiApplyBoundSlug();

[[nodiscard]] QString AiApplyShortcutText();

void TriggerAiApplyInPlace(
	not_null<Main::Session*> session,
	std::shared_ptr<Ui::Show> show,
	not_null<QObject*> guard,
	not_null<Ui::InputField*> field,
	TextWithEntities fullFieldText,
	Fn<void(TextWithTags textWithTags, int cursor)> applyToField);

} // namespace Api
