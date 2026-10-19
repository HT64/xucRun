// Purpose: Defines handling of internal broker test switches in the client.
// Behavior: Executes recognized tests and leaves argv ownership and cleanup to the caller.
#pragma once

#include <optional>
#include <string>

namespace xucrun::internal_broker_tests
{
	std::optional<int> HandleInternalBrokerTestCommand(
		const std::wstring& firstArg, int argc, wchar_t* argv[]);
}