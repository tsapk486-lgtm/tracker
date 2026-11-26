"""HTTP JSON receiver for tracker telemetry."""
import hmac
import json
import os
import math
import sqlite3
from datetime import datetime, timezone
from pathlib import Path

from flask import Flask, jsonify, request, render_template


def create_app(config=None):
    app = Flask(__name__)
    app.config.update(
        TRACKER_API_KEY=os.environ.get("TRACKER_API_KEY"),
        DATABASE=os.environ.get("TRACKER_DATABASE", str(Path(__file__).with_name("tracker.db"))),
        MAX_CONTENT_LENGTH=64 * 1024,
    )
    if config:
        app.config.update(config)
    if not app.config["TRACKER_API_KEY"]:
        raise RuntimeError("Set TRACKER_API_KEY before starting the application")

    with sqlite3.connect(app.config["DATABASE"]) as db:
        db.execute("""CREATE TABLE IF NOT EXISTS readings (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            received_at TEXT NOT NULL,
            payload TEXT NOT NULL
        )""")
        db.execute("""CREATE TABLE IF NOT EXISTS devices (
            device_id TEXT PRIMARY KEY, last_seen TEXT NOT NULL,
            gps_fix INTEGER NOT NULL, uptime_ms INTEGER
        )""")
        db.execute("""CREATE TABLE IF NOT EXISTS device_telemetry (
            device_id TEXT PRIMARY KEY, payload TEXT NOT NULL
        )""")

    @app.get("/health")
    def health():
        return jsonify(status="ok")

    @app.get("/")
    def dashboard():
        return render_template("dashboard.html")

    @app.get("/api/positions")
    def positions():
        supplied = request.headers.get("X-API-Key", "")
        if not hmac.compare_digest(supplied.encode(), app.config["TRACKER_API_KEY"].encode()):
            return jsonify(error="Unauthorized"), 401
        with sqlite3.connect(app.config["DATABASE"], timeout=10) as db:
            rows = db.execute("SELECT id, received_at, payload FROM readings ORDER BY id DESC LIMIT 5000").fetchall()
        points = []
        motion = []
        for row_id, received_at, raw in reversed(rows):
            payload = json.loads(raw)
            imu = payload.get("imu")
            if isinstance(imu, dict):
                values = {name: value for name in ("rms_movement", "peak_magnitude")
                          if type(value := imu.get(name)) in (int, float)
                          and math.isfinite(value) and value >= 0}
                if values:
                    motion.append(dict(id=row_id, received_at=received_at,
                                       device_id=str(payload.get("device_id", "unknown")), **values))
            gps = payload.get("gps")
            if isinstance(gps, dict):
                payload = {**payload, **gps, "speed": gps.get("speed_kmh")}
                if not gps.get("fix"):
                    continue
            lat, lon = payload.get("latitude"), payload.get("longitude")
            if (type(lat) not in (int, float) or type(lon) not in (int, float)
                    or not math.isfinite(lat) or not math.isfinite(lon)
                    or not -90 <= lat <= 90 or not -180 <= lon <= 180):
                continue
            points.append(dict(id=row_id, received_at=received_at, **{
                "device_id": str(payload.get("device_id", "unknown")),
                "latitude": lat, "longitude": lon,
                "battery": payload.get("battery"), "speed": payload.get("speed"),
            }))
        with sqlite3.connect(app.config["DATABASE"], timeout=10) as db:
            devices = db.execute("""SELECT d.device_id, d.last_seen, d.gps_fix,
                d.uptime_ms, t.payload FROM devices d LEFT JOIN device_telemetry t
                ON t.device_id=d.device_id ORDER BY d.device_id""").fetchall()
        now = datetime.now(timezone.utc)
        response = jsonify(points=points, motion=motion, devices=[{
            "device_id": device, "last_seen": seen, "gps_fix": bool(fix),
            "uptime_ms": uptime,
            "telemetry": json.loads(raw) if raw is not None else {},
            "online": (now - datetime.fromisoformat(seen)).total_seconds() < 90,
        } for device, seen, fix, uptime, raw in devices])
        response.headers["Cache-Control"] = "no-store"
        return response

    @app.post("/api/tracker")
    def receive():
        supplied = request.headers.get("X-API-Key", "")
        if not hmac.compare_digest(supplied.encode(), app.config["TRACKER_API_KEY"].encode()):
            return jsonify(error="Unauthorized"), 401
        if not request.is_json:
            return jsonify(error="Use Content-Type: application/json"), 415
        payload = request.get_json(silent=True)
        if not isinstance(payload, dict) or not payload:
            return jsonify(error="Body must be a non-empty JSON object"), 400
        device = payload.get("device_id", "unknown")
        if not isinstance(device, str) or not 1 <= len(device) <= 128:
            return jsonify(error="device_id must be a string of 1 to 128 characters"), 400
        uptime = payload.get("uptime_ms")
        if uptime is not None and (type(uptime) is not int or not 0 <= uptime <= 2**32 - 1):
            return jsonify(error="uptime_ms must be an unsigned 32-bit integer"), 400
        gps = payload.get("gps")
        fix = payload.get("gps_fix", gps.get("fix", False) if isinstance(gps, dict)
                          else "latitude" in payload and "longitude" in payload)
        if type(fix) is not bool:
            return jsonify(error="gps_fix must be boolean"), 400
        received_at = datetime.now(timezone.utc).isoformat()
        with sqlite3.connect(app.config["DATABASE"], timeout=10) as db:
            db.execute("""INSERT INTO devices VALUES (?, ?, ?, ?)
                ON CONFLICT(device_id) DO UPDATE SET last_seen=excluded.last_seen,
                gps_fix=excluded.gps_fix, uptime_ms=excluded.uptime_ms""",
                (device, received_at, int(fix), uptime))
            db.execute("""INSERT INTO device_telemetry VALUES (?, ?)
                ON CONFLICT(device_id) DO UPDATE SET payload=excluded.payload""",
                (device, json.dumps(payload)))
            reading_id = None
            if ("latitude" in payload and "longitude" in payload) or isinstance(gps, dict) or isinstance(payload.get("imu"), dict):
                cursor = db.execute(
                    "INSERT INTO readings (received_at, payload) VALUES (?, ?)",
                    (received_at, json.dumps(payload)),
                )
                reading_id = cursor.lastrowid
        return jsonify(status="received", id=reading_id, received_at=received_at), 201

    @app.errorhandler(413)
    def too_large(error):
        return jsonify(error="Payload exceeds 64 KiB"), 413

    return app
