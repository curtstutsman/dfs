"""
Integration tests for the DFS client/server stack.

Spawns real server and client binaries as subprocesses and asserts on
filesystem state. Tests focus on scenarios that cannot be covered by the
C++ unit tests: inotify auto-sync, async callback propagation, reconnect
after disconnect, and multi-client propagation.

Run: python3 -m unittest tests/integration_test -v
  or: make integration
"""

import os
import shutil
import subprocess
import time
import unittest

SERVER_ADDR   = "0.0.0.0:53998"
SERVER_MNT    = "tmp/integration-server/"
CLIENT_A_MNT  = "tmp/integration-client-a/"
CLIENT_B_MNT  = "tmp/integration-client-b/"
SERVER_BIN    = "bin/dfs-server"
CLIENT_BIN    = "bin/dfs-client"
STARTUP_DELAY = 1.0   # seconds to wait after starting a long-lived process
POLL_TIMEOUT  = 5.0   # max seconds to wait for eventual consistency
POLL_INTERVAL = 0.05  # seconds between filesystem polls


def wait_for(predicate, timeout=POLL_TIMEOUT, interval=POLL_INTERVAL):
    """Poll predicate until it returns True or timeout expires."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return True
        time.sleep(interval)
    return False


def clear_dir(path):
    for entry in os.scandir(path):
        if entry.is_dir(follow_symlinks=False):
            shutil.rmtree(entry.path)
        else:
            os.remove(entry.path)


def write_file(path, content="hello"):
    with open(path, "w") as f:
        f.write(content)


def read_file(path):
    with open(path, "r") as f:
        return f.read()


def file_exists(path):
    return os.path.exists(path)


class IntegrationTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        for d in (SERVER_MNT, CLIENT_A_MNT, CLIENT_B_MNT):
            os.makedirs(d, exist_ok=True)

    def setUp(self):
        clear_dir(SERVER_MNT)
        clear_dir(CLIENT_A_MNT)
        clear_dir(CLIENT_B_MNT)
        self._clients = []
        self.server = subprocess.Popen(
            [SERVER_BIN, "--address", SERVER_ADDR,
             "--mount_path", SERVER_MNT, "--num_async_threads", "2"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        time.sleep(STARTUP_DELAY)

    def tearDown(self):
        for proc in self._clients:
            proc.terminate()
            proc.wait()
        self._clients.clear()
        self.server.terminate()
        self.server.wait()

    # ------------------------------------------------------------------ helpers

    def mount(self, mnt_path):
        """Start a mounted client and wait for the callback loop to register."""
        proc = subprocess.Popen(
            [CLIENT_BIN, "--address", SERVER_ADDR,
             "--mount_path", mnt_path, "mount"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        self._clients.append(proc)
        time.sleep(STARTUP_DELAY)
        return proc

    def unmount(self, proc):
        proc.terminate()
        proc.wait()
        self._clients.remove(proc)

    def one_shot(self, mnt_path, *args):
        """Run a one-shot client command (store/fetch/delete/list/stat)."""
        subprocess.run(
            [CLIENT_BIN, "--address", SERVER_ADDR, "--mount_path", mnt_path, *args],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            check=False,
        )

    # ------------------------------------------------------------------ tests

    def test_inotify_store(self):
        """File written to client mount dir is auto-stored to server via inotify."""
        self.mount(CLIENT_A_MNT)
        write_file(CLIENT_A_MNT + "sync.txt", "inotify store")
        self.assertTrue(
            wait_for(lambda: file_exists(SERVER_MNT + "sync.txt")),
            "server never received inotify-triggered store",
        )
        self.assertEqual(read_file(SERVER_MNT + "sync.txt"), "inotify store")

    def test_inotify_delete(self):
        """File deleted from client mount dir is auto-deleted from server via inotify."""
        self.one_shot(CLIENT_A_MNT, "store", "del.txt")
        write_file(CLIENT_A_MNT + "del.txt", "to delete")
        self.mount(CLIENT_A_MNT)
        write_file(CLIENT_A_MNT + "del.txt", "to delete")
        self.assertTrue(wait_for(lambda: file_exists(SERVER_MNT + "del.txt")))

        os.remove(CLIENT_A_MNT + "del.txt")
        self.assertTrue(
            wait_for(lambda: not file_exists(SERVER_MNT + "del.txt")),
            "server file was not removed after inotify-triggered delete",
        )

    def test_callback_fetch(self):
        """File stored on server by one client is fetched by a second mounted client via callback."""
        self.mount(CLIENT_B_MNT)

        # store from a separate mount so the callback fires on the server
        write_file(CLIENT_A_MNT + "pushed.txt", "from server")
        self.one_shot(CLIENT_A_MNT, "store", "pushed.txt")

        self.assertTrue(
            wait_for(lambda: file_exists(CLIENT_B_MNT + "pushed.txt")),
            "client B never fetched file pushed by client A",
        )
        self.assertEqual(read_file(CLIENT_B_MNT + "pushed.txt"), "from server")

    def test_reconnect_sync(self):
        """Client that remounts after a disconnect syncs files it missed while offline."""
        client_a = self.mount(CLIENT_A_MNT)
        self.unmount(client_a)

        # store a file while client A is offline
        write_file(CLIENT_B_MNT + "offline.txt", "missed while down")
        self.one_shot(CLIENT_B_MNT, "store", "offline.txt")
        self.assertTrue(wait_for(lambda: file_exists(SERVER_MNT + "offline.txt")))

        # remount client A — callback loop should fetch the missed file
        self.mount(CLIENT_A_MNT)
        self.assertTrue(
            wait_for(lambda: file_exists(CLIENT_A_MNT + "offline.txt")),
            "reconnected client A did not sync file stored while offline",
        )

    def test_multi_client_propagation(self):
        """File written on client A propagates to client B via inotify → server → callback."""
        self.mount(CLIENT_A_MNT)
        self.mount(CLIENT_B_MNT)

        write_file(CLIENT_A_MNT + "shared.txt", "cross-client")

        self.assertTrue(
            wait_for(lambda: file_exists(CLIENT_B_MNT + "shared.txt")),
            "client B never received file propagated from client A",
        )
        self.assertEqual(read_file(CLIENT_B_MNT + "shared.txt"), "cross-client")


if __name__ == "__main__":
    unittest.main(verbosity=2)
