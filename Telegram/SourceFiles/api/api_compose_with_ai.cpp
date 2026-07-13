/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_compose_with_ai.h"

#include "api/api_text_entities.h"
#include "apiwrap.h"
#include "base/options.h"
#include "core/shortcuts.h"
#include "data/data_ai_compose_tones.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/layers/show.h"
#include "ui/widgets/fields/input_field.h"

namespace Api {
namespace {

base::options::option<QString> OptionAiApplyToneSlug({
	.id = "ai-apply-tone-slug",
	.name = "AI apply tone slug",
	.description = "Slug of the AI compose tone bound to the in-place"
		" apply hotkey. Empty means no tone is bound.",
});

[[nodiscard]] MTPTextWithEntities Serialize(
		not_null<Main::Session*> session,
		const TextWithEntities &text) {
	return MTP_textWithEntities(
		MTP_string(text.text),
		EntitiesToMTP(session, text.entities, ConvertOption::SkipLocal));
}

} // namespace

ComposeWithAi::ComposeWithAi(not_null<ApiWrap*> api)
: _session(&api->session())
, _api(&api->instance()) {
}

mtpRequestId ComposeWithAi::request(
		Request request,
		Fn<void(Result &&)> done,
		Fn<void(const MTP::Error &)> fail) {
	using Flag = MTPmessages_composeMessageWithAI::Flag;
	auto flags = MTPmessages_composeMessageWithAI::Flags(0);
	if (request.proofread) {
		flags |= Flag::f_proofread;
	}
	if (!request.translateToLang.isEmpty()) {
		flags |= Flag::f_translate_to_lang;
	}
	if (request.tone) {
		flags |= Flag::f_tone;
	}
	if (request.emojify) {
		flags |= Flag::f_emojify;
	}
	const auto session = _session;
	return _api.request(MTPmessages_ComposeMessageWithAI(
		MTP_flags(flags),
		Serialize(session, request.text),
		request.translateToLang.isEmpty()
			? MTPstring()
			: MTP_string(request.translateToLang),
		request.tone
			? (request.tone->id
				? MTP_inputAiComposeToneID(
					MTP_long(request.tone->id),
					MTP_long(request.tone->accessHash))
				: MTP_inputAiComposeToneDefault(
					MTP_string(request.tone->defaultTone)))
			: MTPInputAiComposeTone()
	)).done([=, done = std::move(done)](
			const MTPmessages_ComposedMessageWithAI &result) mutable {
		const auto &data = result.data();
		auto parsed = Result{
			.resultText = ParseTextWithEntities(session, data.vresult_text()),
		};
		if (const auto diff = data.vdiff_text()) {
			parsed.diffText = ParseDiff(session, *diff);
		}
		done(std::move(parsed));
	}).fail([=, fail = std::move(fail)](const MTP::Error &error) mutable {
		if (fail) {
			fail(error);
		}
	}).send();
}

void ComposeWithAi::cancel(mtpRequestId requestId) {
	if (requestId) {
		_api.request(requestId).cancel();
	}
}

ComposeWithAi::Diff ComposeWithAi::ParseDiff(
		not_null<Main::Session*> session,
		const MTPTextWithEntities &text) {
	const auto &data = text.data();
	auto result = Diff{
		.text = ParseTextWithEntities(session, text),
	};
	const auto &entities = data.ventities().v;
	result.entities.reserve(entities.size());
	for (const auto &entity : entities) {
		entity.match([&](const MTPDmessageEntityDiffInsert &data) {
			result.entities.push_back({
				.type = DiffEntity::Type::Insert,
				.offset = data.voffset().v,
				.length = data.vlength().v,
			});
		}, [&](const MTPDmessageEntityDiffReplace &data) {
			result.entities.push_back({
				.type = DiffEntity::Type::Replace,
				.offset = data.voffset().v,
				.length = data.vlength().v,
				.oldText = qs(data.vold_text()),
			});
		}, [&](const MTPDmessageEntityDiffDelete &data) {
			result.entities.push_back({
				.type = DiffEntity::Type::Delete,
				.offset = data.voffset().v,
				.length = data.vlength().v,
			});
		}, [](const auto &) {
		});
	}
	return result;
}

void ApplyAiInPlaceBySlug(
		not_null<Main::Session*> session,
		TextWithEntities text,
		QString slug,
		Fn<void(TextWithEntities)> done,
		Fn<void(const MTP::Error &)> fail) {
	auto apply = [=, text = std::move(text), done = std::move(done)](
			ComposeWithAi::ToneRef tone) mutable {
		(void)session->api().composeWithAi().request({
			.text = std::move(text),
			.tone = std::move(tone),
		}, [done = std::move(done)](ComposeWithAi::Result &&result) {
			if (done) {
				done(std::move(result.resultText));
			}
		}, fail);
	};
	auto &tones = session->data().aiComposeTones();
	for (const auto &cached : tones.list()) {
		if (!cached.isDefault && cached.slug == slug) {
			apply({ .id = cached.id, .accessHash = cached.accessHash });
			return;
		}
	}
	tones.resolve(slug, [apply = std::move(apply)](
			Data::AiComposeTone tone) mutable {
		if (tone.isDefault) {
			apply({ .defaultTone = tone.defaultType });
		} else {
			apply({ .id = tone.id, .accessHash = tone.accessHash });
		}
	}, std::move(fail));
}

QString AiApplyBoundSlug() {
	return OptionAiApplyToneSlug.value();
}

void SetAiApplyBoundSlug(const QString &slug) {
	OptionAiApplyToneSlug.set(slug);
}

void ClearAiApplyBoundSlug() {
	OptionAiApplyToneSlug.set(QString());
}

QString AiApplyShortcutText() {
	for (const auto &[keys, commands] : Shortcuts::KeysCurrents()) {
		if (commands.contains(Shortcuts::Command::ComposeAiApplyInPlace)) {
			auto result = keys.toString();
#ifdef Q_OS_MAC
			result = result.replace(u"Ctrl+"_q, QString() + QChar(0x2318));
			result = result.replace(u"Meta+"_q, QString() + QChar(0x2303));
			result = result.replace(u"Alt+"_q, QString() + QChar(0x2325));
			result = result.replace(u"Shift+"_q, QString() + QChar(0x21E7));
#endif // Q_OS_MAC
			return result;
		}
	}
	return QString();
}

void TriggerAiApplyInPlace(
		not_null<Main::Session*> session,
		std::shared_ptr<Ui::Show> show,
		not_null<QObject*> guard,
		not_null<Ui::InputField*> field,
		TextWithEntities fullFieldText,
		Fn<void(TextWithTags textWithTags, int cursor)> applyToField) {
	const auto slug = AiApplyBoundSlug();
	if (slug.isEmpty()) {
		show->showToast(tr::lng_ai_compose_apply_unbound(tr::now));
		return;
	}
	const auto cursor = field->textCursor();
	const auto hasSelection = cursor.hasSelection();
	const auto selectionStart = cursor.selectionStart();
	const auto selectionEnd = cursor.selectionEnd();
	const auto savedCursorPosition = cursor.position();
	auto text = TextWithEntities();
	if (hasSelection) {
		const auto part = field->getTextWithTagsPart(
			selectionStart,
			selectionEnd);
		text = {
			part.text,
			TextUtilities::ConvertTextTagsToEntities(part.tags),
		};
	} else {
		text = std::move(fullFieldText);
	}
	if (text.text.isEmpty()) {
		show->showToast(tr::lng_ai_compose_apply_empty(tr::now));
		return;
	}
	const auto fieldRaw = field.get();
	ApplyAiInPlaceBySlug(
		session,
		std::move(text),
		slug,
		crl::guard(guard.get(), [=](TextWithEntities result) {
			auto replacement = TextWithTags{
				result.text,
				TextUtilities::ConvertEntitiesToTextTags(result.entities),
			};
			auto full = TextWithTags();
			auto restoreCursor = 0;
			if (hasSelection) {
				const auto fullLength = int(
					fieldRaw->getTextWithTags().text.size());
				const auto from = std::clamp(
					selectionStart,
					0,
					fullLength);
				const auto till = std::clamp(
					selectionEnd,
					from,
					fullLength);
				auto before = fieldRaw->getTextWithTagsPart(0, from);
				auto after = fieldRaw->getTextWithTagsPart(till);
				const auto shiftMiddle = int(before.text.size());
				const auto shiftAfter = shiftMiddle
					+ int(replacement.text.size());
				full.text = before.text
					+ replacement.text
					+ after.text;
				full.tags = std::move(before.tags);
				full.tags.reserve(full.tags.size()
					+ replacement.tags.size()
					+ after.tags.size());
				for (const auto &tag : replacement.tags) {
					full.tags.push_back({
						tag.offset + shiftMiddle,
						tag.length,
						tag.id,
					});
				}
				for (const auto &tag : after.tags) {
					full.tags.push_back({
						tag.offset + shiftAfter,
						tag.length,
						tag.id,
					});
				}
				restoreCursor = shiftAfter;
			} else {
				full = std::move(replacement);
				restoreCursor = std::clamp(
					savedCursorPosition,
					0,
					int(full.text.size()));
			}
			applyToField(std::move(full), restoreCursor);
		}),
		crl::guard(guard.get(), [=](const MTP::Error &error) {
			if (MTP::IgnoreError(error)) {
				return;
			}
			const auto type = error.type();
			if (type == u"AICOMPOSE_TONE_SLUG_INVALID"_q
				|| type == u"AICOMPOSE_TONE_INVALID"_q
				|| type == u"TONE_NOT_FOUND"_q) {
				ClearAiApplyBoundSlug();
				show->showToast(tr::lng_ai_compose_tone_invalid(tr::now));
			} else {
				show->showToast(type);
			}
		}));
}

} // namespace Api
