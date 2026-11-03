#!/usr/bin/env bash
# TEMPORARY: why the real helper does not answer on macOS (test_helper_server*).
# Starts the test helper by hand and watches its port, then runs the fuzz suite.
set -uo pipefail
B="${1:?build dir}"
HELPER="$B/freetunnel_test_helper"
W="$(mktemp -d)"
echo "== tool versions"; sw_vers; uname -a
echo "== listeners on loopback before anything"; lsof -nP -iTCP -sTCP:LISTEN 2>/dev/null | head -40

echo "== self-connect probe: bind :0, close, connect to the same port"
python3 - <<'PY'
import socket, time
for i in range(20):
    s = socket.socket(); s.bind(('127.0.0.1', 0)); s.listen(1); p = s.getsockname()[1]; s.close()
    c = socket.socket(); c.settimeout(0.2)
    try:
        c.connect(('127.0.0.1', p)); print('port', p, 'CONNECTED from local', c.getsockname(), 'peer', c.getpeername())
    except Exception as e:
        print('port', p, 'refused as expected:', type(e).__name__)
    c.close()
PY

PORT=$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1]);s.close()')
printf '%s' "diag-token-0123456789" > "$W/.fthelper-1"
echo "== starting helper on $PORT"
T0=$(python3 -c 'import time;print(time.time())')
FT_TEST_SKIP_PRIVILEGE_CHECK=1 "$HELPER" --helper --port "$PORT" --token-file "$W/.fthelper-1" >"$W/out.txt" 2>"$W/err.txt" &
PID=$!
for i in $(seq 1 30); do
  python3 - "$PORT" "$T0" <<'PY'
import socket, sys, time, json
port=int(sys.argv[1]); t0=float(sys.argv[2])
c=socket.socket(); c.settimeout(0.3)
try:
    c.connect(('127.0.0.1',port))
    c.sendall(b'{"cmd":"hello","nonce":"abc"}\n')
    try:
        d=c.recv(4096)
        print('%.3fs connected local=%s got=%r' % (time.time()-t0, c.getsockname(), d[:120]))
    except Exception as e:
        print('%.3fs connected local=%s no answer (%s)' % (time.time()-t0, c.getsockname(), type(e).__name__))
except Exception as e:
    print('%.3fs connect failed: %s' % (time.time()-t0, type(e).__name__))
c.close()
PY
  lsof -nP -iTCP:"$PORT" 2>/dev/null | sed 's/^/    lsof: /' | head -6
  ps -o pid,stat,etime,command -p "$PID" | tail -1 | sed 's/^/    ps: /'
  sleep 0.2
done
echo "== sample of the helper"; sample "$PID" 2 -mayDie 2>&1 | head -120
echo "== helper stderr"; cat "$W/err.txt" | head -50
echo "== helper stdout"; cat "$W/out.txt" | head -20
kill -TERM "$PID" 2>/dev/null; sleep 1; ps -p "$PID" >/dev/null && { echo "still alive after TERM"; kill -KILL "$PID"; }
wait "$PID"; echo "helper exit: $?"

echo "== the fuzz suite, with the helper's stderr"
(cd "$B" && QT_QPA_PLATFORM=offscreen ./test_helper_server_fuzz 2>&1 | grep -v waitForDisconnected | head -80)
echo "== the lifecycle suite"
(cd "$B" && QT_QPA_PLATFORM=offscreen timeout 300 ./test_helper_server_lifecycle 2>&1 | head -80)
exit 0
