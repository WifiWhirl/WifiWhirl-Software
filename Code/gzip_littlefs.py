"""
LittleFS Build Script for PlatformIO

This script prepares the LittleFS filesystem image.
Since web assets are now embedded in the firmware (via embed_files.py),
this script only copies configuration files that should remain on the filesystem.

Files on LittleFS:
- hwcfg.json: Default hardware configuration (can be modified by user)

Runtime files created by firmware (not included here):
- wifi.json: WiFi credentials
- mqtt.json: MQTT settings
- settings.json: Pool/spa settings
- cmdq.json: Command queue
- webconfig.json: Web interface preferences
- smartschedule.json: Smart schedule data
"""

Import("env")
import shutil, os

# Files that should be included in the LittleFS filesystem image
# These are default/initial configuration files
FILESYSTEM_FILES = [
    "hwcfg.json",
]

def copy_config_files(source, target, env):
    """
    Copy only configuration files to LittleFS data directory.
    Web assets are now embedded in firmware via embed_files.py.
    
    Args:
        source: Source parameter from platformio
        target: Target parameter from platformio
        env: Environment containing project settings
    """
    del_littlefs_data(source, target, env)
    print("Preparing LittleFS with config files only...")
    
    data_dir = env.get("PROJECT_DATA_DIR")  # datazip
    source_dir = data_dir[:-3]  # data
    
    # Create data directory
    os.makedirs(data_dir, exist_ok=True)
    
    # Copy only specified config files
    for filename in FILESYSTEM_FILES:
        src_path = os.path.join(source_dir, filename)
        dst_path = os.path.join(data_dir, filename)
        
        if os.path.exists(src_path):
            shutil.copy(src_path, dst_path)
            print(f"  Copied: {filename}")
        else:
            print(f"  Warning: {filename} not found in {source_dir}")
    
    print(f"LittleFS will contain {len(FILESYSTEM_FILES)} file(s)")

def del_littlefs_data(source, target, env):
    """
    Clean up LittleFS data directory before building.
    
    Args:
        source: Source parameter from platformio
        target: Target parameter from platformio
        env: Environment containing project settings
    """
    print("Clearing LittleFS data directory...")
    data_dir = env.get("PROJECT_DATA_DIR")
    if os.path.exists(data_dir):
        shutil.rmtree(data_dir, True)

env.AddPreAction("$BUILD_DIR/littlefs.bin", copy_config_files)
env.AddPostAction("$BUILD_DIR/littlefs.bin", del_littlefs_data)