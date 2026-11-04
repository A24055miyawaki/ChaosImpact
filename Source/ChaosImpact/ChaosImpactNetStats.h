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
	/**
	 * Always on, online: keeps a slow line from clogging. A phone's tethering or a busy line queues whatever is sent
	 * faster than it carries, and the queue is delay (a second or more). Each connection is watched against the best
	 * round trip it has had lately: when it climbs well above that, less is sent on it (UE's per-connection rate);
	 * when it comes back, the rate climbs back. A line that is only far away (its best is already high) is left as it is.
	 */
	void StartLatencyGuard();
}
