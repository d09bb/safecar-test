# Safe Logistics Robot with TOPST Supervisor and AI-G ArUco Perception

AI-G 비전 인지와 TOPST Supervisor 기반의 마트 창고 물류 이동 로봇 안전 주행 시스템입니다.
본 프로젝트는 사용자가 선택한 ArUco 목적지를 순차적으로 방문하고, ToF 센서 기반 장애물 감지 시 즉시 정지하는 안전 주행 구조를 목표로 합니다.

## 1. Project Overview

본 시스템은 마트 창고 또는 실내 물류 환경에서 이동 로봇이 지정된 물류 지점을 순차적으로 방문하도록 구성한 안전 주행 프로젝트입니다.

핵심 목표는 다음과 같습니다.

1. 사용자가 버튼 3개 중 방문할 target을 선택한다.
2. TOPST가 선택된 target을 기반으로 `target_list`를 생성한다.
3. mapping table을 이용해 다음 target 탐색 방향인 `search_dir`를 결정한다.
4. Raspberry Pi가 카메라 서보를 제어하여 ArUco marker 0~2를 탐색한다.
5. AI-G가 ArUco marker 0~2만 인식한다.
6. TOPST가 현재 target이 인식되면 `GO_TO_TARGET` 명령을 발급한다.
7. Raspberry Pi가 TOPST 명령에 따라 모터와 서보를 제어한다.
8. ToF 장애물 감지 시 TOPST가 즉시 `OBSTACLE_HOLD` 및 `STOP` 명령을 발급한다.
9. 장애물이 제거되면 기존 target 추종 상태로 복귀한다.
10. target 도착 시 다음 target으로 이동하거나 모든 target 방문 후 `COMPLETION` 상태로 종료한다.

본 프로젝트에서는 장애물 회피 주행을 수행하지 않습니다.
장애물 감지 시 무리하게 우회하지 않고, 안전성을 우선하여 즉시 정지하는 방식을 채택했습니다.

## 2. System Architecture

```text
[User / Arduino Controller]
        |
        | target selection, start, E-STOP, deadman
        v
[PC Gateway] <----------------------> [TOPST Supervisor]
        |                                  |
        | PERCEPTION relay                 | CMD generation
        |                                  |
        v                                  v
[Raspberry Pi Vehicle Controller] <--- CMD from PC/TOPST
        |
        | motor control, servo control, ToF perception
        |
        +--------------------+
                             |
                             v
                         [AI-G]
                  ArUco 0~2 perception
```

## 3. Device Roles

### AI-G

AI-G는 ArUco marker 인식을 담당합니다.

* ArUco marker ID 0, 1, 2만 유효하게 사용
* ID 3, 4 및 오검출은 무효 처리
* worker/person/obstacle 판단은 사용하지 않음
* Pi에서 전달받은 카메라 프레임을 기반으로 ArUco 인식 결과를 UDP로 전송

### Raspberry Pi

Raspberry Pi는 차량 실행부와 센서 통합을 담당합니다.

* AI-G로 카메라 프레임 전송
* AI-G ArUco 결과 수신
* ToF 센서 기반 장애물 판단
* ArUco 결과와 ToF 결과를 합쳐 최종 `PERCEPTION` 생성
* TOPST CMD를 받아 모터, 카메라 서보, 정지 명령 실행
* 장애물 감지 시 `STOP` 명령 수행
* 장애물 제거 후 기존 target 추종으로 복귀

### TOPST Supervisor

TOPST는 주행 판단과 안전 명령 생성을 담당합니다.

* target selection 기반 `target_list` 생성
* mapping 기반 `search_dir` 결정
* `SEARCH_TARGET`, `GO_TO_TARGET`, `OBSTACLE_HOLD`, `COMPLETION` 상태 관리
* ToF obstacle=1 수신 시 즉시 `drive=STOP`, `speed=0` 발급
* 장애물 제거 후 기존 target 추종으로 복귀

### PC Gateway

PC는 장치 간 UDP 메시지 중계 및 개발 디버깅을 담당합니다.

* Pi의 `PERCEPTION` 메시지를 TOPST로 전달
* TOPST의 `CMD` 메시지를 Pi로 전달
* obstacle 값을 임의로 0으로 덮어쓰지 않도록 수정됨
* 로그 확인 및 GitHub 백업 기준 장치 역할 수행

### Arduino Controller

Arduino는 사용자 입력 장치 역할을 목표로 합니다.

* target 선택 버튼 3개
* Start 입력
* E-STOP
* Deadman switch
* 현재 미완성: Arduino와 PC/TOPST 통신 연동

## 4. Network Configuration

### Pi ↔ AI-G

```text
Raspberry Pi eth0 : 192.168.60.1/24
AI-G eth0         : 192.168.60.2/24
```

### PC ↔ TOPST

```text
PC USB-LAN eth    : 192.168.50.10/24
TOPST eth0        : 192.168.50.20/24
```

## 5. Repository Structure

```text
safe-logistics-robot-topst-aig/
├── README.md
├── pc/
│   ├── safe_robot_project.py
│   ├── safe_robot_project_pc.sh
│   └── safe_robot_project_net_pc.sh
├── pi/
│   ├── safe_robot_project_pi_all.sh
│   ├── safe_robot_project_pi_perception_tof.py
│   ├── safe_robot_project_vehicle_stable.py
│   └── safe_robot_project_pi_frame_stable.py
├── topst/
│   ├── safe_robot_project_topst.py
│   └── safe_robot_project_topst.sh
└── aig/
    ├── build_aig.sh
    ├── src/
    │   ├── aig_net_perception.c
    │   └── aruco_patterns_4x4.h
    └── runtime/
        ├── safe_robot_project_aig
        └── safe_robot_project_run.sh
```

## 6. Main Message Flow

### PERCEPTION

Raspberry Pi에서 TOPST로 전달되는 인지 메시지입니다.

```text
PERCEPTION seq=... aruco=1 id=2 cx=320 area=30000 obstacle=0 tof_state=CLEAR dist_mm=300
```

주요 필드:

* `aruco`: ArUco 인식 여부
* `id`: 인식된 ArUco marker ID
* `cx`: marker 중심 x좌표
* `area`: marker 면적
* `obstacle`: ToF 기반 장애물 여부
* `tof_state`: `CLEAR` 또는 `BLOCKED`
* `dist_mm`: ToF 거리값

### CMD

TOPST가 차량에 전달하는 주행 명령입니다.

```text
CMD seq=... ttl=2000 mode=GO_TO_TARGET target=2 drive=FORWARD speed=35 steer=CENTER servo=40 buzzer=OFF fault=NONE
```

장애물 감지 시:

```text
CMD seq=... ttl=2000 mode=OBSTACLE_HOLD target=2 drive=STOP speed=0 steer=CENTER servo=40 buzzer=WARN fault=OBSTACLE
```

## 7. Current Implementation Status

### Completed

* AI-G ArUco 0~2 인식 구조 정리
* ID 3, ID 4 무효 처리
* Pi에서 AI-G ArUco 결과와 ToF 결과 합성
* PC Gateway에서 obstacle 값 보존
* TOPST에서 obstacle=1 수신 시 `OBSTACLE_HOLD` / `STOP` 발급
* Raspberry Pi에서 CMD 기반 모터 제어
* Raspberry Pi에서 카메라 서보 탐색 방향 제어
* 장애물 감지 시 정지
* 장애물 제거 후 기존 target 추종 복귀
* GitHub repository 구조 정리
* `pc/`, `pi/`, `topst/`, `aig/` 폴더 분리
* `auto_push.sh`를 이용한 선택 파일 자동 백업 구조 작성

### Not Completed Yet

* mapping table 기반 `search_dir` 최종 구현 및 검증
* target 도착 시 차량이 잠시 정지한 뒤 다음 target으로 이동하는 동작
* Arduino target 선택 / E-STOP / Deadman 입력과 PC/TOPST 통신 연동
* 단일 target 0, 1, 2 전체 반복 검증
* 복수 target `[0,1]`, `[0,2]`, `[1,2]`, `[0,1,2]` 순차 방문 검증
* target 도착 판정용 `area` threshold 튜닝
* E-STOP / Deadman 최종 시연 검증
* 최종 시연 영상 및 로그 확보

## 8. Final Demonstration Goal

최종 시연 목표는 다음 순서입니다.

```text
1. 사용자가 버튼 3개 중 target 선택
2. TOPST가 target_list 생성
3. mapping으로 search_dir 결정
4. Pi 서보가 ArUco marker ID 0~2 순서 기준으로 target 탐색
5. AI-G가 ArUco 0~2 인식
6. TOPST가 GO_TO_TARGET 발급
7. 차량이 target으로 이동
8. ToF 장애물 감지 시 즉시 STOP
9. 장애물 제거 시 기존 target 추종 복귀
10. target 도착 시 다음 target 또는 COMPLETION
```

## 9. Run Commands

### AI-G

```bash
cd /home/root
./safe_robot_project_run.sh
```

### Raspberry Pi

```bash
cd ~/safe_robot_project
./safe_robot_project_pi_all.sh
```

### TOPST

```bash
cd ~/safe_robot_project
./safe_robot_project_topst.sh
```

### PC Gateway

```bash
cd ~/safe_robot_project
./safe_robot_project_pc.sh
```

## 10. Backup and Upload

PC에서 GitHub 백업:

```bash
cd ~/safe_robot_project
./auto_push.sh "update: safe robot runtime files"
```

`auto_push.sh`는 GitHub에 업로드하지 않고 로컬에서만 사용합니다.

## 11. Design Policy

본 프로젝트의 장애물 대응 정책은 회피가 아니라 안전 정지입니다.

```text
obstacle=1 → OBSTACLE_HOLD → drive=STOP, speed=0
obstacle=0 → 기존 target 추종 상태로 복귀
```

이 방식은 창고 물류 로봇이 사람 또는 장애물과 충돌하지 않도록 안전성을 우선하는 구조입니다.
