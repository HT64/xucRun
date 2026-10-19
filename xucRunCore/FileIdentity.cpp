// Purpose: Determines final file paths and SHA-256 hashes through open handles.
// Encapsulates Win32 handles and BCrypt-based hash calculation.
#include "FileIdentity.h"

#include <Windows.h>
#include <bcrypt.h>

#include <utility>

namespace
{
	template<typename Handle, typename Close>
	class ScopedHandle
	{
	public:
		// Creates an empty handle owner.
		ScopedHandle() = default;
		// Takes ownership of an existing handle.
		explicit ScopedHandle(Handle value) : value(value) {}
		// Releases the held handle using the appropriate closer.
		~ScopedHandle()
		{
			if (value)
				Close{}(value);
		}

		// Prevents multiple ownership of the same handle.
		ScopedHandle(const ScopedHandle&) = delete;
		// Prevents multiple ownership through copy assignment.
		ScopedHandle& operator=(const ScopedHandle&) = delete;

		// Returns the held handle without transferring ownership.
		Handle get() const { return value; }
		// Prepares the owner to receive a new handle.
		Handle* put()
		{
			if (value)
				Close{}(value);
			value = nullptr;
			return &value;
		}

	private:
		Handle value = nullptr;
	};

	struct CloseAlgorithm
	{
		// Closes a BCrypt algorithm provider.
		void operator()(BCRYPT_ALG_HANDLE value) const
		{
			BCryptCloseAlgorithmProvider(value, 0);
		}
	};

	struct CloseHash
	{
		// Destroys a BCrypt hash object.
		void operator()(BCRYPT_HASH_HANDLE value) const
		{
			BCryptDestroyHash(value);
		}
	};
}

namespace xucrun::files
{
	// Determines the normalized final path of the open file handle.
	bool ResolveFinalFilePathFromHandle(HANDLE file, std::wstring& resolvedPath)
	{
		resolvedPath.clear();
		if (!file || file == INVALID_HANDLE_VALUE)
			return false;

		DWORD length = GetFinalPathNameByHandleW(file, nullptr, 0,
			FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
		if (length == 0)
			return false;

		std::vector<wchar_t> pathBuffer(static_cast<std::size_t>(length) + 1);
		length = GetFinalPathNameByHandleW(file, pathBuffer.data(),
			static_cast<DWORD>(pathBuffer.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
		if (length == 0 || length >= pathBuffer.size())
			return false;

		resolvedPath.assign(pathBuffer.data(), length);
		if (resolvedPath.rfind(L"\\\\?\\UNC\\", 0) == 0)
			resolvedPath = L"\\\\" + resolvedPath.substr(8);
		else if (resolvedPath.rfind(L"\\\\?\\", 0) == 0)
			resolvedPath.erase(0, 4);
		return true;
	}

	// Resolves a program input through the search path or an absolute path.
	bool ResolveExecutablePath(const std::wstring& input, std::wstring& resolvedPath)
	{
		resolvedPath.clear();
		if (input.empty())
			return false;

		wchar_t pathBuffer[32768] = {};
		DWORD length = 0;
		if (input.find_first_of(L"\\/") == std::wstring::npos)
		{
			length = SearchPathW(nullptr, input.c_str(), L".exe", 32768, pathBuffer, nullptr);
		}
		else
		{
			length = GetFullPathNameW(input.c_str(), 32768, pathBuffer, nullptr);
			if (length > 0 && length < 32768 &&
				GetFileAttributesW(pathBuffer) == INVALID_FILE_ATTRIBUTES)
			{
				const std::wstring executableInput = input + L".exe";
				length = GetFullPathNameW(executableInput.c_str(), 32768, pathBuffer, nullptr);
			}
		}
		if (length == 0 || length >= 32768 ||
			GetFileAttributesW(pathBuffer) == INVALID_FILE_ATTRIBUTES)
		{
			return false;
		}

		resolvedPath.assign(pathBuffer, length);
		return true;
	}

	// Opens a file and determines its normalized final path.
	bool ResolveFinalFilePath(const std::wstring& input, std::wstring& resolvedPath)
	{
		resolvedPath.clear();
		HANDLE file = CreateFileW(input.c_str(), FILE_READ_ATTRIBUTES,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr, OPEN_EXISTING, 0, nullptr);
		if (file == INVALID_HANDLE_VALUE)
			return false;
		const bool success = ResolveFinalFilePathFromHandle(file, resolvedPath);
		CloseHandle(file);
		return success;
	}

	// Calculates the SHA-256 identity of the handle contents and restores the file position.
	bool ComputeFileSHA256FromHandle(HANDLE file, std::vector<std::uint8_t>& hash)
	{
		hash.clear();
		if (!file || file == INVALID_HANDLE_VALUE)
			return false;
		LARGE_INTEGER originalPosition = {};
		LARGE_INTEGER zero = {};
		if (!SetFilePointerEx(file, zero, &originalPosition, FILE_CURRENT) ||
			!SetFilePointerEx(file, zero, nullptr, FILE_BEGIN))
		{
			return false;
		}

		ScopedHandle<BCRYPT_ALG_HANDLE, CloseAlgorithm> algorithm;
		ScopedHandle<BCRYPT_HASH_HANDLE, CloseHash> hashHandle;
		bool success = BCryptOpenAlgorithmProvider(
			algorithm.put(), BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
			BCryptCreateHash(algorithm.get(), hashHandle.put(), nullptr, 0, nullptr, 0, 0) == 0;
		std::vector<std::uint8_t> buffer(64 * 1024);
		while (success)
		{
			DWORD bytesRead = 0;
			if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr))
			{
				success = false;
				break;
			}
			if (bytesRead == 0)
				break;
			success = BCryptHashData(hashHandle.get(), buffer.data(), bytesRead, 0) == 0;
		}
		const bool positionRestored =
			SetFilePointerEx(file, originalPosition, nullptr, FILE_BEGIN) == TRUE;

		if (!success || !positionRestored)
			return false;
		hash.resize(32);
		if (BCryptFinishHash(hashHandle.get(), hash.data(), static_cast<ULONG>(hash.size()), 0) != 0)
		{
			hash.clear();
			return false;
		}
		return true;
	}

	// Opens the specified path and calculates the SHA-256 value of the bytes read from it.
	bool ComputeFileSHA256(const std::wstring& path, std::vector<std::uint8_t>& hash)
	{
		hash.clear();
		HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
		if (file == INVALID_HANDLE_VALUE)
			return false;
		const bool success = ComputeFileSHA256FromHandle(file, hash);
		CloseHandle(file);
		return success;
	}
}