/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_file_origin.h"
#include "ui/basic_click_handlers.h"

class DocumentData;
class HistoryItem;
class PhotoData;

class FileClickHandler : public LeftButtonClickHandler {
public:
	FileClickHandler(FullMsgId context);

	void setMessageId(FullMsgId context);

	[[nodiscard]] FullMsgId context() const;

private:
	FullMsgId _context;

};

class DocumentClickHandler : public FileClickHandler {
public:
	DocumentClickHandler(
		not_null<DocumentData*> document,
		FullMsgId context = FullMsgId());

	[[nodiscard]] not_null<DocumentData*> document() const;

private:
	const not_null<DocumentData*> _document;

};

class DocumentSaveClickHandler : public DocumentClickHandler {
public:
	enum class Mode {
		ToCacheOrFile,
		ToFile,
		ToNewFile,
	};
	using DocumentClickHandler::DocumentClickHandler;
	static void Save(
		Data::FileOrigin origin,
		not_null<DocumentData*> document,
		Mode mode = Mode::ToCacheOrFile);

protected:
	void onClickImpl() const override;

};

class DocumentOpenClickHandler : public DocumentClickHandler {
public:
	DocumentOpenClickHandler(
		not_null<DocumentData*> document,
		Fn<void(FullMsgId)> &&callback,
		FullMsgId context = FullMsgId());

protected:
	void onClickImpl() const override;

private:
	const Fn<void(FullMsgId)> _handler;

};

class DocumentCancelClickHandler : public DocumentClickHandler {
public:
	DocumentCancelClickHandler(
		not_null<DocumentData*> document,
		Fn<void(FullMsgId)> &&callback,
		FullMsgId context = FullMsgId());

protected:
	void onClickImpl() const override;

private:
	const Fn<void(FullMsgId)> _handler;

};

class DocumentOpenWithClickHandler : public DocumentClickHandler {
public:
	using DocumentClickHandler::DocumentClickHandler;
	static void Open(
		Data::FileOrigin origin,
		not_null<DocumentData*> document);

protected:
	void onClickImpl() const override;

};

class VoiceSeekClickHandler : public DocumentOpenClickHandler {
public:
	using DocumentOpenClickHandler::DocumentOpenClickHandler;

protected:
	void onClickImpl() const override {
	}

};

class DocumentWrappedClickHandler : public DocumentClickHandler {
public:
	DocumentWrappedClickHandler(
		ClickHandlerPtr wrapped,
		not_null<DocumentData*> document,
		FullMsgId context = FullMsgId());

protected:
	void onClickImpl() const override;

private:
	ClickHandlerPtr _wrapped;

};

class PhotoClickHandler : public FileClickHandler {
public:
	PhotoClickHandler(
		not_null<PhotoData*> photo,
		FullMsgId context = FullMsgId(),
		PeerData *peer = nullptr);

	[[nodiscard]] not_null<PhotoData*> photo() const;
	[[nodiscard]] PeerData *peer() const;

private:
	const not_null<PhotoData*> _photo;
	PeerData * const _peer = nullptr;

};

class PhotoOpenClickHandler : public PhotoClickHandler {
public:
	PhotoOpenClickHandler(
		not_null<PhotoData*> photo,
		Fn<void(FullMsgId)> &&callback,
		FullMsgId context = FullMsgId());

protected:
	void onClickImpl() const override;

private:
	const Fn<void(FullMsgId)> _handler;

};

class PhotoSaveClickHandler : public PhotoClickHandler {
public:
	using PhotoClickHandler::PhotoClickHandler;

protected:
	void onClickImpl() const override;

};

class PhotoCancelClickHandler : public PhotoClickHandler {
public:
	PhotoCancelClickHandler(
		not_null<PhotoData*> photo,
		Fn<void(FullMsgId)> &&callback,
		FullMsgId context = FullMsgId());

protected:
	void onClickImpl() const override;

private:
	const Fn<void(FullMsgId)> _handler;

};
