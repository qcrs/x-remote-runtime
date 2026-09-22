# Gate 8D — Installable SDK / Clean Consumer Boundary

## Build + install

```bash
COREX=/usr/local/corex-4.4.0 ./build_g8d.sh
```

Expected:

```text
G8C_BUILD=PASS
G8D_INSTALL=PASS
G8D_BUILD_AND_INSTALL=PASS
```

## Clean consumer build

```bash
COREX=/usr/local/corex-4.4.0 ./build_g8d_consumer.sh
```

Expected:

```text
G8D_CONSUMER_LIBCUDART_DEPENDENCY=ABSENT
G8D_CONSUMER_REMOTE_RUNTIME_DEPENDENCY=PASS
G8D_INTERNAL_SOURCE_LEAK=ABSENT
G8D_CONSUMER_BUILD=PASS
```

## Installed server

```bash
stdbuf -oL ./sdk-install/bin/corex-remote-runtime-server   | tee g8d-server.log
```

## Run

```bash
./run_g8d.sh
```

Expected final:

```text
clean_consumer_exit=0
consumer_libcudart_dependency=ABSENT
consumer_remote_runtime_dependency=PASS
server_device_info_count=2
server_get_kernel_count=1
server_launch_generic_count=1
server_abi_validate_pass_count=1
server_session_count=1
G8D_RESULT=PASS
```
