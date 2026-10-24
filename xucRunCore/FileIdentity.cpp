// Purpose: Determines final file paths and SHA-256 hashes through open handles.
// Encapsulates Win32 handles and BCrypt-based hash calculation.
#include "FileIdentity.h"

#include <Windows.h>
#include <aclapi.h>
#include <bcrypt.h>
#include <sddl.h>

#include <array>
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

	constexpr wchar_t TrustedInstallerSid[] =
		L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464";

	struct LocalSid
	{
		PSID value = nullptr;

		~LocalSid()
		{
			if (value)
				LocalFree(value);
		}

		LocalSid(const LocalSid&) = delete;
		LocalSid& operator=(const LocalSid&) = delete;
		LocalSid() = default;
	};

	bool IsExpectedPrivilegedWriter(PSID sid)
	{
		if (!sid || !IsValidSid(sid))
			return false;
		std::array<BYTE, SECURITY_MAX_SID_SIZE> systemBuffer = {};
		std::array<BYTE, SECURITY_MAX_SID_SIZE> administratorsBuffer = {};
		DWORD systemSize = static_cast<DWORD>(systemBuffer.size());
		DWORD administratorsSize = static_cast<DWORD>(administratorsBuffer.size());
		LocalSid trustedInstaller;
		return CreateWellKnownSid(WinLocalSystemSid, nullptr,
				systemBuffer.data(), &systemSize) &&
			CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr,
				administratorsBuffer.data(), &administratorsSize) &&
			ConvertStringSidToSidW(TrustedInstallerSid, &trustedInstaller.value) &&
			(EqualSid(sid, systemBuffer.data()) ||
				EqualSid(sid, administratorsBuffer.data()) ||
				EqualSid(sid, trustedInstaller.value));
	}

	PSID GetAllowedAceSid(void* ace)
	{
		const auto* header = static_cast<ACE_HEADER*>(ace);
		if (header->AceType == ACCESS_ALLOWED_ACE_TYPE ||
			header->AceType == ACCESS_ALLOWED_CALLBACK_ACE_TYPE)
		{
			return &static_cast<ACCESS_ALLOWED_ACE*>(ace)->SidStart;
		}
		if (header->AceType == ACCESS_ALLOWED_OBJECT_ACE_TYPE ||
			header->AceType == ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE)
		{
			auto* objectAce = static_cast<ACCESS_ALLOWED_OBJECT_ACE*>(ace);
			BYTE* sid = reinterpret_cast<BYTE*>(&objectAce->ObjectType);
			if (objectAce->Flags & ACE_OBJECT_TYPE_PRESENT)
				sid += sizeof(GUID);
			if (objectAce->Flags & ACE_INHERITED_OBJECT_TYPE_PRESENT)
				sid += sizeof(GUID);
			return sid;
		}
		return nullptr;
	}

	DWORD GetAllowedAceMask(void* ace)
	{
		const auto* header = static_cast<ACE_HEADER*>(ace);
		if (header->AceType == ACCESS_ALLOWED_ACE_TYPE ||
			header->AceType == ACCESS_ALLOWED_CALLBACK_ACE_TYPE)
		{
			return static_cast<ACCESS_ALLOWED_ACE*>(ace)->Mask;
		}
		if (header->AceType == ACCESS_ALLOWED_OBJECT_ACE_TYPE ||
			header->AceType == ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE)
		{
			return static_cast<ACCESS_ALLOWED_OBJECT_ACE*>(ace)->Mask;
		}
		return 0;
	}
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

	bool IsFileSystemObjectProtectedForPrivilegedWriters(
		HANDLE object, DWORD unexpectedWriteMask, DWORD& error)
	{
		error = ERROR_ACCESS_DENIED;
		if (!object || object == INVALID_HANDLE_VALUE)
		{
			error = ERROR_INVALID_HANDLE;
			return false;
		}
		FILE_ATTRIBUTE_TAG_INFO attributes = {};
		if (!GetFileInformationByHandleEx(object, FileAttributeTagInfo,
			&attributes, sizeof(attributes)))
		{
			error = GetLastError();
			return false;
		}
		if ((attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
		{
			error = ERROR_REPARSE_TAG_INVALID;
			return false;
		}

		PSID owner = nullptr;
		PACL dacl = nullptr;
		PSECURITY_DESCRIPTOR descriptor = nullptr;
		const DWORD status = GetSecurityInfo(object, SE_FILE_OBJECT,
			OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
			&owner, nullptr, &dacl, nullptr, &descriptor);
		if (status != ERROR_SUCCESS)
		{
			error = status;
			return false;
		}

		bool protectedObject = IsExpectedPrivilegedWriter(owner) && dacl != nullptr;
		GENERIC_MAPPING mapping = {
			FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS
		};
		for (DWORD index = 0; protectedObject && index < dacl->AceCount; ++index)
		{
			void* ace = nullptr;
			if (!GetAce(dacl, index, &ace) || !ace)
			{
				protectedObject = false;
				break;
			}
			const auto* header = static_cast<const ACE_HEADER*>(ace);
			if ((header->AceFlags & INHERIT_ONLY_ACE) != 0)
				continue;
			const bool allowedAce = header->AceType == ACCESS_ALLOWED_ACE_TYPE ||
				header->AceType == ACCESS_ALLOWED_OBJECT_ACE_TYPE ||
				header->AceType == ACCESS_ALLOWED_CALLBACK_ACE_TYPE ||
				header->AceType == ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE;
			const bool knownNonGrantingAce =
				header->AceType == ACCESS_DENIED_ACE_TYPE ||
				header->AceType == ACCESS_DENIED_OBJECT_ACE_TYPE ||
				header->AceType == ACCESS_DENIED_CALLBACK_ACE_TYPE ||
				header->AceType == ACCESS_DENIED_CALLBACK_OBJECT_ACE_TYPE ||
				header->AceType == SYSTEM_AUDIT_ACE_TYPE ||
				header->AceType == SYSTEM_AUDIT_OBJECT_ACE_TYPE ||
				header->AceType == SYSTEM_AUDIT_CALLBACK_ACE_TYPE ||
				header->AceType == SYSTEM_AUDIT_CALLBACK_OBJECT_ACE_TYPE ||
				header->AceType == SYSTEM_MANDATORY_LABEL_ACE_TYPE ||
				header->AceType == SYSTEM_RESOURCE_ATTRIBUTE_ACE_TYPE ||
				header->AceType == SYSTEM_SCOPED_POLICY_ID_ACE_TYPE ||
				header->AceType == SYSTEM_PROCESS_TRUST_LABEL_ACE_TYPE ||
				header->AceType == SYSTEM_ACCESS_FILTER_ACE_TYPE;
			if (!allowedAce && !knownNonGrantingAce)
			{
				protectedObject = false;
				break;
			}
			if (!allowedAce)
				continue;
			PSID sid = GetAllowedAceSid(ace);
			DWORD mask = GetAllowedAceMask(ace);
			MapGenericMask(&mask, &mapping);
			if (!sid || !IsValidSid(sid) ||
				((mask & unexpectedWriteMask) != 0 &&
					!IsExpectedPrivilegedWriter(sid)))
			{
				protectedObject = false;
			}
		}
		LocalFree(descriptor);
		error = protectedObject ? NO_ERROR : ERROR_ACCESS_DENIED;
		return protectedObject;
	}
}