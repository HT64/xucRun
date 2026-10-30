// Purpose: Declares the authenticated named pipe server of the broker service.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
// Provides service operation and isolated self-tests for central selection rules.
#pragma once

#include "ClientImageIdentity.h"

#include <Windows.h>

#include <string>

using BrokerLogCallback = void (*)(WORD eventType, const std::wstring& message);

bool RunApprovalChallengeIdentitySelfTest();
bool RunTrackedProcessListSelectionSelfTest();
DWORD RunBrokerPipeServer(HANDLE stopEvent,
	const ClientImageIdentity& brokerImageIdentity,
	BrokerLogCallback log);
