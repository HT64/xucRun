// Purpose: Orchestrates broker service status and administrative commands.
#pragma once

#include <Windows.h>

namespace xucrun::broker_status
{
	enum class BrokerServiceStatus
	{
		Unknown,
		NotInstalled,
		Stopped,
		Disabled,
		Starting,
		Stopping,
		Running
	};

	struct BrokerServiceState
	{
		BrokerServiceStatus status = BrokerServiceStatus::Unknown;
		DWORD error = NO_ERROR;
		DWORD serviceExitCode = NO_ERROR;
	};

	enum class BrokerStorePurgeResult
	{
		Succeeded,
		Cancelled,
		Failed
	};

	BrokerServiceState QueryBrokerServiceState();
	bool WaitForBrokerServiceState(BrokerServiceStatus expectedStatus,
		DWORD timeoutMilliseconds, BrokerServiceState& finalState);
	BrokerStorePurgeResult PurgeBrokerStore();
	bool EnableBrokerService(DWORD& windowsError);
}