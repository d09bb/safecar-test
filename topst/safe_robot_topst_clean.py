#!/usr/bin/env python3
import argparse
import socket
import time


def now_ms():
    return int(time.time() * 1000)


def safe_int(v, default=0):
    try:
        return int(v)
    except Exception:
        return default


def parse_kv(line):
    parts = line.strip().split()
    if not parts:
        return "", {}

    msg_type = parts[0]
    kv = {}

    for token in parts[1:]:
        if "=" not in token:
            continue
        k, v = token.split("=", 1)

        # Arduino/bridge compatibility
        if k == "joyx":
            k = "joy_x"
        elif k == "joyy":
            k = "joy_y"

        kv[k] = v

    return msg_type, kv


def bind_udp(port, blocking=False):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("0.0.0.0", port))
    s.setblocking(blocking)
    return s


def udp_send(sock, ip, port, text):
    sock.sendto(text.encode(), (ip, port))


def parse_targets(s):
    out = []
    for x in str(s).split(","):
        x = x.strip()
        if not x:
            continue
        try:
            v = int(x)
            if v in (0, 1, 2) and v not in out:
                out.append(v)
        except Exception:
            pass
    return out


class SafeTopstState:
    def __init__(self, targets):
        self.latest_p = {
            "seq": 0,
            "aruco": 0,
            "id": -1,
            "cx": 320,
            "area": 0,
            "obstacle": 0,
            "obs_left": 0,
            "obs_center": 0,
            "obs_right": 0,
            "worker": 0,
            "fault": 0,
            "dist_mm": 0,
        }

        self.latest_c = {
            "seq": 0,
            "start": 0,
            "estop": 0,
            "deadman": 1,
            "manual": 0,
            "joy_x": 512,
            "joy_y": 512,
            "target_mask": 0,
        }

        self.last_p_time = 0
        self.last_c_time = 0

        self.default_targets = list(targets)
        self.target_list = []
        self.visited = set()

        self.current_target = -1
        self.target_locked = False
        self.started = False
        self.finished = False

        self.reached_count = 0
        self.last_target_seen_ms = 0
        self.last_target_cx = 320
        self.last_target_area = 0
        self.last_reached = -1

        self.obstacle_seen_count = 0
        self.obstacle_clear_count = 0
        self.obstacle_latched = False

    def targets_from_mask(self, mask):
        mask = safe_int(mask, 0)
        out = []

        if mask & 1:
            out.append(0)
        if mask & 2:
            out.append(1)
        if mask & 4:
            out.append(2)

        return out

    def start_and_lock(self):
        # FINISH 이후에는 Arduino가 start=1을 계속 보내도 재시작하지 않음.
        if self.finished:
            self.started = False
            self.current_target = -1
            print("[SAFE_TOPST FINISH_LOCKED] ignore START after complete", flush=True)
            return

        # 이미 target이 lock된 상태면 재개만 한다.
        if self.target_locked and self.current_target in (0, 1, 2):
            self.started = True
            print(
                f"[SAFE_TOPST RESUME] target_list={self.target_list}, "
                f"current_target={self.current_target}, visited={self.visited}",
                flush=True,
            )
            return

        mask = safe_int(self.latest_c.get("target_mask", 0), 0)
        selected = self.targets_from_mask(mask)

        if not selected:
            self.started = False
            self.target_locked = False
            self.current_target = -1
            print(f"[SAFE_TOPST WAIT_TARGET] mask={mask}, no target selected", flush=True)
            return

        self.target_list = list(selected)
        self.default_targets = list(selected)
        self.visited = set()
        self.current_target = self.target_list[0]
        self.target_locked = True
        self.started = True
        self.finished = False
        self.reached_count = 0
        self.last_target_seen_ms = 0
        self.last_reached = -1

        self.obstacle_seen_count = 0
        self.obstacle_clear_count = 0
        self.obstacle_latched = False

        print(
            f"[SAFE_TOPST LOCK] mask={mask}, target_list={self.target_list}, "
            f"current_target={self.current_target}",
            flush=True,
        )

    def select_next_target(self):
        # target 전환 시 이전 obstacle latch 제거
        self.obstacle_seen_count = 0
        self.obstacle_clear_count = 0
        self.obstacle_latched = False
        self.last_target_seen_ms = 0
        self.reached_count = 0

        remaining = [t for t in self.target_list if t not in self.visited]

        if remaining:
            self.current_target = remaining[0]
            self.started = True
            print(
                f"[SAFE_TOPST NEXT] current_target={self.current_target}, "
                f"visited={self.visited}",
                flush=True,
            )
        else:
            self.current_target = -1
            self.started = False
            self.finished = True
            self.target_locked = True
            print("[SAFE_TOPST COMPLETE] all selected targets visited -> FINISH", flush=True)


def make_cmd(seq, ttl, mode, target, drive, speed, steer="CENTER", servo=40, buzzer="OFF", fault="NONE"):
    stop_modes = (
        "IDLE",
        "WAIT_START",
        "SAFETY_STOP",
        "OBSTACLE_HOLD",
        "ARRIVAL_HOLD",
        "TARGET_REACHED",
        "FINISH",
        "COMPLETION",
    )

    if mode in stop_modes:
        drive = "STOP"
        speed = 0
        steer = "CENTER"
        servo = 40

    return (
        f"CMD seq={seq} "
        f"ttl={ttl} "
        f"mode={mode} "
        f"target={target} "
        f"drive={drive} "
        f"speed={speed} "
        f"steer={steer} "
        f"servo={servo} "
        f"buzzer={buzzer} "
        f"fault={fault}"
    )


def decide_follow(cx, center, deadband, speed):
    if cx < center - deadband:
        return "TURN_LEFT", speed, "LEFT"
    if cx > center + deadband:
        return "TURN_RIGHT", speed, "RIGHT"
    return "FORWARD", speed, "CENTER"


def decide_manual(joy_x, joy_y, deadman, speed):
    if deadman != 1:
        return "STOP", 0, "CENTER"

    if joy_y < 300:
        return "FORWARD", speed, "CENTER"
    if joy_y > 700:
        return "BACKWARD", speed, "CENTER"
    if joy_x < 300:
        return "TURN_LEFT", speed, "LEFT"
    if joy_x > 700:
        return "TURN_RIGHT", speed, "RIGHT"

    return "STOP", 0, "CENTER"


def role_topst(args):
    state = SafeTopstState(parse_targets(args.targets))

    rx = bind_udp(args.listen_port, blocking=False)
    tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    print("[SAFE_TOPST] start", flush=True)
    print(f"[SAFE_TOPST] listen UDP :{args.listen_port}", flush=True)
    print(f"[SAFE_TOPST] CMD -> PC {args.pc_ip}:{args.pc_port}", flush=True)
    print(f"[SAFE_TOPST] initial target=-1 default_targets={state.default_targets}", flush=True)
    print(
        f"[SAFE_TOPST] reach_area={args.reach_area} reach_count={args.reach_count} "
        f"speed={args.speed} center={args.center} deadband={args.deadband}",
        flush=True,
    )

    seq = 1
    interval_ms = int(1000 / max(1.0, args.send_hz))
    last_send = 0

    while True:
        now = now_ms()

        # Receive all pending packets
        while True:
            try:
                data, addr = rx.recvfrom(4096)
            except BlockingIOError:
                break

            line = data.decode(errors="ignore").strip()
            msg_type, kv = parse_kv(line)

            if msg_type == "PERCEPTION":
                for k in state.latest_p:
                    if k in kv:
                        state.latest_p[k] = safe_int(kv[k], state.latest_p[k])
                state.last_p_time = now
                print(f"[SAFE_TOPST P_RX] {state.latest_p}", flush=True)

            elif msg_type == "CONTROLLER":
                for k in state.latest_c:
                    if k in kv:
                        state.latest_c[k] = safe_int(kv[k], state.latest_c[k])
                state.last_c_time = now
                print(f"[SAFE_TOPST C_RX] {state.latest_c}", flush=True)

                if (
                    safe_int(state.latest_c.get("start", 0), 0) == 1
                    and safe_int(state.latest_c.get("estop", 0), 0) == 0
                    and not state.finished
                ):
                    state.start_and_lock()

            else:
                if line:
                    print(f"[SAFE_TOPST IGNORE] {addr} {line}", flush=True)

        if now - last_send < interval_ms:
            time.sleep(0.01)
            continue

        last_send = now

        p = state.latest_p
        c = state.latest_c

        p_age = now - state.last_p_time if state.last_p_time else 999999
        c_age = now - state.last_c_time if state.last_c_time else 999999

        aruco = safe_int(p.get("aruco", 0), 0)
        marker_id = safe_int(p.get("id", -1), -1)
        cx = safe_int(p.get("cx", args.center), args.center)
        area = safe_int(p.get("area", 0), 0)
        obstacle = safe_int(p.get("obstacle", 0), 0)
        worker = safe_int(p.get("worker", 0), 0)
        fault = safe_int(p.get("fault", 0), 0)
        dist_mm = safe_int(p.get("dist_mm", 0), 0)

        start = safe_int(c.get("start", 0), 0)
        estop = safe_int(c.get("estop", 0), 0)
        deadman = safe_int(c.get("deadman", 1), 1)
        manual = safe_int(c.get("manual", 0), 0)
        joy_x = safe_int(c.get("joy_x", 512), 512)
        joy_y = safe_int(c.get("joy_y", 512), 512)

        mode = "IDLE"
        target = state.current_target
        drive = "STOP"
        speed = 0
        steer = "CENTER"
        servo = 40
        buzzer = "OFF"
        fault_text = "NONE"

        # start=0: all stop, servo center
        if start == 0:
            state.started = False
            mode = "WAIT_START"
            target = -1
            fault_text = "WAIT_START"

        # hard safety stop
        elif estop == 1 or fault == 1 or worker == 1 or c_age > 2000:
            mode = "SAFETY_STOP"
            drive = "STOP"
            speed = 0
            target = state.current_target
            fault_text = "SAFETY_STOP"

        # all selected targets done
        elif state.finished:
            mode = "FINISH"
            target = -1
            drive = "STOP"
            speed = 0
            steer = "CENTER"
            servo = 40
            buzzer = "OFF"
            fault_text = "MISSION_COMPLETE"

        # manual mode: ignore obstacle/ArUco, but E-STOP already handled above
        elif manual == 1:
            mode = "MANUAL"
            target = state.current_target
            drive, speed, steer = decide_manual(joy_x, joy_y, deadman, args.speed)
            servo = 40
            fault_text = "NONE"

        # no mission locked yet
        elif not state.target_locked or state.current_target not in (0, 1, 2):
            mode = "IDLE"
            target = -1
            drive = "STOP"
            speed = 0
            fault_text = "NO_TARGET"

        else:
            target = state.current_target

            # Safety-first obstacle policy around current target ArUco
            target_aruco_visible = (
                aruco == 1
                and marker_id == state.current_target
                and state.current_target in (0, 1, 2)
            )

            reached_marker_visible = (
                aruco == 1
                and marker_id in state.visited
                and marker_id != state.current_target
                and area >= 50000
            )

            if target_aruco_visible:
                if area >= args.reach_area:
                    obstacle = 0
                    state.obstacle_latched = False
                    state.obstacle_seen_count = 0
                    state.obstacle_clear_count = args.obstacle_clear_count
                    print(
                        f"[TARGET_ARUCO_ARRIVAL_ZONE] target={state.current_target} "
                        f"area={area} dist_mm={dist_mm}",
                        flush=True,
                    )
                elif dist_mm > 0 and dist_mm <= 140:
                    obstacle = 1
                    print(
                        f"[TARGET_ARUCO_OBSTACLE_STOP] target={state.current_target} "
                        f"area={area} dist_mm={dist_mm}",
                        flush=True,
                    )
                else:
                    obstacle = 0
                    state.obstacle_latched = False
                    state.obstacle_seen_count = 0
                    state.obstacle_clear_count = args.obstacle_clear_count

            elif reached_marker_visible:
                # Already visited marker board should not block search for next target.
                obstacle = 0
                state.obstacle_latched = False
                state.obstacle_seen_count = 0
                state.obstacle_clear_count = args.obstacle_clear_count
                print(
                    f"[REACHED_MARKER_OBS_IGNORE] id={marker_id} next_target={state.current_target} "
                    f"area={area} dist_mm={dist_mm}",
                    flush=True,
                )

            if obstacle == 1 and not args.ignore_obstacle:
                state.obstacle_seen_count += 1
                state.obstacle_clear_count = 0
            else:
                state.obstacle_clear_count += 1
                if state.obstacle_clear_count >= args.obstacle_clear_count:
                    state.obstacle_seen_count = 0
                    state.obstacle_latched = False

            if state.obstacle_seen_count >= args.obstacle_confirm_count:
                state.obstacle_latched = True

            if state.obstacle_latched and not args.ignore_obstacle:
                mode = "OBSTACLE_HOLD"
                drive = "STOP"
                speed = 0
                steer = "CENTER"
                servo = 40
                fault_text = "OBSTACLE"

            elif target_aruco_visible:
                state.last_target_seen_ms = now
                state.last_target_cx = cx
                state.last_target_area = area

                if area >= args.reach_area:
                    state.reached_count += 1
                else:
                    state.reached_count = 0

                if state.reached_count >= args.reach_count:
                    reached = state.current_target
                    state.visited.add(reached)
                    state.last_reached = reached
                    state.select_next_target()

                    if state.finished:
                        mode = "FINISH"
                        target = -1
                        drive = "STOP"
                        speed = 0
                        steer = "CENTER"
                        servo = 40
                        buzzer = "OFF"
                        fault_text = "MISSION_COMPLETE"
                    else:
                        mode = "TARGET_REACHED"
                        target = state.current_target
                        drive = "STOP"
                        speed = 0
                        steer = "CENTER"
                        servo = 40
                        buzzer = "GOAL"
                        fault_text = "TARGET_SWITCH"
                else:
                    mode = "GO_TO_TARGET"
                    drive, speed, steer = decide_follow(cx, args.center, args.deadband, args.speed)
                    fault_text = "NONE"

            elif (
                state.last_target_seen_ms > 0
                and now - state.last_target_seen_ms <= args.target_lost_hold_ms
            ):
                mode = "GO_TO_TARGET_HOLD"
                drive, speed, steer = decide_follow(
                    state.last_target_cx,
                    args.center,
                    args.deadband,
                    args.speed,
                )
                fault_text = "TARGET_HOLD"

            else:
                mode = "SEARCH_TARGET"
                drive = "STOP"
                speed = 0
                steer = "CENTER"
                servo = 40
                fault_text = "SEARCH"

        cmd = make_cmd(
            seq=seq,
            ttl=args.ttl_ms,
            mode=mode,
            target=target,
            drive=drive,
            speed=speed,
            steer=steer,
            servo=servo,
            buzzer=buzzer,
            fault=fault_text,
        )

        udp_send(tx, args.pc_ip, args.pc_port, cmd)
        print(
            f"[SAFE_TOPST CMD_TX] {cmd} | p_age={p_age} c_age={c_age} "
            f"aruco={aruco} id={marker_id} area={area} dist_mm={dist_mm} obstacle={obstacle}",
            flush=True,
        )

        seq += 1


def main():
    parser = argparse.ArgumentParser()

    parser.add_argument("--role", required=True, choices=["topst"])
    parser.add_argument("--listen-port", type=int, default=5005)
    parser.add_argument("--pc-ip", default="192.168.50.10")
    parser.add_argument("--pc-port", type=int, default=5006)

    parser.add_argument("--targets", default="")
    parser.add_argument("--send-hz", type=float, default=10.0)
    parser.add_argument("--ttl-ms", type=int, default=2000)
    parser.add_argument("--center", type=int, default=320)
    parser.add_argument("--deadband", type=int, default=10)
    parser.add_argument("--speed", type=int, default=40)

    parser.add_argument("--reach-area", type=int, default=100000)
    parser.add_argument("--reach-count", type=int, default=3)

    parser.add_argument("--perception-timeout-ms", type=int, default=2000)
    parser.add_argument("--target-lost-hold-ms", type=int, default=1200)
    parser.add_argument("--obstacle-confirm-count", type=int, default=2)
    parser.add_argument("--obstacle-clear-count", type=int, default=2)
    parser.add_argument("--ignore-obstacle", action="store_true")

    args = parser.parse_args()

    role_topst(args)


if __name__ == "__main__":
    main()
