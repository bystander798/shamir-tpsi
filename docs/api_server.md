# TPSI REST API Server (Demo)

This executable exposes set-intersection workflows for the TPSI front-end. It keeps
all data in memory and is meant for local development / demonstrations.

## Build

```bash
cmake -S . -B build-server -DSHAIR_TPSI_BUILD_SERVER=ON
cmake --build build-server -j
```

The server binary will be generated at `build-server/tpsi_server`.

## Run

```bash
TPSI_SERVER_HOST=0.0.0.0 TPSI_SERVER_PORT=8080 ./build-server/tpsi_server
```

Default host is `0.0.0.0`, default port `8080`. Keep the process running while the
front-end is active. Front-end requests to `/api/...` will be proxied to this server.

## REST Endpoints

### Health
- `GET /health` → `{ "status": "ok" }`

### Dataset API
- `GET /api/datasets` → list all datasets (sample receiver/sender datasets on boot)
- `GET /api/datasets/:id` → dataset detail (size, modulus, stats, sample preview)
- `POST /api/datasets` (JSON)
  ```json
  {
    "name": "Dataset A",
    "owner": "receiver1",
    "modulus": 104857601,
    "elements": [1, 2, 3, 4]
  }
  ```
  Returns dataset summary.

### Session API
- `GET /api/sessions` → list intersection jobs
- `GET /api/sessions/:id` → detail (status, metrics, intersection size, logs)
- `POST /api/sessions`
  ```json
  {
    "receiverDatasetId": "ds-receiver-demo",
    "senderDatasetId": "ds-sender-demo",
    "threshold": 500,
    "thresholdRatio": 0.95,
    "modulus": 104857601,
    "notes": "Demo run"
  }
  ```
  Runs an in-memory intersection and returns full detail.
- `POST /api/sessions/:id/rerun` → re-runs the same job with current datasets.
- `GET /api/sessions/:id/intersection.csv` → download intersection elements as CSV.

Sessions are executed with the full native TPSI pipeline: the server spins up in-process
receiver/sender channels, collects protocol metrics (coin-flip, offline, online, RSS
reconstruction), and exposes intersection results together with communication statistics.

### Auth API
- `POST /api/auth/login` → `{ username, password, role }` → issues bearer token + user profile.
- `POST /api/auth/register` → create receiver/sender account (returns token + profile).
- `GET /api/auth/me` → current user profile (requires `Authorization: Bearer <token>`).
- `PATCH /api/users/me` → update name / organization / contact fields.
- `POST /api/users/me/avatar` → multipart upload, returns data-URL avatar (demo storage).
- `PUT /api/users/me/notifications` → update approvals/jobs/alerts switches.
- `POST /api/auth/password` → change password `{ old_password, new_password }`.
- `POST /api/auth/2fa/enable` → enable 2FA, returns shared secret (string).
- `POST /api/auth/2fa/disable` → disable 2FA.

### Request API
- `GET /api/requests` → list pending/processed threshold PSI collaboration requests.
- `POST /api/requests`
  ```json
  {
    "applicant": "receiverA",
    "counterparty": "senderB",
    "receiverDatasetId": "ds-receiver-demo",
    "senderDatasetId": "ds-sender-demo",
    "thresholdRatio": 0.9,
    "threshold": 1200,
    "modulus": 104857601,
    "notes": "Quarterly reconciliation"
  }
  ```
  Creates a request with `pending` status.
- `POST /api/requests/:id/confirm` → sender acknowledges cooperation (`pending` → `confirmed`).
- `POST /api/requests/:id/reject` → sender rejects the run.
- `POST /api/requests/:id/approve` → admin approves and immediately launches a session. Returns both the updated request and the new session detail.

## Notes
- Data is stored in-memory and resets when the process exits.
- A sample receiver/sender dataset pair is created on startup for quick testing.
- Request and approval state transitions are handled in-memory; restart the server
  to reset the pipeline.
- Built-in demo accounts: `receiver/password123`, `sender/password123`, `admin/password123`.
