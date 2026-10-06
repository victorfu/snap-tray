#!/usr/bin/env python3
"""Run media tests against a private PulseAudio server with generated sound."""
import os
import pathlib
import subprocess
import sys
import tempfile
import time

with tempfile.TemporaryDirectory(prefix='snaptray-audio-test-') as directory:
    root = pathlib.Path(directory)
    socket = root / 'pulse.sock'
    config = root / 'default.pa'
    config.write_text(f'''load-module module-native-protocol-unix socket={socket} auth-anonymous=1
load-module module-null-sink sink_name=snaptray_system rate=48000 channels=2
load-module module-null-sink sink_name=snaptray_mic_sink rate=48000 channels=2
load-module module-remap-source master=snaptray_mic_sink.monitor source_name=snaptray_microphone
load-module module-sine sink=snaptray_system frequency=440
load-module module-sine sink=snaptray_mic_sink frequency=880
set-default-sink snaptray_system
set-default-source snaptray_microphone
''')
    environment = dict(os.environ, PULSE_SERVER=f'unix:{socket}',
                       SNAPTRAY_TEST_ISOLATED_AUDIO='1', SNAPTRAY_FFMPEG_ENCODER='software',
                       DBUS_SESSION_BUS_ADDRESS='unix:path=/nonexistent-snaptray-test-bus')
    with (root / 'server.log').open('w+') as log:
        server = subprocess.Popen(['pulseaudio', '-n', '-F', str(config), '--daemonize=no',
                                   '--use-pid-file=no', '--exit-idle-time=-1', '--disable-shm'],
                                  stdout=log, stderr=log, env=environment)
        try:
            for _ in range(100):
                if server.poll() is not None:
                    log.seek(0)
                    raise RuntimeError(log.read())
                if socket.exists():
                    result = subprocess.run(['pactl', 'info'], env=environment,
                                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                    if result.returncode == 0:
                        break
                time.sleep(.05)
            else:
                raise RuntimeError('Private audio server startup timed out')
            result = subprocess.run(sys.argv[1:], env=environment)
            sys.exit(result.returncode)
        finally:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()
