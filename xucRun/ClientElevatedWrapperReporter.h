// Purpose: Defines reporting of an elevated wrapper launch back to the broker.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
// Behavior: Reports launch results and holds the launched process handle until broker acknowledgement.

#pragma once

#include <Windows.h>

#include "BrokerContracts.h"

namespace xucrun::elevated_wrapper
{
	class BrokerElevatedResultReporter
	{
	private:
		HANDLE resultHandle = nullptr;
		HANDLE acknowledgementEvent = nullptr;
		HANDLE reportedProcessHandle = nullptr;
		xucrun::broker::ElevatedLaunchHelperResult result;

	public:
		BrokerElevatedResultReporter(HANDLE handle, HANDLE acknowledgement) noexcept;
		~BrokerElevatedResultReporter();

		bool active() const noexcept;
		bool serverProcessMatches(DWORD processId) const noexcept;
		void complete(DWORD windowsError, DWORD processId = 0,
			HANDLE processHandle = nullptr) noexcept;
	};

	HANDLE ParseBrokerElevatedResultHandle(int argc, wchar_t* argv[]);
	HANDLE ParseBrokerElevatedAcknowledgementHandle(int argc, wchar_t* argv[]);
}