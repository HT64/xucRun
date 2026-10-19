// Purpose: Implements protected memory management for passwords and other secrets.
// Allocates lockable memory and overwrites it before every release.
#include "SensitiveString.h"

#include <algorithm>
#include <limits>

namespace
{
	// Rounds the required locked memory up to whole pages without size overflow.
	bool GetLockedAllocationSize(std::size_t characterCount,
		std::size_t& allocationSize) noexcept
	{
		allocationSize = 0;
		if (characterCount >
			(std::numeric_limits<std::size_t>::max)() / sizeof(wchar_t) - 1)
		{
			return false;
		}
		SYSTEM_INFO systemInfo = {};
		GetSystemInfo(&systemInfo);
		const std::size_t pageSize = systemInfo.dwPageSize;
		const std::size_t requiredSize = (characterCount + 1) * sizeof(wchar_t);
		if (pageSize == 0 || requiredSize >
			(std::numeric_limits<std::size_t>::max)() - (pageSize - 1))
		{
			return false;
		}
		allocationSize = ((requiredSize + pageSize - 1) / pageSize) * pageSize;
		return true;
	}
}

namespace xucrun
{
	// Securely erases the sensitive content before releasing the memory.
	SensitiveString::~SensitiveString()
	{
		clear();
	}

	// Exclusively transfers the locked memory and clears the source.
	SensitiveString::SensitiveString(SensitiveString&& other) noexcept
		: value(other.value), length(other.length), allocationSize(other.allocationSize),
		assigned(other.assigned)
	{
		other.value = nullptr;
		other.length = 0;
		other.allocationSize = 0;
		other.assigned = false;
	}

	// Replaces the content with exclusively transferred memory and clears the source.
	SensitiveString& SensitiveString::operator=(SensitiveString&& other) noexcept
	{
		if (this != &other)
		{
			clear();
			value = other.value;
			length = other.length;
			allocationSize = other.allocationSize;
			assigned = other.assigned;
			other.value = nullptr;
			other.length = 0;
			other.allocationSize = 0;
			other.assigned = false;
		}
		return *this;
	}

	// Accepts null-free characters exclusively into locked memory.
	bool SensitiveString::assign(const wchar_t* input, std::size_t length)
	{
		clear();
		if (!input ||
			std::find(input, input + length, L'\0') != input + length)
		{
			return false;
		}
		if (!GetLockedAllocationSize(length, allocationSize))
			return false;
		value = static_cast<wchar_t*>(VirtualAlloc(
			nullptr, allocationSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
		if (!value || !VirtualLock(value, allocationSize))
		{
			clear();
			return false;
		}
		std::copy(input, input + length, value);
		value[length] = L'\0';
		this->length = length;
		assigned = true;
		return true;
	}

	// Decodes exactly the specified number of null-free UTF-16 characters into locked memory.
	bool SensitiveString::assignUtf16(
		std::span<const std::uint8_t> bytes, std::size_t characterCount)
	{
		clear();
		if ((bytes.size() % 2) != 0 || bytes.size() / 2 != characterCount ||
			!GetLockedAllocationSize(characterCount, allocationSize))
		{
			return false;
		}
		value = static_cast<wchar_t*>(VirtualAlloc(
			nullptr, allocationSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
		if (!value || !VirtualLock(value, allocationSize))
		{
			clear();
			return false;
		}
		for (std::size_t index = 0; index < characterCount; index++)
		{
			const std::uint16_t character =
				static_cast<std::uint16_t>(bytes[index * 2]) |
				(static_cast<std::uint16_t>(bytes[index * 2 + 1]) << 8);
			if (character == 0)
			{
				clear();
				return false;
			}
			value[index] = static_cast<wchar_t>(character);
		}
		value[characterCount] = L'\0';
		length = characterCount;
		assigned = true;
		return true;
	}

	// Overwrites all sensitive memory before unlocking and releasing it.
	void SensitiveString::clear() noexcept
	{
		if (value)
		{
			SecureZeroMemory(value, allocationSize);
			VirtualUnlock(value, allocationSize);
			VirtualFree(value, 0, MEM_RELEASE);
		}
		value = nullptr;
		length = 0;
		allocationSize = 0;
		assigned = false;
	}

	// Always returns a valid, null-terminated read pointer without copying the content.
	const wchar_t* SensitiveString::c_str() const noexcept
	{
		return value ? value : L"";
	}

	// Returns the number of sensitive characters without the null terminator.
	std::size_t SensitiveString::size() const noexcept
	{
		return length;
	}

	// Reports whether the sensitive content contains no characters.
	bool SensitiveString::empty() const noexcept
	{
		return size() == 0;
	}

	// Distinguishes a successful empty assignment from the unassigned state.
	bool SensitiveString::isAssigned() const noexcept
	{
		return assigned;
	}

	// Reports whether the content resides in a locked memory region.
	bool SensitiveString::isMemoryLocked() const noexcept
	{
		return value != nullptr;
	}
}