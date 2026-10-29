// Purpose: Transmits broker requests through the service's authenticated Named Pipe.
// Manages service startup, timeouts, server identity, and response validation.
#include "BrokerPipeClient.h"

#include "BrokerSerialization.h"

#include <Windows.h>
#include <bcrypt.h>

#include <vector>

namespace xucrun::broker
{
	namespace
	{
		// Reads the current broker service status and preserves the associated Windows error.
		bool QueryBrokerServiceStatus(SERVICE_STATUS_PROCESS& status, DWORD& windowsError)
		{
			status = {};
			windowsError = NO_ERROR;
			SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
			if (!manager)
			{
				windowsError = GetLastError();
				return false;
			}
			SC_HANDLE service = OpenServiceW(manager, BrokerServiceName, SERVICE_QUERY_STATUS);
			if (!service)
			{
				windowsError = GetLastError();
				CloseServiceHandle(manager);
				return false;
			}

			DWORD bytesNeeded = 0;
			const bool queried = QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
				reinterpret_cast<BYTE*>(&status), sizeof(status), &bytesNeeded) == TRUE;
			windowsError = queried ? NO_ERROR : GetLastError();
			CloseServiceHandle(service);
			CloseServiceHandle(manager);
			return queried;
		}

		// Returns only the process ID of a demonstrably running broker service.
		bool QueryRunningBrokerProcessId(DWORD& processId)
		{
			processId = 0;
			SERVICE_STATUS_PROCESS status = {};
			DWORD error = NO_ERROR;
			if (!QueryBrokerServiceStatus(status, error))
			{
				SetLastError(error);
				return false;
			}
			if (status.dwCurrentState != SERVICE_RUNNING || status.dwProcessId == 0)
			{
				SetLastError(ERROR_SERVICE_NOT_ACTIVE);
				return false;
			}
			processId = status.dwProcessId;
			return true;
		}

		// Starts the broker service and waits for RUNNING no longer than the specified deadline.
		bool StartBrokerServiceInternal(DWORD timeoutMilliseconds, DWORD& windowsError)
		{
			windowsError = NO_ERROR;
			SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
			if (!manager)
			{
				windowsError = GetLastError();
				return false;
			}
			SC_HANDLE service = OpenServiceW(manager, BrokerServiceName,
				SERVICE_START | SERVICE_QUERY_STATUS);
			if (!service)
			{
				windowsError = GetLastError();
				CloseServiceHandle(manager);
				return false;
			}

			SERVICE_STATUS_PROCESS status = {};
			DWORD bytesNeeded = 0;
			bool success = QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
				reinterpret_cast<BYTE*>(&status), sizeof(status), &bytesNeeded) == TRUE;
			if (!success)
				windowsError = GetLastError();

			const ULONGLONG deadline = GetTickCount64() + timeoutMilliseconds;
			while (success && status.dwCurrentState == SERVICE_STOP_PENDING &&
				GetTickCount64() < deadline)
			{
				Sleep(10);
				success = QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
					reinterpret_cast<BYTE*>(&status), sizeof(status), &bytesNeeded) == TRUE;
				if (!success)
					windowsError = GetLastError();
			}
			if (success && status.dwCurrentState == SERVICE_STOP_PENDING)
			{
				windowsError = ERROR_SERVICE_REQUEST_TIMEOUT;
				success = false;
			}
			if (success && status.dwCurrentState == SERVICE_STOPPED &&
				!StartServiceW(service, 0, nullptr))
			{
				windowsError = GetLastError();
				success = windowsError == ERROR_SERVICE_ALREADY_RUNNING;
			}

			while (success)
			{
				if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
					reinterpret_cast<BYTE*>(&status), sizeof(status), &bytesNeeded))
				{
					windowsError = GetLastError();
					success = false;
					break;
				}
				if (status.dwCurrentState == SERVICE_RUNNING)
					break;
				if (status.dwCurrentState == SERVICE_STOPPED ||
					GetTickCount64() >= deadline)
				{
					windowsError = status.dwCurrentState == SERVICE_STOPPED &&
						status.dwWin32ExitCode != NO_ERROR
						? status.dwWin32ExitCode
						: ERROR_SERVICE_REQUEST_TIMEOUT;
					success = false;
					break;
				}
				Sleep(10);
			}
			CloseServiceHandle(service);
			CloseServiceHandle(manager);
			return success;
		}

		// Connects only to a pipe whose server process matches the running broker service.
		HANDLE ConnectAuthenticatedBrokerPipe(DWORD timeoutMilliseconds,
			DWORD& windowsError, bool startService = true)
		{
			windowsError = NO_ERROR;
			if (startService &&
				!StartBrokerServiceInternal(timeoutMilliseconds, windowsError))
			{
				return INVALID_HANDLE_VALUE;
			}
			const ULONGLONG deadline = GetTickCount64() + timeoutMilliseconds;
			HANDLE pipe = INVALID_HANDLE_VALUE;
			while (true)
			{
				const ULONGLONG now = GetTickCount64();
				const DWORD remaining = now < deadline
					? static_cast<DWORD>(deadline - now)
					: 0;
				if (WaitNamedPipeW(BrokerPipeName, remaining))
				{
					pipe = CreateFileW(BrokerPipeName,
						FILE_READ_DATA | FILE_WRITE_DATA | FILE_READ_ATTRIBUTES |
							FILE_WRITE_ATTRIBUTES | SYNCHRONIZE,
						0, nullptr, OPEN_EXISTING,
						FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT |
							SECURITY_IDENTIFICATION, nullptr);
					if (pipe != INVALID_HANDLE_VALUE)
						break;
				}
				windowsError = GetLastError();
				if (windowsError == ERROR_FILE_NOT_FOUND)
				{
					SERVICE_STATUS_PROCESS serviceStatus = {};
					DWORD serviceError = NO_ERROR;
					if (!QueryBrokerServiceStatus(serviceStatus, serviceError))
					{
						windowsError = serviceError;
						return INVALID_HANDLE_VALUE;
					}
					if (serviceStatus.dwCurrentState != SERVICE_RUNNING &&
						serviceStatus.dwCurrentState != SERVICE_START_PENDING &&
						serviceStatus.dwCurrentState != SERVICE_CONTINUE_PENDING)
					{
						windowsError = ERROR_SERVICE_NOT_ACTIVE;
						return INVALID_HANDLE_VALUE;
					}
				}
				if ((windowsError != ERROR_FILE_NOT_FOUND &&
					windowsError != ERROR_PIPE_BUSY) || remaining == 0)
				{
					return INVALID_HANDLE_VALUE;
				}
				Sleep(1);
			}

			ULONG pipeProcessId = 0;
			DWORD serviceProcessId = 0;
			if (!GetNamedPipeServerProcessId(pipe, &pipeProcessId))
			{
				windowsError = GetLastError();
				CloseHandle(pipe);
				return INVALID_HANDLE_VALUE;
			}
			if (!QueryRunningBrokerProcessId(serviceProcessId))
			{
				windowsError = GetLastError();
				CloseHandle(pipe);
				return INVALID_HANDLE_VALUE;
			}
			if (pipeProcessId != serviceProcessId)
			{
				windowsError = ERROR_ACCESS_DENIED;
				CloseHandle(pipe);
				return INVALID_HANDLE_VALUE;
			}

			DWORD mode = PIPE_READMODE_MESSAGE;
			if (!SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr))
			{
				windowsError = GetLastError();
				CloseHandle(pipe);
				return INVALID_HANDLE_VALUE;
			}
			return pipe;
		}

		// Completes exactly one overlapped transfer within the deadline or cancels it.
		bool TransferWithTimeout(HANDLE pipe, bool write, void* buffer,
			DWORD bufferSize, DWORD timeoutMilliseconds, DWORD& transferred)
		{
			HANDLE operationEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			if (!operationEvent)
				return false;
			OVERLAPPED operation = {};
			operation.hEvent = operationEvent;
			const BOOL started = write
				? WriteFile(pipe, buffer, bufferSize, nullptr, &operation)
				: ReadFile(pipe, buffer, bufferSize, nullptr, &operation);
			if (!started && GetLastError() != ERROR_IO_PENDING)
			{
				CloseHandle(operationEvent);
				return false;
			}
			if (!started && WaitForSingleObject(operationEvent, timeoutMilliseconds) != WAIT_OBJECT_0)
			{
				CancelIoEx(pipe, &operation);
				GetOverlappedResult(pipe, &operation, &transferred, TRUE);
				SetLastError(ERROR_TIMEOUT);
				CloseHandle(operationEvent);
				return false;
			}
			const BOOL completed = GetOverlappedResult(pipe, &operation, &transferred, TRUE);
			CloseHandle(operationEvent);
			return completed == TRUE;
		}

	}

	// Generates a cryptographically random request identifier; zero indicates an RNG error.
	std::uint64_t GenerateBrokerRequestId() noexcept
	{
		std::uint64_t requestId = 0;
		if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&requestId),
			sizeof(requestId), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
		{
			return 0;
		}
		return requestId == 0 ? 1 : requestId;
	}

	// Starts the broker service within the deadline and returns the exact Windows error.
	bool StartBrokerService(std::uint32_t timeoutMilliseconds,
		std::uint32_t* windowsError)
	{
		DWORD error = NO_ERROR;
		const bool started = StartBrokerServiceInternal(timeoutMilliseconds, error);
		if (windowsError)
			*windowsError = error;
		return started;
	}

	bool QueryRunningBrokerServiceProcessId(
		DWORD& processId, DWORD& windowsError) noexcept
	{
		const bool queried = QueryRunningBrokerProcessId(processId);
		windowsError = queried ? NO_ERROR : GetLastError();
		return queried;
	}

	// Confirms broker identity and liveness only through a correlated identifier, nonce, and process ID.
	PipeHealthCheck QueryBrokerPipeHealth(std::uint32_t timeoutMilliseconds)
	{
		HealthRequest request;
		request.requestId = GenerateBrokerRequestId();
		if (request.requestId == 0 ||
			BCryptGenRandom(nullptr, request.nonce.data(),
				static_cast<ULONG>(request.nonce.size()),
				BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
		{
			return { PipeHealthStatus::IoError, ERROR_GEN_FAILURE, 0 };
		}
		std::vector<std::uint8_t> requestFrame = SerializeHealthRequest(request);
		if (requestFrame.empty())
			return { PipeHealthStatus::IoError, ERROR_INVALID_DATA, 0 };

		DWORD connectError = NO_ERROR;
		HANDLE pipe = ConnectAuthenticatedBrokerPipe(
			timeoutMilliseconds, connectError, false);
		if (pipe == INVALID_HANDLE_VALUE)
			return { PipeHealthStatus::Unavailable, connectError, 0 };

		DWORD bytesWritten = 0;
		if (!TransferWithTimeout(pipe, true, requestFrame.data(),
			static_cast<DWORD>(requestFrame.size()), timeoutMilliseconds, bytesWritten) ||
			bytesWritten != requestFrame.size())
		{
			const DWORD error = GetLastError();
			CloseHandle(pipe);
			return { PipeHealthStatus::IoError, error, 0 };
		}

		std::vector<std::uint8_t> resultFrame(HealthResultSize);
		DWORD bytesRead = 0;
		const bool read = TransferWithTimeout(pipe, false, resultFrame.data(),
			static_cast<DWORD>(resultFrame.size()), timeoutMilliseconds, bytesRead);
		const DWORD readError = read ? NO_ERROR : GetLastError();
		ULONG serverProcessId = 0;
		const BOOL processIdRead = GetNamedPipeServerProcessId(pipe, &serverProcessId);
		const DWORD processIdError = processIdRead ? NO_ERROR : GetLastError();
		DWORD serviceProcessId = 0;
		DWORD serviceProcessError = NO_ERROR;
		const bool serviceProcessIdRead = processIdRead &&
			QueryRunningBrokerServiceProcessId(serviceProcessId, serviceProcessError);
		const DWORD error = !read ? readError :
			!processIdRead ? processIdError :
			!serviceProcessIdRead ? serviceProcessError : NO_ERROR;
		if (!read || !processIdRead || !serviceProcessIdRead ||
			bytesRead != resultFrame.size())
		{
			CloseHandle(pipe);
			return { PipeHealthStatus::IoError, error, 0 };
		}

		HealthResult result;
		if (!DeserializeHealthResult(resultFrame, result) ||
			result.requestId != request.requestId || result.nonce != request.nonce ||
			result.brokerProcessId != serverProcessId || serverProcessId != serviceProcessId)
		{
			CloseHandle(pipe);
			return { PipeHealthStatus::InvalidResponse, ERROR_INVALID_DATA, 0 };
		}

		HealthAcknowledgement acknowledgement;
		acknowledgement.requestId = request.requestId;
		acknowledgement.nonce = request.nonce;
		std::vector<std::uint8_t> acknowledgementFrame =
			SerializeHealthAcknowledgement(acknowledgement);
		DWORD acknowledgementBytes = 0;
		if (acknowledgementFrame.empty() ||
			!TransferWithTimeout(pipe, true, acknowledgementFrame.data(),
				static_cast<DWORD>(acknowledgementFrame.size()),
				timeoutMilliseconds, acknowledgementBytes))
		{
			const DWORD acknowledgementError = GetLastError();
			CloseHandle(pipe);
			return { PipeHealthStatus::IoError, acknowledgementError, 0 };
		}
		CloseHandle(pipe);
		return { PipeHealthStatus::Succeeded, NO_ERROR,
			result.brokerProcessId, result.elevatedProcessCount,
			result.brokerVersion };
	}

	// Sends a valid start request and accepts only the correlated valid result.
	PipeLaunchResponse SubmitBrokerLaunchRequest(
		const LaunchRequest& request,
		std::uint32_t timeoutMilliseconds)
	{
		std::vector<std::uint8_t> requestFrame = SerializeLaunchRequest(request);
		if (requestFrame.empty())
			return { PipeLaunchStatus::InvalidResponse, ERROR_INVALID_DATA, {} };

		DWORD connectError = NO_ERROR;
		HANDLE pipe = ConnectAuthenticatedBrokerPipe(timeoutMilliseconds, connectError);
		if (pipe == INVALID_HANDLE_VALUE)
			return { PipeLaunchStatus::Unavailable, connectError, {} };

		DWORD transferred = 0;
		if (!TransferWithTimeout(pipe, true, requestFrame.data(),
			static_cast<DWORD>(requestFrame.size()), timeoutMilliseconds, transferred) ||
			transferred != requestFrame.size())
		{
			const DWORD error = GetLastError();
			CloseHandle(pipe);
			return { PipeLaunchStatus::IoError, error, {} };
		}

		std::vector<std::uint8_t> resultFrame(MaximumLaunchResultSize);
		if (!TransferWithTimeout(pipe, false, resultFrame.data(),
			static_cast<DWORD>(resultFrame.size()), timeoutMilliseconds, transferred))
		{
			const DWORD error = GetLastError();
			CloseHandle(pipe);
			return { PipeLaunchStatus::IoError, error, {} };
		}
		CloseHandle(pipe);
		resultFrame.resize(transferred);

		LaunchResult result;
		if (!DeserializeLaunchResult(resultFrame, result) ||
			result.requestId != request.requestId)
		{
			return { PipeLaunchStatus::InvalidResponse, ERROR_INVALID_DATA, {} };
		}
		return { PipeLaunchStatus::Succeeded, NO_ERROR, std::move(result) };
	}

	PipeSystemLaunchResponse SubmitSystemLaunchRequest(
		const SystemLaunchRequest& request,
		std::uint32_t timeoutMilliseconds)
	{
		std::vector<std::uint8_t> requestFrame =
			SerializeSystemLaunchRequest(request);
		if (requestFrame.empty())
			return { PipeLaunchStatus::InvalidResponse, ERROR_INVALID_DATA, {} };

		DWORD connectError = NO_ERROR;
		HANDLE pipe = ConnectAuthenticatedBrokerPipe(timeoutMilliseconds, connectError);
		if (pipe == INVALID_HANDLE_VALUE)
			return { PipeLaunchStatus::Unavailable, connectError, {} };

		DWORD transferred = 0;
		if (!TransferWithTimeout(pipe, true, requestFrame.data(),
			static_cast<DWORD>(requestFrame.size()), timeoutMilliseconds, transferred) ||
			transferred != requestFrame.size())
		{
			const DWORD error = GetLastError();
			CloseHandle(pipe);
			return { PipeLaunchStatus::IoError, error, {} };
		}

		std::vector<std::uint8_t> resultFrame(MaximumSystemLaunchResultSize);
		if (!TransferWithTimeout(pipe, false, resultFrame.data(),
			static_cast<DWORD>(resultFrame.size()), timeoutMilliseconds, transferred))
		{
			const DWORD error = GetLastError();
			CloseHandle(pipe);
			return { PipeLaunchStatus::IoError, error, {} };
		}
		CloseHandle(pipe);
		resultFrame.resize(transferred);

		SystemLaunchResult result;
		if (!DeserializeSystemLaunchResult(resultFrame, result) ||
			result.requestId != request.requestId)
		{
			return { PipeLaunchStatus::InvalidResponse, ERROR_INVALID_DATA, {} };
		}
		return { PipeLaunchStatus::Succeeded, NO_ERROR, std::move(result) };
	}

	// Sends a metadata request and accepts only the correlated valid result.
	PipeCredentialMetadataResponse SubmitCredentialMetadataRequest(
		const CredentialMetadataRequest& request,
		std::uint32_t timeoutMilliseconds)
	{
		std::vector<std::uint8_t> requestFrame =
			SerializeCredentialMetadataRequest(request);
		if (requestFrame.empty())
			return { PipeCredentialMetadataStatus::InvalidResponse, ERROR_INVALID_DATA, {} };

		DWORD connectError = NO_ERROR;
		HANDLE pipe = ConnectAuthenticatedBrokerPipe(timeoutMilliseconds, connectError);
		if (pipe == INVALID_HANDLE_VALUE)
			return { PipeCredentialMetadataStatus::Unavailable, connectError, {} };

		DWORD transferred = 0;
		if (!TransferWithTimeout(pipe, true, requestFrame.data(),
			static_cast<DWORD>(requestFrame.size()), timeoutMilliseconds, transferred) ||
			transferred != requestFrame.size())
		{
			const DWORD error = GetLastError();
			CloseHandle(pipe);
			return { PipeCredentialMetadataStatus::IoError, error, {} };
		}

		std::vector<std::uint8_t> resultFrame(CredentialMetadataResultSize);
		if (!TransferWithTimeout(pipe, false, resultFrame.data(),
			static_cast<DWORD>(resultFrame.size()), timeoutMilliseconds, transferred))
		{
			const DWORD error = GetLastError();
			CloseHandle(pipe);
			return { PipeCredentialMetadataStatus::IoError, error, {} };
		}
		CloseHandle(pipe);
		resultFrame.resize(transferred);

		CredentialMetadataResult result;
		if (!DeserializeCredentialMetadataResult(resultFrame, result) ||
			result.requestId != request.requestId)
		{
			return { PipeCredentialMetadataStatus::InvalidResponse, ERROR_INVALID_DATA, {} };
		}
		return { PipeCredentialMetadataStatus::Succeeded, NO_ERROR, std::move(result) };
	}

	// Sends a session test request and accepts only the correlated valid result.
	PipeSessionTestResponse SubmitSessionTestRequest(
		const SessionTestRequest& request,
		std::uint32_t timeoutMilliseconds)
	{
		std::vector<std::uint8_t> requestFrame = SerializeSessionTestRequest(request);
		if (requestFrame.empty())
			return { PipeSessionTestStatus::InvalidResponse, ERROR_INVALID_DATA, {} };

		DWORD connectError = NO_ERROR;
		HANDLE pipe = ConnectAuthenticatedBrokerPipe(timeoutMilliseconds, connectError);
		if (pipe == INVALID_HANDLE_VALUE)
			return { PipeSessionTestStatus::Unavailable, connectError, {} };

		DWORD transferred = 0;
		if (!TransferWithTimeout(pipe, true, requestFrame.data(),
			static_cast<DWORD>(requestFrame.size()), timeoutMilliseconds, transferred) ||
			transferred != requestFrame.size())
		{
			const DWORD transferError = GetLastError();
			CloseHandle(pipe);
			return { PipeSessionTestStatus::IoError, transferError, {} };
		}

		std::vector<std::uint8_t> resultFrame(SessionTestResultSize);
		if (!TransferWithTimeout(pipe, false, resultFrame.data(),
			static_cast<DWORD>(resultFrame.size()), timeoutMilliseconds, transferred))
		{
			const DWORD transferError = GetLastError();
			CloseHandle(pipe);
			return { PipeSessionTestStatus::IoError, transferError, {} };
		}
		CloseHandle(pipe);
		resultFrame.resize(transferred);

		SessionTestResult result;
		if (!DeserializeSessionTestResult(resultFrame, result) ||
			result.requestId != request.requestId)
		{
			return { PipeSessionTestStatus::InvalidResponse, ERROR_INVALID_DATA, {} };
		}
		return { PipeSessionTestStatus::Succeeded, NO_ERROR, std::move(result) };
	}

	// Sends a store test request and accepts only the correlated valid result.
	PipeCredentialStoreTestResponse SubmitCredentialStoreTestRequest(
		const CredentialStoreTestRequest& request,
		std::uint32_t timeoutMilliseconds)
	{
		std::vector<std::uint8_t> requestFrame =
			SerializeCredentialStoreTestRequest(request);
		if (requestFrame.empty())
			return { PipeCredentialStoreTestStatus::InvalidResponse,
				ERROR_INVALID_DATA, {} };

		DWORD connectError = NO_ERROR;
		HANDLE pipe = ConnectAuthenticatedBrokerPipe(timeoutMilliseconds, connectError);
		if (pipe == INVALID_HANDLE_VALUE)
			return { PipeCredentialStoreTestStatus::Unavailable, connectError, {} };

		DWORD transferred = 0;
		if (!TransferWithTimeout(pipe, true, requestFrame.data(),
			static_cast<DWORD>(requestFrame.size()), timeoutMilliseconds, transferred) ||
			transferred != requestFrame.size())
		{
			const DWORD transferError = GetLastError();
			CloseHandle(pipe);
			return { PipeCredentialStoreTestStatus::IoError, transferError, {} };
		}

		std::vector<std::uint8_t> resultFrame(CredentialStoreTestResultSize);
		if (!TransferWithTimeout(pipe, false, resultFrame.data(),
			static_cast<DWORD>(resultFrame.size()), timeoutMilliseconds, transferred))
		{
			const DWORD transferError = GetLastError();
			CloseHandle(pipe);
			return { PipeCredentialStoreTestStatus::IoError, transferError, {} };
		}
		CloseHandle(pipe);
		resultFrame.resize(transferred);

		CredentialStoreTestResult result;
		if (!DeserializeCredentialStoreTestResult(resultFrame, result) ||
			result.requestId != request.requestId)
		{
			return { PipeCredentialStoreTestStatus::InvalidResponse,
				ERROR_INVALID_DATA, {} };
		}
		return { PipeCredentialStoreTestStatus::Succeeded, NO_ERROR, std::move(result) };
	}

	// Sends a store request and accepts only the correlated valid result.
	PipeCredentialSaveResponse SubmitCredentialSaveRequest(
		const CredentialSaveRequest& request,
		std::uint32_t timeoutMilliseconds)
	{
		std::vector<std::uint8_t> requestFrame = SerializeCredentialSaveRequest(request);
		if (requestFrame.empty())
			return { PipeCredentialSaveStatus::InvalidResponse, ERROR_INVALID_DATA, {} };

		DWORD connectError = NO_ERROR;
		HANDLE pipe = ConnectAuthenticatedBrokerPipe(timeoutMilliseconds, connectError);
		if (pipe == INVALID_HANDLE_VALUE)
		{
			SecureZeroMemory(requestFrame.data(), requestFrame.size());
			return { PipeCredentialSaveStatus::Unavailable, connectError, {} };
		}

		DWORD transferred = 0;
		const bool written = TransferWithTimeout(pipe, true, requestFrame.data(),
			static_cast<DWORD>(requestFrame.size()), timeoutMilliseconds, transferred) &&
			transferred == requestFrame.size();
		SecureZeroMemory(requestFrame.data(), requestFrame.size());
		if (!written)
		{
			const DWORD transferError = GetLastError();
			CloseHandle(pipe);
			return { PipeCredentialSaveStatus::IoError, transferError, {} };
		}

		std::vector<std::uint8_t> resultFrame(CredentialSaveResultSize);
		if (!TransferWithTimeout(pipe, false, resultFrame.data(),
			static_cast<DWORD>(resultFrame.size()), timeoutMilliseconds, transferred))
		{
			const DWORD transferError = GetLastError();
			CloseHandle(pipe);
			return { PipeCredentialSaveStatus::IoError, transferError, {} };
		}
		CloseHandle(pipe);
		resultFrame.resize(transferred);

		CredentialSaveResult result;
		if (!DeserializeCredentialSaveResult(resultFrame, result) ||
			result.requestId != request.requestId)
		{
			return { PipeCredentialSaveStatus::InvalidResponse, ERROR_INVALID_DATA, {} };
		}
		return { PipeCredentialSaveStatus::Succeeded, NO_ERROR, std::move(result) };
	}

	PipeCredentialCaptureChallengeResponse SubmitCredentialCaptureChallengeRequest(
		const CredentialCaptureChallengeRequest& request,
		std::uint32_t timeoutMilliseconds)
	{
		std::vector<std::uint8_t> requestFrame =
			SerializeCredentialCaptureChallengeRequest(request);
		if (requestFrame.empty())
			return { PipeCredentialSaveStatus::InvalidResponse, ERROR_INVALID_DATA, {} };
		DWORD error = NO_ERROR;
		HANDLE pipe = ConnectAuthenticatedBrokerPipe(timeoutMilliseconds, error);
		if (pipe == INVALID_HANDLE_VALUE)
			return { PipeCredentialSaveStatus::Unavailable, error, {} };
		DWORD transferred = 0;
		if (!TransferWithTimeout(pipe, true, requestFrame.data(),
			static_cast<DWORD>(requestFrame.size()), timeoutMilliseconds, transferred) ||
			transferred != requestFrame.size())
		{
			error = GetLastError();
			CloseHandle(pipe);
			return { PipeCredentialSaveStatus::IoError, error, {} };
		}
		std::vector<std::uint8_t> resultFrame(MaximumCredentialCaptureChallengeResultSize);
		if (!TransferWithTimeout(pipe, false, resultFrame.data(),
			static_cast<DWORD>(resultFrame.size()), timeoutMilliseconds, transferred))
		{
			error = GetLastError();
			CloseHandle(pipe);
			return { PipeCredentialSaveStatus::IoError, error, {} };
		}
		CloseHandle(pipe);
		resultFrame.resize(transferred);
		CredentialCaptureChallengeResult result;
		if (!DeserializeCredentialCaptureChallengeResult(resultFrame, result) ||
			result.requestId != request.requestId)
		{
			return { PipeCredentialSaveStatus::InvalidResponse, ERROR_INVALID_DATA, {} };
		}
		return { PipeCredentialSaveStatus::Succeeded, NO_ERROR, std::move(result) };
	}

	PipeCredentialCaptureHelperResponse SubmitCredentialCaptureHelperRequest(
		const CredentialCaptureHelperRequest& request,
		std::uint32_t timeoutMilliseconds)
	{
		std::vector<std::uint8_t> requestFrame =
			SerializeCredentialCaptureHelperRequest(request);
		if (requestFrame.empty())
			return { PipeCredentialSaveStatus::InvalidResponse, ERROR_INVALID_DATA, {} };
		DWORD error = NO_ERROR;
		HANDLE pipe = ConnectAuthenticatedBrokerPipe(timeoutMilliseconds, error);
		if (pipe == INVALID_HANDLE_VALUE)
			return { PipeCredentialSaveStatus::Unavailable, error, {} };
		DWORD transferred = 0;
		if (!TransferWithTimeout(pipe, true, requestFrame.data(),
			static_cast<DWORD>(requestFrame.size()), timeoutMilliseconds, transferred) ||
			transferred != requestFrame.size())
		{
			error = GetLastError();
			CloseHandle(pipe);
			return { PipeCredentialSaveStatus::IoError, error, {} };
		}
		std::vector<std::uint8_t> resultFrame(MaximumCredentialCaptureHelperResultSize);
		if (!TransferWithTimeout(pipe, false, resultFrame.data(),
			static_cast<DWORD>(resultFrame.size()), timeoutMilliseconds, transferred))
		{
			error = GetLastError();
			CloseHandle(pipe);
			return { PipeCredentialSaveStatus::IoError, error, {} };
		}
		CloseHandle(pipe);
		resultFrame.resize(transferred);
		CredentialCaptureHelperResult result;
		if (!DeserializeCredentialCaptureHelperResult(resultFrame, result) ||
			result.requestId != request.requestId)
		{
			return { PipeCredentialSaveStatus::InvalidResponse, ERROR_INVALID_DATA, {} };
		}
		return { PipeCredentialSaveStatus::Succeeded, NO_ERROR, std::move(result) };
	}

	PipeCredentialCaptureSubmitResponse SubmitCredentialCaptureSubmitRequest(
		const CredentialCaptureSubmitRequest& request,
		std::uint32_t timeoutMilliseconds)
	{
		std::vector<std::uint8_t> requestFrame =
			SerializeCredentialCaptureSubmitRequest(request);
		if (requestFrame.empty())
			return { PipeCredentialSaveStatus::InvalidResponse, ERROR_INVALID_DATA, {} };
		DWORD error = NO_ERROR;
		HANDLE pipe = ConnectAuthenticatedBrokerPipe(timeoutMilliseconds, error);
		if (pipe == INVALID_HANDLE_VALUE)
		{
			SecureZeroMemory(requestFrame.data(), requestFrame.size());
			return { PipeCredentialSaveStatus::Unavailable, error, {} };
		}
		DWORD transferred = 0;
		const bool written = TransferWithTimeout(pipe, true, requestFrame.data(),
			static_cast<DWORD>(requestFrame.size()), timeoutMilliseconds, transferred) &&
			transferred == requestFrame.size();
		SecureZeroMemory(requestFrame.data(), requestFrame.size());
		if (!written)
		{
			error = GetLastError();
			CloseHandle(pipe);
			return { PipeCredentialSaveStatus::IoError, error, {} };
		}
		std::vector<std::uint8_t> resultFrame(CredentialCaptureSubmitResultSize);
		if (!TransferWithTimeout(pipe, false, resultFrame.data(),
			static_cast<DWORD>(resultFrame.size()), timeoutMilliseconds, transferred))
		{
			error = GetLastError();
			CloseHandle(pipe);
			return { PipeCredentialSaveStatus::IoError, error, {} };
		}
		CloseHandle(pipe);
		resultFrame.resize(transferred);
		CredentialCaptureSubmitResult result;
		if (!DeserializeCredentialCaptureSubmitResult(resultFrame, result) ||
			result.requestId != request.requestId)
		{
			return { PipeCredentialSaveStatus::InvalidResponse, ERROR_INVALID_DATA, {} };
		}
		return { PipeCredentialSaveStatus::Succeeded, NO_ERROR, std::move(result) };
	}

	PipeCredentialCaptureCommitResponse SubmitCredentialCaptureCommitRequest(
		const CredentialCaptureCommitRequest& request,
		std::uint32_t timeoutMilliseconds)
	{
		std::vector<std::uint8_t> requestFrame =
			SerializeCredentialCaptureCommitRequest(request);
		if (requestFrame.empty())
			return { PipeCredentialSaveStatus::InvalidResponse, ERROR_INVALID_DATA, {} };
		DWORD error = NO_ERROR;
		HANDLE pipe = ConnectAuthenticatedBrokerPipe(timeoutMilliseconds, error);
		if (pipe == INVALID_HANDLE_VALUE)
			return { PipeCredentialSaveStatus::Unavailable, error, {} };
		DWORD transferred = 0;
		if (!TransferWithTimeout(pipe, true, requestFrame.data(),
			static_cast<DWORD>(requestFrame.size()), timeoutMilliseconds, transferred) ||
			transferred != requestFrame.size())
		{
			error = GetLastError();
			CloseHandle(pipe);
			return { PipeCredentialSaveStatus::IoError, error, {} };
		}
		std::vector<std::uint8_t> resultFrame(CredentialCaptureCommitResultSize);
		if (!TransferWithTimeout(pipe, false, resultFrame.data(),
			static_cast<DWORD>(resultFrame.size()), timeoutMilliseconds, transferred))
		{
			error = GetLastError();
			CloseHandle(pipe);
			return { PipeCredentialSaveStatus::IoError, error, {} };
		}
		CloseHandle(pipe);
		resultFrame.resize(transferred);
		CredentialCaptureCommitResult result;
		if (!DeserializeCredentialCaptureCommitResult(resultFrame, result) ||
			result.requestId != request.requestId)
		{
			return { PipeCredentialSaveStatus::InvalidResponse, ERROR_INVALID_DATA, {} };
		}
		return { PipeCredentialSaveStatus::Succeeded, NO_ERROR, std::move(result) };
	}

	// Sends a delete request and accepts only the correlated valid result.
	PipeCredentialDeleteResponse SubmitCredentialDeleteRequest(
		const CredentialDeleteRequest& request,
		std::uint32_t timeoutMilliseconds)
	{
		std::vector<std::uint8_t> requestFrame = SerializeCredentialDeleteRequest(request);
		if (requestFrame.empty())
			return { PipeCredentialDeleteStatus::InvalidResponse, ERROR_INVALID_DATA, {} };

		DWORD connectError = NO_ERROR;
		HANDLE pipe = ConnectAuthenticatedBrokerPipe(timeoutMilliseconds, connectError);
		if (pipe == INVALID_HANDLE_VALUE)
			return { PipeCredentialDeleteStatus::Unavailable, connectError, {} };

		DWORD transferred = 0;
		if (!TransferWithTimeout(pipe, true, requestFrame.data(),
			static_cast<DWORD>(requestFrame.size()), timeoutMilliseconds, transferred) ||
			transferred != requestFrame.size())
		{
			const DWORD transferError = GetLastError();
			CloseHandle(pipe);
			return { PipeCredentialDeleteStatus::IoError, transferError, {} };
		}

		std::vector<std::uint8_t> resultFrame(CredentialDeleteResultSize);
		if (!TransferWithTimeout(pipe, false, resultFrame.data(),
			static_cast<DWORD>(resultFrame.size()), timeoutMilliseconds, transferred))
		{
			const DWORD transferError = GetLastError();
			CloseHandle(pipe);
			return { PipeCredentialDeleteStatus::IoError, transferError, {} };
		}
		CloseHandle(pipe);
		resultFrame.resize(transferred);

		CredentialDeleteResult result;
		if (!DeserializeCredentialDeleteResult(resultFrame, result) ||
			result.requestId != request.requestId)
		{
			return { PipeCredentialDeleteStatus::InvalidResponse, ERROR_INVALID_DATA, {} };
		}
		return { PipeCredentialDeleteStatus::Succeeded, NO_ERROR, std::move(result) };
	}

	// Sends a list request and accepts only the correlated valid result.
	PipeCredentialListResponse SubmitCredentialListRequest(
		const CredentialListRequest& request,
		std::uint32_t timeoutMilliseconds)
	{
		std::vector<std::uint8_t> requestFrame = SerializeCredentialListRequest(request);
		if (requestFrame.empty())
			return { PipeCredentialListStatus::InvalidResponse, ERROR_INVALID_DATA, {} };

		DWORD connectError = NO_ERROR;
		HANDLE pipe = ConnectAuthenticatedBrokerPipe(timeoutMilliseconds, connectError);
		if (pipe == INVALID_HANDLE_VALUE)
			return { PipeCredentialListStatus::Unavailable, connectError, {} };

		DWORD transferred = 0;
		if (!TransferWithTimeout(pipe, true, requestFrame.data(),
			static_cast<DWORD>(requestFrame.size()), timeoutMilliseconds, transferred) ||
			transferred != requestFrame.size())
		{
			const DWORD transferError = GetLastError();
			CloseHandle(pipe);
			return { PipeCredentialListStatus::IoError, transferError, {} };
		}

		std::vector<std::uint8_t> resultFrame(MaximumCredentialListResultSize);
		if (!TransferWithTimeout(pipe, false, resultFrame.data(),
			static_cast<DWORD>(resultFrame.size()), timeoutMilliseconds, transferred))
		{
			const DWORD transferError = GetLastError();
			CloseHandle(pipe);
			return { PipeCredentialListStatus::IoError, transferError, {} };
		}
		CloseHandle(pipe);
		resultFrame.resize(transferred);

		CredentialListResult result;
		if (!DeserializeCredentialListResult(resultFrame, result) ||
			result.requestId != request.requestId)
		{
			return { PipeCredentialListStatus::InvalidResponse, ERROR_INVALID_DATA, {} };
		}
		return { PipeCredentialListStatus::Succeeded, NO_ERROR, std::move(result) };
	}

	PipeAdministrativeInventoryResponse SubmitAdministrativeInventoryRequest(
		const AdministrativeInventoryRequest& request,
		std::uint32_t timeoutMilliseconds)
	{
		std::vector<std::uint8_t> requestFrame =
			SerializeAdministrativeInventoryRequest(request);
		if (requestFrame.empty())
		{
			return { PipeAdministrativeInventoryStatus::InvalidResponse,
				ERROR_INVALID_DATA, {} };
		}
		DWORD error = NO_ERROR;
		HANDLE pipe = ConnectAuthenticatedBrokerPipe(timeoutMilliseconds, error);
		if (pipe == INVALID_HANDLE_VALUE)
			return { PipeAdministrativeInventoryStatus::Unavailable, error, {} };
		DWORD transferred = 0;
		if (!TransferWithTimeout(pipe, true, requestFrame.data(),
			static_cast<DWORD>(requestFrame.size()), timeoutMilliseconds, transferred) ||
			transferred != requestFrame.size())
		{
			error = GetLastError();
			CloseHandle(pipe);
			return { PipeAdministrativeInventoryStatus::IoError, error, {} };
		}
		std::vector<std::uint8_t> resultFrame(
			MaximumAdministrativeInventoryResultSize);
		if (!TransferWithTimeout(pipe, false, resultFrame.data(),
			static_cast<DWORD>(resultFrame.size()), timeoutMilliseconds, transferred))
		{
			error = GetLastError();
			CloseHandle(pipe);
			return { PipeAdministrativeInventoryStatus::IoError, error, {} };
		}
		CloseHandle(pipe);
		resultFrame.resize(transferred);
		BrokerPolicyRejection rejection;
		if (DeserializeBrokerPolicyRejection(resultFrame, rejection) &&
			rejection.requestId == request.requestId)
		{
			return { PipeAdministrativeInventoryStatus::PolicyDenied,
				rejection.windowsError, {} };
		}
		AdministrativeInventoryResult result;
		if (!DeserializeAdministrativeInventoryResult(resultFrame, result) ||
			result.requestId != request.requestId)
		{
			return { PipeAdministrativeInventoryStatus::InvalidResponse,
				ERROR_INVALID_DATA, {} };
		}
		return { PipeAdministrativeInventoryStatus::Succeeded,
			NO_ERROR, std::move(result) };
	}

	PipeAdministrativeDeleteResponse SubmitAdministrativeDeleteRequest(
		const AdministrativeDeleteRequest& request,
		std::uint32_t timeoutMilliseconds)
	{
		const auto requestFrame = SerializeAdministrativeDeleteRequest(request);
		if (requestFrame.empty())
		{
			return { PipeAdministrativeDeleteStatus::InvalidResponse,
				ERROR_INVALID_DATA, {} };
		}
		DWORD error = NO_ERROR;
		HANDLE pipe = ConnectAuthenticatedBrokerPipe(timeoutMilliseconds, error);
		if (pipe == INVALID_HANDLE_VALUE)
			return { PipeAdministrativeDeleteStatus::Unavailable, error, {} };
		DWORD transferred = 0;
		if (!TransferWithTimeout(pipe, true,
			const_cast<std::uint8_t*>(requestFrame.data()),
			static_cast<DWORD>(requestFrame.size()), timeoutMilliseconds, transferred) ||
			transferred != requestFrame.size())
		{
			error = GetLastError();
			CloseHandle(pipe);
			return { PipeAdministrativeDeleteStatus::IoError, error, {} };
		}
		std::vector<std::uint8_t> resultFrame(AdministrativeDeleteResultSize);
		if (!TransferWithTimeout(pipe, false, resultFrame.data(),
			static_cast<DWORD>(resultFrame.size()), timeoutMilliseconds, transferred))
		{
			error = GetLastError();
			CloseHandle(pipe);
			return { PipeAdministrativeDeleteStatus::IoError, error, {} };
		}
		CloseHandle(pipe);
		resultFrame.resize(transferred);
		AdministrativeDeleteResult result;
		if (!DeserializeAdministrativeDeleteResult(resultFrame, result) ||
			result.requestId != request.requestId)
		{
			return { PipeAdministrativeDeleteStatus::InvalidResponse,
				ERROR_INVALID_DATA, {} };
		}
		return { PipeAdministrativeDeleteStatus::Succeeded,
			NO_ERROR, std::move(result) };
	}

	PipeBackupResponse SubmitBackupRequest(const BackupRequest& request,
		std::uint32_t timeoutMilliseconds)
	{
		const auto requestFrame = SerializeBackupRequest(request);
		if (requestFrame.empty())
			return { PipeBackupStatus::InvalidResponse, ERROR_INVALID_DATA, {} };
		DWORD error = NO_ERROR;
		HANDLE pipe = ConnectAuthenticatedBrokerPipe(timeoutMilliseconds, error);
		if (pipe == INVALID_HANDLE_VALUE)
			return { PipeBackupStatus::Unavailable, error, {} };
		DWORD transferred = 0;
		if (!TransferWithTimeout(pipe, true, const_cast<std::uint8_t*>(requestFrame.data()),
			static_cast<DWORD>(requestFrame.size()), timeoutMilliseconds, transferred) ||
			transferred != requestFrame.size())
		{
			error = GetLastError();
			CloseHandle(pipe);
			return { PipeBackupStatus::IoError, error, {} };
		}
		std::vector<std::uint8_t> resultFrame(MaximumBackupResultSize);
		if (!TransferWithTimeout(pipe, false, resultFrame.data(),
			static_cast<DWORD>(resultFrame.size()), timeoutMilliseconds, transferred))
		{
			error = GetLastError();
			CloseHandle(pipe);
			return { PipeBackupStatus::IoError, error, {} };
		}
		CloseHandle(pipe);
		resultFrame.resize(transferred);
		BackupResult result;
		if (!DeserializeBackupResult(resultFrame, result) ||
			result.requestId != request.requestId)
		{
			return { PipeBackupStatus::InvalidResponse, ERROR_INVALID_DATA, {} };
		}
		return { PipeBackupStatus::Succeeded, NO_ERROR, std::move(result) };
	}

	// Sends a process list request and accepts only the correlated valid result.
	PipeTrackedProcessListResponse SubmitTrackedProcessListRequest(
		const TrackedProcessListRequest& request,
		std::uint32_t timeoutMilliseconds)
	{
		std::vector<std::uint8_t> requestFrame = SerializeTrackedProcessListRequest(request);
		if (requestFrame.empty())
			return { PipeTrackedProcessListStatus::InvalidResponse, ERROR_INVALID_DATA, {} };

		DWORD connectError = NO_ERROR;
		HANDLE pipe = ConnectAuthenticatedBrokerPipe(timeoutMilliseconds, connectError);
		if (pipe == INVALID_HANDLE_VALUE)
			return { PipeTrackedProcessListStatus::Unavailable, connectError, {} };

		DWORD transferred = 0;
		if (!TransferWithTimeout(pipe, true, requestFrame.data(),
			static_cast<DWORD>(requestFrame.size()), timeoutMilliseconds, transferred) ||
			transferred != requestFrame.size())
		{
			const DWORD transferError = GetLastError();
			CloseHandle(pipe);
			return { PipeTrackedProcessListStatus::IoError, transferError, {} };
		}

		std::vector<std::uint8_t> resultFrame(MaximumTrackedProcessListResultSize);
		if (!TransferWithTimeout(pipe, false, resultFrame.data(),
			static_cast<DWORD>(resultFrame.size()), timeoutMilliseconds, transferred))
		{
			const DWORD transferError = GetLastError();
			CloseHandle(pipe);
			return { PipeTrackedProcessListStatus::IoError, transferError, {} };
		}
		CloseHandle(pipe);
		resultFrame.resize(transferred);

		TrackedProcessListResult result;
		if (!DeserializeTrackedProcessListResult(resultFrame, result) ||
			result.requestId != request.requestId)
		{
			return { PipeTrackedProcessListStatus::InvalidResponse, ERROR_INVALID_DATA, {} };
		}
		return { PipeTrackedProcessListStatus::Succeeded, NO_ERROR, std::move(result) };
	}

	PipeStatusIconListResponse SubmitStatusIconListRequest(
		const StatusIconListRequest& request, std::uint32_t timeoutMilliseconds)
	{
		std::vector<std::uint8_t> requestFrame = SerializeStatusIconListRequest(request);
		if (requestFrame.empty())
			return { PipeStatusIconListStatus::InvalidResponse, ERROR_INVALID_DATA, {} };
		DWORD connectError = NO_ERROR;
		HANDLE pipe = ConnectAuthenticatedBrokerPipe(timeoutMilliseconds, connectError);
		if (pipe == INVALID_HANDLE_VALUE)
			return { PipeStatusIconListStatus::Unavailable, connectError, {} };
		DWORD transferred = 0;
		if (!TransferWithTimeout(pipe, true, requestFrame.data(),
			static_cast<DWORD>(requestFrame.size()), timeoutMilliseconds, transferred) ||
			transferred != requestFrame.size())
		{
			const DWORD error = GetLastError();
			CloseHandle(pipe);
			return { PipeStatusIconListStatus::IoError, error, {} };
		}
		std::vector<std::uint8_t> resultFrame(MaximumStatusIconListResultSize);
		if (!TransferWithTimeout(pipe, false, resultFrame.data(),
			static_cast<DWORD>(resultFrame.size()), timeoutMilliseconds, transferred))
		{
			const DWORD error = GetLastError();
			CloseHandle(pipe);
			return { PipeStatusIconListStatus::IoError, error, {} };
		}
		CloseHandle(pipe);
		resultFrame.resize(transferred);
		StatusIconListResult result;
		if (!DeserializeStatusIconListResult(resultFrame, result) ||
			result.requestId != request.requestId)
			return { PipeStatusIconListStatus::InvalidResponse, ERROR_INVALID_DATA, {} };
		return { PipeStatusIconListStatus::Succeeded, NO_ERROR, std::move(result) };
	}

	// Sends a process registration and accepts only the correlated valid result.
	PipeTrackedProcessRegistrationResponse SubmitTrackedProcessRegistrationRequest(
		const TrackedProcessRegistrationRequest& request,
		std::uint32_t timeoutMilliseconds)
	{
		std::vector<std::uint8_t> requestFrame =
			SerializeTrackedProcessRegistrationRequest(request);
		if (requestFrame.empty())
			return { PipeTrackedProcessRegistrationStatus::InvalidResponse,
				ERROR_INVALID_DATA, {} };

		DWORD connectError = NO_ERROR;
		HANDLE pipe = ConnectAuthenticatedBrokerPipe(timeoutMilliseconds, connectError);
		if (pipe == INVALID_HANDLE_VALUE)
			return { PipeTrackedProcessRegistrationStatus::Unavailable, connectError, {} };

		DWORD transferred = 0;
		if (!TransferWithTimeout(pipe, true, requestFrame.data(),
			static_cast<DWORD>(requestFrame.size()), timeoutMilliseconds, transferred) ||
			transferred != requestFrame.size())
		{
			const DWORD transferError = GetLastError();
			CloseHandle(pipe);
			return { PipeTrackedProcessRegistrationStatus::IoError, transferError, {} };
		}

		std::vector<std::uint8_t> resultFrame(TrackedProcessRegistrationResultSize);
		if (!TransferWithTimeout(pipe, false, resultFrame.data(),
			static_cast<DWORD>(resultFrame.size()), timeoutMilliseconds, transferred))
		{
			const DWORD transferError = GetLastError();
			CloseHandle(pipe);
			return { PipeTrackedProcessRegistrationStatus::IoError, transferError, {} };
		}
		CloseHandle(pipe);
		resultFrame.resize(transferred);

		TrackedProcessRegistrationResult result;
		if (!DeserializeTrackedProcessRegistrationResult(resultFrame, result) ||
			result.requestId != request.requestId)
		{
			return { PipeTrackedProcessRegistrationStatus::InvalidResponse,
				ERROR_INVALID_DATA, {} };
		}
		return { PipeTrackedProcessRegistrationStatus::Succeeded,
			NO_ERROR, std::move(result) };
	}

	// Sends a security check request and accepts only the correlated valid result.
	PipeSecurityAuditResponse SubmitSecurityAuditRequest(
		const SecurityAuditRequest& request,
		std::uint32_t timeoutMilliseconds)
	{
		std::vector<std::uint8_t> requestFrame = SerializeSecurityAuditRequest(request);
		if (requestFrame.empty())
			return { PipeSecurityAuditStatus::InvalidResponse, ERROR_INVALID_DATA, {} };

		DWORD connectError = NO_ERROR;
		HANDLE pipe = ConnectAuthenticatedBrokerPipe(timeoutMilliseconds, connectError);
		if (pipe == INVALID_HANDLE_VALUE)
			return { PipeSecurityAuditStatus::Unavailable, connectError, {} };

		DWORD transferred = 0;
		if (!TransferWithTimeout(pipe, true, requestFrame.data(),
			static_cast<DWORD>(requestFrame.size()), timeoutMilliseconds, transferred) ||
			transferred != requestFrame.size())
		{
			const DWORD transferError = GetLastError();
			CloseHandle(pipe);
			return { PipeSecurityAuditStatus::IoError, transferError, {} };
		}

		std::vector<std::uint8_t> resultFrame(MaximumSecurityAuditResultSize);
		if (!TransferWithTimeout(pipe, false, resultFrame.data(),
			static_cast<DWORD>(resultFrame.size()), timeoutMilliseconds, transferred))
		{
			const DWORD transferError = GetLastError();
			CloseHandle(pipe);
			return { PipeSecurityAuditStatus::IoError, transferError, {} };
		}
		CloseHandle(pipe);
		resultFrame.resize(transferred);

		SecurityAuditResult result;
		if (!DeserializeSecurityAuditResult(resultFrame, result) ||
			result.requestId != request.requestId)
		{
			return { PipeSecurityAuditStatus::InvalidResponse, ERROR_INVALID_DATA, {} };
		}
		return { PipeSecurityAuditStatus::Succeeded, NO_ERROR, std::move(result) };
	}

	// Sends a broker stop request and accepts only the correlated valid result.
	PipeBrokerStopResponse SubmitBrokerStopRequest(
		const BrokerStopRequest& request, std::uint32_t timeoutMilliseconds)
	{
		std::vector<std::uint8_t> requestFrame = SerializeBrokerStopRequest(request);
		if (requestFrame.empty())
			return { PipeBrokerStopStatus::InvalidResponse, ERROR_INVALID_DATA, {} };

		DWORD error = NO_ERROR;
		HANDLE pipe = ConnectAuthenticatedBrokerPipe(timeoutMilliseconds, error, false);
		if (pipe == INVALID_HANDLE_VALUE)
			return { PipeBrokerStopStatus::Unavailable, error, {} };
		DWORD transferred = 0;
		const bool requestWritten = TransferWithTimeout(pipe, true, requestFrame.data(),
			static_cast<DWORD>(requestFrame.size()), timeoutMilliseconds, transferred);
		if (!requestWritten || transferred != requestFrame.size())
		{
			error = requestWritten ? ERROR_WRITE_FAULT : GetLastError();
			CloseHandle(pipe);
			return { PipeBrokerStopStatus::IoError, error, {} };
		}
		std::vector<std::uint8_t> resultFrame(BrokerStopResultSize);
		if (!TransferWithTimeout(pipe, false, resultFrame.data(),
			static_cast<DWORD>(resultFrame.size()), timeoutMilliseconds, transferred))
		{
			error = GetLastError();
			CloseHandle(pipe);
			return { PipeBrokerStopStatus::IoError, error, {} };
		}
		CloseHandle(pipe);
		resultFrame.resize(transferred);
		BrokerStopResult result;
		if (!DeserializeBrokerStopResult(resultFrame, result) ||
			result.requestId != request.requestId)
		{
			return { PipeBrokerStopStatus::InvalidResponse, ERROR_INVALID_DATA, {} };
		}
		return { PipeBrokerStopStatus::Succeeded, NO_ERROR, result };
	}

	// Sends a usage test and accepts only the correlated valid result.
	PipeCredentialUseTestResponse SubmitCredentialUseTestRequest(
		const CredentialUseTestRequest& request,
		std::uint32_t timeoutMilliseconds)
	{
		std::vector<std::uint8_t> requestFrame = SerializeCredentialUseTestRequest(request);
		if (requestFrame.empty())
			return { PipeCredentialUseTestStatus::InvalidResponse, ERROR_INVALID_DATA, {} };

		DWORD connectError = NO_ERROR;
		HANDLE pipe = ConnectAuthenticatedBrokerPipe(timeoutMilliseconds, connectError);
		if (pipe == INVALID_HANDLE_VALUE)
			return { PipeCredentialUseTestStatus::Unavailable, connectError, {} };

		DWORD transferred = 0;
		if (!TransferWithTimeout(pipe, true, requestFrame.data(),
			static_cast<DWORD>(requestFrame.size()), timeoutMilliseconds, transferred) ||
			transferred != requestFrame.size())
		{
			const DWORD transferError = GetLastError();
			CloseHandle(pipe);
			return { PipeCredentialUseTestStatus::IoError, transferError, {} };
		}

		std::vector<std::uint8_t> resultFrame(CredentialUseTestResultSize);
		if (!TransferWithTimeout(pipe, false, resultFrame.data(),
			static_cast<DWORD>(resultFrame.size()), timeoutMilliseconds, transferred))
		{
			const DWORD transferError = GetLastError();
			CloseHandle(pipe);
			return { PipeCredentialUseTestStatus::IoError, transferError, {} };
		}
		CloseHandle(pipe);
		resultFrame.resize(transferred);

		CredentialUseTestResult result;
		if (!DeserializeCredentialUseTestResult(resultFrame, result) ||
			result.requestId != request.requestId)
		{
			return { PipeCredentialUseTestStatus::InvalidResponse, ERROR_INVALID_DATA, {} };
		}
		return { PipeCredentialUseTestStatus::Succeeded, NO_ERROR, std::move(result) };
	}

	// Sends an approval challenge and accepts only the correlated valid result.
	PipeApprovalChallengeResponse SubmitApprovalChallengeRequest(
		const ApprovalChallengeRequest& request,
		std::uint32_t timeoutMilliseconds)
	{
		std::vector<std::uint8_t> requestFrame = SerializeApprovalChallengeRequest(request);
		if (requestFrame.empty())
			return { PipeApprovalChallengeStatus::InvalidResponse, ERROR_INVALID_DATA, {} };

		DWORD connectError = NO_ERROR;
		HANDLE pipe = ConnectAuthenticatedBrokerPipe(timeoutMilliseconds, connectError);
		if (pipe == INVALID_HANDLE_VALUE)
			return { PipeApprovalChallengeStatus::Unavailable, connectError, {} };

		DWORD transferred = 0;
		if (!TransferWithTimeout(pipe, true, requestFrame.data(),
			static_cast<DWORD>(requestFrame.size()), timeoutMilliseconds, transferred) ||
			transferred != requestFrame.size())
		{
			const DWORD transferError = GetLastError();
			CloseHandle(pipe);
			return { PipeApprovalChallengeStatus::IoError, transferError, {} };
		}

		std::vector<std::uint8_t> resultFrame(MaximumApprovalChallengeResultSize);
		if (!TransferWithTimeout(pipe, false, resultFrame.data(),
			static_cast<DWORD>(resultFrame.size()), timeoutMilliseconds, transferred))
		{
			const DWORD transferError = GetLastError();
			CloseHandle(pipe);
			return { PipeApprovalChallengeStatus::IoError, transferError, {} };
		}
		CloseHandle(pipe);
		resultFrame.resize(transferred);

		ApprovalChallengeResult result;
		if (!DeserializeApprovalChallengeResult(resultFrame, result) ||
			result.requestId != request.requestId)
		{
			return { PipeApprovalChallengeStatus::InvalidResponse, ERROR_INVALID_DATA, {} };
		}
		return { PipeApprovalChallengeStatus::Succeeded, NO_ERROR, std::move(result) };
	}

	// Sends a challenge confirmation and accepts only the correlated valid result.
	PipeApprovalConfirmationResponse SubmitApprovalConfirmationRequest(
		const ApprovalConfirmationRequest& request,
		std::uint32_t timeoutMilliseconds)
	{
		std::vector<std::uint8_t> requestFrame =
			SerializeApprovalConfirmationRequest(request);
		if (requestFrame.empty())
			return { PipeApprovalConfirmationStatus::InvalidResponse,
				ERROR_INVALID_DATA, {} };

		DWORD connectError = NO_ERROR;
		HANDLE pipe = ConnectAuthenticatedBrokerPipe(timeoutMilliseconds, connectError);
		if (pipe == INVALID_HANDLE_VALUE)
			return { PipeApprovalConfirmationStatus::Unavailable, connectError, {} };

		DWORD transferred = 0;
		if (!TransferWithTimeout(pipe, true, requestFrame.data(),
			static_cast<DWORD>(requestFrame.size()), timeoutMilliseconds, transferred) ||
			transferred != requestFrame.size())
		{
			const DWORD transferError = GetLastError();
			CloseHandle(pipe);
			return { PipeApprovalConfirmationStatus::IoError, transferError, {} };
		}

		std::vector<std::uint8_t> resultFrame(ApprovalConfirmationResultSize);
		if (!TransferWithTimeout(pipe, false, resultFrame.data(),
			static_cast<DWORD>(resultFrame.size()), timeoutMilliseconds, transferred))
		{
			const DWORD transferError = GetLastError();
			CloseHandle(pipe);
			return { PipeApprovalConfirmationStatus::IoError, transferError, {} };
		}
		CloseHandle(pipe);
		resultFrame.resize(transferred);

		ApprovalConfirmationResult result;
		if (!DeserializeApprovalConfirmationResult(resultFrame, result) ||
			result.requestId != request.requestId)
		{
			return { PipeApprovalConfirmationStatus::InvalidResponse,
				ERROR_INVALID_DATA, {} };
		}
		return { PipeApprovalConfirmationStatus::Succeeded, NO_ERROR, std::move(result) };
	}

	PipeApprovalHelperResponse SubmitApprovalHelperRequest(
		const ApprovalHelperRequest& request,
		std::uint32_t timeoutMilliseconds)
	{
		std::vector<std::uint8_t> requestFrame =
			SerializeApprovalHelperRequest(request);
		if (requestFrame.empty())
			return { PipeApprovalHelperStatus::InvalidResponse, ERROR_INVALID_DATA, {} };
		DWORD error = NO_ERROR;
		HANDLE pipe = ConnectAuthenticatedBrokerPipe(timeoutMilliseconds, error);
		if (pipe == INVALID_HANDLE_VALUE)
			return { PipeApprovalHelperStatus::Unavailable, error, {} };
		DWORD transferred = 0;
		if (!TransferWithTimeout(pipe, true, requestFrame.data(),
			static_cast<DWORD>(requestFrame.size()), timeoutMilliseconds, transferred) ||
			transferred != requestFrame.size())
		{
			error = GetLastError();
			CloseHandle(pipe);
			return { PipeApprovalHelperStatus::IoError, error, {} };
		}
		std::vector<std::uint8_t> resultFrame(MaximumApprovalChallengeResultSize);
		if (!TransferWithTimeout(pipe, false, resultFrame.data(),
			static_cast<DWORD>(resultFrame.size()), timeoutMilliseconds, transferred))
		{
			error = GetLastError();
			CloseHandle(pipe);
			return { PipeApprovalHelperStatus::IoError, error, {} };
		}
		CloseHandle(pipe);
		resultFrame.resize(transferred);
		ApprovalChallengeResult result;
		if (!DeserializeApprovalChallengeResult(resultFrame, result) ||
			result.requestId != request.requestId)
		{
			return { PipeApprovalHelperStatus::InvalidResponse, ERROR_INVALID_DATA, {} };
		}
		return { PipeApprovalHelperStatus::Succeeded, NO_ERROR, std::move(result) };
	}

	PipeApprovalHelperDecisionResponse SubmitApprovalHelperDecisionRequest(
		const ApprovalHelperDecisionRequest& request,
		std::uint32_t timeoutMilliseconds)
	{
		std::vector<std::uint8_t> requestFrame =
			SerializeApprovalHelperDecisionRequest(request);
		if (requestFrame.empty())
			return { PipeApprovalHelperStatus::InvalidResponse, ERROR_INVALID_DATA, {} };
		DWORD error = NO_ERROR;
		HANDLE pipe = ConnectAuthenticatedBrokerPipe(timeoutMilliseconds, error);
		if (pipe == INVALID_HANDLE_VALUE)
			return { PipeApprovalHelperStatus::Unavailable, error, {} };
		DWORD transferred = 0;
		if (!TransferWithTimeout(pipe, true, requestFrame.data(),
			static_cast<DWORD>(requestFrame.size()), timeoutMilliseconds, transferred) ||
			transferred != requestFrame.size())
		{
			error = GetLastError();
			CloseHandle(pipe);
			return { PipeApprovalHelperStatus::IoError, error, {} };
		}
		std::vector<std::uint8_t> resultFrame(ApprovalHelperDecisionResultSize);
		if (!TransferWithTimeout(pipe, false, resultFrame.data(),
			static_cast<DWORD>(resultFrame.size()), timeoutMilliseconds, transferred))
		{
			error = GetLastError();
			CloseHandle(pipe);
			return { PipeApprovalHelperStatus::IoError, error, {} };
		}
		CloseHandle(pipe);
		resultFrame.resize(transferred);
		ApprovalHelperDecisionResult result;
		if (!DeserializeApprovalHelperDecisionResult(resultFrame, result) ||
			result.requestId != request.requestId)
		{
			return { PipeApprovalHelperStatus::InvalidResponse, ERROR_INVALID_DATA, {} };
		}
		return { PipeApprovalHelperStatus::Succeeded, NO_ERROR, std::move(result) };
	}

	PipeApprovalHelperDecisionResponse SubmitApprovalHelperLeaseRequest(
		const ApprovalHelperRequest& request,
		std::uint32_t timeoutMilliseconds)
	{
		std::vector<std::uint8_t> requestFrame =
			SerializeApprovalHelperLeaseRequest(request);
		if (requestFrame.empty())
			return { PipeApprovalHelperStatus::InvalidResponse, ERROR_INVALID_DATA, {} };
		DWORD error = NO_ERROR;
		HANDLE pipe = ConnectAuthenticatedBrokerPipe(timeoutMilliseconds, error);
		if (pipe == INVALID_HANDLE_VALUE)
			return { PipeApprovalHelperStatus::Unavailable, error, {} };
		DWORD transferred = 0;
		if (!TransferWithTimeout(pipe, true, requestFrame.data(),
			static_cast<DWORD>(requestFrame.size()), timeoutMilliseconds, transferred) ||
			transferred != requestFrame.size())
		{
			error = GetLastError();
			CloseHandle(pipe);
			return { PipeApprovalHelperStatus::IoError, error, {} };
		}
		std::vector<std::uint8_t> resultFrame(ApprovalHelperLeaseResultSize);
		if (!TransferWithTimeout(pipe, false, resultFrame.data(),
			static_cast<DWORD>(resultFrame.size()), timeoutMilliseconds, transferred))
		{
			error = GetLastError();
			CloseHandle(pipe);
			return { PipeApprovalHelperStatus::IoError, error, {} };
		}
		CloseHandle(pipe);
		resultFrame.resize(transferred);
		ApprovalHelperDecisionResult result;
		if (!DeserializeApprovalHelperLeaseResult(resultFrame, result) ||
			result.requestId != request.requestId)
		{
			return { PipeApprovalHelperStatus::InvalidResponse, ERROR_INVALID_DATA, {} };
		}
		return { PipeApprovalHelperStatus::Succeeded, NO_ERROR, std::move(result) };
	}

	// Sends an approval list request and accepts only the correlated valid result.
	PipeApprovalListResponse SubmitApprovalListRequest(
		const ApprovalListRequest& request, std::uint32_t timeoutMilliseconds)
	{
		std::vector<std::uint8_t> requestFrame = SerializeApprovalListRequest(request);
		if (requestFrame.empty())
			return { PipeApprovalManagementStatus::InvalidResponse, ERROR_INVALID_DATA, {} };
		DWORD error = NO_ERROR;
		HANDLE pipe = ConnectAuthenticatedBrokerPipe(timeoutMilliseconds, error);
		if (pipe == INVALID_HANDLE_VALUE)
			return { PipeApprovalManagementStatus::Unavailable, error, {} };
		DWORD transferred = 0;
		if (!TransferWithTimeout(pipe, true, requestFrame.data(),
			static_cast<DWORD>(requestFrame.size()), timeoutMilliseconds, transferred) ||
			transferred != requestFrame.size())
		{
			error = GetLastError();
			CloseHandle(pipe);
			return { PipeApprovalManagementStatus::IoError, error, {} };
		}
		std::vector<std::uint8_t> resultFrame(MaximumApprovalListResultSize);
		if (!TransferWithTimeout(pipe, false, resultFrame.data(),
			static_cast<DWORD>(resultFrame.size()), timeoutMilliseconds, transferred))
		{
			error = GetLastError();
			CloseHandle(pipe);
			return { PipeApprovalManagementStatus::IoError, error, {} };
		}
		CloseHandle(pipe);
		resultFrame.resize(transferred);
		ApprovalListResult result;
		if (!DeserializeApprovalListResult(resultFrame, result) ||
			result.requestId != request.requestId)
			return { PipeApprovalManagementStatus::InvalidResponse, ERROR_INVALID_DATA, {} };
		return { PipeApprovalManagementStatus::Succeeded, NO_ERROR, std::move(result) };
	}

	// Sends an approval reset and accepts only the correlated valid result.
	PipeApprovalResetResponse SubmitApprovalResetRequest(
		const ApprovalResetRequest& request, std::uint32_t timeoutMilliseconds)
	{
		std::vector<std::uint8_t> requestFrame = SerializeApprovalResetRequest(request);
		if (requestFrame.empty())
			return { PipeApprovalManagementStatus::InvalidResponse, ERROR_INVALID_DATA, {} };
		DWORD error = NO_ERROR;
		HANDLE pipe = ConnectAuthenticatedBrokerPipe(timeoutMilliseconds, error);
		if (pipe == INVALID_HANDLE_VALUE)
			return { PipeApprovalManagementStatus::Unavailable, error, {} };
		DWORD transferred = 0;
		if (!TransferWithTimeout(pipe, true, requestFrame.data(),
			static_cast<DWORD>(requestFrame.size()), timeoutMilliseconds, transferred) ||
			transferred != requestFrame.size())
		{
			error = GetLastError();
			CloseHandle(pipe);
			return { PipeApprovalManagementStatus::IoError, error, {} };
		}
		std::vector<std::uint8_t> resultFrame(ApprovalResetResultSize);
		if (!TransferWithTimeout(pipe, false, resultFrame.data(),
			static_cast<DWORD>(resultFrame.size()), timeoutMilliseconds, transferred))
		{
			error = GetLastError();
			CloseHandle(pipe);
			return { PipeApprovalManagementStatus::IoError, error, {} };
		}
		CloseHandle(pipe);
		resultFrame.resize(transferred);
		ApprovalResetResult result;
		if (!DeserializeApprovalResetResult(resultFrame, result) ||
			result.requestId != request.requestId)
			return { PipeApprovalManagementStatus::InvalidResponse, ERROR_INVALID_DATA, {} };
		return { PipeApprovalManagementStatus::Succeeded, NO_ERROR, std::move(result) };
	}
}