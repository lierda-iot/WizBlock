# Network Manager

`network_manager` is an optional ESP-IDF component that owns Wi-Fi and LTE
connection policy for one of three startup-locked modes:

- Wi-Fi only
- 4G only
- Wi-Fi preferred with 4G fallback

## Build Configuration

Enable `CONFIG_NETWORK_MANAGER_ENABLE` and select one default mode in Kconfig.
The component is not compiled and exports no include path or dependencies while
disabled. When enabled, it links the ESP-IDF, `net_mgmt`, and `lte_hal`
dependencies needed by all three runtime modes.

Timing, debounce, and retry policy is fixed when the firmware is built. There
is no runtime tuning API. The available Kconfig values are:

| Kconfig option | Default | Purpose |
| --- | ---: | --- |
| `NETWORK_MANAGER_INITIAL_ATTEMPT_TIMEOUT_MS` | 6000 ms | Maximum cellular startup barrier wait; zero makes the wait non-blocking. |
| `NETWORK_MANAGER_WIFI_CONNECT_STABLE_MS` | 10000 ms | Wi-Fi stable-ready and retry-reset window. |
| `NETWORK_MANAGER_WIFI_DISCONNECT_STABLE_MS` | 3000 ms | Wi-Fi stable-disconnect window. |
| `NETWORK_MANAGER_WIFI_RETRY_LIMIT` | 10 | Automatic Wi-Fi retries per cycle. |
| `NETWORK_MANAGER_WIFI_RETRY_UNLIMITED` | n | Continue retries after the limit; the counter saturates at 255 and `retry_exhausted` is not entered. |
| `NETWORK_MANAGER_WIFI_RETRY_INITIAL_BACKOFF_MS` | 1000 ms | First Wi-Fi retry delay. |
| `NETWORK_MANAGER_WIFI_RETRY_MAX_BACKOFF_MS` | 30000 ms | Wi-Fi retry delay cap. |
| `NETWORK_MANAGER_CELLULAR_INITIAL_IPV4_WAIT_MS` | 20000 ms | Initial IPv4 observation window. |
| `NETWORK_MANAGER_CELLULAR_POWER_OFF_HOLD_MS` | 1000 ms | LTE off interval used only by explicit reconnect. |
| `NETWORK_MANAGER_TASK_STACK_IN_PSRAM` | n | Place worker and dispatcher stacks in PSRAM when supported. |

Time values other than the startup barrier accept `1..600000` ms; the startup
barrier accepts `0..60000` ms, and retry limits accept `1..255`. Wi-Fi backoff
doubles from its initial value and is capped by its maximum.

Every cellular consumer project must also set the ESP-IDF option
`CONFIG_ESP_NETIF_IP_LOST_TIMER_INTERVAL=5`. This is a passive lost-IP fallback
for cases where a direct cellular event does not promptly result in an IP
event. It does not poll the modem or trigger reconnect, power-cycle, retry, or
self-recovery behavior.

## Cellular Observation Boundary

Cellular state is observation-only. The component consumes the closed-source
stack's existing `NET_4G_EVENT_CONNECTED` and `NET_4G_EVENT_DISCONNECTED`
reports through GNU linker `--wrap=lsd_net_send_event`, together with ESP
ETH/IP facts and the existing `lsd_net_mgmt` active-interface callback. The
wrapper calls `__real_lsd_net_send_event()` first and preserves its result. It
then records only the cellular link/IPv4 fact under the component lock, clears
the 4G internet fact on disconnect when 4G is active, and wakes the worker. It
does not synthesize or resend a cellular event, and it never holds the
component lock while calling the closed-source function.

The worker commits those facts to snapshots and events within its next runtime
cycle. A 4G disconnect does not disturb a healthy Wi-Fi path in dual mode. The
component does not poll `lte_hal_get_state()`, `lsd_network_is_ready()`, or
`lsd_netif_get()`, infer SIM presence, synthesize recovery events, run LTE power
cycles, retry/back off, self-check, or recreate the manager after a cellular
disconnect or lost IP.

`network_manager_is_ready()` and `network_manager_get_snapshot()` are read-only
queries over the worker's committed cache. They do not access the cellular
driver, change the revision, publish an event, or create an effect. Cellular
retry fields remain ABI-compatible and report `0/0/false`.

## Startup

The public API is declared in `include/network_manager.h`. Call
`network_manager_set_mode()` only before the first `network_manager_start()` to
override the Kconfig default for the current boot. The mode is not persisted.
`network_manager_start()` is idempotent after start has been accepted. A
`START_FAILED` instance is not restarted in place because `net_mgmt` must not be
initialized more than once; use a controlled reboot for recovery.

In 4G-only and dual mode, `network_manager_start()` waits at most
`NETWORK_MANAGER_INITIAL_ATTEMPT_TIMEOUT_MS` for worker-owned LTE power-on and
the single manager initialization attempt. Timeout returns `ESP_OK`, records a
startup barrier fault, and leaves the worker running. Observe `START_RESULT`
and the lifecycle snapshot for the eventual result; link, IPv4, and internet
readiness are always asynchronous.

A typical call order is:

1. Optionally subscribe before start to receive startup events.
2. Optionally override the Kconfig mode.
3. Start the component.
4. Query snapshots and react to events for readiness.
5. Apply Wi-Fi configuration after start when the selected mode uses Wi-Fi.

`ESP_OK` from an asynchronous configuration, disconnect, or reconnect API means
that the request was accepted, not that the operation has completed. Use the
returned nonzero `operation_id` to correlate result events.

## Wi-Fi Configuration

Wi-Fi credentials are length-delimited byte arrays, not C strings. SSIDs use
1..32 bytes. Passwords use either an 8..63 byte printable passphrase or an exact
64-character hexadecimal PSK. Open networks are not supported.

`persist=false` changes only the current in-memory configuration.
`persist=true` also saves it through the component's two-slot NVS transaction.
Clearing persisted configuration does not clear or disconnect the current
configuration. Wi-Fi credentials are never included in events, snapshots,
fault records, or logs.

## Disconnect and Reconnect

Both `network_manager_request_disconnect()` and
`network_manager_request_reconnect()` are shared by all three modes. Disconnect
means complete manual offline for every link enabled by the locked mode.
Reconnect reapplies the current in-memory Wi-Fi configuration, resets the
applicable Wi-Fi retry budget, and rebuilds every link enabled by the mode. It is an explicit caller
operation, not SIM hot-plug recovery. In 4G-only or dual mode, the LTE
off/hold/on sequence may expose closed-source USB/netif, memory, or network-
readiness transients and can leave the lower stack in an unexpected state.
Business-layer callers should therefore avoid this compatibility entry unless
they explicitly accept those risks. The entry is retained for compatibility
and is temporarily not recommended for business use. Cellular observation
itself never invokes it, and it never calls `lsd_network_mgmt_deinit()` or
initializes the manager a second time.

Repeated requests of the same type are idempotent and report
`ALREADY_IN_PROGRESS`. A reconnect and disconnect are not accepted at the same
time; the opposite active operation returns `ESP_ERR_INVALID_STATE`.

## State, Events, and Threading

Configuration and disconnect/reconnect APIs validate and enqueue requests, then
report completion through events and snapshots. User callbacks run on a
dispatcher task, separate from the network worker. The event pointer is valid
only during the callback, and a callback must not block while waiting for
network progress. Public APIs may be called from task context but not from an
ISR.

The snapshot is the authoritative current state. Events are ordered change
notifications and include the committed snapshot at publication time. Critical
fault and exhaustion events have an out-of-band retention path; if ordinary
events overflow, callers can recover using snapshot revisions, overflow counts,
and the bounded fault history.

Common synchronous API errors are:

- `ESP_ERR_INVALID_ARG`: invalid pointer, mode, or Wi-Fi credential.
- `ESP_ERR_INVALID_STATE`: lifecycle or opposite-operation conflict.
- `ESP_ERR_TIMEOUT`: the non-blocking command queue is full.
- `ESP_ERR_NOT_FOUND`: requested configuration or subscription is absent.
- `ESP_ERR_NO_MEM`: no subscription slot or startup task/queue resource.

## Host Verification

Run the Windows LLVM host regression suite with:

```powershell
.\tests\run_host_tests.ps1 -ClangPath <path-to-clang.exe>
```

The runner compiles and executes 21 pure C model, tuning, and fake-adapter
facade tests. Its generated files stay under `tests/build`.

The 2026-08-14 regression used clang 22.1.8 for target
`x86_64-w64-windows-gnu`; all 21 executables returned zero. Coverage includes
direct 4G disconnect consumption, idempotent duplicate lost-IP, dual-mode Wi-Fi
isolation, and zero runtime calls to `lsd_network_is_ready()` and
`lsd_netif_get()`. The same component image was then accepted through
`lte_net_demo`: a real lower-stack disconnect at 64681 ms was visible in the
next 5-second Demo snapshot at 66791 ms (`NONE`, not ready, `WAIT_LINK`) without
reconnect, LTE power-cycle, recovery, panic, assert, or watchdog activity.
