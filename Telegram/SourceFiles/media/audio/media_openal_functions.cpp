/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/audio/media_openal_functions.h"

#include <al.h>

namespace OpenAL {

void LoadEFXExtension() {
#define LOAD_PROC(x)  ((x) = reinterpret_cast<decltype(x)>(alGetProcAddress(#x)))
	LOAD_PROC(alGenEffects);
	LOAD_PROC(alDeleteEffects);
	LOAD_PROC(alIsEffect);
	LOAD_PROC(alEffecti);
	LOAD_PROC(alEffectiv);
	LOAD_PROC(alEffectf);
	LOAD_PROC(alEffectfv);
	LOAD_PROC(alGetEffecti);
	LOAD_PROC(alGetEffectiv);
	LOAD_PROC(alGetEffectf);
	LOAD_PROC(alGetEffectfv);

	LOAD_PROC(alGenFilters);
	LOAD_PROC(alDeleteFilters);
	LOAD_PROC(alIsFilter);
	LOAD_PROC(alFilteri);
	LOAD_PROC(alFilteriv);
	LOAD_PROC(alFilterf);
	LOAD_PROC(alFilterfv);
	LOAD_PROC(alGetFilteri);
	LOAD_PROC(alGetFilteriv);
	LOAD_PROC(alGetFilterf);
	LOAD_PROC(alGetFilterfv);

	LOAD_PROC(alGenAuxiliaryEffectSlots);
	LOAD_PROC(alDeleteAuxiliaryEffectSlots);
	LOAD_PROC(alIsAuxiliaryEffectSlot);
	LOAD_PROC(alAuxiliaryEffectSloti);
	LOAD_PROC(alAuxiliaryEffectSlotiv);
	LOAD_PROC(alAuxiliaryEffectSlotf);
	LOAD_PROC(alAuxiliaryEffectSlotfv);
	LOAD_PROC(alGetAuxiliaryEffectSloti);
	LOAD_PROC(alGetAuxiliaryEffectSlotiv);
	LOAD_PROC(alGetAuxiliaryEffectSlotf);
	LOAD_PROC(alGetAuxiliaryEffectSlotfv);
#undef LOAD_PROC
}

bool HasEFXExtension() {
	return (alGenEffects != nullptr)
		&& (alDeleteEffects != nullptr)
		&& (alIsEffect != nullptr)
		&& (alEffecti != nullptr)
		&& (alEffectiv != nullptr)
		&& (alEffectf != nullptr)
		&& (alEffectfv != nullptr)
		&& (alGetEffecti != nullptr)
		&& (alGetEffectiv != nullptr)
		&& (alGetEffectf != nullptr)
		&& (alGetEffectfv != nullptr)
		&& (alGenFilters != nullptr)
		&& (alDeleteFilters != nullptr)
		&& (alIsFilter != nullptr)
		&& (alFilteri != nullptr)
		&& (alFilteriv != nullptr)
		&& (alFilterf != nullptr)
		&& (alFilterfv != nullptr)
		&& (alGetFilteri != nullptr)
		&& (alGetFilteriv != nullptr)
		&& (alGetFilterf != nullptr)
		&& (alGetFilterfv != nullptr)
		&& (alGenAuxiliaryEffectSlots != nullptr)
		&& (alDeleteAuxiliaryEffectSlots != nullptr)
		&& (alIsAuxiliaryEffectSlot != nullptr)
		&& (alAuxiliaryEffectSloti != nullptr)
		&& (alAuxiliaryEffectSlotiv != nullptr)
		&& (alAuxiliaryEffectSlotf != nullptr)
		&& (alAuxiliaryEffectSlotfv != nullptr)
		&& (alGetAuxiliaryEffectSloti != nullptr)
		&& (alGetAuxiliaryEffectSlotiv != nullptr)
		&& (alGetAuxiliaryEffectSlotf != nullptr)
		&& (alGetAuxiliaryEffectSlotfv != nullptr);
}

} // namespace OpenAL
