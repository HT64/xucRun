// Purpose: Implements a secure lifecycle for short-lived sensitive strings.
// Approach: Implements the behavior with focused helpers built on shared xucRun interfaces and Windows APIs.
// Behavior: Manages its own buffer and clears contents with SecureZeroMemory.
#include "ClientSensitiveString.h"

#include <Windows.h>

#include <cstring>

namespace xucrun
{
	SecureString::SecureString(const std::wstring& value)
	{
		assign(value.c_str(), value.length());
	}

	SecureString::~SecureString()
	{
		clear();
	}

	void SecureString::clear() noexcept
	{
		if (!data_)
			return;
		SecureZeroMemory(data_, (length_ + 1) * sizeof(wchar_t));
		delete[] data_;
		data_ = nullptr;
		length_ = 0;
	}

	void SecureString::assign(const wchar_t* value, std::size_t valueLength)
	{
		clear();
		if (valueLength == 0)
			return;

		data_ = new wchar_t[valueLength + 1];
		memcpy(data_, value, valueLength * sizeof(wchar_t));
		data_[valueLength] = L'\0';
		length_ = valueLength;
	}

	const wchar_t* SecureString::c_str() const
	{
		return data_ ? data_ : L"";
	}

	bool SecureString::empty() const
	{
		return !data_ || length_ == 0;
	}

	std::size_t SecureString::size() const
	{
		return length_;
	}
}