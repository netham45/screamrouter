"""System status FastAPI endpoints."""
from __future__ import annotations

import datetime as _dt
import os
import platform
import socket
import sys
import threading
import time
from typing import Any

from fastapi import FastAPI

try:  # Optional dependency; fall back to stdlib when unavailable
    import psutil  # type: ignore
except ImportError:  # pragma: no cover - psutil might not be installed
    psutil = None  # type: ignore


_PROCESS_START_TIME = time.time()


def _bytes_to_mb(value: float | int | None) -> float | None:
    """Convert bytes to megabytes with one decimal precision."""
    if value is None:
        return None
    return round(float(value) / (1024 * 1024), 1)


def _format_uptime(seconds: float | None) -> str | None:
    if seconds is None:
        return None
    seconds = int(seconds)
    days, rem = divmod(seconds, 86400)
    hours, rem = divmod(rem, 3600)
    minutes, seconds = divmod(rem, 60)
    parts: list[str] = []
    if days:
        parts.append(f"{days}d")
    if hours or parts:
        parts.append(f"{hours:02}h")
    parts.append(f"{minutes:02}m")
    parts.append(f"{seconds:02}s")
    return " ".join(parts)


def _system_memory_stats() -> dict[str, Any]:
    total = available = used = percent = None

    if psutil:
        vm = psutil.virtual_memory()  # pragma: no cover - requires psutil
        total = vm.total
        available = vm.available
        used = vm.used
        percent = vm.percent
    else:
        try:
            meminfo: dict[str, int] = {}
            with open("/proc/meminfo", "r", encoding="utf-8") as mem_file:
                for line in mem_file:
                    key, value, *_ = line.split()
                    meminfo[key.rstrip(":").lower()] = int(value) * 1024
            total = meminfo.get("memtotal")
            available = meminfo.get("memavailable")
            if total is not None and available is not None:
                used = total - available
                percent = round((used / total) * 100, 1)
        except FileNotFoundError:  # pragma: no cover - non-Linux fallback
            pass

    return {
        "total_mb": _bytes_to_mb(total),
        "available_mb": _bytes_to_mb(available),
        "used_mb": _bytes_to_mb(used),
        "used_percent": round(percent, 1) if percent is not None else None,
    }


def _load_average() -> dict[str, float] | None:
    try:
        one, five, fifteen = os.getloadavg()
        return {
            "one": round(one, 2),
            "five": round(five, 2),
            "fifteen": round(fifteen, 2),
        }
    except (AttributeError, OSError):  # pragma: no cover - unsupported platforms
        return None


def _uptime_seconds() -> float | None:
    if psutil:
        return float(time.time() - psutil.boot_time())  # pragma: no cover - requires psutil
    try:
        with open("/proc/uptime", "r", encoding="utf-8") as uptime_file:
            uptime_str = uptime_file.readline().split()[0]
            return float(uptime_str)
    except (FileNotFoundError, ValueError):  # pragma: no cover - non-Linux fallback
        return None


def _process_stats() -> dict[str, Any]:
    pid = os.getpid()
    process_name = os.path.basename(sys.argv[0]) or "python"
    create_time = _PROCESS_START_TIME
    cpu_percent = None
    memory_percent = None
    rss = None
    num_threads = threading.active_count()
    cpu_time = None

    if psutil:
        proc = psutil.Process(pid)  # pragma: no cover - requires psutil
        process_name = proc.name()
        create_time = proc.create_time()
        cpu_percent = proc.cpu_percent(interval=0.0)
        memory_percent = proc.memory_percent()
        rss = proc.memory_info().rss
        num_threads = proc.num_threads()
    else:
        try:
            import resource

            usage = resource.getrusage(resource.RUSAGE_SELF)
            rss = usage.ru_maxrss * 1024
            cpu_time = usage.ru_utime + usage.ru_stime
        except ImportError:  # pragma: no cover - resource missing (Windows)
            pass

    now = time.time()
    uptime = now - create_time if create_time else None

    if cpu_percent is None and cpu_time is not None and uptime and uptime > 0:
        cpu_percent = (cpu_time / uptime) * 100

    return {
        "pid": pid,
        "name": process_name,
        "status": "running",
        "cpu_percent": round(cpu_percent, 1) if cpu_percent is not None else None,
        "memory_percent": round(memory_percent, 1) if memory_percent is not None else None,
        "memory_rss_mb": _bytes_to_mb(rss),
        "num_threads": num_threads,
        "uptime_seconds": round(uptime, 1) if uptime is not None else None,
        "uptime_human": _format_uptime(uptime) if uptime is not None else None,
        "started_at": _dt.datetime.fromtimestamp(create_time, tz=_dt.timezone.utc).isoformat() if create_time else None,
    }


class APISystemInfo:
    """Registers endpoints that expose host and process statistics."""

    def __init__(self, app: FastAPI):
        self._app = app
        app.add_api_route("/api/system/info", self.get_system_info, methods=["GET"])

    async def get_system_info(self) -> dict[str, Any]:
        now_local = _dt.datetime.now().astimezone()
        now_utc = now_local.astimezone(_dt.timezone.utc)

        uptime_seconds = _uptime_seconds()

        return {
            "hostname": socket.gethostname(),
            "fqdn": "",
            "platform": {
                "system": platform.system(),
                "release": platform.release(),
                "version": platform.version(),
                "machine": platform.machine(),
                "python": platform.python_version(),
            },
            "server_time": {
                "local_iso": now_local.isoformat(),
                "utc_iso": now_utc.isoformat(),
            },
            "uptime_seconds": uptime_seconds,
            "uptime_human": _format_uptime(uptime_seconds),
            "cpu_count": os.cpu_count(),
            "load_average": _load_average(),
            "memory": _system_memory_stats(),
            "process": _process_stats(),
        }
