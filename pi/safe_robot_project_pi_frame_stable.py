#!/usr/bin/env python3
import socket
import struct
import time
import cv2
import argparse

def connect_loop(ip, port):
    while True:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((ip, port))
            print(f"[PI_FRAME_STABLE] connected to AI-G {ip}:{port}", flush=True)
            return s
        except Exception as e:
            print(f"[PI_FRAME_STABLE] connect retry: {e}", flush=True)
            time.sleep(1)

def open_camera_loop(dev, cam_width, cam_height, cam_fps):
    while True:
        print(f"[PI_FRAME_STABLE] try open camera {dev}", flush=True)

        cap = cv2.VideoCapture(dev, cv2.CAP_V4L2)
        cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, cam_width)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, cam_height)
        cap.set(cv2.CAP_PROP_FPS, cam_fps)

        if not cap.isOpened():
            print(f"[PI_FRAME_STABLE] open failed {dev}, retry in 2s", flush=True)
            cap.release()
            time.sleep(2)
            continue

        for i in range(30):
            ok, frame = cap.read()
            if ok and frame is not None:
                print(f"[PI_FRAME_STABLE] camera ready {dev} shape={frame.shape}", flush=True)
                return cap
            time.sleep(0.1)

        print(f"[PI_FRAME_STABLE] read failed {dev}, reopen", flush=True)
        cap.release()
        time.sleep(2)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--aig-ip", default="192.168.60.2")
    parser.add_argument("--aig-port", type=int, default=7000)
    parser.add_argument("--dev", default="/dev/video1")
    parser.add_argument("--width", type=int, default=320)
    parser.add_argument("--height", type=int, default=240)
    parser.add_argument("--fps", type=float, default=10.0)
    parser.add_argument("--cam-width", type=int, default=640)
    parser.add_argument("--cam-height", type=int, default=480)
    parser.add_argument("--cam-fps", type=int, default=15)
    args = parser.parse_args()

    print("[PI_FRAME_STABLE] start", flush=True)

    cap = open_camera_loop(args.dev, args.cam_width, args.cam_height, args.cam_fps)
    sock = connect_loop(args.aig_ip, args.aig_port)

    seq = 1
    interval = 1.0 / args.fps

    while True:
        ok, frame = cap.read()

        if not ok or frame is None:
            print("[PI_FRAME_STABLE] frame read failed -> reopen camera", flush=True)
            cap.release()
            cap = open_camera_loop(args.dev, args.cam_width, args.cam_height, args.cam_fps)
            continue

        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        gray = cv2.resize(gray, (args.width, args.height))
        payload = gray.tobytes()
        header = struct.pack("!4sIHHI", b"FRAM", seq, args.width, args.height, len(payload))

        try:
            sock.sendall(header + payload)
            print(f"[PI_FRAME_STABLE] sent seq={seq} {args.width}x{args.height} dev={args.dev}", flush=True)
        except Exception as e:
            print(f"[PI_FRAME_STABLE] send failed: {e}", flush=True)
            try:
                sock.close()
            except Exception:
                pass
            sock = connect_loop(args.aig_ip, args.aig_port)

        seq += 1
        time.sleep(interval)

if __name__ == "__main__":
    main()
