// Purpose: Implements the secure-desktop approval helper that displays and answers broker challenges.
#include <Windows.h>
#include <CommCtrl.h>
#include <shellapi.h>
#include <wincred.h>

#include <atomic>
#include <array>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "../xucRunSharedResources/BuildVersion.h"
#include "ApplicationIcon.h"
#include "BrokerContracts.h"
#include "BrokerPipeClient.h"
#include "SecureDesktopHost.h"
#include "SensitiveString.h"
#include "resource.h"

namespace
{
	constexpr int UpdateCurrentButton = 130;
	constexpr int UpdateAllButton = 131;
	constexpr int WritableTargetApproveButton = 132;
	constexpr UINT_PTR ApprovalCountdownTimer = 1;
	constexpr int ApprovalDetailsControl = 1250;
	constexpr int ApprovalDurationFirst = 1300;
	constexpr int ApprovalTargetProgram = 1400;
	constexpr int ApprovalProgramGeneral = 1401;
	constexpr int ApprovalAccountCurrent = 1450;
	constexpr int ApprovalAccountAll = 1451;
	constexpr int ApprovalCountdownControl = 1500;
	constexpr int ApprovalInterpreterWarningY = 167;
	constexpr int ApprovalInterpreterWarningHeight = 52;
	constexpr int ApprovalInterpreterLayoutOffset = 56;
	constexpr int ApprovalExistingContentBottom = 324;
	constexpr int ApprovalMarkerY = 329;
	constexpr int ApprovalMarkerHeight = 24;

	struct CredentialDialogTimeoutState
	{
		HANDLE stopEvent = nullptr;
		DWORD timeoutMilliseconds = 0;
		std::atomic_bool timedOut = false;
		std::atomic<HWND> window = nullptr;
	};

	CredentialDialogTimeoutState* activeCredentialDialogTimeout = nullptr;

	DWORD WINAPI CredentialDialogTimeoutThread(void* parameter)
	{
		auto& state = *static_cast<CredentialDialogTimeoutState*>(parameter);
		if (WaitForSingleObject(state.stopEvent, state.timeoutMilliseconds) == WAIT_TIMEOUT)
		{
			state.timedOut.store(true);
			if (HWND window = state.window.load())
				PostMessageW(window, WM_CLOSE, 0, 0);
		}
		return 0;
	}

#ifdef _DEBUG
	bool RunCredentialDialogTimeoutSelfTest()
	{
		CredentialDialogTimeoutState expired;
		expired.stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		expired.timeoutMilliseconds = 1;
		HANDLE expiredThread = expired.stopEvent
			? CreateThread(nullptr, 0, CredentialDialogTimeoutThread,
				&expired, 0, nullptr)
			: nullptr;
		if (!expired.stopEvent || !expiredThread ||
			WaitForSingleObject(expiredThread, 5000) != WAIT_OBJECT_0 ||
			!expired.timedOut.load())
		{
			if (expiredThread) CloseHandle(expiredThread);
			if (expired.stopEvent) CloseHandle(expired.stopEvent);
			return false;
		}
		CloseHandle(expiredThread);
		CloseHandle(expired.stopEvent);

		CredentialDialogTimeoutState cancelled;
		cancelled.stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		cancelled.timeoutMilliseconds = 5000;
		HANDLE cancelledThread = cancelled.stopEvent
			? CreateThread(nullptr, 0, CredentialDialogTimeoutThread,
				&cancelled, 0, nullptr)
			: nullptr;
		if (!cancelled.stopEvent || !cancelledThread)
		{
			if (cancelledThread) CloseHandle(cancelledThread);
			if (cancelled.stopEvent) CloseHandle(cancelled.stopEvent);
			return false;
		}
		SetEvent(cancelled.stopEvent);
		const bool stopped = WaitForSingleObject(
			cancelledThread, 5000) == WAIT_OBJECT_0;
		CloseHandle(cancelledThread);
		CloseHandle(cancelled.stopEvent);
		return stopped && !cancelled.timedOut.load();
	}
#endif
	constexpr int ApprovalButtonY = 357;
	constexpr int ApprovalButtonHeight = 14;
	constexpr int ApprovalDialogHeight = 372;
	static_assert(ApprovalExistingContentBottom < ApprovalMarkerY);
	static_assert(ApprovalMarkerY + ApprovalMarkerHeight < ApprovalButtonY);
	static_assert(ApprovalButtonY + ApprovalButtonHeight <= ApprovalDialogHeight);
	bool g_useGerman = false;

	enum class InterpreterWarningType
	{
		None,
		General,
		PowerShell
	};

	InterpreterWarningType GetInterpreterWarningType(
		const std::wstring& executablePath)
	{
		const std::size_t separator = executablePath.find_last_of(L"\\/");
		const std::wstring fileName = separator == std::wstring::npos
			? executablePath : executablePath.substr(separator + 1);
		const auto equals = [&fileName](const wchar_t* candidate)
		{
			return CompareStringOrdinal(fileName.c_str(), -1,
				candidate, -1, TRUE) == CSTR_EQUAL;
		};
		if (equals(L"powershell.exe") || equals(L"pwsh.exe"))
			return InterpreterWarningType::PowerShell;
		if (equals(L"cmd.exe") || equals(L"tcc.exe") || equals(L"tcmd.exe") ||
			equals(L"wscript.exe") || equals(L"cscript.exe"))
		{
			return InterpreterWarningType::General;
		}
		return InterpreterWarningType::None;
	}

	bool ApplyEarlyProcessHardening()
	{
		if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32))
			return false;

		PROCESS_MITIGATION_DYNAMIC_CODE_POLICY dynamicCodePolicy = {};
		dynamicCodePolicy.ProhibitDynamicCode = 1;
		if (!SetProcessMitigationPolicy(ProcessDynamicCodePolicy,
			&dynamicCodePolicy, sizeof(dynamicCodePolicy)))
		{
			return false;
		}

		PROCESS_MITIGATION_EXTENSION_POINT_DISABLE_POLICY extensionPointPolicy = {};
		extensionPointPolicy.DisableExtensionPoints = 1;
		if (!SetProcessMitigationPolicy(ProcessExtensionPointDisablePolicy,
			&extensionPointPolicy, sizeof(extensionPointPolicy)))
		{
			return false;
		}

		PROCESS_MITIGATION_IMAGE_LOAD_POLICY imageLoadPolicy = {};
		imageLoadPolicy.NoRemoteImages = 1;
		imageLoadPolicy.NoLowMandatoryLabelImages = 1;
		imageLoadPolicy.PreferSystem32Images = 1;
		if (!SetProcessMitigationPolicy(ProcessImageLoadPolicy,
			&imageLoadPolicy, sizeof(imageLoadPolicy)))
		{
			return false;
		}

		PROCESS_MITIGATION_DYNAMIC_CODE_POLICY effectiveDynamicCodePolicy = {};
		PROCESS_MITIGATION_EXTENSION_POINT_DISABLE_POLICY effectiveExtensionPointPolicy = {};
		PROCESS_MITIGATION_IMAGE_LOAD_POLICY effectiveImageLoadPolicy = {};
		return GetProcessMitigationPolicy(GetCurrentProcess(), ProcessDynamicCodePolicy,
			&effectiveDynamicCodePolicy, sizeof(effectiveDynamicCodePolicy)) &&
			GetProcessMitigationPolicy(GetCurrentProcess(), ProcessExtensionPointDisablePolicy,
				&effectiveExtensionPointPolicy, sizeof(effectiveExtensionPointPolicy)) &&
			GetProcessMitigationPolicy(GetCurrentProcess(), ProcessImageLoadPolicy,
				&effectiveImageLoadPolicy, sizeof(effectiveImageLoadPolicy)) &&
			effectiveDynamicCodePolicy.ProhibitDynamicCode &&
			effectiveExtensionPointPolicy.DisableExtensionPoints &&
			effectiveImageLoadPolicy.NoRemoteImages &&
			effectiveImageLoadPolicy.NoLowMandatoryLabelImages &&
			effectiveImageLoadPolicy.PreferSystem32Images;
	}

	bool InitializeLanguage()
	{
		std::array<wchar_t, 32768> modulePath = {};
		const DWORD pathLength = GetModuleFileNameW(
			nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
		if (pathLength == 0 || pathLength >= modulePath.size())
			return false;

		const std::wstring helperPath(modulePath.data(), pathLength);
		const std::size_t separator = helperPath.find_last_of(L"\\/");
		if (separator == std::wstring::npos)
			return false;
		const std::wstring configPath =
			helperPath.substr(0, separator + 1) + L"xucRun.ini";
		wchar_t language[16] = {};
		GetPrivateProfileStringW(L"xucRun", L"Language", L"",
			language, ARRAYSIZE(language), configPath.c_str());
		if (_wcsicmp(language, L"de") == 0)
			g_useGerman = true;
		else if (_wcsicmp(language, L"en") != 0)
			return false;
		return true;
	}

	const wchar_t* Text(const wchar_t* german, const wchar_t* english)
	{
		return g_useGerman ? german : english;
	}

	std::wstring WindowTitle()
	{
		std::wstringstream title;
		title << L"xucRun " << VERSION_MAJOR << L"." << VERSION_MINOR << L"."
			<< VERSION_PATCH << L"." << BUILD_NUMBER << L" - "
			<< Text(L"Freigabe", L"Approval");
		return title.str();
	}

	void ApplyApplicationIcon(HWND window)
	{
		static HICON largeIcon = static_cast<HICON>(LoadImageW(
			GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_XUCRUN_APPROVAL), IMAGE_ICON,
			GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
		static HICON smallIcon = static_cast<HICON>(LoadImageW(
			GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_XUCRUN_APPROVAL), IMAGE_ICON,
			GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
		static HICON redLargeIcon = xucrun::application_icon::CreateColorized(
			largeIcon, xucrun::application_icon::Color::Red);
		static HICON redSmallIcon = xucrun::application_icon::CreateColorized(
			smallIcon, xucrun::application_icon::Color::Red);
		HICON selectedLargeIcon = redLargeIcon ? redLargeIcon : largeIcon;
		HICON selectedSmallIcon = redSmallIcon ? redSmallIcon : smallIcon;

		if (selectedLargeIcon)
			SendMessageW(window, WM_SETICON, ICON_BIG,
				reinterpret_cast<LPARAM>(selectedLargeIcon));
		if (selectedSmallIcon)
			SendMessageW(window, WM_SETICON, ICON_SMALL,
				reinterpret_cast<LPARAM>(selectedSmallIcon));
	}

	LRESULT CALLBACK ApplyDialogIcon(int code, WPARAM wParam, LPARAM lParam)
	{
		if (code == HCBT_ACTIVATE)
		{
			HWND window = reinterpret_cast<HWND>(wParam);
			wchar_t className[16] = {};
			if (GetClassNameW(window, className, ARRAYSIZE(className)) &&
				wcscmp(className, L"#32770") == 0)
			{
				ApplyApplicationIcon(window);
				if (activeCredentialDialogTimeout)
				{
					activeCredentialDialogTimeout->window.store(window);
					if (activeCredentialDialogTimeout->timedOut.load())
						PostMessageW(window, WM_CLOSE, 0, 0);
				}
			}
		}
		return CallNextHookEx(nullptr, code, wParam, lParam);
	}

	int ShowMessageBox(const std::wstring& message, const std::wstring& title, UINT type)
	{
		HHOOK iconHook = SetWindowsHookExW(
			WH_CBT, ApplyDialogIcon, nullptr, GetCurrentThreadId());
		const int result = MessageBoxW(nullptr, message.c_str(), title.c_str(), type);
		if (iconHook)
			UnhookWindowsHookEx(iconHook);
		return result;
	}

	void WriteDefaultDialogFont(WORD* destination, std::size_t faceCapacity)
	{
		LOGFONTW font = {};
		GetObjectW(GetStockObject(DEFAULT_GUI_FONT), sizeof(font), &font);
		UINT dpi = 96;
		HDC screen = GetDC(nullptr);
		if (screen)
		{
			dpi = static_cast<UINT>(GetDeviceCaps(screen, LOGPIXELSY));
			ReleaseDC(nullptr, screen);
		}
		const LONG fontHeight = font.lfHeight < 0 ? -font.lfHeight : font.lfHeight;
		const WORD pointSize = static_cast<WORD>(MulDiv(fontHeight, 72, dpi));
		*destination++ = pointSize ? pointSize : 8;
		*destination++ = static_cast<WORD>(font.lfWeight ? font.lfWeight : FW_NORMAL);
		BYTE* attributes = reinterpret_cast<BYTE*>(destination);
		*attributes++ = font.lfItalic;
		*attributes++ = font.lfCharSet;
		wcscpy_s(reinterpret_cast<wchar_t*>(attributes), faceCapacity,
			font.lfFaceName[0] ? font.lfFaceName : L"MS Shell Dlg");
	}

	std::wstring ToWindowsLineEndings(const std::wstring& text)
	{
		std::wstring result;
		result.reserve(text.size() + 16);
		for (std::size_t index = 0; index < text.size(); index++)
		{
			if (text[index] == L'\n' && (index == 0 || text[index - 1] != L'\r'))
				result.push_back(L'\r');
			result.push_back(text[index]);
		}
		return result;
	}

	std::wstring AuthenticityMarkerText(const std::wstring& marker,
		const std::wstring& clientName)
	{
		return std::wstring(Text(
			L"xucRun-Sicherheitsmerkmal für ",
			L"xucRun security marker for ")) + clientName + L": " + marker;
	}

	bool ParseChallenge(const wchar_t* value, xucrun::broker::RequestNonce& challenge)
	{
		constexpr wchar_t Prefix[] = L"/challenge:";
		if (!value || _wcsnicmp(value, Prefix, std::size(Prefix) - 1) != 0)
			return false;
		const wchar_t* hex = value + std::size(Prefix) - 1;
		if (wcslen(hex) != challenge.size() * 2)
			return false;
		for (std::size_t index = 0; index < challenge.size(); index++)
		{
			wchar_t digits[3] = { hex[index * 2], hex[index * 2 + 1], L'\0' };
			wchar_t* end = nullptr;
			const unsigned long byte = wcstoul(digits, &end, 16);
			if (end != digits + 2 || byte > 0xFF)
				return false;
			challenge[index] = static_cast<std::uint8_t>(byte);
		}
		return true;
	}

	bool ParseCredentialChallenge(
		const wchar_t* value, xucrun::broker::RequestNonce& challenge)
	{
		constexpr wchar_t Prefix[] = L"/credentialchallenge:";
		if (!value || _wcsnicmp(value, Prefix, std::size(Prefix) - 1) != 0)
			return false;
		const wchar_t* hex = value + std::size(Prefix) - 1;
		if (wcslen(hex) != challenge.size() * 2)
			return false;
		for (std::size_t index = 0; index < challenge.size(); index++)
		{
			wchar_t digits[3] = { hex[index * 2], hex[index * 2 + 1], L'\0' };
			wchar_t* end = nullptr;
			const unsigned long byte = wcstoul(digits, &end, 16);
			if (end != digits + 2 || byte > 0xFF)
				return false;
			challenge[index] = static_cast<std::uint8_t>(byte);
		}
		return true;
	}

	DWORD SubmitCredentialCaptureCancellation(
		const xucrun::broker::RequestNonce& challengeId,
		const std::wstring& accountName)
	{
		xucrun::broker::CredentialCaptureSubmitRequest submit;
		submit.requestId = xucrun::broker::GenerateBrokerRequestId();
		submit.challengeId = challengeId;
		submit.cancelled = true;
		submit.enteredAccountName = accountName;
		const auto response =
			xucrun::broker::SubmitCredentialCaptureSubmitRequest(submit);
		return response.status == xucrun::broker::PipeCredentialSaveStatus::Succeeded
			? ERROR_CANCELLED
			: response.windowsError != NO_ERROR
				? response.windowsError : ERROR_BROKEN_PIPE;
	}

	DWORD RunCredentialCapture(const xucrun::broker::RequestNonce& challengeId)
	{
		xucrun::broker::CredentialCaptureHelperRequest helperRequest;
		helperRequest.requestId = xucrun::broker::GenerateBrokerRequestId();
		helperRequest.challengeId = challengeId;
		const auto helperResponse =
			xucrun::broker::SubmitCredentialCaptureHelperRequest(helperRequest);
		if (helperResponse.status != xucrun::broker::PipeCredentialSaveStatus::Succeeded ||
			helperResponse.result.status != xucrun::broker::ApprovalChallengeStatus::Issued)
		{
			return helperResponse.windowsError != NO_ERROR
				? helperResponse.windowsError : ERROR_ACCESS_DENIED;
		}
		const auto consumeChallenge = [&challengeId, &helperResponse]() {
			SubmitCredentialCaptureCancellation(
				challengeId, helperResponse.result.canonicalAccountName);
		};

		const std::wstring caption = WindowTitle();
		const std::wstring message = AuthenticityMarkerText(
			helperResponse.result.authenticityMarker,
			helperResponse.result.canonicalClientName) + L"\n\n" + std::wstring(Text(
			L"Zugangsdaten auf dem geschützten privaten Desktop eingeben für ",
			L"Enter credentials on the protected private desktop for ")) +
			helperResponse.result.canonicalAccountName;
		CREDUI_INFOW ui = {};
		ui.cbSize = sizeof(ui);
		ui.pszCaptionText = caption.c_str();
		ui.pszMessageText = message.c_str();
		std::vector<wchar_t> userName(CREDUI_MAX_USERNAME_LENGTH + 1);
		std::vector<wchar_t> password(CREDUI_MAX_PASSWORD_LENGTH + 1);
		wcscpy_s(userName.data(), userName.size(),
			helperResponse.result.canonicalAccountName.c_str());
		BOOL save = FALSE;
		CredentialDialogTimeoutState timeoutState;
		timeoutState.stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		timeoutState.timeoutMilliseconds =
			xucrun::broker::CredentialCaptureDialogTimeoutMilliseconds;
		HANDLE timeoutThread = timeoutState.stopEvent
			? CreateThread(nullptr, 0, CredentialDialogTimeoutThread,
				&timeoutState, 0, nullptr)
			: nullptr;
		if (!timeoutState.stopEvent || !timeoutThread)
		{
			const DWORD timeoutError = GetLastError();
			if (timeoutState.stopEvent) CloseHandle(timeoutState.stopEvent);
			consumeChallenge();
			return timeoutError == NO_ERROR ? ERROR_NOT_ENOUGH_MEMORY : timeoutError;
		}
		activeCredentialDialogTimeout = &timeoutState;
		HHOOK iconHook = SetWindowsHookExW(
			WH_CBT, ApplyDialogIcon, nullptr, GetCurrentThreadId());
		if (!iconHook)
		{
			const DWORD hookError = GetLastError();
			activeCredentialDialogTimeout = nullptr;
			SetEvent(timeoutState.stopEvent);
			WaitForSingleObject(timeoutThread, INFINITE);
			CloseHandle(timeoutThread);
			CloseHandle(timeoutState.stopEvent);
			consumeChallenge();
			return hookError == NO_ERROR ? ERROR_GEN_FAILURE : hookError;
		}
		const DWORD promptResult = CredUIPromptForCredentialsW(&ui,
			L"xucRun/CredentialCapture", nullptr, 0,
			userName.data(), static_cast<ULONG>(userName.size()),
			password.data(), static_cast<ULONG>(password.size()), &save,
			CREDUI_FLAGS_ALWAYS_SHOW_UI | CREDUI_FLAGS_DO_NOT_PERSIST |
			CREDUI_FLAGS_GENERIC_CREDENTIALS | CREDUI_FLAGS_KEEP_USERNAME |
			CREDUI_FLAGS_VALIDATE_USERNAME);
		if (iconHook)
			UnhookWindowsHookEx(iconHook);
		activeCredentialDialogTimeout = nullptr;
		SetEvent(timeoutState.stopEvent);
		WaitForSingleObject(timeoutThread, INFINITE);
		CloseHandle(timeoutThread);
		CloseHandle(timeoutState.stopEvent);
		const bool timedOut = timeoutState.timedOut.load();
		if (timedOut)
		{
			SecureZeroMemory(password.data(), password.size() * sizeof(wchar_t));
			SubmitCredentialCaptureCancellation(
				challengeId, helperResponse.result.canonicalAccountName);
			return ERROR_TIMEOUT;
		}
		if (promptResult == ERROR_CANCELLED)
		{
			SecureZeroMemory(password.data(), password.size() * sizeof(wchar_t));
			return SubmitCredentialCaptureCancellation(
				challengeId, helperResponse.result.canonicalAccountName);
		}
		if (promptResult != ERROR_SUCCESS)
		{
			SecureZeroMemory(password.data(), password.size() * sizeof(wchar_t));
			consumeChallenge();
			return promptResult;
		}

		xucrun::broker::CredentialCaptureSubmitRequest submit;
		submit.requestId = xucrun::broker::GenerateBrokerRequestId();
		submit.challengeId = challengeId;
		submit.enteredAccountName = userName.data();
		const std::size_t passwordCharacters = wcsnlen_s(password.data(), password.size());
		if (!submit.password.assign(password.data(), passwordCharacters))
		{
			SecureZeroMemory(password.data(), password.size() * sizeof(wchar_t));
			consumeChallenge();
			return ERROR_NOT_ENOUGH_MEMORY;
		}
		SecureZeroMemory(password.data(), password.size() * sizeof(wchar_t));
		const auto submitResponse =
			xucrun::broker::SubmitCredentialCaptureSubmitRequest(submit);
		submit.password.clear();
		if (submitResponse.status != xucrun::broker::PipeCredentialSaveStatus::Succeeded)
			return submitResponse.windowsError != NO_ERROR
				? submitResponse.windowsError : ERROR_BROKEN_PIPE;
		return submitResponse.result.status == xucrun::broker::CredentialSaveStatus::Succeeded
			? ERROR_SUCCESS
			: submitResponse.result.windowsError != NO_ERROR
				? submitResponse.result.windowsError : ERROR_GEN_FAILURE;
	}

	std::wstring Hex(const std::array<std::uint8_t, 32>& value)
	{
		constexpr wchar_t Digits[] = L"0123456789ABCDEF";
		std::wstring text;
		text.reserve(value.size() * 2);
		for (const std::uint8_t byte : value)
		{
			text.push_back(Digits[byte >> 4]);
			text.push_back(Digits[byte & 0x0F]);
		}
		return text;
	}

	std::wstring TrustStatus(std::int32_t status, bool trusted)
	{
		wchar_t statusCode[11] = {};
		swprintf_s(statusCode, L"0x%08X", static_cast<std::uint32_t>(status));
		return std::wstring(trusted
			? Text(L"Vertrauenswürdig", L"Trusted")
			: Text(L"Nicht vertrauenswürdig", L"Not trusted")) + L" (" + statusCode + L")";
	}

	bool ActivateApprovalWindow(HWND window)
	{
		SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
		BringWindowToTop(window);
		bool activated = SetForegroundWindow(window) == TRUE;
		if (!activated)
		{
			const DWORD currentThreadId = GetCurrentThreadId();
			const HWND foregroundWindow = GetForegroundWindow();
			const DWORD foregroundThreadId = foregroundWindow
				? GetWindowThreadProcessId(foregroundWindow, nullptr)
				: 0;
			const bool attached = foregroundThreadId != 0 &&
				foregroundThreadId != currentThreadId &&
				AttachThreadInput(currentThreadId, foregroundThreadId, TRUE) == TRUE;
			BringWindowToTop(window);
			activated = SetForegroundWindow(window) == TRUE;
			if (attached)
				AttachThreadInput(currentThreadId, foregroundThreadId, FALSE);
		}
		if (activated)
		{
			SetActiveWindow(window);
			return true;
		}
		FLASHWINFO flash = { sizeof(flash), window,
			FLASHW_TRAY | FLASHW_TIMERNOFG, 3, 0 };
		FlashWindowEx(&flash);
		return false;
	}

	struct ChoiceDialogContext
	{
		const std::wstring* authenticityMarker;
		const std::wstring* clientName;
		bool* timedOut;
	};

	std::wstring ChoiceFooter(const std::wstring& authenticityMarker,
		const std::wstring& clientName, std::uint32_t remainingSeconds)
	{
		return AuthenticityMarkerText(authenticityMarker, clientName) + L"  |  " +
			Text(L"Bedenkzeit: ", L"Time remaining: ") +
			std::to_wstring(remainingSeconds) + Text(L" Sekunden", L" seconds");
	}

	HRESULT CALLBACK CountdownCallback(HWND window, UINT notification,
		WPARAM value, LPARAM, LONG_PTR referenceData)
	{
		if (notification == TDN_CREATED)
		{
			ApplyApplicationIcon(window);
			ActivateApprovalWindow(window);
			return S_OK;
		}
		if (notification != TDN_TIMER)
			return S_OK;
		if (value <= 3000 && GetForegroundWindow() != window)
			ActivateApprovalWindow(window);
		const std::uint32_t elapsedSeconds = static_cast<std::uint32_t>(value) / 1000;
		const std::uint32_t lifetimeSeconds =
			xucrun::broker::ApprovalDialogTimeoutMilliseconds / 1000;
		const std::uint32_t remainingSeconds = elapsedSeconds < lifetimeSeconds
			? lifetimeSeconds - elapsedSeconds : 0;
		auto* context = reinterpret_cast<ChoiceDialogContext*>(referenceData);
		if (!context || !context->authenticityMarker || !context->clientName ||
			!context->timedOut)
			return E_INVALIDARG;
		const std::wstring countdown = ChoiceFooter(
			*context->authenticityMarker, *context->clientName, remainingSeconds);
		SendMessageW(window, TDM_SET_ELEMENT_TEXT, TDE_FOOTER,
			reinterpret_cast<LPARAM>(countdown.c_str()));
		if (remainingSeconds == 0)
		{
			*context->timedOut = true;
			SendMessageW(window, TDM_CLICK_BUTTON, IDCANCEL, 0);
		}
		return S_OK;
	}

	bool RefreshLease(const xucrun::broker::RequestNonce& challengeId)
	{
		xucrun::broker::ApprovalHelperRequest request;
		request.requestId = xucrun::broker::GenerateBrokerRequestId();
		request.challengeId = challengeId;
		const auto response = xucrun::broker::SubmitApprovalHelperLeaseRequest(request);
		return response.status == xucrun::broker::PipeApprovalHelperStatus::Succeeded &&
			response.result.status ==
				xucrun::broker::ApprovalConfirmationStatus::Confirmed;
	}

	int ShowChoice(const xucrun::broker::RequestNonce& challengeId,
		const std::wstring& authenticityMarker,
		const std::wstring& clientName,
		const std::wstring& instruction, const std::wstring& content,
		const TASKDIALOG_BUTTON* buttons, UINT buttonCount, bool& timedOut)
	{
		if (!RefreshLease(challengeId))
			return IDCANCEL;
		TASKDIALOGCONFIG config = {};
		config.cbSize = sizeof(config);
		config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT |
			TDF_CALLBACK_TIMER;
		config.dwCommonButtons = TDCBF_CANCEL_BUTTON;
		const std::wstring title = WindowTitle();
		config.pszWindowTitle = title.c_str();
		config.pszMainIcon = TD_SHIELD_ICON;
		config.pszMainInstruction = instruction.c_str();
		config.pszContent = content.c_str();
		config.cButtons = buttonCount;
		config.pButtons = buttons;
		config.nDefaultButton = IDCANCEL;
		const std::wstring footer = ChoiceFooter(authenticityMarker, clientName,
			xucrun::broker::ApprovalDialogTimeoutMilliseconds / 1000);
		config.pszFooter = footer.c_str();
		config.pfCallback = CountdownCallback;
		ChoiceDialogContext context = { &authenticityMarker, &clientName, &timedOut };
		config.lpCallbackData = reinterpret_cast<LONG_PTR>(&context);
		int selected = IDCANCEL;
		return SUCCEEDED(TaskDialogIndirect(&config, &selected, nullptr, nullptr))
			? selected : IDCANCEL;
	}

	struct ApprovalDialogContext
	{
		xucrun::broker::ApprovalHelperDecisionRequest* decision;
		const std::wstring* fixedDetails;
		const std::wstring* details;
		const std::wstring* authenticityMarker;
		const std::wstring* clientName;
		InterpreterWarningType interpreterWarningType;
		std::uint32_t elapsedSeconds;
		bool foregroundActivated;
		bool* timedOut;
	};

	void SetControlFont(HWND control, HFONT font)
	{
		SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
	}

	HWND AddControl(HWND dialog, const wchar_t* className, const std::wstring& text,
		DWORD style, int x, int y, int width, int height, int id, HFONT font,
		DWORD extendedStyle = 0)
	{
		RECT bounds = { x, y, x + width, y + height };
		MapDialogRect(dialog, &bounds);
		HWND control = CreateWindowExW(extendedStyle, className, text.c_str(),
			WS_CHILD | WS_VISIBLE | style, bounds.left, bounds.top,
			bounds.right - bounds.left, bounds.bottom - bounds.top, dialog,
			reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr),
			nullptr);
		SetControlFont(control, font);
		return control;
	}

	void UpdateCountdown(HWND dialog, std::uint32_t elapsedSeconds)
	{
		const std::uint32_t lifetimeSeconds =
			xucrun::broker::ApprovalDialogTimeoutMilliseconds / 1000;
		const std::uint32_t remainingSeconds = elapsedSeconds < lifetimeSeconds
			? lifetimeSeconds - elapsedSeconds : 0;
		const std::wstring countdown = std::wstring(Text(
			L"Bedenkzeit: ", L"Time remaining: ")) +
			std::to_wstring(remainingSeconds) + Text(L" Sekunden", L" seconds");
		SetWindowTextW(GetDlgItem(dialog, ApprovalCountdownControl), countdown.c_str());
	}

	INT_PTR CALLBACK ApprovalDialogProc(HWND dialog, UINT message, WPARAM wParam,
		LPARAM lParam)
	{
		switch (message)
		{
		case WM_INITDIALOG:
		{
			auto* context = reinterpret_cast<ApprovalDialogContext*>(lParam);
			SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(context));
			SetWindowTextW(dialog, WindowTitle().c_str());
			ApplyApplicationIcon(dialog);
			HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

			AddControl(dialog, L"STATIC", Text(
				L"Start unter einem anderen Benutzerkonto freigeben?",
				L"Approve launch under another user account?"),
				SS_LEFT, 8, 8, 374, 12, 0, font);
			const std::wstring formattedFixedDetails =
				ToWindowsLineEndings(*context->fixedDetails);
			AddControl(dialog, L"STATIC", formattedFixedDetails,
				SS_LEFT, 8, 24, 374, 43, 0, font);
			const std::wstring formattedDetails = ToWindowsLineEndings(*context->details);
			HWND detailsControl = AddControl(dialog, L"EDIT", formattedDetails,
				ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
				8, 69, 374, 92, ApprovalDetailsControl, font, WS_EX_CLIENTEDGE);
			SendMessageW(detailsControl, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
				MAKELPARAM(6, 6));

			const bool showInterpreterWarning =
				context->interpreterWarningType != InterpreterWarningType::None;
			const int layoutOffset = showInterpreterWarning
				? ApprovalInterpreterLayoutOffset : 0;
			if (showInterpreterWarning)
			{
				const std::wstring warning = context->interpreterWarningType ==
					InterpreterWarningType::PowerShell
					? Text(
						L"WARNUNG: PowerShell kann beliebige Befehle und Skripte ausführen. "
						L"Eine wiederverwendbare Freigabe ist nicht an die hier angezeigten "
						L"Argumente gebunden und kann später für andere PowerShell-Befehle "
						L"verwendet werden. Prüfen Sie daher Ziel, Argumente und Arbeitsverzeichnis "
						L"besonders sorgfältig und wählen Sie möglichst „Nur dieses eine Mal“.\r\n"
						L"Eine administrativ erzwungene AllSigned-Richtlinie kann "
						L"PowerShell-Skriptdateien zusätzlich absichern, schützt jedoch nicht vor "
						L"direkten oder interaktiven Befehlen.",
						L"WARNING: PowerShell can run arbitrary commands and scripts. A reusable "
						L"approval is not bound to the arguments shown here and can later be used "
						L"for other PowerShell commands. Carefully verify the target, arguments, "
						L"and working directory, and choose \"This time only\" whenever possible.\r\n"
						L"An administratively enforced AllSigned policy can additionally protect "
						L"PowerShell script files, but does not protect against direct or "
						L"interactive commands.")
					: Text(
						L"WARNUNG: Dieses Ziel ist ein universeller Befehls- oder "
						L"Skriptinterpreter. Eine wiederverwendbare Freigabe ist nicht an die "
						L"hier angezeigten Argumente gebunden und kann später für andere Befehle "
						L"verwendet werden. Prüfen Sie daher Ziel, Argumente und Arbeitsverzeichnis "
						L"besonders sorgfältig und wählen Sie möglichst „Nur dieses eine Mal“.",
						L"WARNING: This target is a universal command or script interpreter. "
						L"A reusable approval is not bound to the arguments shown here and can "
						L"later be used for other commands. Carefully verify the target, arguments, "
						L"and working directory, and choose \"This time only\" whenever possible.");
				AddControl(dialog, L"STATIC", warning, SS_LEFT,
					8, ApprovalInterpreterWarningY, 374,
					ApprovalInterpreterWarningHeight, 0, font);
			}

			AddControl(dialog, L"STATIC", Text(L"Geltungsbereich:", L"Scope:"),
				SS_LEFT, 8, 167 + layoutOffset, 180, 10, 0, font);
			AddControl(dialog, L"BUTTON", Text(L"Nur für dieses Zielprogramm",
				L"Only for this target program"),
				BS_AUTORADIOBUTTON | WS_TABSTOP | WS_GROUP,
				11, 179 + layoutOffset, 210, 11,
				ApprovalTargetProgram, font);
			AddControl(dialog, L"BUTTON", Text(L"Für alle Programme",
				L"For all programs"), BS_AUTORADIOBUTTON | WS_TABSTOP,
				11, 192 + layoutOffset, 210, 11, ApprovalProgramGeneral, font);
			SendMessageW(GetDlgItem(dialog, ApprovalTargetProgram), BM_SETCHECK,
				BST_CHECKED, 0);

			AddControl(dialog, L"STATIC", Text(L"Kontobereich:", L"Account scope:"),
				SS_LEFT, 8, 208 + layoutOffset, 180, 10, 0, font);
			AddControl(dialog, L"BUTTON", Text(L"Nur für dieses Zielkonto",
				L"Only for this target account"),
				BS_AUTORADIOBUTTON | WS_TABSTOP | WS_GROUP,
				11, 220 + layoutOffset, 210, 11,
				ApprovalAccountCurrent, font);
			AddControl(dialog, L"BUTTON", Text(L"Für alle gespeicherten Zielkonten",
				L"For all stored target accounts"), BS_AUTORADIOBUTTON | WS_TABSTOP,
				11, 233 + layoutOffset, 210, 11, ApprovalAccountAll, font);
			SendMessageW(GetDlgItem(dialog, ApprovalAccountCurrent), BM_SETCHECK,
				BST_CHECKED, 0);

			AddControl(dialog, L"STATIC", Text(L"Freigabedauer:", L"Approval duration:"),
				SS_LEFT, 8, 249 + layoutOffset, 180, 10, 0, font);
			const wchar_t* choices[] = {
				Text(L"Nur dieses eine Mal", L"This time only"),
				Text(L"Für 10 Minuten", L"For 10 minutes"),
				Text(L"Für einen Tag", L"For one day"),
				Text(L"Für diese Anmeldung", L"For this logon session"),
				Text(L"Dauerhaft", L"Permanently")
			};
			for (int index = 0; index < static_cast<int>(std::size(choices)); index++)
			{
				AddControl(dialog, L"BUTTON", choices[index], BS_AUTORADIOBUTTON |
					WS_TABSTOP | (index == 0 ? WS_GROUP : 0),
					11, 261 + layoutOffset + index * 13,
					180, 11, ApprovalDurationFirst + index, font);
			}
			SendMessageW(GetDlgItem(dialog, ApprovalDurationFirst), BM_SETCHECK,
				BST_CHECKED, 0);
			EnableWindow(GetDlgItem(dialog, ApprovalAccountAll), FALSE);

			AddControl(dialog, L"STATIC", AuthenticityMarkerText(
				*context->authenticityMarker, *context->clientName), SS_CENTER,
				8, ApprovalMarkerY + layoutOffset, 374,
				ApprovalMarkerHeight, 0, font);
			AddControl(dialog, L"STATIC", L"", SS_LEFT,
				11, 361 + layoutOffset, 130, 10,
				ApprovalCountdownControl, font);
			AddControl(dialog, L"BUTTON", Text(L"Freigeben", L"Approve"),
				BS_DEFPUSHBUTTON | WS_TABSTOP,
				278, ApprovalButtonY + layoutOffset, 50,
				ApprovalButtonHeight, IDOK, font);
			AddControl(dialog, L"BUTTON", Text(L"Abbrechen", L"Cancel"),
				BS_PUSHBUTTON | WS_TABSTOP,
				332, ApprovalButtonY + layoutOffset, 50,
				ApprovalButtonHeight, IDCANCEL, font);

			UpdateCountdown(dialog, 0);
			SetTimer(dialog, ApprovalCountdownTimer, 1000, nullptr);
			context->foregroundActivated = ActivateApprovalWindow(dialog);
			SetFocus(GetDlgItem(dialog, ApprovalDurationFirst));
			MessageBeep(MB_ICONQUESTION);
			return FALSE;
		}

		case WM_TIMER:
		{
			auto* context = reinterpret_cast<ApprovalDialogContext*>(
				GetWindowLongPtrW(dialog, DWLP_USER));
			if (!context || wParam != ApprovalCountdownTimer)
				return FALSE;
			context->elapsedSeconds++;
			if (!context->foregroundActivated && context->elapsedSeconds <= 3)
				context->foregroundActivated = ActivateApprovalWindow(dialog);
			UpdateCountdown(dialog, context->elapsedSeconds);
			if (context->elapsedSeconds >=
				xucrun::broker::ApprovalDialogTimeoutMilliseconds / 1000)
			{
				if (context->timedOut)
					*context->timedOut = true;
				SendMessageW(dialog, WM_COMMAND,
					MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
			}
			return TRUE;
		}

		case WM_COMMAND:
		{
			auto* context = reinterpret_cast<ApprovalDialogContext*>(
				GetWindowLongPtrW(dialog, DWLP_USER));
			if (!context || !context->decision)
				return FALSE;
			if (LOWORD(wParam) >= ApprovalDurationFirst &&
				LOWORD(wParam) <= ApprovalDurationFirst + 4 && HIWORD(wParam) == BN_CLICKED)
			{
				const bool once = LOWORD(wParam) == ApprovalDurationFirst;
				EnableWindow(GetDlgItem(dialog, ApprovalAccountAll), !once);
				if (once)
				{
					SendMessageW(GetDlgItem(dialog, ApprovalAccountCurrent), BM_SETCHECK,
						BST_CHECKED, 0);
					SendMessageW(GetDlgItem(dialog, ApprovalAccountAll), BM_SETCHECK,
						BST_UNCHECKED, 0);
				}
				return TRUE;
			}
			if (LOWORD(wParam) == IDOK)
			{
				for (int index = 0; index < 5; index++)
				{
					if (SendMessageW(GetDlgItem(dialog, ApprovalDurationFirst + index),
						BM_GETCHECK, 0, 0) == BST_CHECKED)
					{
						context->decision->persistence =
							static_cast<xucrun::broker::ApprovalPersistence>(index);
						break;
					}
				}
				context->decision->programScope = SendMessageW(GetDlgItem(dialog,
					ApprovalProgramGeneral), BM_GETCHECK, 0, 0) == BST_CHECKED
					? xucrun::broker::ApprovalProgramScope::General
					: xucrun::broker::ApprovalProgramScope::TargetProgram;
				context->decision->accountScope = SendMessageW(GetDlgItem(dialog,
					ApprovalAccountAll), BM_GETCHECK, 0, 0) == BST_CHECKED
					? xucrun::broker::ApprovalAccountScope::AllAccounts
					: xucrun::broker::ApprovalAccountScope::CurrentAccount;
				KillTimer(dialog, ApprovalCountdownTimer);
				EndDialog(dialog, IDOK);
				return TRUE;
			}
			if (LOWORD(wParam) == IDCANCEL)
			{
				KillTimer(dialog, ApprovalCountdownTimer);
				EndDialog(dialog, IDCANCEL);
				return TRUE;
			}
			break;
		}

		case WM_CLOSE:
			KillTimer(dialog, ApprovalCountdownTimer);
			EndDialog(dialog, IDCANCEL);
			return TRUE;
		}
		return FALSE;
	}

	bool ShowApprovalDialog(const xucrun::broker::ApprovalChallengeResult& challenge,
		xucrun::broker::ApprovalHelperDecisionRequest& decision,
		const std::wstring& fixedDetails, const std::wstring& details,
		bool& timedOut)
	{
		if (!RefreshLease(challenge.challengeId))
			return false;
		struct DialogTemplateHeader
		{
			WORD dlgVer;
			WORD signature;
			DWORD helpId;
			DWORD extendedStyle;
			DWORD style;
			WORD itemCount;
			short x;
			short y;
			short width;
			short height;
		};
		alignas(DWORD) BYTE buffer[1024] = {};
		auto* dialogTemplate = reinterpret_cast<DialogTemplateHeader*>(buffer);
		dialogTemplate->dlgVer = 1;
		dialogTemplate->signature = 0xFFFF;
		dialogTemplate->style = DS_SETFONT | DS_CENTER | WS_POPUP | WS_CAPTION |
			WS_SYSMENU | WS_BORDER;
		dialogTemplate->width = 390;
		const InterpreterWarningType interpreterWarningType =
			GetInterpreterWarningType(challenge.finalExecutablePath);
		dialogTemplate->height = ApprovalDialogHeight +
			(interpreterWarningType != InterpreterWarningType::None
				? ApprovalInterpreterLayoutOffset : 0);
		WORD* data = reinterpret_cast<WORD*>(buffer + sizeof(DialogTemplateHeader));
		*data++ = 0;
		*data++ = 0;
		const std::wstring title = WindowTitle();
		wcscpy_s(reinterpret_cast<wchar_t*>(data), 256, title.c_str());
		data += title.size() + 1;
		WriteDefaultDialogFont(data, 50);

		ApprovalDialogContext context = {
			&decision, &fixedDetails, &details, &challenge.authenticityMarker,
			&challenge.canonicalClientName, interpreterWarningType,
			0, false, &timedOut
		};
		return DialogBoxIndirectParamW(GetModuleHandleW(nullptr),
			reinterpret_cast<DLGTEMPLATE*>(buffer), nullptr, ApprovalDialogProc,
			reinterpret_cast<LPARAM>(&context)) == IDOK;
	}

	bool SelectDecision(const xucrun::broker::ApprovalChallengeResult& challenge,
		xucrun::broker::ApprovalHelperDecisionRequest& decision, bool& timedOut)
	{
		timedOut = false;
		if (!challenge.approvalHelperView)
			return false;
		std::wstring launchDetails;
		switch (challenge.helperStartMode)
		{
		case xucrun::broker::ApprovalHelperStartMode::BrokerStarted:
			launchDetails = Text(
				L"Der xucRun-Dienst hat diesen Freigabeassistenten mit hoher Integrität gestartet, "
				L"damit Sie diese Freigabe auf einem geschützten privaten Desktop bestätigen können.",
				L"The xucRun service started this approval helper with high integrity so that you "
				L"can confirm this approval on a protected private desktop.");
			break;
		case xucrun::broker::ApprovalHelperStartMode::ClientRunAsRequired:
			launchDetails = Text(
				L"Der xucRun-Client hat diesen Freigabeassistenten über Windows-UAC mit hoher "
				L"Integrität gestartet, damit Sie diese Freigabe auf einem geschützten privaten "
				L"Desktop bestätigen können.",
				L"The xucRun client started this approval helper with high integrity through "
				L"Windows UAC so that you can confirm this approval on a protected private desktop.");
			break;
		default:
			return false;
		}
		const std::wstring accountDetails = std::wstring(Text(
			L"Aufrufendes Konto: ", L"Calling account: ")) + challenge.canonicalClientName +
			Text(L"\nZielkonto: ", L"\nTarget account: ") + challenge.canonicalAccountName;
		const std::wstring details = std::wstring(Text(
			L"Geprüfter xucRun-Client: ", L"Verified xucRun client: ")) + challenge.clientPath +
			L"\nSHA-256: " + Hex(challenge.clientSha256) +
			Text(L"\nAuthenticode: ", L"\nAuthenticode: ") +
			TrustStatus(challenge.clientTrustStatus, challenge.clientSignatureTrusted) +
			Text(L"\n\nZiel: ", L"\n\nTarget: ") + challenge.finalExecutablePath +
			L"\nSHA-256: " + Hex(challenge.executableSha256) +
			Text(L"\nAuthenticode: ", L"\nAuthenticode: ") +
			TrustStatus(challenge.executableTrustStatus, challenge.executableSignatureTrusted) +
			Text(L"\n\nArgumente: ", L"\n\nArguments: ") + challenge.argumentTail +
			Text(L"\nArbeitsverzeichnis: ", L"\nWorking directory: ") +
			challenge.workingDirectory;
		const std::wstring fixedDetails = launchDetails + L"\n\n" + accountDetails;
		const std::wstring firstStepDetails = fixedDetails + L"\n\n" + details;
		if (challenge.writableElevatedTargetReason !=
			xucrun::broker::ApprovalFailureReason::None)
		{
			const TASKDIALOG_BUTTON buttons[] = {
				{ WritableTargetApproveButton, Text(
					L"Trotzdem einmalig erhöht ausführen",
					L"Run elevated once anyway") }
			};
			const std::wstring risk = Text(
				L"Der aufrufende Benutzer besitzt Schreibrechte für die Startdatei. "
				L"Sie könnte vor dem erhöhten Start verändert werden. xucRun prüft "
				L"Pfad und Dateiinhalt unmittelbar vor dem Start erneut und blockiert "
				L"eine veränderte Datei.",
				L"The calling user has write permissions for the launch file. "
				L"It could be modified before the elevated launch. xucRun checks "
				L"the path and file contents again immediately before launch and blocks "
				L"a changed file.");
			if (ShowChoice(challenge.challengeId, challenge.authenticityMarker,
				challenge.canonicalClientName,
				Text(L"Unsicheres erhöhtes Ziel einmalig starten?",
					L"Run unsafe elevated target once?"),
				risk + L"\n\n" + firstStepDetails,
				buttons, static_cast<UINT>(std::size(buttons)), timedOut) !=
				WritableTargetApproveButton)
			{
				return false;
			}
			decision.decision = xucrun::broker::ApprovalDecision::ApproveOnce;
			decision.persistence = xucrun::broker::ApprovalPersistence::Once;
			decision.programScope =
				xucrun::broker::ApprovalProgramScope::TargetProgram;
			decision.accountScope =
				xucrun::broker::ApprovalAccountScope::CurrentAccount;
			decision.updateScope = xucrun::broker::ApprovalUpdateScope::None;
			return true;
		}
		if (challenge.storedApprovalPresenceConfirmation)
		{
			decision.decision = xucrun::broker::ApprovalDecision::ApproveOnce;
			decision.persistence = xucrun::broker::ApprovalPersistence::Once;
			decision.programScope =
				xucrun::broker::ApprovalProgramScope::TargetProgram;
			decision.accountScope =
				xucrun::broker::ApprovalAccountScope::CurrentAccount;
			decision.updateScope = xucrun::broker::ApprovalUpdateScope::None;
			return true;
		}

		if (challenge.programUpdateAvailable)
		{
			const TASKDIALOG_BUTTON buttons[] = {
				{ UpdateCurrentButton, Text(L"Änderung für dieses Zielkonto übernehmen",
					L"Accept change for this target account") },
				{ UpdateAllButton, Text(L"Änderung für alle Zielkonten übernehmen",
					L"Accept change for all target accounts") }
			};
			const int selected = ShowChoice(challenge.challengeId,
				challenge.authenticityMarker, challenge.canonicalClientName,
				Text(L"Geänderten Programmhash freigeben?",
				L"Approve the changed program hash?"),
				firstStepDetails + Text(L"\n\nGeändertes Programm: ", L"\n\nChanged program: ") +
				challenge.changedProgramPath + Text(L"\nAlter SHA-256: ", L"\nOld SHA-256: ") +
				Hex(challenge.oldProgramSha256) + Text(L"\nNeuer SHA-256: ", L"\nNew SHA-256: ") +
				Hex(challenge.newProgramSha256),
				buttons, static_cast<UINT>(std::size(buttons)), timedOut);
			if (selected != UpdateCurrentButton && selected != UpdateAllButton)
				return false;
			decision.updateScope = selected == UpdateAllButton
				? xucrun::broker::ApprovalUpdateScope::AllAccounts
				: xucrun::broker::ApprovalUpdateScope::CurrentAccount;
			return true;
		}

		return ShowApprovalDialog(challenge, decision, fixedDetails, details, timedOut);
	}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	if (!ApplyEarlyProcessHardening())
		return ERROR_ACCESS_DENIED;
	if (!InitializeLanguage())
		return ERROR_INVALID_DATA;

	int argc = 0;
	wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
#ifdef _DEBUG
	if (argv && argc == 2 &&
		_wcsicmp(argv[1], L"/credential-dialog-timeout-test") == 0)
	{
		LocalFree(argv);
		return RunCredentialDialogTimeoutSelfTest() ? ERROR_SUCCESS : ERROR_GEN_FAILURE;
	}
#endif
	if (argv && (argc == 3 || argc == 4) &&
		_wcsicmp(argv[1], L"/secure-desktop-bootstrap") == 0)
	{
		wchar_t helperPath[32768] = {};
		const DWORD pathLength = GetModuleFileNameW(
			nullptr, helperPath, static_cast<DWORD>(std::size(helperPath)));
		HANDLE processToken = nullptr;
		HANDLE helperToken = nullptr;
		constexpr wchar_t CancelEventPrefix[] = L"/cancel-event:";
		const bool brokerStarted = argc == 4;
		const wchar_t* cancellationEventName = brokerStarted &&
			_wcsnicmp(argv[2], CancelEventPrefix, std::size(CancelEventPrefix) - 1) == 0
				? argv[2] + std::size(CancelEventPrefix) - 1 : nullptr;
		HANDLE cancellationEvent = cancellationEventName && *cancellationEventName
			? OpenEventW(SYNCHRONIZE, FALSE, cancellationEventName) : nullptr;
		const bool tokenReady = (!brokerStarted || cancellationEvent) &&
			pathLength != 0 &&
			pathLength < std::size(helperPath) &&
			OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE,
				&processToken) == TRUE &&
			DuplicateTokenEx(processToken,
				TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE | TOKEN_QUERY |
				TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID,
				nullptr, SecurityImpersonation, TokenPrimary, &helperToken) == TRUE;
		const DWORD tokenError = tokenReady ? NO_ERROR : GetLastError();
		if (processToken)
			CloseHandle(processToken);
		const std::wstring helperArgument = argv[brokerStarted ? 3 : 2];
		LocalFree(argv);
		if (!tokenReady)
		{
			if (helperToken) CloseHandle(helperToken);
			if (cancellationEvent) CloseHandle(cancellationEvent);
			return static_cast<int>(tokenError == NO_ERROR
				? ERROR_ACCESS_DENIED : tokenError);
		}
		std::wstring workingDirectory(helperPath, pathLength);
		const std::size_t separator = workingDirectory.find_last_of(L"\\/");
		workingDirectory = separator == std::wstring::npos
			? std::wstring() : workingDirectory.substr(0, separator);
		int result = xucrun::secure_desktop::RunHost(helperToken,
			helperPath, helperArgument.c_str(), workingDirectory.c_str(), nullptr,
			cancellationEvent, false);
		CloseHandle(helperToken);
		if (cancellationEvent) CloseHandle(cancellationEvent);
		xucrun::broker::RequestNonce credentialChallengeId = {};
		if (result == ERROR_SUCCESS && ParseCredentialChallenge(
			helperArgument.c_str(), credentialChallengeId))
		{
			xucrun::broker::CredentialCaptureCommitRequest request;
			request.requestId = xucrun::broker::GenerateBrokerRequestId();
			request.challengeId = credentialChallengeId;
			const auto response =
				xucrun::broker::SubmitCredentialCaptureCommitRequest(request);
			if (response.status != xucrun::broker::PipeCredentialSaveStatus::Succeeded ||
				response.result.status != xucrun::broker::CredentialSaveStatus::Succeeded)
			{
				result = static_cast<int>(response.status ==
					xucrun::broker::PipeCredentialSaveStatus::Succeeded
					? response.result.windowsError : response.windowsError);
				if (result == ERROR_SUCCESS)
					result = ERROR_GEN_FAILURE;
			}
		}
		return result;
	}
	xucrun::broker::RequestNonce challengeId = {};
	const bool credentialCapture = argc == 2 &&
		ParseCredentialChallenge(argv[1], challengeId);
	const bool approval = !credentialCapture && argc == 2 &&
		ParseChallenge(argv[1], challengeId);
	if (argv)
		LocalFree(argv);
	if (!credentialCapture && !approval)
		return ERROR_INVALID_PARAMETER;
	if (credentialCapture)
		return RunCredentialCapture(challengeId);

	xucrun::broker::ApprovalHelperRequest request;
	request.requestId = xucrun::broker::GenerateBrokerRequestId();
	request.challengeId = challengeId;
	const auto response = xucrun::broker::SubmitApprovalHelperRequest(request);
	if (response.status != xucrun::broker::PipeApprovalHelperStatus::Succeeded ||
		response.result.status != xucrun::broker::ApprovalChallengeStatus::Issued)
	{
		ShowMessageBox(Text(L"Die Freigabe konnte nicht geladen werden.",
			L"The approval could not be loaded."),
			WindowTitle(),
			MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
		return ERROR_ACCESS_DENIED;
	}

	xucrun::broker::ApprovalHelperDecisionRequest decision;
	decision.requestId = xucrun::broker::GenerateBrokerRequestId();
	decision.challengeId = challengeId;
	decision.decision = xucrun::broker::ApprovalDecision::ApproveOnce;
	bool timedOut = false;
	if (!SelectDecision(response.result, decision, timedOut))
	{
		decision.persistence = xucrun::broker::ApprovalPersistence::Once;
		decision.programScope = xucrun::broker::ApprovalProgramScope::TargetProgram;
		decision.accountScope = xucrun::broker::ApprovalAccountScope::CurrentAccount;
		decision.updateScope = xucrun::broker::ApprovalUpdateScope::None;
		decision.decision = xucrun::broker::ApprovalDecision::Deny;
	}
	const auto decisionResponse =
		xucrun::broker::SubmitApprovalHelperDecisionRequest(decision);
	if (decisionResponse.status != xucrun::broker::PipeApprovalHelperStatus::Succeeded)
		return decisionResponse.windowsError != NO_ERROR
			? decisionResponse.windowsError : ERROR_BROKEN_PIPE;
	if (timedOut)
		return ERROR_TIMEOUT;
	return decisionResponse.result.status ==
		xucrun::broker::ApprovalConfirmationStatus::Confirmed
		? ERROR_SUCCESS
		: decisionResponse.result.windowsError != NO_ERROR
			? decisionResponse.result.windowsError : ERROR_CANCELLED;
}