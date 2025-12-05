#!/usr/bin/env python3
import os
import subprocess
import sys

# 配置
SOURCE_DIRS = ["src"]
EXTENSIONS = (".cpp", ".h", ".hpp", ".cc", ".cxx")
CLANG_FORMAT_CMD = "clang-format"

def apply_format():
    # 检查 clang-format 是否安装
    try:
        subprocess.check_output([CLANG_FORMAT_CMD, "--version"])
    except FileNotFoundError:
        print(f"错误: 未找到 '{CLANG_FORMAT_CMD}'。请确保安装了 LLVM/Clang 工具链。")
        sys.exit(1)

    print(f"正在应用代码格式化 (基于 .clang-format)...")
    
    count = 0
    for source_dir in SOURCE_DIRS:
        for root, _, files in os.walk(source_dir):
            for file in files:
                if file.endswith(EXTENSIONS):
                    file_path = os.path.join(root, file)
                    print(f"正在格式化: {file_path}")
                    
                    try:
                        # -i 表示 in-place (直接修改文件)
                        # -style=file 表示使用项目根目录的 .clang-format 配置
                        subprocess.check_call(
                            [CLANG_FORMAT_CMD, "-i", "-style=file", file_path]
                        )
                        count += 1
                    except subprocess.CalledProcessError as e:
                        print(f"格式化文件失败: {file_path}")
                        sys.exit(1)

    print("\n" + "="*40)
    print(f"完成! 已处理 {count} 个文件。")
    print("="*40)

if __name__ == "__main__":
    apply_format()

