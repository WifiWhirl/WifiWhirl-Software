Import("env")
import shutil, pathlib, gzip, os

def copy_data(src, dst):
  """
  Copy files from src to dst, compressing specific file types.
  
  Args:
      src: Source file path
      dst: Destination file path
  """
  ext = pathlib.Path(src).suffix[1:]
  myfilename = "filelist.txt"
  
  # Use consistent path operations with os.path
  file_dir = os.path.dirname(dst)
  file_name = os.path.basename(dst)
  filelist_path = os.path.join(file_dir, myfilename)
  
  print(f"Processing: {file_name} -> {dst}")
  
  with open(filelist_path, 'a') as myfile:
    if (ext in ["js", "css", "html", "ico", "eot", "woff", "txt"]):
      myfile.write(f"{file_name}.gz\n")
      with open(src, 'rb') as src_file, gzip.open(f"{dst}.gz", 'wb') as dst_file:
        for chunk in iter(lambda: src_file.read(4096), b""):
          dst_file.write(chunk)
    else:
      myfile.write(f"{file_name}\n")
      shutil.copy(src, dst)

def copy_gzip_data(source, target, env):
  """
  Prepare data for littlefs by copying files and compressing eligible files.
  
  Args:
      source: Source parameter from platformio
      target: Target parameter from platformio
      env: Environment containing project settings
  """
  del_gzip_data(source, target, env)
  print("Compressing web assets for littlefs...")
  data = env.get("PROJECT_DATA_DIR")
  source_dir = data[:-3]
  print(f"Copying from {source_dir} to {data}")
  shutil.copytree(source_dir, data, copy_function=copy_data)

def del_gzip_data(source, target, env):
  """
  Clean up littlefs data directory before building.
  
  Args:
      source: Source parameter from platformio
      target: Target parameter from platformio
      env: Environment containing project settings
  """
  print("Clearing littlefs data directory...")
  data = env.get("PROJECT_DATA_DIR")
  if os.path.exists(data):
    shutil.rmtree(data, True)

env.AddPreAction("$BUILD_DIR/littlefs.bin", copy_gzip_data)
env.AddPostAction("$BUILD_DIR/littlefs.bin", del_gzip_data)