// Purpose: Launches and displays the broker-backed administrative status inventory.
// Approach: Implements the behavior with focused helpers built on shared xucRun interfaces and Windows APIs.
#include "ClientAdministrativeStatus.h"

#include <Windows.h>
#include <sddl.h>

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "BrokerContracts.h"
#include "BrokerPipeClient.h"
#include "ClientAccountIdentity.h"
#include "ClientBrokerCredentialSelection.h"
#include "ClientBrokerLaunch.h"
#include "ClientBrokerStatus.h"
#include "ClientBackupManagement.h"
#include "ClientConsoleOutput.h"
#include "ClientCredentialFormatting.h"
#include "ClientTableDialog.h"
#include "ClientTextDialog.h"
#include "ClientUiCommon.h"
#include "ClientWindowsError.h"
#include "DocumentationNameMapping.h"
#include "Localization.h"
#include "TargetNaming.h"

namespace xucrun::administrative_status
{
	namespace
	{
		constexpr DWORD ConsolePipeTimeoutMilliseconds = 15000;
		constexpr DWORD ConsoleResponseMagic = 0x53554358;
		constexpr DWORD MaximumConsoleCharacters = 4 * 1024 * 1024;
		constexpr wchar_t ConsolePipeNamePrefix[] =
			L"\\\\.\\pipe\\xucRun.AdminStatus.";

		struct ConsoleResponseHeader
		{
			DWORD magic = ConsoleResponseMagic;
			DWORD windowsError = NO_ERROR;
			DWORD characterCount = 0;
		};

		struct AdministrativeViewData
		{
			std::size_t ownerCount = 0;
			std::size_t credentialCount = 0;
			std::size_t approvalCount = 0;
			std::vector<table_dialog::Column> columns;
			std::vector<table_dialog::Row> rows;
			std::vector<broker::AdministrativeDeleteRequest> deleteRequests;
		};

		constexpr std::size_t OwnerColumn = 0;
		constexpr std::size_t TypeColumn = 1;
		constexpr std::size_t AccountColumn = 2;
		constexpr std::size_t ScopeColumn = 3;
		constexpr std::size_t TargetColumn = 4;
		constexpr std::size_t BindingColumn = 5;
		constexpr std::size_t TimeColumn = 6;

		std::wstring ResolveOwnerName(const std::wstring& ownerSid)
		{
			PSID sid = nullptr;
			std::wstring name;
			if (ConvertStringSidToSidW(ownerSid.c_str(), &sid))
			{
				account_identity::GetCanonicalAccountName(sid, name);
				LocalFree(sid);
			}
			return name.empty() ? ownerSid : name;
		}

		std::wstring FormatApprovalLifetime(
			const approval::BrokerApprovalValue& value)
		{
			switch (value.lifetime)
			{
			case approval::BrokerApprovalLifetime::Forever:
				return localization::GetText(
					localization::TextId::ShowApprovalsLifetimeForeverValue);
			case approval::BrokerApprovalLifetime::LogonSession:
				return localization::GetText(
					localization::TextId::ShowApprovalsLifetimeSessionValue);
			default:
				return credential_formatting::FormatCredentialTimestamp(value.expiresAt);
			}
		}

		bool IsValidConsolePipeToken(const std::wstring& token)
		{
			return token.size() ==
				broker::AdministrativeInventoryConsoleTokenLength &&
				std::all_of(token.begin(), token.end(), [](wchar_t character) {
					return (character >= L'0' && character <= L'9') ||
						(character >= L'a' && character <= L'f') ||
						(character >= L'A' && character <= L'F');
				});
		}

		std::wstring CreateConsolePipeToken()
		{
			const std::uint64_t first = broker::GenerateBrokerRequestId();
			const std::uint64_t second = broker::GenerateBrokerRequestId();
			if (first == 0 || second == 0)
				return L"";
			std::wostringstream token;
			token << std::hex << std::nouppercase << std::setfill(L'0') <<
				std::setw(16) << first << std::setw(16) << second;
			return token.str();
		}

		std::wstring BuildConsolePipeName(const std::wstring& token)
		{
			return std::wstring(ConsolePipeNamePrefix) + token;
		}

		bool LoadAdministrativeView(AdministrativeViewData& view, DWORD& error)
		{
			broker::AdministrativeInventoryRequest request;
			request.requestId = broker::GenerateBrokerRequestId();
			const auto response = broker::SubmitAdministrativeInventoryRequest(request);
			if (response.status != broker::PipeAdministrativeInventoryStatus::Succeeded ||
				response.result.status != broker::AdministrativeInventoryStatus::Succeeded)
			{
				error = response.status ==
					broker::PipeAdministrativeInventoryStatus::Succeeded
					? response.result.windowsError : response.windowsError;
				return false;
			}

			struct OwnerData
			{
				std::vector<const broker::AdministrativeCredentialListEntry*> credentials;
				std::vector<const approval::BrokerApprovalValue*> approvals;
			};
			std::map<std::wstring, OwnerData> owners;
			for (const auto& credential : response.result.credentials)
				owners[credential.ownerUserSid].credentials.push_back(&credential);
			for (const auto& approvalValue : response.result.approvals)
				owners[approvalValue.ownerUserSid].approvals.push_back(&approvalValue);

			view.ownerCount = owners.size();
			view.credentialCount = response.result.credentials.size();
			view.approvalCount = response.result.approvals.size();
			view.columns = {
				{ localization::GetText(
					localization::TextId::AdministrativeStatusColumnOwner), 105 },
				{ localization::GetText(
					localization::TextId::AdministrativeStatusColumnType), 70 },
				{ localization::GetText(
					localization::TextId::AdministrativeStatusColumnAccount), 110 },
				{ localization::GetText(
					localization::TextId::ShowApprovalsColumnScope), 120 },
				{ localization::GetText(
					localization::TextId::ShowApprovalsColumnTarget), 220 },
				{ localization::GetText(
					localization::TextId::ShowApprovalsColumnInvocationBinding), 300 },
				{ localization::GetText(
					localization::TextId::AdministrativeStatusColumnTime), 125 },
				{ localization::GetText(
					localization::TextId::ShowApprovalsColumnPrivilegeMode), 100 },
				{ localization::GetText(
					localization::TextId::ShowApprovalsColumnTargetHash), 140, true }
			};
			std::map<std::wstring, std::wstring> accountNames;
			for (const auto& credential : response.result.credentials)
				accountNames.try_emplace(
					credential.accountSid, credential.canonicalAccountName);
			struct RowRecord
			{
				table_dialog::Row row;
				broker::AdministrativeDeleteRequest deleteRequest;
			};
			std::vector<RowRecord> records;
			records.reserve(view.credentialCount + view.approvalCount);
			for (const auto& [ownerSid, data] : owners)
			{
				const std::wstring ownerName = ResolveOwnerName(ownerSid);
				const std::wstring displayedOwnerName = ownerName == ownerSid
					? localization::GetText(localization::TextId::ValueNotAvailable)
					: documentation_names::DisplayAccount(ownerName);
				for (const auto* credential : data.credentials)
				{
					broker::AdministrativeDeleteRequest deleteRequest;
					deleteRequest.kind = broker::AdministrativeDeleteKind::Credential;
					deleteRequest.ownerUserSid = ownerSid;
					deleteRequest.accountSid = credential->accountSid;
					records.push_back({ { displayedOwnerName, localization::GetText(
							localization::TextId::AdministrativeStatusTypeCredential),
						documentation_names::DisplayAccount(
							credential->canonicalAccountName), L"",
						L"", L"",
						credential_formatting::FormatCredentialTimestamp(
							credential->lastWritten),
						L"", L"" },
						std::move(deleteRequest) });
				}
				for (const auto* approvalValue : data.approvals)
				{
					const bool allAccounts = approvalValue->accountScope ==
						approval::BrokerApprovalAccountScope::AllAccounts;
					const auto accountName = accountNames.find(
						approvalValue->targetAccountSid);
					const std::wstring account = allAccounts
						? localization::GetText(
							localization::TextId::ShowApprovalsAllAccountsValue)
						: accountName != accountNames.end()
							? documentation_names::DisplayAccount(accountName->second)
							: localization::GetText(
								localization::TextId::ValueNotAvailable);
					const bool general = approvalValue->programScope ==
						approval::BrokerApprovalProgramScope::General;
					const bool exact = approvalValue->programScope ==
						approval::BrokerApprovalProgramScope::ExactInvocation;
					broker::AdministrativeDeleteRequest deleteRequest;
					deleteRequest.kind = broker::AdministrativeDeleteKind::Approval;
					deleteRequest.ownerUserSid = ownerSid;
					deleteRequest.approval = *approvalValue;
					records.push_back({ { displayedOwnerName, localization::GetText(
							localization::TextId::AdministrativeStatusTypeApproval),
						account,
						credential_formatting::FormatBrokerApprovalScope(*approvalValue),
						general ? L"" : approvalValue->targetPath,
						exact
							? credential_formatting::FormatExactInvocationBinding(
								*approvalValue, L"\n")
							: L"",
						FormatApprovalLifetime(*approvalValue),
						localization::GetText(approvalValue->elevationMode == 0
							? localization::TextId::
								ShowApprovalsPrivilegeUnchangedValue
							: localization::TextId::
								ShowApprovalsPrivilegeElevatedValue),
						general ? L"" :
							targets::BytesToUpperHex(approvalValue->targetHash)
						},
						std::move(deleteRequest) });
				}
			}
			std::sort(records.begin(), records.end(), [](const auto& left, const auto& right) {
				const std::size_t sortColumns[] = {
					OwnerColumn, AccountColumn, TypeColumn, ScopeColumn,
					TargetColumn, BindingColumn };
				for (const std::size_t column : sortColumns)
				{
					const int comparison = _wcsicmp(
						left.row[column].c_str(), right.row[column].c_str());
					if (comparison != 0)
						return comparison < 0;
				}
				return false;
			});
			view.rows.reserve(records.size());
			view.deleteRequests.reserve(records.size());
			for (RowRecord& record : records)
			{
				view.rows.push_back(std::move(record.row));
				view.deleteRequests.push_back(std::move(record.deleteRequest));
			}
			error = NO_ERROR;
			return true;
		}

		bool DeleteAdministrativeEntry(
			broker::AdministrativeDeleteRequest request, bool& notFound, DWORD& error)
		{
			request.requestId = broker::GenerateBrokerRequestId();
			const auto response = broker::SubmitAdministrativeDeleteRequest(request);
			if (response.status != broker::PipeAdministrativeDeleteStatus::Succeeded)
			{
				error = response.windowsError;
				return false;
			}
			notFound = response.result.status ==
				broker::AdministrativeDeleteStatus::NotFound;
			if (response.result.status != broker::AdministrativeDeleteStatus::Succeeded &&
				!notFound)
			{
				error = response.result.windowsError;
				return false;
			}
			error = response.result.windowsError;
			return true;
		}

		std::wstring BuildConsoleContent(const AdministrativeViewData& view)
		{
			if (view.rows.empty())
				return localization::GetText(
					localization::TextId::AdministrativeStatusEmpty);

			std::wstring output = localization::Format(
				localization::TextId::AdministrativeStatusSummary,
				{ std::to_wstring(view.ownerCount),
					std::to_wstring(view.credentialCount),
					std::to_wstring(view.approvalCount) });
			for (std::size_t rowIndex = 0; rowIndex < view.rows.size(); rowIndex++)
			{
				output += L"\n\n[" + std::to_wstring(rowIndex + 1) + L"]\n";
				for (std::size_t column = 0;
					column < view.columns.size() && column < view.rows[rowIndex].size();
					column++)
				{
					if (!view.rows[rowIndex][column].empty())
					{
						const std::wstring heading = column == TimeColumn
							? localization::GetText(view.rows[rowIndex][TypeColumn] ==
								localization::GetText(localization::TextId::
									AdministrativeStatusTypeCredential)
								? localization::TextId::ShowCredentialsColumnStored
								: localization::TextId::ShowApprovalsColumnLifetime)
							: view.columns[column].heading;
						output += heading + L": " +
							view.rows[rowIndex][column] + L"\n";
					}
				}
				while (!output.empty() && output.back() == L'\n')
					output.pop_back();
			}
			return output;
		}

		HANDLE CreateConsolePipe(const std::wstring& token, DWORD& error)
		{
			std::wstring currentUserSid;
			if (!account_identity::GetCurrentUserSID(currentUserSid))
			{
				error = GetLastError();
				return INVALID_HANDLE_VALUE;
			}
			const std::wstring descriptorText =
				L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;" + currentUserSid + L")";
			PSECURITY_DESCRIPTOR descriptor = nullptr;
			if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
				descriptorText.c_str(), SDDL_REVISION_1, &descriptor, nullptr))
			{
				error = GetLastError();
				return INVALID_HANDLE_VALUE;
			}
			SECURITY_ATTRIBUTES security = {
				sizeof(security), descriptor, FALSE };
			HANDLE pipe = CreateNamedPipeW(BuildConsolePipeName(token).c_str(),
				PIPE_ACCESS_INBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE | FILE_FLAG_OVERLAPPED,
				PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
				1, 0, 64 * 1024, 0, &security);
			error = pipe == INVALID_HANDLE_VALUE ? GetLastError() : NO_ERROR;
			LocalFree(descriptor);
			return pipe;
		}

		bool ReadPipeBytes(HANDLE pipe, void* buffer, DWORD byteCount, DWORD& error)
		{
			auto* destination = static_cast<std::uint8_t*>(buffer);
			DWORD position = 0;
			while (position < byteCount)
			{
				OVERLAPPED operation = {};
				operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
				if (!operation.hEvent)
				{
					error = GetLastError();
					return false;
				}
				DWORD transferred = 0;
				bool succeeded = ReadFile(pipe, destination + position,
					byteCount - position, &transferred, &operation) == TRUE;
				if (!succeeded && GetLastError() == ERROR_IO_PENDING)
				{
					if (WaitForSingleObject(operation.hEvent,
						ConsolePipeTimeoutMilliseconds) == WAIT_OBJECT_0)
					{
						succeeded = GetOverlappedResult(
							pipe, &operation, &transferred, FALSE) == TRUE;
					}
					else
					{
						CancelIoEx(pipe, &operation);
						WaitForSingleObject(operation.hEvent, INFINITE);
						SetLastError(ERROR_TIMEOUT);
					}
				}
				if (!succeeded || transferred == 0)
				{
					error = succeeded ? ERROR_BROKEN_PIPE : GetLastError();
					CloseHandle(operation.hEvent);
					return false;
				}
				CloseHandle(operation.hEvent);
				position += transferred;
			}
			error = NO_ERROR;
			return true;
		}

		bool WritePipeBytes(HANDLE pipe, const void* buffer, DWORD byteCount)
		{
			const auto* source = static_cast<const std::uint8_t*>(buffer);
			DWORD position = 0;
			while (position < byteCount)
			{
				DWORD written = 0;
				if (!WriteFile(pipe, source + position, byteCount - position,
					&written, nullptr) || written == 0)
				{
					return false;
				}
				position += written;
			}
			return true;
		}

		bool SendConsoleResponse(HANDLE pipe, DWORD responseError,
			const std::wstring& content)
		{
			if (content.size() > MaximumConsoleCharacters)
				responseError = ERROR_INSUFFICIENT_BUFFER;
			ConsoleResponseHeader header;
			header.windowsError = responseError;
			header.characterCount = responseError == NO_ERROR
				? static_cast<DWORD>(content.size()) : 0;
			return WritePipeBytes(pipe, &header, sizeof(header)) &&
				(header.characterCount == 0 || WritePipeBytes(pipe, content.data(),
					header.characterCount * sizeof(wchar_t)));
		}

		bool ReceiveConsoleResponse(HANDLE pipe, std::wstring& content, DWORD& error)
		{
			ConsoleResponseHeader header;
			if (!ReadPipeBytes(pipe, &header, sizeof(header), error))
				return false;
			if (header.magic != ConsoleResponseMagic ||
				header.characterCount > MaximumConsoleCharacters)
			{
				error = ERROR_INVALID_DATA;
				return false;
			}
			if (header.windowsError != NO_ERROR)
			{
				error = header.windowsError;
				return false;
			}
			content.resize(header.characterCount);
			return header.characterCount == 0 || ReadPipeBytes(pipe, content.data(),
				header.characterCount * sizeof(wchar_t), error);
		}
	}

	int LaunchView()
	{
		CredentialInfo credential;
		DWORD error = NO_ERROR;
		const bool useCurrentAccountUac = account_identity::IsUserInAdminGroup();
		const auto selection = useCurrentAccountUac
			? broker_credential_selection::BrokerCredentialSelectionOutcome::NotFound
			: broker_credential_selection::SelectBrokerCredential(
				L"", true, credential, error);
		if (selection == broker_credential_selection::
			BrokerCredentialSelectionOutcome::Cancelled)
		{
			return 0;
		}
		if (selection == broker_credential_selection::
			BrokerCredentialSelectionOutcome::Failed)
		{
			ui::ReportError(localization::Format(
				localization::TextId::BrokerCredentialsQueryFailed,
				{ windows_error::FormatWindowsError(error) }),
				L"Selecting an administrator credential for the administrative view failed. "
				L"Windows error: " + std::to_wstring(error));
			return 1;
		}
		const bool useStoredCredential = selection == broker_credential_selection::
			BrokerCredentialSelectionOutcome::Selected;

		std::vector<wchar_t> executablePath(512);
		DWORD pathLength = 0;
		for (;;)
		{
			pathLength = GetModuleFileNameW(nullptr, executablePath.data(),
				static_cast<DWORD>(executablePath.size()));
			if (pathLength == 0)
			{
				ui::ReportError(localization::Format(
					localization::TextId::CommandLaunchFailed,
					{ L"/admin-status" }),
					L"Determining the xucRun executable path for the administrative view failed.");
				return 1;
			}
			if (pathLength < executablePath.size() - 1)
				break;
			executablePath.resize(executablePath.size() * 2);
		}
		if (console_output::HasAvailableTarget())
		{
			const std::wstring pipeToken = CreateConsolePipeToken();
			if (pipeToken.empty())
			{
				const DWORD tokenError = GetLastError();
				ui::ReportError(localization::Format(
					localization::TextId::AdministrativeStatusQueryFailed,
					{ windows_error::FormatWindowsError(tokenError) }),
					L"Generating the administrative console pipe token failed. Windows error: " +
						std::to_wstring(tokenError));
				return 1;
			}
			HANDLE pipe = CreateConsolePipe(pipeToken, error);
			if (pipe == INVALID_HANDLE_VALUE)
			{
				ui::ReportError(localization::Format(
					localization::TextId::AdministrativeStatusQueryFailed,
					{ windows_error::FormatWindowsError(error) }),
					L"Creating the administrative console pipe failed. Windows error: " +
						std::to_wstring(error));
				return 1;
			}

			OVERLAPPED connection = {};
			connection.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			bool connectionPending = false;
			bool connected = false;
			if (connection.hEvent)
			{
				connected = ConnectNamedPipe(pipe, &connection) == TRUE;
				if (!connected)
				{
					const DWORD connectError = GetLastError();
					connectionPending = connectError == ERROR_IO_PENDING;
					connected = connectError == ERROR_PIPE_CONNECTED;
					if (connected)
						SetEvent(connection.hEvent);
					else if (!connectionPending)
						error = connectError;
				}
			}
			else
			{
				error = GetLastError();
			}

			if (!connected && !connectionPending)
			{
				if (connection.hEvent) CloseHandle(connection.hEvent);
				CloseHandle(pipe);
				ui::ReportError(localization::Format(
					localization::TextId::AdministrativeStatusQueryFailed,
					{ windows_error::FormatWindowsError(error) }),
					L"Connecting the administrative console pipe failed. Windows error: " +
						std::to_wstring(error));
				return 1;
			}

			const std::wstring argument =
				std::wstring(broker::AdministrativeInventoryConsoleArgumentPrefix) +
				pipeToken;
			const bool launched = useStoredCredential
				? broker_launch::LaunchWithBrokerCredential(
					credential, std::wstring(executablePath.data(), pathLength), argument,
					broker::ElevationMode::RequireElevated,
					broker::LaunchVisibility::Hidden) ==
						broker_launch::BrokerLaunchOutcome::Succeeded
				: broker_launch::LaunchWithUac(
					std::wstring(executablePath.data(), pathLength), argument,
					broker::LaunchVisibility::Hidden, error);
			if (!launched)
			{
				CancelIoEx(pipe, &connection);
				if (connectionPending)
					WaitForSingleObject(connection.hEvent, INFINITE);
				CloseHandle(connection.hEvent);
				CloseHandle(pipe);
				return !useStoredCredential && error == ERROR_CANCELLED ? 0 : 1;
			}

			if (connectionPending)
			{
				const DWORD wait = WaitForSingleObject(
					connection.hEvent, ConsolePipeTimeoutMilliseconds);
				DWORD transferred = 0;
				connected = wait == WAIT_OBJECT_0 &&
					GetOverlappedResult(pipe, &connection, &transferred, FALSE);
				if (!connected)
				{
					error = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
					CancelIoEx(pipe, &connection);
					WaitForSingleObject(connection.hEvent, INFINITE);
				}
			}
			CloseHandle(connection.hEvent);

			std::wstring content;
			if (connected)
				connected = ReceiveConsoleResponse(pipe, content, error);
			DisconnectNamedPipe(pipe);
			CloseHandle(pipe);
			if (!connected)
			{
				ui::ReportError(localization::Format(
					localization::TextId::AdministrativeStatusQueryFailed,
					{ windows_error::FormatWindowsError(error) }),
					L"Receiving the administrative console response failed. Windows error: " +
						std::to_wstring(error));
				return 1;
			}

			text_dialog::Show(ui::GetWindowTitle(localization::GetText(
				localization::TextId::AdministrativeStatusTitle)),
				localization::GetText(
					localization::TextId::AdministrativeStatusHeading),
				content, ui::GetDialogOwnerWindow());
			return 0;
		}
		if (!useStoredCredential)
		{
			return broker_launch::LaunchWithUac(
				std::wstring(executablePath.data(), pathLength),
				broker::AdministrativeInventoryViewerArgumentTail,
				broker::LaunchVisibility::Normal, error) || error == ERROR_CANCELLED
				? 0 : 1;
		}
		return broker_launch::LaunchWithBrokerCredential(
			credential, std::wstring(executablePath.data(), pathLength),
			broker::AdministrativeInventoryViewerArgumentTail,
			broker::ElevationMode::RequireElevated,
			broker::LaunchVisibility::Normal) ==
				broker_launch::BrokerLaunchOutcome::Succeeded ? 0 : 1;
	}

	int ShowView()
	{
		AdministrativeViewData view;
		DWORD error = NO_ERROR;
		if (!LoadAdministrativeView(view, error))
		{
			ui::ReportError(localization::Format(
				localization::TextId::AdministrativeStatusQueryFailed,
				{ windows_error::FormatWindowsError(error) }),
				L"Loading the administrative data view failed. Windows error: " +
					std::to_wstring(error));
			return 1;
		}

		if (view.rows.empty())
		{
			text_dialog::Show(ui::GetWindowTitle(localization::GetText(
				localization::TextId::AdministrativeStatusTitle)),
				localization::GetText(
					localization::TextId::AdministrativeStatusHeading),
				localization::GetText(
					localization::TextId::AdministrativeStatusEmpty),
				ui::GetDialogOwnerWindow());
			return 0;
		}

		for (;;)
		{
			const auto dialogResult = table_dialog::Show(
				ui::GetWindowTitle(localization::GetText(
					localization::TextId::AdministrativeStatusTitle)),
				localization::Format(
					localization::TextId::AdministrativeStatusSummary,
					{ std::to_wstring(view.ownerCount),
						std::to_wstring(view.credentialCount),
						std::to_wstring(view.approvalCount) }),
				localization::GetText(
					localization::TextId::AdministrativeStatusLegend),
				view.columns, view.rows, nullptr,
				localization::GetText(
					localization::TextId::AdministrativeStatusDeleteAll),
				localization::GetText(
					localization::TextId::BackupManage),
				localization::GetText(
					localization::TextId::AdministrativeStatusDeleteSelected),
				440, 640);
			if (dialogResult.action == table_dialog::TableDialogAction::Closed)
				return 0;
			if (dialogResult.action == table_dialog::TableDialogAction::SelectionAction)
			{
				if (dialogResult.selectedRow < 0 ||
					static_cast<std::size_t>(dialogResult.selectedRow) >=
						view.deleteRequests.size())
				{
					continue;
				}
				const std::size_t selected =
					static_cast<std::size_t>(dialogResult.selectedRow);
				const auto& request = view.deleteRequests[selected];
				const auto& row = view.rows[selected];
				const std::wstring confirmation = request.kind ==
					broker::AdministrativeDeleteKind::Credential
					? localization::Format(localization::TextId::
						AdministrativeStatusDeleteCredentialConfirm,
						{ row[AccountColumn], row[OwnerColumn] })
					: localization::Format(localization::TextId::
						AdministrativeStatusDeleteApprovalConfirm,
						{ row[OwnerColumn], row[AccountColumn],
							row[ScopeColumn], row[TargetColumn] });
				if (ui::ShowMessageBox(ui::GetDialogOwnerWindow(), confirmation,
					ui::GetWindowTitle(localization::GetText(
						localization::TextId::WarningTitle)),
					MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
				{
					continue;
				}
				bool notFound = false;
				if (!DeleteAdministrativeEntry(request, notFound, error))
				{
					ui::ReportError(localization::Format(
						localization::TextId::AdministrativeStatusDeleteFailed,
						{ windows_error::FormatWindowsError(error) }),
						L"Deleting the selected administrative data entry failed. Windows error: " +
							std::to_wstring(error));
					continue;
				}
				if (notFound)
				{
					ui::ReportWarning(localization::GetText(
						localization::TextId::AdministrativeStatusDeleteNotFound),
						L"The selected administrative data entry no longer existed.");
				}
				view = {};
				if (!LoadAdministrativeView(view, error))
				{
					ui::ReportError(localization::Format(
						localization::TextId::AdministrativeStatusQueryFailed,
						{ windows_error::FormatWindowsError(error) }),
						L"Reloading the administrative data view after deletion failed. "
						L"Windows error: " + std::to_wstring(error));
					return 1;
				}
				if (view.rows.empty())
				{
					text_dialog::Show(ui::GetWindowTitle(localization::GetText(
						localization::TextId::AdministrativeStatusTitle)),
						localization::GetText(
							localization::TextId::AdministrativeStatusHeading),
						localization::GetText(
							localization::TextId::AdministrativeStatusEmpty),
						ui::GetDialogOwnerWindow());
					return 0;
				}
				continue;
			}
			if (dialogResult.action == table_dialog::TableDialogAction::SecondaryAction)
			{
				if (backup_management::ShowView() != 0)
					return 1;
				view = {};
				if (!LoadAdministrativeView(view, error))
				{
					ui::ReportError(localization::Format(
						localization::TextId::AdministrativeStatusQueryFailed,
						{ windows_error::FormatWindowsError(error) }),
						L"Reloading the administrative data view after backup management failed. "
						L"Windows error: " + std::to_wstring(error));
					return 1;
				}
				if (view.rows.empty())
				{
					text_dialog::Show(ui::GetWindowTitle(localization::GetText(
						localization::TextId::AdministrativeStatusTitle)),
						localization::GetText(
							localization::TextId::AdministrativeStatusHeading),
						localization::GetText(
							localization::TextId::AdministrativeStatusEmpty),
						ui::GetDialogOwnerWindow());
					return 0;
				}
				continue;
			}
			const auto purgeResult = broker_status::PurgeBrokerStore();
			if (purgeResult == broker_status::BrokerStorePurgeResult::Succeeded)
				return 0;
			if (purgeResult == broker_status::BrokerStorePurgeResult::Failed)
				return 1;
		}
	}

	int WriteConsoleView(const std::wstring& pipeToken)
	{
		if (!IsValidConsolePipeToken(pipeToken))
			return 1;
		const std::wstring pipeName = BuildConsolePipeName(pipeToken);
		if (!WaitNamedPipeW(pipeName.c_str(), ConsolePipeTimeoutMilliseconds))
			return 1;
		HANDLE pipe = CreateFileW(pipeName.c_str(), GENERIC_WRITE, 0, nullptr,
			OPEN_EXISTING, 0, nullptr);
		if (pipe == INVALID_HANDLE_VALUE)
			return 1;

		AdministrativeViewData view;
		DWORD error = NO_ERROR;
		const bool loaded = LoadAdministrativeView(view, error);
		const std::wstring content = loaded ? BuildConsoleContent(view) : L"";
		const bool sent = SendConsoleResponse(pipe, loaded ? NO_ERROR : error, content);
		CloseHandle(pipe);
		return loaded && sent ? 0 : 1;
	}
}