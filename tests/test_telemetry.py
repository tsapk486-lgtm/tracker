import sqlite3
import tempfile
import unittest

from app import create_app


class TelemetryTest(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.database = self.folder.name + "/tracker.db"
        self.config = {"TESTING": True, "DATABASE": self.database, "TRACKER_API_KEY": "test-key"}
        self.client = create_app(self.config).test_client()
        self.headers = {"X-API-Key": "test-key"}

    def tearDown(self):
        self.folder.cleanup()

    def send(self, payload):
        return self.client.post("/api/tracker", json=payload, headers=self.headers)

    def read(self):
        return self.client.get("/api/positions", headers=self.headers).get_json()

    def test_unknown_nested_fields_and_heartbeat(self):
        payload = {"device_id": "sensor-a", "gps_fix": False, "temperature": 0,
                   "sensors": {"enabled": False, "samples": [1, None, {"volts": 3.7}]},
                   "label": "<script>alert('x')</script>", "empty": ""}
        self.assertEqual(self.send(payload).status_code, 201)
        data = self.read()
        self.assertEqual(data["points"], [])
        self.assertEqual(data["devices"][0]["telemetry"], payload)
        self.assertTrue(data["devices"][0]["online"])
        self.assertEqual(self.client.get("/api/positions").status_code, 401)

    def test_latest_snapshot_and_device_isolation(self):
        self.send({"device_id": "a", "temperature": 20, "latitude": 0, "longitude": 0})
        self.send({"device_id": "b", "pressure": 1000})
        latest = {"device_id": "a", "gps_fix": False, "temperature": 21,
                  "online": False, "last_seen": "untrusted"}
        self.send(latest)
        data = self.read()
        devices = {d["device_id"]: d for d in data["devices"]}
        self.assertEqual(devices["a"]["telemetry"], latest)
        self.assertTrue(devices["a"]["online"])
        self.assertNotEqual(devices["a"]["last_seen"], "untrusted")
        self.assertEqual(devices["b"]["telemetry"]["pressure"], 1000)
        self.assertEqual(len(data["points"]), 1)

    def test_existing_database(self):
        with sqlite3.connect(self.database) as db:
            db.execute("DROP TABLE device_telemetry")
            db.execute("INSERT INTO devices VALUES ('legacy', '2026-01-01T00:00:00+00:00', 0, NULL)")
        data = create_app(self.config).test_client().get("/api/positions", headers=self.headers).get_json()
        self.assertEqual(data["devices"][0]["telemetry"], {})

    def test_nested_firmware_motion_without_gps_fix(self):
        payload = {"device_id": "imu-a", "gps": {"fix": False, "latitude": 0, "longitude": 0},
                   "imu": {"rms_movement": .12, "peak_magnitude": 1.8}}
        self.assertEqual(self.send(payload).status_code, 201)
        self.send({**payload, "imu": {"rms_movement": .3, "peak_magnitude": 2.1}})
        data = self.read()
        self.assertEqual(len(data["motion"]), 2)
        self.assertEqual(data["motion"][0]["rms_movement"], .12)
        self.assertEqual(data["points"], [])
        self.assertFalse(data["devices"][0]["gps_fix"])
        self.assertEqual(self.client.get('/api/positions').status_code, 401)

    def test_nested_gps_and_invalid_motion_values(self):
        self.send({"device_id": "a", "gps": {"fix": True, "latitude": -1.2,
                   "longitude": 36.8, "speed_kmh": 12},
                   "imu": {"rms_movement": True, "peak_magnitude": -1}})
        data = self.read()
        self.assertEqual(data["motion"], [])
        self.assertEqual(data["points"][0]["speed"], 12)
        self.assertTrue(data["devices"][0]["gps_fix"])


if __name__ == "__main__":
    unittest.main()
