#pragma once

#include "CoreMinimal.h"

/**
 * Development (-CINetStats): every two seconds this machine's frame times and, for each of its network connections
 * (the server's, on a guest; every guest's, on a host), the round trip, the traffic each way, the packet loss and how
 * much is waiting to be sent, to the log as NETSTATS lines. For measuring online lag; costs nothing without the flag.
 */
namespace ChaosImpactNetStats
{
	/** Starts the reports once per run, if the command line asks for them. */
	void StartIfAsked();
}
