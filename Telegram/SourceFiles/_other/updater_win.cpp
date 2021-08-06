/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "updater.h"

#include "base/platform/win/base_windows_safe_library.h"

bool _debug = false;

wstring updaterName, updaterDir, updateTo, exeName, customWorkingDir, customKeyFile;

bool equal(const wstring &a, const wstring &b) {
	return !_wcsicmp(a.c_str(), b.c_str());
}

void updateError(const WCHAR *msg, DWORD errorCode) {
	WCHAR errMsg[2048];
	LPWSTR errorTextFormatted = nullptr;
	auto formatFlags = FORMAT_MESSAGE_FROM_SYSTEM
		| FORMAT_MESSAGE_ALLOCATE_BUFFER
		| FORMAT_MESSAGE_IGNORE_INSERTS;
	FormatMessage(
		formatFlags,
		NULL,
		errorCode,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPWSTR)&errorTextFormatted,
		0,
		0);
	auto errorText = errorTextFormatted
		? errorTextFormatted
		: L"(Unknown error)";
	wsprintf(errMsg, L"%s, error code: %d\nError message: %s", msg, errorCode, errorText);

	MessageBox(0, errMsg, L"Update error!", MB_ICONERROR);

	LocalFree(errorTextFormatted);
}

HANDLE _logFile = 0;
void openLog() {
	if (!_debug || _logFile) return;
	wstring logPath = L"DebugLogs";
	if (!CreateDirectory(logPath.c_str(), NULL)) {
		DWORD errorCode = GetLastError();
		if (errorCode && errorCode != ERROR_ALREADY_EXISTS) {
			updateError(L"Failed to create log directory", errorCode);
			return;
		}
	}

	SYSTEMTIME stLocalTime;

	GetLocalTime(&stLocalTime);

	static const int maxFileLen = MAX_PATH * 10;
	WCHAR logName[maxFileLen];
	wsprintf(logName, L"DebugLogs\\%04d%02d%02d_%02d%02d%02d_upd.txt",
		stLocalTime.wYear, stLocalTime.wMonth, stLocalTime.wDay,
		stLocalTime.wHour, stLocalTime.wMinute, stLocalTime.wSecond);
	_logFile = CreateFile(logName, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
	if (_logFile == INVALID_HANDLE_VALUE) { // :(
		updateError(L"Failed to create log file", GetLastError());
		_logFile = 0;
		return;
	}
}

void closeLog() {
	if (!_logFile) return;

	CloseHandle(_logFile);
	_logFile = 0;
}

void writeLog(const wstring &msg) {
	if (!_logFile) return;

	wstring full = msg + L'\n';
	DWORD written = 0;
	BOOL result = WriteFile(_logFile, full.c_str(), full.size() * sizeof(wchar_t), &written, 0);
	if (!result) {
		updateError((L"Failed to write log entry '" + msg + L"'").c_str(), GetLastError());
		closeLog();
		return;
	}
	BOOL flushr = FlushFileBuffers(_logFile);
	if (!flushr) {
		updateError((L"Failed to flush log on entry '" + msg + L"'").c_str(), GetLastError());
		closeLog();
		return;
	}
}

void fullClearPath(const wstring &dir) {
	WCHAR path[4096];
	memcpy(path, dir.c_str(), (dir.size() + 1) * sizeof(WCHAR));
	path[dir.size() + 1] = 0;
	writeLog(L"Fully clearing path '" + dir + L"'..");
	SHFILEOPSTRUCT file_op = {
		NULL,
		FO_DELETE,
		path,
		L"",
		FOF_NOCONFIRMATION |
		FOF_NOERRORUI |
		FOF_SILENT,
		false,
		0,
		L""
	};
	int res = SHFileOperation(&file_op);
	if (res) writeLog(L"Error: failed to clear path! :(");
}

void delFolder() {
	wstring delPathOld = L"tupdates\\ready", delPath = L"tupdates\\temp", delFolder = L"tupdates";
	fullClearPath(delPathOld);
	fullClearPath(delPath);
	RemoveDirectory(delFolder.c_str());
}

DWORD versionNum = 0, versionLen = 0, readLen = 0;
WCHAR versionStr[32] = { 0 };

bool update() {
	writeLog(L"Update started..");

	wstring updDir = L"tupdates\\temp", readyFilePath = L"tupdates\\temp\\ready", tdataDir = L"tupdates\\temp\\tdata";
	{
		HANDLE readyFile = CreateFile(readyFilePath.c_str(), GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
		if (readyFile != INVALID_HANDLE_VALUE) {
			CloseHandle(readyFile);
		} else {
			updDir = L"tupdates\\ready"; // old
			tdataDir = L"tupdates\\ready\\tdata";
		}
	}

	HANDLE versionFile = CreateFile((tdataDir + L"\\version").c_str(), GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
	if (versionFile != INVALID_HANDLE_VALUE) {
		if (!ReadFile(versionFile, &versionNum, sizeof(DWORD), &readLen, NULL) || readLen != sizeof(DWORD)) {
			versionNum = 0;
		} else {
			if (versionNum == 0x7FFFFFFF) { // alpha version

			} else if (!ReadFile(versionFile, &versionLen, sizeof(DWORD), &readLen, NULL) || readLen != sizeof(DWORD) || versionLen > 63) {
				versionNum = 0;
			} else if (!ReadFile(versionFile, versionStr, versionLen, &readLen, NULL) || readLen != versionLen) {
				versionNum = 0;
			}
		}
		CloseHandle(versionFile);
		writeLog(L"Version file read.");
	} else {
		writeLog(L"Could not open version file to update registry :(");
	}

	deque<wstring> dirs;
	dirs.push_back(updDir);

	deque<wstring> from, to, forcedirs;

	do {
		wstring dir = dirs.front();
		dirs.pop_front();

		wstring toDir = updateTo;
		if (dir.size() > updDir.size() + 1) {
			toDir += (dir.substr(updDir.size() + 1) + L"\\");
			forcedirs.push_back(toDir);
			writeLog(L"Parsing dir '" + toDir + L"' in update tree..");
		}

		WIN32_FIND_DATA findData;
		HANDLE findHandle = FindFirstFileEx((dir + L"\\*").c_str(), FindExInfoStandard, &findData, FindExSearchNameMatch, 0, 0);
		if (findHandle == INVALID_HANDLE_VALUE) {
			DWORD errorCode = GetLastError();
			if (errorCode == ERROR_PATH_NOT_FOUND) { // no update is ready
				return true;
			}
			writeLog(L"Error: failed to find update files :(");
			updateError(L"Failed to find update files", errorCode);
			delFolder();
			return false;
		}

		do {
			wstring fname = dir + L"\\" + findData.cFileName;
			if (fname.substr(0, tdataDir.size()) == tdataDir && (fname.size() <= tdataDir.size() || fname.at(tdataDir.size()) == '/')) {
				writeLog(L"Skipped 'tdata' path '" + fname + L"'");
			} else if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				if (findData.cFileName != wstring(L".") && findData.cFileName != wstring(L"..")) {
					dirs.push_back(fname);
					writeLog(L"Added dir '" + fname + L"' in update tree..");
				}
			} else {
				wstring tofname = updateTo + fname.substr(updDir.size() + 1);
				if (equal(tofname, updaterName)) { // bad update - has Updater.exe - delete all dir
					writeLog(L"Error: bad update, has Updater.exe! '" + tofname + L"' equal '" + updaterName + L"'");
					delFolder();
					return false;
				} else if (equal(tofname, updateTo + L"Telegram.exe") && exeName != L"Telegram.exe") {
					wstring fullBinaryPath = updateTo + exeName;
					writeLog(L"Target binary found: '" + tofname + L"', changing to '" + fullBinaryPath + L"'");
					tofname = fullBinaryPath;
				}
				if (equal(fname, readyFilePath)) {
					writeLog(L"Skipped ready file '" + fname + L"'");
				} else {
					from.push_back(fname);
					to.push_back(tofname);
					writeLog(L"Added file '" + fname + L"' to be copied to '" + tofname + L"'");
				}
			}
		} while (FindNextFile(findHandle, &findData));
		DWORD errorCode = GetLastError();
		if (errorCode && errorCode != ERROR_NO_MORE_FILES) { // everything is found
			writeLog(L"Error: failed to find next update file :(");
			updateError(L"Failed to find next update file", errorCode);
			delFolder();
			return false;
		}
		FindClose(findHandle);
	} while (!dirs.empty());

	for (size_t i = 0; i < forcedirs.size(); ++i) {
		wstring forcedir = forcedirs[i];
		writeLog(L"Forcing dir '" + forcedir + L"'..");
		if (!forcedir.empty() && !CreateDirectory(forcedir.c_str(), NULL)) {
			DWORD errorCode = GetLastError();
			if (errorCode && errorCode != ERROR_ALREADY_EXISTS) {
				writeLog(L"Error: failed to create dir '" + forcedir + L"'..");
				updateError(L"Failed to create directory", errorCode);
				delFolder();
				return false;
			}
			writeLog(L"Already exists!");
		}
	}

	for (size_t i = 0; i < from.size(); ++i) {
		wstring fname = from[i], tofname = to[i];
		BOOL copyResult;
		do {
			writeLog(L"Copying file '" + fname + L"' to '" + tofname + L"'..");
			int copyTries = 0;
			do {
				copyResult = CopyFile(fname.c_str(), tofname.c_str(), FALSE);
				if (!copyResult) {
					++copyTries;
					Sleep(100);
				} else {
					break;
				}
			} while (copyTries < 100);
			if (!copyResult) {
				writeLog(L"Error: failed to copy, asking to retry..");
				WCHAR errMsg[2048];
				wsprintf(errMsg, L"Failed to update Telegram :(\n%s is not accessible.", tofname.c_str());
				if (MessageBox(0, errMsg, L"Update error!", MB_ICONERROR | MB_RETRYCANCEL) != IDRETRY) {
					delFolder();
					return false;
				}
			}
		} while (!copyResult);
	}

	writeLog(L"Update succeed! Clearing folder..");
	delFolder();
	return true;
}

void updateRegistry() {
	if (versionNum && versionNum != 0x7FFFFFFF) {
		writeLog(L"Updating registry..");
		versionStr[versionLen / 2] = 0;
		HKEY rkey;
		LSTATUS status = RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{53F49750-6209-4FBF-9CA8-7A333C87D1ED}_is1", 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &rkey);
		if (status == ERROR_SUCCESS) {
			writeLog(L"Checking registry install location..");
			static const int bufSize = 4096;
			DWORD locationType, locationSize = bufSize * 2;
			WCHAR locationStr[bufSize], exp[bufSize];
			if (RegQueryValueEx(rkey, L"InstallLocation", 0, &locationType, (BYTE*)locationStr, &locationSize) == ERROR_SUCCESS) {
				locationSize /= 2;
				if (locationStr[locationSize - 1]) {
					locationStr[locationSize++] = 0;
				}
				if (locationType == REG_EXPAND_SZ) {
					DWORD copy = ExpandEnvironmentStrings(locationStr, exp, bufSize);
					if (copy <= bufSize) {
						memcpy(locationStr, exp, copy * sizeof(WCHAR));
					}
				}
				if (locationType == REG_EXPAND_SZ || locationType == REG_SZ) {
					if (PathCanonicalize(exp, locationStr)) {
						memcpy(locationStr, exp, bufSize * sizeof(WCHAR));
						if (GetFullPathName(L".", bufSize, exp, 0) < bufSize) {
							wstring installpath = locationStr, mypath = exp;
							if (installpath == mypath + L"\\" || true) { // always update reg info, if we found it
								WCHAR nameStr[bufSize], dateStr[bufSize], publisherStr[bufSize], icongroupStr[bufSize];
								SYSTEMTIME stLocalTime;
								GetLocalTime(&stLocalTime);
								RegSetValueEx(rkey, L"DisplayVersion", 0, REG_SZ, (const BYTE*)versionStr, ((versionLen / 2) + 1) * sizeof(WCHAR));
								wsprintf(nameStr, L"Telegram Desktop version %s", versionStr);
								RegSetValueEx(rkey, L"DisplayName", 0, REG_SZ, (const BYTE*)nameStr, (wcslen(nameStr) + 1) * sizeof(WCHAR));
								wsprintf(publisherStr, L"Telegram FZ-LLC");
								RegSetValueEx(rkey, L"Publisher", 0, REG_SZ, (const BYTE*)publisherStr, (wcslen(publisherStr) + 1) * sizeof(WCHAR));
								wsprintf(icongroupStr, L"Telegram Desktop");
								RegSetValueEx(rkey, L"Inno Setup: Icon Group", 0, REG_SZ, (const BYTE*)icongroupStr, (wcslen(icongroupStr) + 1) * sizeof(WCHAR));
								wsprintf(dateStr, L"%04d%02d%02d", stLocalTime.wYear, stLocalTime.wMonth, stLocalTime.wDay);
								RegSetValueEx(rkey, L"InstallDate", 0, REG_SZ, (const BYTE*)dateStr, (wcslen(dateStr) + 1) * sizeof(WCHAR));

								const WCHAR *appURL = L"https://desktop.telegram.org";
								RegSetValueEx(rkey, L"HelpLink", 0, REG_SZ, (const BYTE*)appURL, (wcslen(appURL) + 1) * sizeof(WCHAR));
								RegSetValueEx(rkey, L"URLInfoAbout", 0, REG_SZ, (const BYTE*)appURL, (wcslen(appURL) + 1) * sizeof(WCHAR));
								RegSetValueEx(rkey, L"URLUpdateInfo", 0, REG_SZ, (const BYTE*)appURL, (wcslen(appURL) + 1) * sizeof(WCHAR));
							}
						}
					}
				}
			}
			RegCloseKey(rkey);
		}
	}
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE prevInstance, LPWSTR cmdParamarg, int cmdShow) {
	base::Platform::InitDynamicLibraries();

	openLog();

	_oldWndExceptionFilter = SetUnhandledExceptionFilter(_exceptionFilter);
//	CAPIHook apiHook("kernel32.dll", "SetUnhandledExceptionFilter", (PROC)RedirectedSetUnhandledExceptionFilter);

	writeLog(L"Updaters started..");

	LPWSTR *args;
	int argsCount;

	bool needupdate = false, autostart = false, debug = false, writeprotected = false, startintray = false, testmode = false, freetype = false, externalupdater = false;
	args = CommandLineToArgvW(GetCommandLine(), &argsCount);
	if (args) {
		for (int i = 1; i < argsCount; ++i) {
			writeLog(std::wstring(L"Argument: ") + args[i]);
			if (equal(args[i], L"-update")) {
				needupdate = true;
			} else if (equal(args[i], L"-autostart")) {
				autostart = true;
			} else if (equal(args[i], L"-debug")) {
				debug = _debug = true;
				openLog();
			} else if (equal(args[i], L"-startintray")) {
				startintray = true;
			} else if (equal(args[i], L"-testmode")) {
				testmode = true;
			} else if (equal(args[i], L"-freetype")) {
				freetype = true;
			} else if (equal(args[i], L"-externalupdater")) {
				externalupdater = true;
			} else if (equal(args[i], L"-writeprotected") && ++i < argsCount) {
				writeLog(std::wstring(L"Argument: ") + args[i]);
				writeprotected = true;
				updateTo = args[i];
				for (int j = 0, l = updateTo.size(); j < l; ++j) {
					if (updateTo[j] == L'/') {
						updateTo[j] = L'\\';
					}
				}
			} else if (equal(args[i], L"-workdir") && ++i < argsCount) {
				writeLog(std::wstring(L"Argument: ") + args[i]);
				customWorkingDir = args[i];
			} else if (equal(args[i], L"-key") && ++i < argsCount) {
				writeLog(std::wstring(L"Argument: ") + args[i]);
				customKeyFile = args[i];
			} else if (equal(args[i], L"-exename") && ++i < argsCount) {
				writeLog(std::wstring(L"Argument: ") + args[i]);
				exeName = args[i];
				for (int j = 0, l = exeName.size(); j < l; ++j) {
					if (exeName[j] == L'/' || exeName[j] == L'\\') {
						exeName = L"Telegram.exe";
						break;
					}
				}
			}
		}
		if (exeName.empty()) {
			exeName = L"Telegram.exe";
		}
		if (needupdate) writeLog(L"Need to update!");
		if (autostart) writeLog(L"From autostart!");
		if (writeprotected) writeLog(L"Write Protected folder!");
		if (!customWorkingDir.empty()) writeLog(L"Will pass custom working dir: " + customWorkingDir);

		updaterName = args[0];
		writeLog(L"Updater name is: " + updaterName);
		if (updaterName.size() > 11) {
			if (equal(updaterName.substr(updaterName.size() - 11), L"Updater.exe")) {
				updaterDir = updaterName.substr(0, updaterName.size() - 11);
				writeLog(L"Updater dir is: " + updaterDir);
				if (!writeprotected) {
					updateTo = updaterDir;
				}
				writeLog(L"Update to: " + updateTo);
				if (needupdate && update()) {
					updateRegistry();
				}
				if (writeprotected) { // if we can't clear all tupdates\ready (Updater.exe is there) - clear only version
					if (DeleteFile(L"tupdates\\temp\\tdata\\version") || DeleteFile(L"tupdates\\ready\\tdata\\version")) {
						writeLog(L"Version file deleted!");
					} else {
						writeLog(L"Error: could not delete version file");
					}
				}
			} else {
				writeLog(L"Error: bad exe name!");
			}
		} else {
			writeLog(L"Error: short exe name!");
		}
		LocalFree(args);
	} else {
		writeLog(L"Error: No command line arguments!");
	}

	wstring targs;
	if (autostart) targs += L" -autostart";
	if (debug) targs += L" -debug";
	if (startintray) targs += L" -startintray";
	if (testmode) targs += L" -testmode";
	if (freetype) targs += L" -freetype";
	if (externalupdater) targs += L" -externalupdater";
	if (!customWorkingDir.empty()) {
		targs += L" -workdir \"" + customWorkingDir + L"\"";
	}
	if (!customKeyFile.empty()) {
		targs += L" -key \"" + customKeyFile + L"\"";
	}
	writeLog(L"Result arguments: " + targs);

	bool executed = false;
	if (writeprotected) { // run un-elevated
		writeLog(L"Trying to run un-elevated by temp.lnk");

		HRESULT hres = CoInitialize(0);
		if (SUCCEEDED(hres)) {
			IShellLink* psl;
			HRESULT hres = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&psl);
			if (SUCCEEDED(hres)) {
				IPersistFile* ppf;

				wstring exe = updateTo + exeName, dir = updateTo;
				psl->SetArguments((targs.size() ? targs.substr(1) : targs).c_str());
				psl->SetPath(exe.c_str());
				psl->SetWorkingDirectory(dir.c_str());
				psl->SetDescription(L"");

				hres = psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf);

				if (SUCCEEDED(hres)) {
					wstring lnk = L"tupdates\\temp\\temp.lnk";
					hres = ppf->Save(lnk.c_str(), TRUE);
					if (!SUCCEEDED(hres)) {
						lnk = L"tupdates\\ready\\temp.lnk"; // old
						hres = ppf->Save(lnk.c_str(), TRUE);
					}
					ppf->Release();

					if (SUCCEEDED(hres)) {
						writeLog(L"Executing un-elevated through link..");
						ShellExecute(0, 0, L"explorer.exe", lnk.c_str(), 0, SW_SHOWNORMAL);
						executed = true;
					} else {
						writeLog(L"Error: ppf->Save failed");
					}
				} else {
					writeLog(L"Error: Could not create interface IID_IPersistFile");
				}
				psl->Release();
			} else {
				writeLog(L"Error: could not create instance of IID_IShellLink");
			}
			CoUninitialize();
		} else {
			writeLog(L"Error: Could not initialize COM");
		}
	}
	if (!executed) {
		ShellExecute(0, 0, (updateTo + exeName).c_str(), (L"-noupdate" + targs).c_str(), 0, SW_SHOWNORMAL);
	}

	writeLog(L"Executed '" + exeName + L"', closing log and quitting..");
	closeLog();

	return 0;
}

static const WCHAR *_programName = L"Telegram Desktop"; // folder in APPDATA, if current path is unavailable for writing
static const WCHAR *_exeName = L"Updater.exe";

LPTOP_LEVEL_EXCEPTION_FILTER _oldWndExceptionFilter = 0;

typedef BOOL (FAR STDAPICALLTYPE *t_miniDumpWriteDump)(
	_In_ HANDLE hProcess,
	_In_ DWORD ProcessId,
	_In_ HANDLE hFile,
	_In_ MINIDUMP_TYPE DumpType,
	_In_opt_ PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
	_In_opt_ PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
	_In_opt_ PMINIDUMP_CALLBACK_INFORMATION CallbackParam
);
t_miniDumpWriteDump miniDumpWriteDump = 0;

HANDLE _generateDumpFileAtPath(const WCHAR *path) {
	static const int maxFileLen = MAX_PATH * 10;

	WCHAR szPath[maxFileLen];
	wsprintf(szPath, L"%stdata\\", path);
	if (!CreateDirectory(szPath, NULL)) {
		if (GetLastError() != ERROR_ALREADY_EXISTS) {
			return 0;
		}
	}
	wsprintf(szPath, L"%sdumps\\", path);
	if (!CreateDirectory(szPath, NULL)) {
		if (GetLastError() != ERROR_ALREADY_EXISTS) {
			return 0;
		}
	}

	WCHAR szFileName[maxFileLen];
	WCHAR szExeName[maxFileLen];

	wcscpy_s(szExeName, _exeName);
	WCHAR *dotFrom = wcschr(szExeName, WCHAR(L'.'));
	if (dotFrom) {
		wsprintf(dotFrom, L"");
	}

	SYSTEMTIME stLocalTime;

	GetLocalTime(&stLocalTime);

	wsprintf(szFileName, L"%s%s-%s-%04d%02d%02d-%02d%02d%02d-%ld-%ld.dmp",
	         szPath, szExeName, updaterVersionStr,
	         stLocalTime.wYear, stLocalTime.wMonth, stLocalTime.wDay,
	         stLocalTime.wHour, stLocalTime.wMinute, stLocalTime.wSecond,
	         GetCurrentProcessId(), GetCurrentThreadId());
	return CreateFile(szFileName, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_WRITE|FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
}

void _generateDump(EXCEPTION_POINTERS* pExceptionPointers) {
	static const int maxFileLen = MAX_PATH * 10;

	closeLog();

	HMODULE hDll = LoadLibrary(L"DBGHELP.DLL");
	if (!hDll) return;

	miniDumpWriteDump = (t_miniDumpWriteDump)GetProcAddress(hDll, "MiniDumpWriteDump");
	if (!miniDumpWriteDump) return;

	HANDLE hDumpFile = 0;

	WCHAR szPath[maxFileLen];
	DWORD len = GetModuleFileName(GetModuleHandle(0), szPath, maxFileLen);
	if (!len) return;

	WCHAR *pathEnd = szPath  + len;

	if (!_wcsicmp(pathEnd - wcslen(_exeName), _exeName)) {
		wsprintf(pathEnd - wcslen(_exeName), L"");
		hDumpFile = _generateDumpFileAtPath(szPath);
	}
	if (!hDumpFile || hDumpFile == INVALID_HANDLE_VALUE) {
		WCHAR wstrPath[maxFileLen];
		DWORD wstrPathLen;
		if (wstrPathLen = GetEnvironmentVariable(L"APPDATA", wstrPath, maxFileLen)) {
			wsprintf(wstrPath + wstrPathLen, L"\\%s\\", _programName);
			hDumpFile = _generateDumpFileAtPath(wstrPath);
		}
	}

	if (!hDumpFile || hDumpFile == INVALID_HANDLE_VALUE) {
		return;
	}

	MINIDUMP_EXCEPTION_INFORMATION ExpParam = {0};
	ExpParam.ThreadId = GetCurrentThreadId();
	ExpParam.ExceptionPointers = pExceptionPointers;
	ExpParam.ClientPointers = TRUE;

	miniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hDumpFile, MiniDumpWithDataSegs, &ExpParam, NULL, NULL);
}

LONG CALLBACK _exceptionFilter(EXCEPTION_POINTERS* pExceptionPointers) {
	_generateDump(pExceptionPointers);
	return _oldWndExceptionFilter ? (*_oldWndExceptionFilter)(pExceptionPointers) : EXCEPTION_CONTINUE_SEARCH;
}

// see http://www.codeproject.com/Articles/154686/SetUnhandledExceptionFilter-and-the-C-C-Runtime-Li
LPTOP_LEVEL_EXCEPTION_FILTER WINAPI RedirectedSetUnhandledExceptionFilter(_In_opt_ LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter) {
	// When the CRT calls SetUnhandledExceptionFilter with NULL parameter
	// our handler will not get removed.
	_oldWndExceptionFilter = lpTopLevelExceptionFilter;
	return 0;
}
