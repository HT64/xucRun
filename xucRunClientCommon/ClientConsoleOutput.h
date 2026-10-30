// Purpose: Defines output of longer client text to console, redirection, or dialog.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
// Behavior: Reports the actual output target used and offers a separate error path.
#pragma once

#include <string>

namespace xucrun::console_output
{
	enum class TextOutputResult
	{
		NoOutputTarget,
		Complete,
		WriteFailed
	};

	bool HasAvailableTarget();
	TextOutputResult WriteToAvailableTarget(const std::wstring& heading,
		const std::wstring& content);

	bool WriteErrorToAvailableTarget(const std::wstring& message);
}