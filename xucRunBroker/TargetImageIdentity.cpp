// Purpose: Determines the immutable identity of a requested launch file.
// Opens the target fail-closed and validates its final path, hash, and supported binary type.
#include "TargetImageIdentity.h"

#include "FileIdentity.h"

namespace
{
	// Identifies supported script targets by their file extension.
	bool IsBatchFile(const std::wstring& path)
	{
		const size_t extension = path.find_last_of(L'.');
		return extension != std::wstring::npos &&
			(_wcsicmp(path.c_str() + extension, L".bat") == 0 ||
			 _wcsicmp(path.c_str() + extension, L".cmd") == 0);
	}
}

// Binds path and hash to the open launch file; Windows validates the permitted binary type at the final path.
bool ReadTargetImageIdentity(
	const std::wstring& requestedPath,
	TargetImageIdentity& identity,
	DWORD& windowsError)
{
	identity = {};
	windowsError = NO_ERROR;

	std::wstring resolvedPath;
	if (!xucrun::files::ResolveExecutablePath(requestedPath, resolvedPath))
	{
		windowsError = GetLastError();
		if (windowsError == NO_ERROR)
			windowsError = ERROR_FILE_NOT_FOUND;
		return false;
	}

	HANDLE image = CreateFileW(resolvedPath.c_str(), GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
	if (image == INVALID_HANDLE_VALUE)
	{
		windowsError = GetLastError();
		return false;
	}

	BY_HANDLE_FILE_INFORMATION information = {};
	bool success = GetFileInformationByHandle(image, &information) == TRUE &&
		(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
		xucrun::files::ResolveFinalFilePathFromHandle(image, identity.path) &&
		xucrun::files::ComputeFileSHA256FromHandle(image, identity.sha256);
	if (success && !IsBatchFile(identity.path))
	{
		const bool binaryTypeRead = GetBinaryTypeW(
			identity.path.c_str(), &identity.binaryType) == TRUE;
		success = binaryTypeRead &&
			(identity.binaryType == SCS_32BIT_BINARY ||
			 identity.binaryType == SCS_64BIT_BINARY);
		if (!success && binaryTypeRead)
			SetLastError(ERROR_BAD_EXE_FORMAT);
	}
	if (!success)
	{
		windowsError = GetLastError();
		if (windowsError == NO_ERROR)
			windowsError = ERROR_INVALID_DATA;
	}
	CloseHandle(image);
	return success;
}