#!/usr/bin/env python3
import os
import subprocess
import sys

# 配置
SOURCE_DIRS = ["src"]
EXTENSIONS = (".cpp", ".h", ".hpp", ".cc", ".cxx")

# ============================================================
# 动态查找当前 Python 环境下的 clang-format 可执行文件
# ============================================================
def find_clang_format():
    bin_dir = os.path.dirname(sys.executable)
    executable_name = "clang-format.exe" if os.name == 'nt' else "clang-format"
    target_path = os.path.join(bin_dir, executable_name)
    
    if os.path.exists(target_path):
        return [target_path]
    else:
        return ["clang-format"]

CLANG_FORMAT_CMD = find_clang_format()
# ============================================================

def apply_format():
    try:
        subprocess.check_output(CLANG_FORMAT_CMD + ["--version"])
    except Exception:
        print("错误: 无法执行 clang-format。")
        sys.exit(1)

    print(f"正在应用代码格式化 (使用 {CLANG_FORMAT_CMD[0]})...")
    
    count = 0
    for source_dir in SOURCE_DIRS:
        for root, _, files in os.walk(source_dir):
            for file in files:
                if file.endswith(EXTENSIONS):
                    file_path = os.path.join(root, file)
                    print(f"正在格式化: {file_path}")
                    
                    try:
                        subprocess.check_call(
                            CLANG_FORMAT_CMD + ["-i", "-style=file", file_path]
                        )
                        count += 1
                    except subprocess.CalledProcessError:
                        print(f"格式化文件失败: {file_path}")
                        sys.exit(1)

    print("\n" + "="*40)
    print(f"完成! 已处理 {count} 个文件。")
    print("="*40)

if __name__ == "__main__":
    apply_format()

