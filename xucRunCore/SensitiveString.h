// Purpose: Defines a non-copyable storage container for sensitive strings.
// Locks the memory when possible and deterministically erases its content.
#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace xucrun
{
	class SensitiveString
	{
	public:
		// Creates an unassigned sensitive string without allocating memory.
		SensitiveString() = default;
		~SensitiveString();

		// Prevents copying sensitive memory contents.
		SensitiveString(const SensitiveString&) = delete;
		// Prevents copy assignment of sensitive memory contents.
		SensitiveString& operator=(const SensitiveString&) = delete;
		SensitiveString(SensitiveString&& other) noexcept;
		SensitiveString& operator=(SensitiveString&& other) noexcept;

		bool assign(const wchar_t* value, std::size_t length);
		bool assignUtf16(std::span<const std::uint8_t> bytes, std::size_t characterCount);
		void clear() noexcept;
		const wchar_t* c_str() const noexcept;
		std::size_t size() const noexcept;
		bool empty() const noexcept;
		bool isAssigned() const noexcept;
		bool isMemoryLocked() const noexcept;

	private:
		wchar_t* value = nullptr;
		std::size_t length = 0;
		std::size_t allocationSize = 0;
		bool assigned = false;
	};
}