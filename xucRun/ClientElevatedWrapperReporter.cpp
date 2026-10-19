// Purpose: Reports exactly one elevated-wrapper launch result through broker-provided handles.
#include "ClientElevatedWrapperReporter.h"

#include <string>

namespace xucrun::elevated_wrapper
{
	// Initializes a default failure response for the broker-bound wrapper launch.
	BrokerElevatedResultReporter::BrokerElevatedResultReporter(
		HANDLE handle, HANDLE acknowledgement) noexcept
		: resultHandle(handle), acknowledgementEvent(acknowledgement)
	{
		result.windowsError = ERROR_GEN_FAILURE;
	}

	// Reports the final launch result exactly once through the inherited pipe handle.
	BrokerElevatedResultReporter::~BrokerElevatedResultReporter()
	{
		if (!resultHandle)
		{
			if (reportedProcessHandle) CloseHandle(reportedProcessHandle);
			if (acknowledgementEvent) CloseHandle(acknowledgementEvent);
			return;
		}
		DWORD written = 0;
		const bool reported = WriteFile(resultHandle, &result, sizeof(result),
			&written, nullptr) == TRUE && written == sizeof(result);
		if (reported && reportedProcessHandle && acknowledgementEvent)
		{
			const DWORD acknowledgement = WaitForSingleObject(acknowledgementEvent,
				xucrun::broker::ElevatedLaunchHelperTimeoutMilliseconds);
			if (acknowledgement != WAIT_OBJECT_0)
			{
				TerminateProcess(reportedProcessHandle, ERROR_TIMEOUT);
				WaitForSingleObject(reportedProcessHandle, INFINITE);
			}
		}
		if (reportedProcessHandle) CloseHandle(reportedProcessHandle);
		if (acknowledgementEvent) CloseHandle(acknowledgementEvent);
		CloseHandle(resultHandle);
	}

	// Indicates whether this wrapper launch is bound to a broker result channel.
	bool BrokerElevatedResultReporter::active() const noexcept
	{
		return resultHandle != nullptr && acknowledgementEvent != nullptr;
	}

	// Binds the internal wrapper call to the actual broker PID through the kernel pipe handle.
	bool BrokerElevatedResultReporter::serverProcessMatches(DWORD processId) const noexcept
	{
		ULONG serverProcessId = 0;
		return resultHandle &&
			GetNamedPipeServerProcessId(resultHandle, &serverProcessId) == TRUE &&
			serverProcessId == processId;
	}

	// Stores the result and retains a local process handle until broker acknowledgement.
	void BrokerElevatedResultReporter::complete(DWORD windowsError, DWORD processId,
		HANDLE processHandle) noexcept
	{
		result.windowsError = windowsError;
		result.processId = processId;
		result.processHandle = 0;
		if (windowsError != NO_ERROR || !processHandle || !resultHandle)
			return;

		if (!acknowledgementEvent)
		{
			result.windowsError = ERROR_INVALID_HANDLE;
			result.processId = 0;
		}
		else if (
			!DuplicateHandle(GetCurrentProcess(), processHandle, GetCurrentProcess(),
				&reportedProcessHandle, 0, FALSE, DUPLICATE_SAME_ACCESS))
		{
			result.windowsError = GetLastError();
			result.processId = 0;
		}
		else
		{
			result.processHandle = static_cast<std::uint64_t>(
				reinterpret_cast<ULONG_PTR>(reportedProcessHandle));
		}
	}

	// Reads the internal result pipe handle only from a fully numeric, position-bound option.
	HANDLE ParseBrokerElevatedResultHandle(int argc, wchar_t* argv[])
	{
		constexpr const wchar_t* prefix = L"/internal-broker-result:";
		int resultIndex = 2;
		if (argc > resultIndex && _wcsicmp(argv[resultIndex], L"/internal-elevated") == 0)
			resultIndex++;
		if (argc > resultIndex && _wcsicmp(argv[resultIndex], L"/internal-hidden") == 0)
			resultIndex++;
		if (argc > resultIndex && _wcsicmp(argv[resultIndex], L"/internal-hidden-monitor") == 0)
			resultIndex++;
		if (argc <= resultIndex ||
			_wcsnicmp(argv[resultIndex], prefix, wcslen(prefix)) != 0)
		{
			return nullptr;
		}
		wchar_t* end = nullptr;
		const unsigned long long value = wcstoull(
			argv[resultIndex] + wcslen(prefix), &end, 10);
		return value != 0 && end && *end == L'\0'
			? reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(value))
			: nullptr;
	}

	HANDLE ParseBrokerElevatedAcknowledgementHandle(int argc, wchar_t* argv[])
	{
		constexpr const wchar_t* resultPrefix = L"/internal-broker-result:";
		constexpr const wchar_t* acknowledgementPrefix = L"/internal-broker-ack:";
		int acknowledgementIndex = 2;
		if (argc > acknowledgementIndex &&
			_wcsicmp(argv[acknowledgementIndex], L"/internal-elevated") == 0)
		{
			acknowledgementIndex++;
		}
		if (argc > acknowledgementIndex &&
			_wcsicmp(argv[acknowledgementIndex], L"/internal-hidden") == 0)
		{
			acknowledgementIndex++;
		}
		if (argc > acknowledgementIndex &&
			_wcsicmp(argv[acknowledgementIndex], L"/internal-hidden-monitor") == 0)
		{
			acknowledgementIndex++;
		}
		if (argc <= acknowledgementIndex ||
			_wcsnicmp(argv[acknowledgementIndex], resultPrefix,
				wcslen(resultPrefix)) != 0)
		{
			return nullptr;
		}
		acknowledgementIndex++;
		if (argc <= acknowledgementIndex ||
			_wcsnicmp(argv[acknowledgementIndex], acknowledgementPrefix,
				wcslen(acknowledgementPrefix)) != 0)
		{
			return nullptr;
		}
		wchar_t* end = nullptr;
		const unsigned long long value = wcstoull(
			argv[acknowledgementIndex] + wcslen(acknowledgementPrefix), &end, 10);
		return value != 0 && end && *end == L'\0'
			? reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(value))
			: nullptr;
	}
}