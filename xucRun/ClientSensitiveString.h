// Purpose: Defines exclusive storage for short-lived sensitive strings.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
// Behavior: Overwrites used memory before release and forbids copy and move operations.
#pragma once

#include <cstddef>
#include <string>

namespace xucrun
{
	class SecureString
	{
	public:
		SecureString() = default;
		explicit SecureString(const std::wstring& value);
		~SecureString();

		void clear() noexcept;
		void assign(const wchar_t* value, std::size_t valueLength);
		const wchar_t* c_str() const;
		bool empty() const;
		std::size_t size() const;

		SecureString(const SecureString&) = delete;
		SecureString& operator=(const SecureString&) = delete;
		SecureString(SecureString&&) = delete;
		SecureString& operator=(SecureString&&) = delete;

	private:
		wchar_t* data_ = nullptr;
		std::size_t length_ = 0;
	};
}