// Purpose: Checks the running client for tampering and injection indicators.
// Behavior: Inspects debuggers, API entry points, loaded modules, and their signature trust.
#include "ClientSecurityAudit.h"

#include <Windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>

#include "BrokerContracts.h"
#include "BrokerPipeClient.h"
#include "ClientImageTrust.h"
#include "ClientLogging.h"
#include "ClientTextDialog.h"
#include "ClientUiCommon.h"
#include "ClientWindowsError.h"
#include "Localization.h"

namespace
{
	bool HasExecutableProtection(DWORD protection)
	{
		if ((protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
			return false;

		switch (protection & 0xFF)
		{
		case PAGE_EXECUTE:
		case PAGE_EXECUTE_READ:
		case PAGE_EXECUTE_READWRITE:
		case PAGE_EXECUTE_WRITECOPY:
			return true;
		default:
			return false;
		}
	}

	bool IsExecutableSystemImageAddress(const void* address)
	{
		MEMORY_BASIC_INFORMATION memoryInfo = {};
		if (VirtualQuery(address, &memoryInfo, sizeof(memoryInfo)) != sizeof(memoryInfo) ||
			memoryInfo.State != MEM_COMMIT || memoryInfo.Type != MEM_IMAGE ||
			!HasExecutableProtection(memoryInfo.Protect))
		{
			return false;
		}

		wchar_t systemDirectory[MAX_PATH] = {};
		wchar_t ownerPath[MAX_PATH] = {};
		const UINT systemDirectoryLength = GetSystemDirectoryW(systemDirectory, MAX_PATH);
		const DWORD ownerPathLength = GetModuleFileNameW(
			reinterpret_cast<HMODULE>(memoryInfo.AllocationBase), ownerPath, MAX_PATH);
		return systemDirectoryLength > 0 && systemDirectoryLength < MAX_PATH &&
			ownerPathLength > 0 && ownerPathLength < MAX_PATH &&
			_wcsnicmp(ownerPath, systemDirectory, systemDirectoryLength) == 0 &&
			(ownerPath[systemDirectoryLength] == L'\\' ||
				ownerPath[systemDirectoryLength] == L'/');
	}

	bool IsReadablePointerAddress(const void* address)
	{
		MEMORY_BASIC_INFORMATION memoryInfo = {};
		if (VirtualQuery(address, &memoryInfo, sizeof(memoryInfo)) != sizeof(memoryInfo) ||
			memoryInfo.State != MEM_COMMIT ||
			(memoryInfo.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
		{
			return false;
		}

		const BYTE* pointerAddress = static_cast<const BYTE*>(address);
		const BYTE* regionEnd = static_cast<const BYTE*>(memoryInfo.BaseAddress) +
			memoryInfo.RegionSize;
		return regionEnd - pointerAddress >= sizeof(void*);
	}

	bool GetImagePathFromAddress(const void* address, std::wstring& imagePath)
	{
		MEMORY_BASIC_INFORMATION memoryInfo = {};
		if (VirtualQuery(address, &memoryInfo, sizeof(memoryInfo)) != sizeof(memoryInfo) ||
			memoryInfo.Type != MEM_IMAGE)
		{
			return false;
		}

		wchar_t path[MAX_PATH] = {};
		const DWORD pathLength = GetModuleFileNameW(
			reinterpret_cast<HMODULE>(memoryInfo.AllocationBase), path, MAX_PATH);
		if (pathLength == 0 || pathLength >= MAX_PATH)
			return false;

		imagePath.assign(path, pathLength);
		return true;
	}

	void AuditImagePath(const std::wstring& imagePath, std::wstring& warnings,
		std::vector<std::wstring>& auditedImages)
	{
		if (std::find(auditedImages.begin(), auditedImages.end(), imagePath) !=
			auditedImages.end())
		{
			return;
		}
		auditedImages.push_back(imagePath);

		LONG trustStatus = ERROR_SUCCESS;
		bool microsoftPublisher = false;
		if (!xucrun::image_trust::Verify(imagePath, trustStatus, microsoftPublisher))
		{
			wchar_t statusText[16] = {};
			swprintf_s(statusText, L"%08lX", static_cast<unsigned long>(trustStatus));
			warnings += xucrun::localization::Format(
				xucrun::localization::TextId::ImageSignatureFailed,
				{ imagePath, statusText });
		}
		else if (!microsoftPublisher)
		{
			warnings += xucrun::localization::Format(
				xucrun::localization::TextId::ImagePublisherWarning,
				{ imagePath });
		}
	}

	void AuditImageTrust(const void* address, const wchar_t* context,
		std::wstring& warnings, std::vector<std::wstring>& auditedImages)
	{
		std::wstring imagePath;
		if (!GetImagePathFromAddress(address, imagePath))
		{
			warnings += xucrun::localization::Format(
				xucrun::localization::TextId::ModulePathFailed,
				{ context });
			return;
		}

		AuditImagePath(imagePath, warnings, auditedImages);
	}

	bool AuditLoadedDlls(std::wstring& warnings,
		std::vector<std::wstring>& auditedImages)
	{
		HANDLE snapshot = CreateToolhelp32Snapshot(
			TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
		if (snapshot == INVALID_HANDLE_VALUE)
		{
			warnings += xucrun::localization::Format(
				xucrun::localization::TextId::DllEnumerationFailed,
				{ xucrun::windows_error::FormatWindowsError(GetLastError()) });
			return false;
		}

		MODULEENTRY32W moduleEntry = {};
		moduleEntry.dwSize = sizeof(moduleEntry);
		if (!Module32FirstW(snapshot, &moduleEntry))
		{
			const DWORD error = GetLastError();
			CloseHandle(snapshot);
			warnings += xucrun::localization::Format(
				xucrun::localization::TextId::DllReadFailed,
				{ xucrun::windows_error::FormatWindowsError(error) });
			return false;
		}

		do
		{
			std::wstring modulePath = moduleEntry.szExePath;
			const size_t extensionPosition = modulePath.find_last_of(L'.');
			if (extensionPosition != std::wstring::npos &&
				_wcsicmp(modulePath.c_str() + extensionPosition, L".dll") == 0)
			{
				AuditImagePath(modulePath, warnings, auditedImages);
			}
		} while (Module32NextW(snapshot, &moduleEntry));

		CloseHandle(snapshot);
		std::sort(auditedImages.begin(), auditedImages.end(),
			[](const std::wstring& left, const std::wstring& right) {
				return _wcsicmp(left.c_str(), right.c_str()) < 0;
			});
		return true;
	}

	bool TryResolveEntryJumpTarget(const BYTE* code, const BYTE* regionEnd,
		const BYTE*& target)
	{
		target = nullptr;
		if (regionEnd - code < 16)
			return false;

		if (code[0] == 0xE9)
		{
			int32_t displacement = 0;
			memcpy(&displacement, code + 1, sizeof(displacement));
			target = code + 5 + displacement;
			return true;
		}

		if (code[0] == 0xEB)
		{
			target = code + 2 + static_cast<int8_t>(code[1]);
			return true;
		}

#ifdef _WIN64
		size_t instructionLength = 0;
		const BYTE* displacementAddress = nullptr;
		if (code[0] == 0xFF && code[1] == 0x25)
		{
			instructionLength = 6;
			displacementAddress = code + 2;
		}
		else if (code[0] == 0x48 && code[1] == 0xFF && code[2] == 0x25)
		{
			instructionLength = 7;
			displacementAddress = code + 3;
		}

		if (displacementAddress)
		{
			int32_t displacement = 0;
			memcpy(&displacement, displacementAddress, sizeof(displacement));
			const BYTE* pointerAddress = code + instructionLength + displacement;
			if (!IsReadablePointerAddress(pointerAddress))
				return true;
			memcpy(&target, pointerAddress, sizeof(target));
			return true;
		}

		if (code[0] == 0x48 && code[1] == 0xB8 &&
			code[10] == 0xFF && code[11] == 0xE0)
		{
			memcpy(&target, code + 2, sizeof(target));
			return true;
		}

		if (code[0] == 0x49 && code[1] == 0xBB &&
			code[10] == 0x41 && code[11] == 0xFF && code[12] == 0xE3)
		{
			memcpy(&target, code + 2, sizeof(target));
			return true;
		}
#else
		if (code[0] == 0x68 && code[5] == 0xC3)
		{
			uint32_t absoluteTarget = 0;
			memcpy(&absoluteTarget, code + 1, sizeof(absoluteTarget));
			target = reinterpret_cast<const BYTE*>(absoluteTarget);
			return true;
		}
#endif

		return false;
	}

	bool IsCriticalApiEntryPointTrusted(const wchar_t* moduleName,
		const char* functionName, std::wstring& reason,
		std::wstring& signatureWarnings,
		std::vector<std::wstring>& auditedImages)
	{
		HMODULE module = GetModuleHandleW(moduleName);
		FARPROC procedure = module ? GetProcAddress(module, functionName) : nullptr;
		if (!procedure)
		{
			reason = xucrun::localization::GetText(
				xucrun::localization::TextId::CriticalApiResolveFailed);
			return false;
		}

		if (!IsExecutableSystemImageAddress(reinterpret_cast<LPCVOID>(procedure)))
		{
			reason = xucrun::localization::GetText(
				xucrun::localization::TextId::CriticalApiNotSystemCode);
			return false;
		}
		AuditImageTrust(reinterpret_cast<LPCVOID>(procedure), moduleName,
			signatureWarnings, auditedImages);

		MEMORY_BASIC_INFORMATION memoryInfo = {};
		if (VirtualQuery(reinterpret_cast<LPCVOID>(procedure), &memoryInfo,
			sizeof(memoryInfo)) != sizeof(memoryInfo))
		{
			reason = xucrun::localization::GetText(
				xucrun::localization::TextId::CriticalApiMemoryFailed);
			return false;
		}

		const BYTE* code = reinterpret_cast<const BYTE*>(procedure);
		const BYTE* regionEnd = static_cast<const BYTE*>(memoryInfo.BaseAddress) +
			memoryInfo.RegionSize;
		const BYTE* jumpTarget = nullptr;
		if (TryResolveEntryJumpTarget(code, regionEnd, jumpTarget) &&
			(!jumpTarget || !IsExecutableSystemImageAddress(jumpTarget)))
		{
			reason = xucrun::localization::GetText(
				xucrun::localization::TextId::CriticalApiJumpFailed);
			return false;
		}
		if (jumpTarget)
		{
			AuditImageTrust(jumpTarget, xucrun::localization::GetText(
				xucrun::localization::TextId::JumpTargetContext).c_str(),
				signatureWarnings, auditedImages);
		}

		return true;
	}
}

namespace xucrun::security_audit
{
	int RunAuditCommand()
	{
		broker::SecurityAuditRequest auditRequest;
		auditRequest.requestId = broker::GenerateBrokerRequestId();
		auditRequest.language = localization::UsesGermanResources()
			? broker::SecurityAuditRequest::Language::German
			: broker::SecurityAuditRequest::Language::English;
		const broker::PipeSecurityAuditResponse auditResponse =
			broker::SubmitSecurityAuditRequest(auditRequest);
		const bool auditAvailable =
			auditResponse.status == broker::PipeSecurityAuditStatus::Succeeded &&
			auditResponse.result.status == broker::SecurityAuditStatus::Succeeded;
		const bool auditWarning = !auditAvailable || auditResponse.result.findingCount != 0;
		std::wstring auditDetails;
		if (auditAvailable)
		{
			auditDetails = auditResponse.result.report;
			client_logging::Write(auditResponse.result.findingCount == 0 ? L"Ok" : L"Warning",
				L"Broker ACL check for /audit: " + auditResponse.result.report);
		}
		else
		{
			const DWORD auditError =
				auditResponse.status == broker::PipeSecurityAuditStatus::Succeeded
					? auditResponse.result.windowsError
					: auditResponse.windowsError;
			auditDetails = localization::Format(
				localization::TextId::AuditBrokerUnavailable,
				{ std::to_wstring(auditError == NO_ERROR ? ERROR_INVALID_DATA : auditError) });
			client_logging::Write(L"Warning", L"Broker ACL check for /audit unavailable: " +
				std::to_wstring(auditError == NO_ERROR ? ERROR_INVALID_DATA : auditError));
		}
		const std::wstring auditInfo = localization::Format(
			localization::TextId::AuditBrokerSection, { auditDetails });
		std::wstring blockingReason;
		std::wstring signatureWarnings;
		std::vector<std::wstring> checkedImages;
		const bool trusted = IsCredentialAccessEnvironmentTrusted(
			blockingReason, signatureWarnings, &checkedImages);

		wchar_t executablePathBuffer[32768] = {};
		const DWORD executablePathLength = GetModuleFileNameW(
			nullptr, executablePathBuffer,
			static_cast<DWORD>(std::size(executablePathBuffer)));
		const bool executablePathAvailable = executablePathLength > 0 &&
			executablePathLength < std::size(executablePathBuffer);
		LONG selfTrustStatus = TRUST_E_NOSIGNATURE;
		bool selfMicrosoftPublisher = false;
		const bool selfSignatureValid = executablePathAvailable && image_trust::Verify(
			executablePathBuffer, selfTrustStatus, selfMicrosoftPublisher);
		const std::wstring selfPath = executablePathAvailable
			? executablePathBuffer
			: localization::GetText(localization::TextId::AuditPathUnavailable);
		std::wstring selfSignatureInfo;
		if (selfSignatureValid)
		{
			selfSignatureInfo = localization::Format(
				localization::TextId::AuditSelfSignatureValid, { selfPath });
			client_logging::Write(L"Ok", L"xucRun self-signature is valid: " +
				std::wstring(executablePathBuffer));
		}
		else if (executablePathAvailable)
		{
			wchar_t statusText[16] = {};
			swprintf_s(statusText, L"%08lX", static_cast<unsigned long>(selfTrustStatus));
			selfSignatureInfo = localization::Format(
				localization::TextId::AuditSelfSignatureFailed,
				{ selfPath, statusText });
			client_logging::Write(L"Warning", L"xucRun self-signature check failed (0x" +
				std::wstring(statusText) + L"): " + executablePathBuffer);
		}
		else
		{
			selfSignatureInfo = localization::Format(
				localization::TextId::AuditSelfSignatureUnchecked, { selfPath });
			client_logging::Write(L"Warning",
				L"xucRun self-signature check not possible: executable path could not be determined.");
		}

		std::wstring imageEntries;
		if (checkedImages.empty())
		{
			imageEntries = localization::GetText(localization::TextId::AuditImageNone);
		}
		else
		{
			for (const std::wstring& imagePath : checkedImages)
				imageEntries += L"\n- " + imagePath;
		}
		const std::wstring imageList = localization::Format(
			localization::TextId::AuditImageSection, { imageEntries });

		if (!trusted)
		{
			const std::wstring result = localization::Format(
				localization::TextId::AuditFailedBody,
				{ blockingReason, selfSignatureInfo, imageList, auditInfo });
			text_dialog::Show(
				ui::GetWindowTitle(localization::GetText(localization::TextId::AuditTitle)),
				localization::GetText(localization::TextId::AuditFailedHeading), result,
				ui::GetDialogOwnerWindow());
			return 1;
		}

		const bool hasWarnings = !signatureWarnings.empty() ||
			!selfSignatureValid || auditWarning;
		if (hasWarnings)
		{
			const std::wstring moduleWarnings = signatureWarnings.empty()
				? L""
				: localization::Format(localization::TextId::AuditModuleWarnings,
					{ signatureWarnings });
			const std::wstring result = localization::Format(
				localization::TextId::AuditWarningBody,
				{ moduleWarnings, selfSignatureInfo, imageList, auditInfo });
			text_dialog::Show(
				ui::GetWindowTitle(localization::GetText(localization::TextId::AuditTitle)),
				localization::GetText(localization::TextId::AuditWarningHeading), result,
				ui::GetDialogOwnerWindow());
		}
		else
		{
			const std::wstring result = localization::Format(
				localization::TextId::AuditSuccessBody,
				{ selfSignatureInfo, imageList, auditInfo });
			text_dialog::Show(
				ui::GetWindowTitle(localization::GetText(localization::TextId::AuditTitle)),
				localization::GetText(localization::TextId::AuditSuccessHeading), result,
				ui::GetDialogOwnerWindow());
		}

		return hasWarnings ? 1 : 0;
	}

	bool IsCredentialAccessEnvironmentTrusted(std::wstring& reason,
		std::wstring& signatureWarnings,
		std::vector<std::wstring>* checkedImages)
	{
		if (IsDebuggerPresent())
		{
			reason = localization::GetText(
				localization::TextId::LocalDebuggerDetected);
			return false;
		}

		BOOL remoteDebuggerPresent = FALSE;
		if (CheckRemoteDebuggerPresent(GetCurrentProcess(), &remoteDebuggerPresent) &&
			remoteDebuggerPresent)
		{
			reason = localization::GetText(
				localization::TextId::ProcessDebuggerDetected);
			return false;
		}

		struct CriticalApi
		{
			const wchar_t* moduleName;
			const char* functionName;
		};
		const CriticalApi criticalApis[] = {
			{ L"Advapi32.dll", "CredReadW" },
			{ L"Bcrypt.dll", "BCryptDecrypt" }
		};
		std::vector<std::wstring> auditedImages;

		for (const CriticalApi& api : criticalApis)
		{
			if (!IsCriticalApiEntryPointTrusted(api.moduleName, api.functionName,
				reason, signatureWarnings, auditedImages))
			{
				if (checkedImages)
					*checkedImages = auditedImages;
				return false;
			}
		}
		AuditLoadedDlls(signatureWarnings, auditedImages);
		if (checkedImages)
			*checkedImages = auditedImages;

		return true;
	}
}