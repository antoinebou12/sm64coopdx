# CoopNet server deployment for the Switch build

The game build writes `build-logs/switch-coopnet-identity.txt`. Its final
`UINT64_C(...)` line is the fingerprint entry to add to the official server's
existing private `server_extra` allowlist. Use only the fingerprint from the
exact NRO being distributed; do not substitute an official desktop executable
hash.

No CoopNet server source or protocol patch is required. The operator only needs
to authorize the final fingerprint using the server's existing policy/config
mechanism. Repeat this authorization whenever a newly distributed NRO has a
different fingerprint.

Allowlist access and deployment remain the responsibility of the official
CoopNet operator.
