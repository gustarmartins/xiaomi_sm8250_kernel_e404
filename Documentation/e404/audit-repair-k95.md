# K95 thermal and scheduler integration repairs

The K94 audit found controls that were exposed without reaching the active
scheduler paths, and two thermal drivers overwriting one pressure value.

- cpufreq exposes read-only scaling_requested_min_freq/max_freq from user_policy
  under the existing policy lock. Controllers can preserve requested ceilings
  without saving an effective thermal cap. The inherited userspace-minimum
  no-op remains; this interface does not promise writable CPU minimums.
- Software cooling and hardware DCVSH publish separate pressure contributions.
  The scheduler receives their maximum. Recovery releases only that producer.
  Pressure subtraction cannot underflow if a reported frequency exceeds max.
- Successful primary-panel enable and seamless timing updates notify WALT.
  Failed transitions do not publish a new rate. Read-only sched_ravg_window_ns
  and sched_display_refresh_rate expose the effective window and applied mode.
  The existing dynamic enable and user tick value remain authoritative; the
  latest display mode is remembered while automatic changes are disabled.
- CASS honors latency-sensitive tasks by preferring an idle CPU that meets
  demand and clamp requirements. Ordinary tasks keep the existing selection.
  Extreme thermal pressure cannot reduce a candidate denominator to zero.
- The WLAN scheduler watchdog queues the established CDS self-recovery path
  instead of BUGing in timer interrupt context. The existing recovery enable
  policy still applies. This contains the immediate watchdog panic; it does
  not by itself prove Camera/audio survival under memory pressure.

Host regression command:

    python3 tools/testing/selftests/e404/audit_regressions.py

The fixtures compile actual functions with ASan/UBSan and cover both thermal
recovery orders, WALT60/90/120 transitions and disabled/manual settings,
watchdog shutdown/recovery guards, and CASS idle/clamp/affinity/extreme pressure.
Changed scheduler/core objects and complete display/WLAN directories compiled
successfully. Full image, boot proof and user workload results are tracked in
the Poco workspace investigations/audit-repair-20260906 delivery receipt.
