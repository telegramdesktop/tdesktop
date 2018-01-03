/*
WARNING! All changes made in this file will be lost!
Created from 'colors.palette' by 'codegen_style'

This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "zip.h"
#include "unzip.h"

namespace zlib {
namespace internal {

class InMemoryFile {
public:
	InMemoryFile(const QByteArray &data = QByteArray()) : _data(data) {
	}

	zlib_filefunc_def funcs() {
		zlib_filefunc_def result;
		result.opaque = this;
		result.zopen_file = &InMemoryFile::Open;
		result.zerror_file = &InMemoryFile::Error;
		result.zread_file = &InMemoryFile::Read;
		result.zwrite_file = &InMemoryFile::Write;
		result.zclose_file = &InMemoryFile::Close;
		result.zseek_file = &InMemoryFile::Seek;
		result.ztell_file = &InMemoryFile::Tell;
		return result;
	}

	int error() const {
		return _error;
	}

	QByteArray result() const {
		return _data;
	}

private:
	voidpf open(const char *filename, int mode) {
		if (mode & ZLIB_FILEFUNC_MODE_WRITE) {
			if (mode & ZLIB_FILEFUNC_MODE_CREATE) {
				_data.clear();
			}
			_position = _data.size();
			_data.reserve(2 * 1024 * 1024);
		} else if (mode & ZLIB_FILEFUNC_MODE_READ) {
			_position = 0;
		}
		_error = 0;
		return this;
	}

	uLong read(voidpf stream, void* buf, uLong size) {
		uLong toRead = 0;
		if (!_error) {
			if (_data.size() > int(_position)) {
				toRead = qMin(size, uLong(_data.size() - _position));
				memcpy(buf, _data.constData() + _position, toRead);
				_position += toRead;
			}
			if (toRead < size) {
				_error = -1;
			}
		}
		return toRead;
	}

	uLong write(voidpf stream, const void* buf, uLong size) {
		if (_data.size() < int(_position + size)) {
			_data.resize(_position + size);
		}
		memcpy(_data.data() + _position, buf, size);
		_position += size;
		return size;
	}

	int close(voidpf stream) {
		auto result = _error;
		_position = 0;
		_error = 0;
		return result;
	}

	int error(voidpf stream) const {
		return _error;
	}

	long tell(voidpf stream) const {
		return _position;
	}

	long seek(voidpf stream, uLong offset, int origin) {
		if (!_error) {
			switch (origin) {
			case ZLIB_FILEFUNC_SEEK_SET: _position = offset; break;
			case ZLIB_FILEFUNC_SEEK_CUR: _position += offset; break;
			case ZLIB_FILEFUNC_SEEK_END: _position = _data.size() + offset; break;
			}
			if (int(_position) > _data.size()) {
				_error = -1;
			}
		}
		return _error;
	}

	static voidpf Open(voidpf opaque, const char* filename, int mode) {
		return static_cast<InMemoryFile*>(opaque)->open(filename, mode);
	}

	static uLong Read(voidpf opaque, voidpf stream, void* buf, uLong size) {
		return static_cast<InMemoryFile*>(opaque)->read(stream, buf, size);
	}

	static uLong Write(voidpf opaque, voidpf stream, const void* buf, uLong size) {
		return static_cast<InMemoryFile*>(opaque)->write(stream, buf, size);
	}

	static int Close(voidpf opaque, voidpf stream) {
		return static_cast<InMemoryFile*>(opaque)->close(stream);
	}

	static int Error(voidpf opaque, voidpf stream) {
		return static_cast<InMemoryFile*>(opaque)->error(stream);
	}

	static long Tell(voidpf opaque, voidpf stream) {
		return static_cast<InMemoryFile*>(opaque)->tell(stream);
	}

	static long Seek(voidpf opaque, voidpf stream, uLong offset, int origin) {
		return static_cast<InMemoryFile*>(opaque)->seek(stream, offset, origin);
	}

	uLong _position = 0;
	int _error = 0;
	QByteArray _data;

};

} // namespace internal

constexpr int kCaseSensitive = 1;
constexpr int kCaseInsensitive = 2;

class FileToRead {
public:
	FileToRead(const QByteArray &content) : _data(content) {
		auto funcs = _data.funcs();
		if (!(_handle = unzOpen2(nullptr, &funcs))) {
			_error = -1;
		}
	}

	int getGlobalInfo(unz_global_info *pglobal_info) {
		if (error() == UNZ_OK) {
			_error = _handle ? unzGetGlobalInfo(_handle, pglobal_info) : -1;
		}
		return _error;
	}

	int locateFile(const char *szFileName, int iCaseSensitivity) {
		if (error() == UNZ_OK) {
			_error = _handle ? unzLocateFile(_handle, szFileName, iCaseSensitivity) : -1;
		}
		return error();
	}

	int getCurrentFileInfo(
		unz_file_info *pfile_info,
		char *szFileName,
		uLong fileNameBufferSize,
		void *extraField,
		uLong extraFieldBufferSize,
		char *szComment,
		uLong commentBufferSize
	) {
		if (error() == UNZ_OK) {
			_error = _handle ? unzGetCurrentFileInfo(
				_handle,
				pfile_info,
				szFileName,
				fileNameBufferSize,
				extraField,
				extraFieldBufferSize,
				szComment,
				commentBufferSize
			) : -1;
		}
		return error();
	}

	int openCurrentFile() {
		if (error() == UNZ_OK) {
			_error = _handle ? unzOpenCurrentFile(_handle) : -1;
		}
		return error();
	}

	int readCurrentFile(voidp buf, unsigned len) {
		if (error() == UNZ_OK) {
			auto result = _handle ? unzReadCurrentFile(_handle, buf, len) : -1;
			if (result >= 0) {
				return result;
			} else {
				_error = result;
			}
		}
		return error();
	}

	int closeCurrentFile() {
		if (error() == UNZ_OK) {
			_error = _handle ? unzCloseCurrentFile(_handle) : -1;
		}
		return error();
	}

	QByteArray readCurrentFileContent(int fileSizeLimit) {
		unz_file_info fileInfo = { 0 };
		if (getCurrentFileInfo(&fileInfo, nullptr, 0, nullptr, 0, nullptr, 0) != UNZ_OK) {
			LOG(("Error: could not get current file info in a zip file."));
			return QByteArray();
		}

		auto size = fileInfo.uncompressed_size;
		if (size > static_cast<uint32>(fileSizeLimit)) {
			if (_error == UNZ_OK) _error = -1;
			LOG(("Error: current file is too large (should be less than %1, got %2) in a zip file.").arg(fileSizeLimit).arg(size));
			return QByteArray();
		}
		if (openCurrentFile() != UNZ_OK) {
			LOG(("Error: could not open current file in a zip file."));
			return QByteArray();
		}

		QByteArray result;
		result.resize(size);

		auto couldRead = readCurrentFile(result.data(), size);
		if (couldRead != static_cast<int>(size)) {
			LOG(("Error: could not read current file in a zip file, got %1.").arg(couldRead));
			return QByteArray();
		}

		if (closeCurrentFile() != UNZ_OK) {
			LOG(("Error: could not close current file in a zip file."));
			return QByteArray();
		}

		return result;
	}

	QByteArray readFileContent(const char *szFileName, int iCaseSensitivity, int fileSizeLimit) {
		if (locateFile(szFileName, iCaseSensitivity) != UNZ_OK) {
			LOG(("Error: could not locate '%1' in a zip file.").arg(szFileName));
			return QByteArray();
		}
		return readCurrentFileContent(fileSizeLimit);
	}

	void close() {
		if (_handle && unzClose(_handle) != UNZ_OK && _error == UNZ_OK) {
			_error = -1;
		}
		_handle = nullptr;
	}

	int error() const {
		if (auto dataError = _data.error()) {
			return dataError;
		}
		return _error;
	}

	void clearError() {
		_error = UNZ_OK;
	}

	~FileToRead() {
		close();
	}

private:
	internal::InMemoryFile _data;
	unzFile _handle = nullptr;
	int _error = 0;

};

class FileToWrite {
public:
	FileToWrite() {
		auto funcs = _data.funcs();
		if (!(_handle = zipOpen2(nullptr, APPEND_STATUS_CREATE, nullptr, &funcs))) {
			_error = -1;
		}
	}

	int openNewFile(
		const char *filename,
		const zip_fileinfo *zipfi,
		const void *extrafield_local,
		uInt size_extrafield_local,
		const void* extrafield_global,
		uInt size_extrafield_global,
		const char* comment,
		int method,
		int level
	) {
		if (error() == ZIP_OK) {
			_error = _handle ? zipOpenNewFileInZip(
				_handle,
				filename,
				zipfi,
				extrafield_local,
				size_extrafield_local,
				extrafield_global,
				size_extrafield_global,
				comment,
				method,
				level
			) : -1;
		}
		return error();
	}

	int writeInFile(const void* buf, unsigned len) {
		if (error() == ZIP_OK) {
			_error = _handle ? zipWriteInFileInZip(_handle, buf, len) : -1;
		}
		return error();
	}

	int closeFile() {
		if (error() == ZIP_OK) {
			_error = _handle ? zipCloseFileInZip(_handle) : -1;
		}
		return error();
	}

	void close() {
		if (_handle && zipClose(_handle, nullptr) != ZIP_OK && _error == ZIP_OK) {
			_error = -1;
		}
		_handle = nullptr;
	}

	int error() const {
		if (auto dataError = _data.error()) {
			return dataError;
		}
		return _error;
	}

	QByteArray result() const {
		return _data.result();
	}

	~FileToWrite() {
		close();
	}

private:
	internal::InMemoryFile _data;
	zipFile _handle = nullptr;
	int _error = 0;

};

} // namespace zlib
