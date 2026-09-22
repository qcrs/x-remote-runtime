# Local remote simulation

This helper runs the formal client and server as two processes on
`server310p`. It uses TCP on the loopback interface and does not contain a
second copy of either Runtime implementation.

Build the repository first:

```bash
COREX=/usr/local/corex-4.4.0 ./scripts/build-sdk.sh
```

Then use two terminals:

```bash
# Terminal A
cd /home/lvtong/corex-remote-runtime
./dev/local-remote/start-server.sh
```

```bash
# Terminal B
cd /home/lvtong/corex-remote-runtime
./dev/local-remote/run-client.sh
```

The default endpoint is `127.0.0.1:50051`. `COREX_REMOTE_HOST` selects the
client endpoint and `COREX_REMOTE_PORT` selects the TCP port. The server is
intentionally bound to loopback for this local mode.

This can exercise Client/Server separation, RPC, Runtime semantics,
concurrency, CUDA API behavior already present in the baseline, and error
handling. It does not replace real network validation, x86_64-to-aarch64
validation, WSL deployment validation, or cross-host ABI validation.
