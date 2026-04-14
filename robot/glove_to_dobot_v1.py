from __future__ import annotations

import argparse
import contextlib
import importlib.util
import logging
import os
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import serial
import yaml


COMMAND_NAMES = {
    0x05: "rock / special",
    0x06: "left / Y+",
    0x07: "right / Y-",
    0x08: "up / Z+",
    0x0A: "down / Z-",
    0x19: "three / safe-home",
    0x69: "one / X+",
}

IGNORED_COMMANDS = {
    0x00: "idle",
    0x02: "common-ignore",
}


class ConfigError(RuntimeError):
    pass


class DobotError(RuntimeError):
    pass


@dataclass(frozen=True)
class AxisLimit:
    minimum: float
    maximum: float

    def clamp(self, value: float) -> float:
        return max(self.minimum, min(self.maximum, value))


@dataclass
class CartesianPose:
    x: float
    y: float
    z: float
    r: float

    def summary(self) -> str:
        return f"x={self.x:.1f}, y={self.y:.1f}, z={self.z:.1f}, r={self.r:.1f}"


@dataclass(frozen=True)
class AppConfig:
    serial_port: str
    serial_baudrate: int
    serial_timeout_s: float
    serial_flush_on_start: bool
    dobot_port: str
    dobot_baudrate: int
    sdk_dir: Path
    log_dir: Path
    startup_confirmation: bool
    startup_message: str
    auto_move_to_safe_pose_on_start: bool
    queue_poll_interval_s: float
    queue_wait_timeout_s: float
    motion_mode: str
    joint_velocity: tuple[float, float, float, float]
    joint_acceleration: tuple[float, float, float, float]
    coordinate_xyz_velocity: float
    coordinate_xyz_acceleration: float
    coordinate_r_velocity: float
    coordinate_r_acceleration: float
    common_velocity_ratio: float
    common_acceleration_ratio: float
    step_x_mm: float
    step_y_mm: float
    step_z_mm: float
    min_command_interval_s: float
    duplicate_debounce_s: float
    flush_serial_after_motion: bool
    special_action_mode: str
    safe_pose: CartesianPose
    x_limit: AxisLimit
    y_limit: AxisLimit
    z_limit: AxisLimit


def build_arg_parser() -> argparse.ArgumentParser:
    default_config = Path(__file__).resolve().with_name("config.yaml")
    parser = argparse.ArgumentParser(
        description="最小闭环版：手套原始命令字节 -> PC 串口 -> Dobot Magician"
    )
    parser.add_argument(
        "--config",
        default=str(default_config),
        help="配置文件路径，默认使用脚本同目录下的 config.yaml",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="不连接真实 Dobot，只打印目标动作，便于本地自检。",
    )
    parser.add_argument(
        "--single-command",
        help="直接执行一次单条命令后退出，例如 0x69、0x06。",
    )
    parser.add_argument(
        "--no-confirm",
        action="store_true",
        help="跳过启动时的人工安全确认提示。",
    )
    return parser


def setup_logging(log_dir: Path) -> logging.Logger:
    log_dir.mkdir(parents=True, exist_ok=True)
    logger = logging.getLogger("glove_to_dobot_v1")
    logger.setLevel(logging.INFO)
    logger.handlers.clear()

    formatter = logging.Formatter(
        fmt="%(asctime)s | %(levelname)s | %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S",
    )

    console_handler = logging.StreamHandler(sys.stdout)
    console_handler.setFormatter(formatter)
    logger.addHandler(console_handler)

    file_handler = logging.FileHandler(log_dir / "glove_to_dobot_v1.log", encoding="utf-8")
    file_handler.setFormatter(formatter)
    logger.addHandler(file_handler)

    return logger


def _require_section(data: dict[str, Any], name: str) -> dict[str, Any]:
    section = data.get(name)
    if not isinstance(section, dict):
        raise ConfigError(f"配置缺少 `{name}` 段。")
    return section


def _require_float(data: dict[str, Any], key: str) -> float:
    if key not in data:
        raise ConfigError(f"配置缺少 `{key}`。")
    try:
        return float(data[key])
    except (TypeError, ValueError) as exc:
        raise ConfigError(f"`{key}` 必须是数字。") from exc


def _require_int(data: dict[str, Any], key: str) -> int:
    if key not in data:
        raise ConfigError(f"配置缺少 `{key}`。")
    try:
        return int(data[key])
    except (TypeError, ValueError) as exc:
        raise ConfigError(f"`{key}` 必须是整数。") from exc


def _require_bool(data: dict[str, Any], key: str) -> bool:
    if key not in data:
        raise ConfigError(f"配置缺少 `{key}`。")
    value = data[key]
    if isinstance(value, bool):
        return value
    raise ConfigError(f"`{key}` 必须是布尔值。")


def _require_str(data: dict[str, Any], key: str) -> str:
    if key not in data:
        raise ConfigError(f"配置缺少 `{key}`。")
    value = data[key]
    if not isinstance(value, str):
        raise ConfigError(f"`{key}` 必须是字符串。")
    return value


def _load_axis_limit(data: dict[str, Any], name: str) -> AxisLimit:
    section = _require_section(data, name)
    minimum = _require_float(section, "min")
    maximum = _require_float(section, "max")
    if minimum > maximum:
        raise ConfigError(f"`{name}.min` 不能大于 `{name}.max`。")
    return AxisLimit(minimum=minimum, maximum=maximum)


def _load_pose(data: dict[str, Any], name: str) -> CartesianPose:
    section = _require_section(data, name)
    return CartesianPose(
        x=_require_float(section, "x"),
        y=_require_float(section, "y"),
        z=_require_float(section, "z"),
        r=_require_float(section, "r"),
    )


def _load_float_tuple(data: dict[str, Any], key: str, expected_len: int) -> tuple[float, ...]:
    value = data.get(key)
    if not isinstance(value, list) or len(value) != expected_len:
        raise ConfigError(f"`{key}` 必须是长度为 {expected_len} 的列表。")
    try:
        return tuple(float(item) for item in value)
    except (TypeError, ValueError) as exc:
        raise ConfigError(f"`{key}` 的所有值都必须是数字。") from exc


def load_config(config_path: Path) -> AppConfig:
    try:
        raw = yaml.safe_load(config_path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise ConfigError(f"找不到配置文件：{config_path}") from exc
    except yaml.YAMLError as exc:
        raise ConfigError(f"配置文件 YAML 解析失败：{exc}") from exc

    if not isinstance(raw, dict):
        raise ConfigError("配置文件顶层必须是字典。")

    serial_cfg = _require_section(raw, "serial")
    dobot_cfg = _require_section(raw, "dobot")
    motion_cfg = _require_section(dobot_cfg, "motion")
    control_cfg = _require_section(raw, "control")
    safety_cfg = _require_section(raw, "safety")
    soft_limits_cfg = _require_section(safety_cfg, "soft_limits")

    config_dir = config_path.resolve().parent
    sdk_dir = (config_dir / _require_str(dobot_cfg, "sdk_dir")).resolve()
    log_dir = (config_dir / _require_str(raw, "log_dir")).resolve()

    safe_pose = _load_pose(safety_cfg, "safe_pose")
    x_limit = _load_axis_limit(soft_limits_cfg, "x")
    y_limit = _load_axis_limit(soft_limits_cfg, "y")
    z_limit = _load_axis_limit(soft_limits_cfg, "z")

    if not (x_limit.minimum <= safe_pose.x <= x_limit.maximum):
        raise ConfigError("`safe_pose.x` 超出 X 软限位。")
    if not (y_limit.minimum <= safe_pose.y <= y_limit.maximum):
        raise ConfigError("`safe_pose.y` 超出 Y 软限位。")
    if not (z_limit.minimum <= safe_pose.z <= z_limit.maximum):
        raise ConfigError("`safe_pose.z` 超出 Z 软限位。")

    return AppConfig(
        serial_port=_require_str(serial_cfg, "port"),
        serial_baudrate=_require_int(serial_cfg, "baudrate"),
        serial_timeout_s=_require_float(serial_cfg, "timeout_s"),
        serial_flush_on_start=_require_bool(serial_cfg, "flush_input_on_start"),
        dobot_port=_require_str(dobot_cfg, "port"),
        dobot_baudrate=_require_int(dobot_cfg, "baudrate"),
        sdk_dir=sdk_dir,
        log_dir=log_dir,
        startup_confirmation=_require_bool(dobot_cfg, "startup_confirmation"),
        startup_message=_require_str(dobot_cfg, "startup_message"),
        auto_move_to_safe_pose_on_start=_require_bool(dobot_cfg, "auto_move_to_safe_pose_on_start"),
        queue_poll_interval_s=_require_float(dobot_cfg, "queue_poll_interval_s"),
        queue_wait_timeout_s=_require_float(dobot_cfg, "queue_wait_timeout_s"),
        motion_mode=_require_str(motion_cfg, "ptp_mode"),
        joint_velocity=_load_float_tuple(motion_cfg, "joint_velocity", 4),
        joint_acceleration=_load_float_tuple(motion_cfg, "joint_acceleration", 4),
        coordinate_xyz_velocity=_require_float(motion_cfg, "coordinate_xyz_velocity"),
        coordinate_xyz_acceleration=_require_float(motion_cfg, "coordinate_xyz_acceleration"),
        coordinate_r_velocity=_require_float(motion_cfg, "coordinate_r_velocity"),
        coordinate_r_acceleration=_require_float(motion_cfg, "coordinate_r_acceleration"),
        common_velocity_ratio=_require_float(motion_cfg, "common_velocity_ratio"),
        common_acceleration_ratio=_require_float(motion_cfg, "common_acceleration_ratio"),
        step_x_mm=_require_float(control_cfg, "step_x_mm"),
        step_y_mm=_require_float(control_cfg, "step_y_mm"),
        step_z_mm=_require_float(control_cfg, "step_z_mm"),
        min_command_interval_s=_require_float(control_cfg, "min_command_interval_s"),
        duplicate_debounce_s=_require_float(control_cfg, "duplicate_debounce_s"),
        flush_serial_after_motion=_require_bool(control_cfg, "flush_serial_after_motion"),
        special_action_mode=_require_str(control_cfg, "special_action_mode"),
        safe_pose=safe_pose,
        x_limit=x_limit,
        y_limit=y_limit,
        z_limit=z_limit,
    )


@contextlib.contextmanager
def pushd(path: Path):
    previous = Path.cwd()
    os.chdir(path)
    try:
        yield
    finally:
        os.chdir(previous)


class DobotMagicianController:
    def __init__(self, config: AppConfig, logger: logging.Logger, dry_run: bool) -> None:
        self.config = config
        self.logger = logger
        self.dry_run = dry_run
        self.api: Any | None = None
        self.d_type: Any | None = None
        self.connected = False
        self.last_pose = CartesianPose(
            x=config.safe_pose.x,
            y=config.safe_pose.y,
            z=config.safe_pose.z,
            r=config.safe_pose.r,
        )
        self.gripper_closed = False
        self._dll_handles: list[Any] = []

    def connect(self) -> None:
        if self.dry_run:
            self.logger.info("当前为 dry-run：跳过真实 Dobot 连接。")
            return

        if not self.config.sdk_dir.exists():
            raise DobotError(f"Dobot SDK 目录不存在：{self.config.sdk_dir}")

        # 官方 demo 的 Python 封装与 DLL 已经在本地资料中给出，
        # 这里优先复用它，而不是自己重写底层通信协议。
        self.d_type = self._import_sdk_module()
        self._add_dll_search_path(self.config.sdk_dir)

        # 官方 load() 默认按相对路径加载 DobotDll.dll，
        # 因此临时切到 SDK 目录下执行可以避免用户必须手工切工作目录。
        with pushd(self.config.sdk_dir):
            self.api = self.d_type.load()

        devices = self.d_type.SearchDobot(self.api)
        if devices:
            self.logger.info("SearchDobot 发现设备：%s", devices)
        else:
            self.logger.warning("SearchDobot 未发现可用设备，仍尝试按配置连接。")

        connect_port = self.config.dobot_port
        state = self.d_type.ConnectDobot(self.api, connect_port, self.config.dobot_baudrate)[0]
        status_name = self._connect_status_name(state)
        self.logger.info("Dobot 连接结果：%s", status_name)
        if state != self.d_type.DobotConnect.DobotConnect_NoError:
            raise DobotError(
                "Dobot 连接失败。请检查 USB 连接、驱动、占用情况以及 `config.yaml` 中的 Dobot 端口。"
            )

        self.connected = True

    def initialize(self) -> None:
        if self.dry_run:
            self.logger.info("dry-run 初始化：默认当前 Pose 取安全位。")
            self.log_pose("初始化 Pose", self.last_pose)
            return

        if not self.connected or self.d_type is None or self.api is None:
            raise DobotError("Dobot 尚未连接，无法初始化。")

        self.logger.info("开始初始化 Dobot：清报警、清队列、设置运动参数。")
        self.d_type.ClearAllAlarmsState(self.api)
        self.d_type.SetQueuedCmdForceStopExec(self.api)
        self.d_type.SetQueuedCmdClear(self.api)

        # 将 SDK 内的 HOME 参数直接对齐到我们配置的安全位，
        # 这样后续如果你希望切换成真正的 HOME 流程，也不用再改一套坐标。
        self.d_type.SetHOMEParams(
            self.api,
            self.config.safe_pose.x,
            self.config.safe_pose.y,
            self.config.safe_pose.z,
            self.config.safe_pose.r,
            isQueued=0,
        )
        self.d_type.SetPTPJointParams(
            self.api,
            self.config.joint_velocity[0],
            self.config.joint_acceleration[0],
            self.config.joint_velocity[1],
            self.config.joint_acceleration[1],
            self.config.joint_velocity[2],
            self.config.joint_acceleration[2],
            self.config.joint_velocity[3],
            self.config.joint_acceleration[3],
            isQueued=0,
        )
        self.d_type.SetPTPCoordinateParams(
            self.api,
            self.config.coordinate_xyz_velocity,
            self.config.coordinate_xyz_acceleration,
            self.config.coordinate_r_velocity,
            self.config.coordinate_r_acceleration,
            isQueued=0,
        )
        self.d_type.SetPTPCommonParams(
            self.api,
            self.config.common_velocity_ratio,
            self.config.common_acceleration_ratio,
            isQueued=0,
        )
        self.d_type.SetQueuedCmdStartExec(self.api)

        self.last_pose = self.get_pose()
        self.log_pose("初始化完成，当前 Pose", self.last_pose)

    def move_to_safe_pose(self, reason: str) -> CartesianPose:
        self.logger.info("执行安全位动作：%s", reason)
        return self.move_to_pose(self.config.safe_pose, reason=reason)

    def execute_glove_command(self, command: int) -> CartesianPose | None:
        if command == 0x06:
            return self.move_increment(0.0, self.config.step_y_mm, 0.0, "0x06 -> Y 正方向一步")
        if command == 0x07:
            return self.move_increment(0.0, -self.config.step_y_mm, 0.0, "0x07 -> Y 负方向一步")
        if command == 0x08:
            return self.move_increment(0.0, 0.0, self.config.step_z_mm, "0x08 -> Z 正方向一步")
        if command == 0x0A:
            return self.move_increment(0.0, 0.0, -self.config.step_z_mm, "0x0A -> Z 负方向一步")
        if command == 0x69:
            return self.move_increment(self.config.step_x_mm, 0.0, 0.0, "0x69 -> X 正方向一步")
        if command == 0x19:
            return self.move_to_safe_pose("0x19 -> 返回安全位")
        if command == 0x05:
            self.handle_special_action()
            return None
        raise DobotError(f"未定义的手套命令：0x{command:02X}")

    def move_increment(self, dx: float, dy: float, dz: float, reason: str) -> CartesianPose:
        current = self.get_pose()
        target = CartesianPose(
            x=current.x + dx,
            y=current.y + dy,
            z=current.z + dz,
            r=current.r,
        )
        clamped = self._apply_soft_limits(target)
        if self._poses_close(current, clamped):
            self.logger.warning("%s 被软限位拦截，当前 Pose 已在边界附近：%s", reason, current.summary())
            return current
        return self.move_to_pose(clamped, reason=reason)

    def move_to_pose(self, target: CartesianPose, reason: str) -> CartesianPose:
        target = self._apply_soft_limits(target)
        self.logger.info("准备执行动作：%s | 目标 Pose: %s", reason, target.summary())

        if self.dry_run:
            time.sleep(0.05)
            self.last_pose = CartesianPose(target.x, target.y, target.z, target.r)
            self.log_pose("dry-run 动作完成", self.last_pose)
            return self.last_pose

        if not self.connected or self.d_type is None or self.api is None:
            raise DobotError("Dobot 尚未连接，无法运动。")

        ptp_mode = self._resolve_ptp_mode()
        queued_index = self.d_type.SetPTPCmd(
            self.api,
            ptp_mode,
            target.x,
            target.y,
            target.z,
            target.r,
            isQueued=1,
        )[0]
        self._wait_for_queue_index(queued_index)
        self.last_pose = self.get_pose()
        self.log_pose("动作完成，当前 Pose", self.last_pose)
        return self.last_pose

    def handle_special_action(self) -> None:
        mode = self.config.special_action_mode.strip().lower()
        if mode != "gripper":
            self.logger.info("0x05 -> 特殊动作占位。当前未启用夹爪控制，仅记录日志。")
            return

        if self.dry_run:
            self.gripper_closed = not self.gripper_closed
            state = "闭合" if self.gripper_closed else "释放"
            self.logger.info("dry-run 夹爪动作：%s", state)
            return

        if not self.connected or self.d_type is None or self.api is None:
            raise DobotError("Dobot 尚未连接，无法执行夹爪动作。")

        self.gripper_closed = not self.gripper_closed
        desired = 1 if self.gripper_closed else 0
        state = "闭合" if desired else "释放"
        self.logger.info("0x05 -> 夹爪动作：%s", state)
        queued_index = self.d_type.SetEndEffectorGripper(self.api, 1, desired, isQueued=1)[0]
        self._wait_for_queue_index(queued_index)

    def get_pose(self) -> CartesianPose:
        if self.dry_run:
            return CartesianPose(self.last_pose.x, self.last_pose.y, self.last_pose.z, self.last_pose.r)

        if not self.connected or self.d_type is None or self.api is None:
            raise DobotError("Dobot 尚未连接，无法读取 Pose。")

        pose = self.d_type.GetPose(self.api)
        return CartesianPose(x=float(pose[0]), y=float(pose[1]), z=float(pose[2]), r=float(pose[3]))

    def emergency_stop(self) -> None:
        if self.dry_run:
            self.logger.info("dry-run 已执行安全停止。")
            return

        if not self.connected or self.d_type is None or self.api is None:
            return

        self.logger.warning("执行 best-effort 安全停止：ForceStop + ClearQueue。")
        try:
            self.d_type.SetQueuedCmdForceStopExec(self.api)
            self.d_type.SetQueuedCmdClear(self.api)
        except Exception as exc:  # noqa: BLE001
            self.logger.error("安全停止过程中出现异常：%s", exc)

    def disconnect(self) -> None:
        if self.dry_run:
            self.logger.info("dry-run 结束，无需断开 Dobot。")
            return

        if not self.connected or self.d_type is None or self.api is None:
            return

        try:
            self.d_type.DisconnectDobot(self.api)
            self.logger.info("Dobot 已断开连接。")
        finally:
            self.connected = False
            for handle in self._dll_handles:
                close = getattr(handle, "close", None)
                if callable(close):
                    close()
            self._dll_handles.clear()

    def log_pose(self, title: str, pose: CartesianPose) -> None:
        self.logger.info("%s：%s", title, pose.summary())

    def _wait_for_queue_index(self, target_index: int) -> None:
        if self.dry_run:
            return

        if not self.connected or self.d_type is None or self.api is None:
            raise DobotError("Dobot 尚未连接，无法等待队列完成。")

        # V1 明确不做持续灌队列。
        # 每次只下发一条动作，然后阻塞等待该索引执行完成，再允许下一条命令进入。
        deadline = time.monotonic() + self.config.queue_wait_timeout_s
        while time.monotonic() < deadline:
            current_index = int(self.d_type.GetQueuedCmdCurrentIndex(self.api)[0])
            if current_index >= int(target_index):
                return
            time.sleep(self.config.queue_poll_interval_s)

        raise DobotError(
            f"等待 Dobot 执行命令超时，target_index={target_index}，请检查机械臂是否报警或运动受阻。"
        )

    def _resolve_ptp_mode(self) -> int:
        if self.d_type is None:
            raise DobotError("Dobot SDK 尚未加载。")

        normalized = self.config.motion_mode.strip().upper()
        mode_map = {
            "MOVJXYZ": self.d_type.PTPMode.PTPMOVJXYZMode,
            "MOVLXYZ": self.d_type.PTPMode.PTPMOVLXYZMode,
        }
        if normalized not in mode_map:
            raise ConfigError("`dobot.motion.ptp_mode` 仅支持 `MOVJXYZ` 或 `MOVLXYZ`。")
        return mode_map[normalized]

    def _apply_soft_limits(self, pose: CartesianPose) -> CartesianPose:
        clamped = CartesianPose(
            x=self.config.x_limit.clamp(pose.x),
            y=self.config.y_limit.clamp(pose.y),
            z=self.config.z_limit.clamp(pose.z),
            r=pose.r,
        )
        if not self._poses_close(clamped, pose):
            self.logger.warning(
                "目标 Pose 超出软限位，已裁剪为：%s | 原目标：%s",
                clamped.summary(),
                pose.summary(),
            )
        return clamped

    @staticmethod
    def _poses_close(left: CartesianPose, right: CartesianPose, tolerance: float = 1e-3) -> bool:
        return (
            abs(left.x - right.x) <= tolerance
            and abs(left.y - right.y) <= tolerance
            and abs(left.z - right.z) <= tolerance
            and abs(left.r - right.r) <= tolerance
        )

    def _import_sdk_module(self) -> Any:
        module_path = self.config.sdk_dir / "DobotDllType.py"
        if not module_path.exists():
            raise DobotError(f"找不到 SDK 文件：{module_path}")

        spec = importlib.util.spec_from_file_location("dobot_dll_type_runtime", module_path)
        if spec is None or spec.loader is None:
            raise DobotError(f"无法加载 SDK 模块：{module_path}")

        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def _add_dll_search_path(self, dll_dir: Path) -> None:
        if hasattr(os, "add_dll_directory"):
            self._dll_handles.append(os.add_dll_directory(str(dll_dir)))
        os.environ["PATH"] = f"{dll_dir}{os.pathsep}{os.environ.get('PATH', '')}"

    def _connect_status_name(self, state: int) -> str:
        if self.d_type is None:
            return str(state)
        mapping = {
            self.d_type.DobotConnect.DobotConnect_NoError: "DobotConnect_NoError",
            self.d_type.DobotConnect.DobotConnect_NotFound: "DobotConnect_NotFound",
            self.d_type.DobotConnect.DobotConnect_Occupied: "DobotConnect_Occupied",
        }
        return mapping.get(state, f"Unknown({state})")


class GloveToDobotApp:
    def __init__(self, config: AppConfig, logger: logging.Logger, dry_run: bool) -> None:
        self.config = config
        self.logger = logger
        self.controller = DobotMagicianController(config=config, logger=logger, dry_run=dry_run)
        self.motion_in_progress = False
        self.last_executed_command: int | None = None
        self.last_executed_at = 0.0
        self.last_seen_command: int | None = None
        self.last_seen_at = 0.0

    def run(self, single_command: int | None, no_confirm: bool) -> None:
        self._maybe_confirm_startup(no_confirm=no_confirm)
        self.controller.connect()
        self.controller.initialize()

        if self.config.auto_move_to_safe_pose_on_start:
            self.motion_in_progress = True
            try:
                self.controller.move_to_safe_pose("启动初始化 -> 回到安全位")
            finally:
                self.motion_in_progress = False

        if single_command is not None:
            self.logger.info("进入单命令测试模式：0x%02X", single_command)
            self._process_command(single_command, serial_port=None, source="single-command")
            return

        self._run_serial_loop()

    def _run_serial_loop(self) -> None:
        self.logger.info(
            "开始监听手套串口：%s @ %d",
            self.config.serial_port,
            self.config.serial_baudrate,
        )

        with serial.Serial(
            port=self.config.serial_port,
            baudrate=self.config.serial_baudrate,
            timeout=self.config.serial_timeout_s,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
        ) as serial_port:
            if self.config.serial_flush_on_start:
                serial_port.reset_input_buffer()
                self.logger.info("启动时已清空手套串口输入缓冲区。")

            while True:
                raw = serial_port.read(1)
                if not raw:
                    continue
                command = raw[0]
                self._process_command(command, serial_port=serial_port, source="serial")

    def _process_command(
        self,
        command: int,
        serial_port: serial.Serial | None,
        source: str,
    ) -> None:
        command_name = COMMAND_NAMES.get(command) or IGNORED_COMMANDS.get(command) or "unknown"
        self.logger.info("收到原始命令字节：0x%02X | %s | source=%s", command, command_name, source)

        if command in IGNORED_COMMANDS:
            self.logger.info("0x%02X 当前不参与机械臂联动，已忽略。", command)
            return

        if command not in COMMAND_NAMES:
            self.logger.warning("未知命令 0x%02X，已忽略。", command)
            return

        now = time.monotonic()
        if command == self.last_seen_command and (now - self.last_seen_at) < self.config.duplicate_debounce_s:
            self.logger.info(
                "命令 0x%02X 在 %.3fs 去抖窗口内重复出现，已丢弃。",
                command,
                self.config.duplicate_debounce_s,
            )
            self.last_seen_at = now
            return

        self.last_seen_command = command
        self.last_seen_at = now

        if self.motion_in_progress:
            self.logger.warning("当前机械臂动作尚未完成，忽略新命令 0x%02X。", command)
            return

        interval = now - self.last_executed_at
        if interval < self.config.min_command_interval_s:
            self.logger.info(
                "命令 0x%02X 触发过快：距上次执行仅 %.3fs，小于最小间隔 %.3fs，已丢弃。",
                command,
                interval,
                self.config.min_command_interval_s,
            )
            return

        self.motion_in_progress = True
        try:
            self.controller.execute_glove_command(command)
            self.last_executed_command = command
            self.last_executed_at = time.monotonic()
        finally:
            self.motion_in_progress = False
            if serial_port is not None and self.config.flush_serial_after_motion:
                # 动作执行期间如果用户一直维持手势，底层串口可能已经积压了多次重复字节。
                # 这里主动清空，避免“动作刚完成就补发一串旧命令”导致机械臂乱跳。
                pending = serial_port.in_waiting
                if pending > 0:
                    serial_port.reset_input_buffer()
                    self.logger.info("动作执行期间累计了 %d 字节串口数据，已清空以避免重复触发。", pending)

    def _maybe_confirm_startup(self, no_confirm: bool) -> None:
        if no_confirm or not self.config.startup_confirmation:
            return

        self.logger.warning(self.config.startup_message)
        if sys.stdin.isatty():
            input("请确认机械臂已处于安全工作区，确认后按回车继续...")
        else:
            self.logger.warning("当前不是交互式终端，无法等待人工确认，请自行确认后继续。")


def parse_single_command(value: str | None) -> int | None:
    if value is None:
        return None

    try:
        command = int(value, 0)
    except ValueError as exc:
        raise ConfigError(f"`--single-command` 不是合法的 16 进制/10 进制字节：{value}") from exc

    if not (0 <= command <= 0xFF):
        raise ConfigError("`--single-command` 必须落在 0x00~0xFF 范围内。")
    return command


def main() -> int:
    parser = build_arg_parser()
    args = parser.parse_args()

    try:
        config_path = Path(args.config).resolve()
        config = load_config(config_path)
        logger = setup_logging(config.log_dir)
        single_command = parse_single_command(args.single_command)

        app = GloveToDobotApp(config=config, logger=logger, dry_run=args.dry_run)
        try:
            app.run(single_command=single_command, no_confirm=args.no_confirm)
        except KeyboardInterrupt:
            logger.warning("收到 Ctrl+C，开始执行安全停止。")
            app.controller.emergency_stop()
            return 130
        except Exception:
            logger.exception("程序运行异常，开始执行安全停止。")
            app.controller.emergency_stop()
            return 1
        finally:
            app.controller.disconnect()

    except ConfigError as exc:
        print(f"配置错误：{exc}", file=sys.stderr)
        return 2
    except Exception as exc:  # noqa: BLE001
        print(f"启动失败：{exc}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
