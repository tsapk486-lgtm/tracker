# Tracker receiver

Receives JSON over HTTP POST and stores the complete payload with a server UTC
timestamp in SQLite. Payload fields are intentionally unrestricted until the
tracker protocol is known. This receiver does not implement raw TCP/UDP protocols.

## Local use

```sh
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
export TRACKER_API_KEY='replace-with-a-long-random-secret'
flask --app 'app:create_app()' run
```

Send a reading (replace the sample fields with your tracker's data):

```sh
curl -X POST http://127.0.0.1:5000/api/tracker \
  -H 'Content-Type: application/json' \
  -H 'X-API-Key: replace-with-a-long-random-secret' \
  -d '{"device_id":"tracker-001","latitude":-1.2864,"longitude":36.8172,"battery":85}'
```

Successful requests return HTTP 201 with `status`, the stored row `id`, and
`received_at`. Missing/incorrect keys return 401; invalid JSON objects return 400;
non-JSON requests return 415; payloads over 64 KiB return 413.
`GET /health` returns `{"status":"ok"}`. Data retrieval requires the API key.
Every accepted POST creates a new row, including retries of the same payload.

## Live map dashboard

Open `/` on the deployed domain and enter your tracker API key to connect.
The dashboard refreshes every five seconds, shows each device's latest marker,
draws its stored route, and supports device selection and following. The key is
kept only in page memory. `GET /api/positions` requires `X-API-Key` and returns
valid coordinates from the 5,000 most recent readings. Map tiles use OpenStreetMap.

Tracker messages should contain `device_id`, numeric `latitude` (-90 to 90),
numeric `longitude` (-180 to 180), and optionally `speed` in km/h and `battery`
as a percentage. Send these as JSON via HTTPS POST to `/api/tracker` with
`Content-Type: application/json` and the `X-API-Key` header. HTTP 201 confirms
storage. Never put the real API key in public firmware source.

## Railway deployment from GitHub

1. In Railway, choose **New Project → Deploy from GitHub repo** and select
   `alxLenno/tracker`. Allow Railway access to this private repository if prompted.
2. Attach a volume to the service with mount path `/data` so SQLite readings
   survive deployments. Keep this service at one replica.
3. Set these service variables before deploying:

   - `TRACKER_API_KEY`: a long random secret, generated locally with
     `python3 -c 'import secrets; print(secrets.token_urlsafe(32))'`.
   - `TRACKER_DATABASE`: `/data/tracker.db`.

4. Deploy the service. `railway.json` sets the Gunicorn start command and the
   `/health` deployment health check. Gunicorn binds to Railway's `PORT`.
5. Under the service's **Settings → Networking**, generate a public domain.
   Verify `https://YOUR_DOMAIN/health`, then send readings to
   `https://YOUR_DOMAIN/api/tracker` using the curl example above with your key.

The volume is required for persistent SQLite storage; the default container
filesystem is temporary. Do not commit the API key to GitHub.

Official guides: https://railway.com/guides/flask and
https://docs.railway.com/volumes

## PythonAnywhere deployment (alternative)

1. Upload `app.py` and `requirements.txt` into `/home/YOURUSERNAME/tracker`.
2. In a Bash console, create a virtual environment and install dependencies:

   ```sh
   cd ~/tracker
   python3 -m venv .venv
   .venv/bin/pip install -r requirements.txt
   python3 -c 'import secrets; print(secrets.token_urlsafe(32))'
   ```

3. Under **Web**, add a web app with **Manual configuration**. Choose the same
   Python version as the virtual environment. Set its virtualenv path to
   `/home/YOURUSERNAME/tracker/.venv`.
4. Replace the Web tab's WSGI configuration file with the following. Substitute
   your username and the generated secret. Keep the secret private.

   ```python
   import os
   import sys

   sys.path.insert(0, '/home/YOURUSERNAME/tracker')
   os.environ['TRACKER_API_KEY'] = 'YOUR_GENERATED_SECRET'
   os.environ['TRACKER_DATABASE'] = '/home/YOURUSERNAME/tracker/tracker.db'

   from app import create_app
   application = create_app()
   ```

5. Reload the web app. Configure the tracker to POST JSON to
   `https://YOURUSERNAME.pythonanywhere.com/api/tracker`, with `Content-Type:
   application/json` and `X-API-Key: YOUR_GENERATED_SECRET` headers. Use the actual
   domain shown on your Web tab if it differs.
6. Test using the curl example, replacing the local URL and API key. Check
   `/health` and the Web tab's error log if deployment fails.

PythonAnywhere serves the application through WSGI; do not call `app.run()` in
the deployed module. Official setup guide:
https://help.pythonanywhere.com/pages/Flask/

Data is stored in `tracker.db`; back it up and monitor disk usage as readings
accumulate. Before connecting a physical tracker, verify its HTTP support,
payload format, custom-header support, and required acknowledgement response.

Heartbeats: POST `{"device_id":"tracker-001","gps_fix":false,"uptime_ms":15000}`
to `/api/tracker` with the usual headers. Device status is returned in the
`devices` array of `/api/positions`; online means last seen less than 90 seconds
ago. Heartbeats do not add route points.

## Automatic extra telemetry

Send any additional JSON fields (for example `satellites`, `hdop`, `temperature`,
or nested sensor objects) to `/api/tracker`. The dashboard automatically displays
them in Live telemetry and the marker card without adding code for each field.
The tracker firmware must still measure and send the fields.

Each device's `telemetry` in `/api/positions` contains its latest complete message.
Heartbeat-only messages are supported. A new message replaces the previous
snapshot, so omitted fields are not presented as current readings. GPS history
remains stored separately; additional fields in coordinate-bearing historical
messages remain in their raw database payload. Existing data is preserved.

Values are displayed as text, with expandable nested objects and arrays; the
dashboard does not infer units for unfamiliar fields. Include units in field names
(e.g. `temperature_c`) or send `{"value": 23, "unit": "C"}`. Authentication and
the 64 KiB request limit still apply. Do not include secrets in telemetry.
