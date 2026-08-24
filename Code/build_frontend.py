"""
Frontend Build Script for PlatformIO

Runs the Vite frontend build before embed_files.py so the firmware always
embeds the latest frontend bundle from frontend/ into data/.

Uses npm when available on PATH; otherwise falls back to Docker (node:22-alpine),
matching frontend/Dockerfile.
"""

Import("env")

import os
import re
import shutil
import subprocess
import sys


def run_frontend_build(env):
    project_dir = env.get("PROJECT_DIR")
    frontend_dir = os.path.join(project_dir, "frontend")
    data_dir = os.path.join(project_dir, "data")
    data_index = os.path.join(project_dir, "data", "index.html")

    if not os.path.isdir(frontend_dir):
        print(f"Error: frontend directory not found: {frontend_dir}")
        sys.exit(1)

    print("Building frontend (Vite -> data/index.html + cached assets)...")

    # MSPA UART is ESP32-only (CIO_MSPA #if defined(ESP32)); expose the option in
    # the UI only for esp32 builds. Vite picks up VITE_-prefixed env vars.
    mspa = "1" if env.get("PIOPLATFORM") == "espressif32" else "0"
    print(f"  VITE_ENABLE_MSPA={mspa} (platform {env.get('PIOPLATFORM')})")

    npm = shutil.which("npm")
    if npm:
        print(f"  Using npm: {npm}")
        subprocess.run([npm, "install"], cwd=frontend_dir, check=True)
        subprocess.run([npm, "run", "build"], cwd=frontend_dir, check=True,
                       env={**os.environ, "VITE_ENABLE_MSPA": mspa})
    elif shutil.which("docker"):
        print("  npm not found; using Docker (node:22-alpine)")
        subprocess.run(
            [
                "docker",
                "run",
                "--rm",
                "-e",
                f"VITE_ENABLE_MSPA={mspa}",
                "-v",
                f"{project_dir}:/repo",
                "-w",
                "/repo/frontend",
                "node:22-alpine",
                "sh",
                "-c",
                "npm install && npm run build",
            ],
            check=True,
        )
    else:
        print("Error: neither npm nor docker found; cannot build frontend")
        sys.exit(1)

    if not os.path.isfile(data_index):
        print(f"Error: frontend build did not produce {data_index}")
        sys.exit(1)

    with open(data_index, "r", encoding="utf-8") as f:
        html = f.read()

    script_files = sorted(set(re.findall(r'src="/([^"]+\.js)"', html)))
    style_files = sorted(set(re.findall(r'href="/([^"]+\.css)"', html)))
    missing = [name for name in script_files + style_files if not os.path.isfile(os.path.join(data_dir, name))]

    if len(script_files) != 1 or len(style_files) != 1 or missing:
        print("Error: frontend build must produce one JS and one CSS asset referenced by index.html")
        print(f"  JS: {script_files or 'none'}")
        print(f"  CSS: {style_files or 'none'}")
        if missing:
            print(f"  Missing: {missing}")
        sys.exit(1)

    index_kb = os.path.getsize(data_index) / 1024
    js_kb = os.path.getsize(os.path.join(data_dir, script_files[0])) / 1024
    css_kb = os.path.getsize(os.path.join(data_dir, style_files[0])) / 1024
    print(
        "Frontend build complete "
        f"(index.html {index_kb:.1f} KiB, {script_files[0]} {js_kb:.1f} KiB, "
        f"{style_files[0]} {css_kb:.1f} KiB)"
    )


run_frontend_build(env)
