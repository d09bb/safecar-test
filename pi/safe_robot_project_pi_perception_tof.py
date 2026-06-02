#!/usr/bin/env python3
import socket
import time
import threading
import os
from collections import deque

os.environ.setdefault("BLINKA_FORCEBOARD", "RASPBERRY_PI_4B")

PC_IP = "192.168.0.8"
PC_PORT = 6002
LISTEN_PORT = 6002

# ToF valid window and thresholds.
# This sensor returns very small values such as 40~60mm when no reliable target exists.
MIN_VALID_MM = 90
DANGER_MM = 120
OBSTACLE_MM = 230
MAX_VALID_MM = 430
STALE_MS = 400

raw_dist_mm = -1
dist_mm_latest = -1
dist_valid = False
dist_state = "INIT"
dist_last_update_ms = 0
dist_buf = deque(maxlen=5)

def now_ms():
    return int(time.time() * 1000)



def parse_kv(line):
    parts = line.strip().split()
    if not parts:
        return "", {}
    msg_type = parts[0]
    d = {}
    for tok in parts[1:]:
        if "=" in tok:
            k, v = tok.split("=", 1)
            d[k] = v
    return msg_type, d

def build_kv(msg_type, d):
    return " ".join([msg_type] + [f"{k}={v}" for k, v in d.items()])

def update_tof(raw):
    global raw_dist_mm, dist_mm_latest, dist_valid, dist_state, dist_last_update_ms

    raw_dist_mm = int(raw) if raw is not None else -1
    dist_last_update_ms = now_ms()

    # Invalid low values: treat as no reliable target.
    if raw_dist_mm <= 0 or raw_dist_mm <= MIN_VALID_MM:
        dist_buf.clear()
        dist_mm_latest = -1
        dist_valid = False
        dist_state = "INVALID_LOW_CLEAR"
        return

    # Above sensor useful range: clear.
    if raw_dist_mm >= MAX_VALID_MM:
        dist_buf.clear()
        dist_mm_latest = raw_dist_mm
        dist_valid = False
        dist_state = "OUT_OF_RANGE_CLEAR"
        return

    # Valid measurement.
    dist_buf.append(raw_dist_mm)
    b = sorted(dist_buf)
    dist_mm_latest = b[len(b) // 2]
    dist_valid = True

    if dist_mm_latest < DANGER_MM:
        dist_state = "DANGER"
    elif dist_mm_latest < OBSTACLE_MM:
        dist_state = "OBSTACLE"
    else:
        dist_state = "CLEAR"

def tof_loop():
    try:
        import board
        import busio
        i2c = busio.I2C(board.SCL, board.SDA)
    except Exception as e:
        print("[TOF] I2C init failed:", e, flush=True)
        return

    sensor = None
    sensor_type = None

    try:
        import adafruit_vl53l1x
        sensor = adafruit_vl53l1x.VL53L1X(i2c)
        sensor.distance_mode = 1
        sensor.timing_budget = 50
        sensor.start_ranging()
        sensor_type = "VL53L1X"
        print("[TOF] VL53L1X detected", flush=True)
    except Exception:
        try:
            import adafruit_vl53l0x
            sensor = adafruit_vl53l0x.VL53L0X(i2c)
            sensor_type = "VL53L0X"
            print("[TOF] VL53L0X detected", flush=True)
        except Exception as e:
            print("[TOF] sensor init failed:", e, flush=True)
            return

    while True:
        try:
            if sensor_type == "VL53L1X":
                if sensor.data_ready:
                    cm = sensor.distance
                    sensor.clear_interrupt()
                    raw = int(cm * 10) if cm is not None else -1
                    update_tof(raw)
            else:
                raw = int(sensor.range)
                update_tof(raw)

        except Exception as e:
            print("[TOF] read error:", e, flush=True)
            update_tof(-1)

        time.sleep(0.05)

def main():
    threading.Thread(target=tof_loop, daemon=True).start()

    rx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    rx.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    rx.bind(("0.0.0.0", LISTEN_PORT))

    tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    print(f"[PI_TOF_RELAY] listen :{LISTEN_PORT} -> PC {PC_IP}:{PC_PORT}", flush=True)
    print(
        f"[PI_TOF_RELAY] min_valid>{MIN_VALID_MM}mm "
        f"danger<{DANGER_MM}mm obstacle<{OBSTACLE_MM}mm max_valid<{MAX_VALID_MM}mm",
        flush=True
    )

    while True:
        data, addr = rx.recvfrom(4096)
        line = data.decode(errors="ignore").strip()
        msg_type, d = parse_kv(line)

        if msg_type != "PERCEPTION":
            print(f"[PI_TOF_RELAY IGNORE] {addr} {line}", flush=True)
            continue

        age_ms = now_ms() - dist_last_update_ms if dist_last_update_ms > 0 else 999999

        # Preserve AI-G raw vision obstacle separately.
        d["aig_obstacle"] = d.get("obstacle", "0")
        d["aig_obs_left"] = d.get("obs_left", "0")
        d["aig_obs_center"] = d.get("obs_center", "0")
        d["aig_obs_right"] = d.get("obs_right", "0")

        # Default final decision from ToF only.
        d["raw_dist_mm"] = str(raw_dist_mm)
        d["dist_mm"] = str(dist_mm_latest)
        d["tof_age_ms"] = str(age_ms)

        if age_ms > STALE_MS:
            d["obstacle"] = "0"
            d["obs_left"] = "0"
            d["obs_center"] = "0"
            d["obs_right"] = "0"
            d["fault"] = "0"
            d["tof_state"] = "STALE_CLEAR"

        elif not dist_valid:
            d["obstacle"] = "0"
            d["obs_left"] = "0"
            d["obs_center"] = "0"
            d["obs_right"] = "0"
            d["fault"] = "0"
            d["tof_state"] = dist_state

        elif dist_mm_latest < DANGER_MM:
            d["obstacle"] = "1"
            d["obs_left"] = "0"
            d["obs_center"] = "1"
            d["obs_right"] = "0"
            d["worker"] = "0"
            d["fault"] = "1"
            d["tof_state"] = "DANGER"

        elif dist_mm_latest < OBSTACLE_MM:
            d["obstacle"] = "1"
            d["obs_left"] = "0"
            d["obs_center"] = "1"
            d["obs_right"] = "0"
            d["worker"] = "0"
            d["fault"] = "0"
            d["tof_state"] = "OBSTACLE"

        else:
            d["obstacle"] = "0"
            d["obs_left"] = "0"
            d["obs_center"] = "0"
            d["obs_right"] = "0"
            d["fault"] = "0"
            d["tof_state"] = "CLEAR"

        # Final Pi-side perception normalization.
        # Project rule:
        # - Valid ArUco IDs are only 0, 1, 2.
        # - ID 3, ID 4, and false detections are invalid.
        # - AI-G obstacle/worker fields are ignored.
        # - Final obstacle is decided only by Pi ToF distance.
        def _to_int(v, default=-1):
            try:
                return int(float(v))
            except Exception:
                return default

        # 1) Normalize ArUco result from AI-G.
        _aruco = _to_int(d.get("aruco", 0), 0)
        _id = _to_int(d.get("id", -1), -1)

        if _aruco != 1 or _id not in (0, 1, 2):
            d["aruco"] = 0
            d["id"] = -1
            d["cx"] = 320
            d["area"] = 0
        else:
            d["aruco"] = 1
            d["id"] = _id
            d["cx"] = _to_int(d.get("cx", 320), 320)
            d["area"] = _to_int(d.get("area", 0), 0)

        # 2) Remove AI-G-side obstacle/person/worker fields.
        for _k in (
            "worker", "fault",
            "aig_obstacle", "aig_obs_left", "aig_obs_center", "aig_obs_right",
            "obs_left", "obs_center", "obs_right"
        ):
            d.pop(_k, None)

        # 3) Decide obstacle only from Pi ToF.
        # Tune this value if needed. 300~400 mm is a practical demo range.
        TOF_OBSTACLE_MM = 180

        _dist = _to_int(d.get("dist_mm", -1), -1)
        _tof_state = str(d.get("tof_state", "UNKNOWN"))

        if _dist > 0 and _dist <= TOF_OBSTACLE_MM:
            d["obstacle"] = 1
            d["tof_state"] = "BLOCKED"
            d["dist_mm"] = _dist
        else:
            d["obstacle"] = 0
            d["dist_mm"] = _dist
            if (not _tof_state) or ("INVALID" in _tof_state) or _tof_state == "UNKNOWN":
                d["tof_state"] = "CLEAR"

        out = build_kv("PERCEPTION", d)
        tx.sendto(out.encode(), (PC_IP, PC_PORT))

        print(
            f"[PI_TOF_RELAY TX] raw={raw_dist_mm} dist={d['dist_mm']} "
            f"age={age_ms} state={d['tof_state']} "
            f"final_obstacle={d['obstacle']} aig_obstacle={d.get('aig_obstacle','?')} "
            f"id={d.get('id','?')} cx={d.get('cx','?')}",
            flush=True
        )

if __name__ == "__main__":
    main()
