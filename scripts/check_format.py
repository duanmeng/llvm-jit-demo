#!/usr/bin/env python3
import os
import subprocess
import sys
import difflib

# =================配置区域=================
SOURCE_DIRS = ["src"]
EXTENSIONS = (".cpp", ".h", ".hpp", ".cc", ".cxx")

# ============================================================
# 核心逻辑：动态查找当前 Python 环境下的 clang-format 可执行文件
# ============================================================
def find_clang_format():
    # 1. 获取当前 Python 解释器的目录 (例如 .../venv/bin)
    bin_dir = os.path.dirname(sys.executable)
    
    # 2. 拼接 clang-format 的路径
    executable_name = "clang-format.exe" if os.name == 'nt' else "clang-format"
    target_path = os.path.join(bin_dir, executable_name)
    
    # 3. 如果存在 (pip 安装版)，就使用绝对路径；否则回退到系统 PATH
    if os.path.exists(target_path):
        return [target_path]
    else:
        return ["clang-format"]

CLANG_FORMAT_CMD = find_clang_format()

def check_format():
    files_needing_format = []
    
    # 0. 打印版本信息，方便 CI 调试
    try:
        version_output = subprocess.check_output(CLANG_FORMAT_CMD + ["--version"]).decode('utf-8')
        print(f"正在使用格式化工具: {CLANG_FORMAT_CMD[0]}")
        print(f"工具版本: {version_output.strip()}")
    except Exception as e:
        print(f"错误: 无法执行 clang-format。")
        print(f"请确保已运行: pip install clang-format==19.1.0")
        print(f"调试信息: {e}")
        sys.exit(1)

    print(f"正在检查代码格式 (基于 .clang-format)...")
    print("-" * 60)
    
    for source_dir in SOURCE_DIRS:
        for root, _, files in os.walk(source_dir):
            for file in files:
                if file.endswith(EXTENSIONS):
                    file_path = os.path.join(root, file)
                    
                    try:
                        # 1. 读取原始文件内容
                        with open(file_path, 'rb') as f:
                            original_bytes = f.read()
                        
                        # 2. 获取格式化后的内容 (不修改文件，只输出到 stdout)
                        formatted_bytes = subprocess.check_output(
                            CLANG_FORMAT_CMD + ["-style=file", file_path]
                        )
                        
                        # 3. 对比
                        if original_bytes != formatted_bytes:
                            files_needing_format.append(file_path)
                            print(f" [X] 格式错误: {file_path}")
                            
                            # =========================================
                            # 生成并打印 Diff
                            # =========================================
                            try:
                                original_text = original_bytes.decode('utf-8').splitlines()
                                formatted_text = formatted_bytes.decode('utf-8').splitlines()
                                
                                diff = difflib.unified_diff(
                                    original_text, 
                                    formatted_text, 
                                    fromfile=f'Current ({file})', 
                                    tofile=f'Expected ({file})', 
                                    lineterm=''
                                )
                                
                                print("\n    >>> 差异详情:")
                                for line in diff:
                                    # 红色: 需要删除/修改的行 (当前代码)
                                    if line.startswith('-'):
                                        print(f"    \033[31m{line}\033[0m") 
                                    # 绿色: 期望变成的样子 (格式化后)
                                    elif line.startswith('+'):
                                        print(f"    \033[32m{line}\033[0m")
                                    # 头部信息 (@@ ... @@)
                                    elif line.startswith('@'):
                                        print(f"    \033[36m{line}\033[0m")
                                    else:
                                        print(f"    {line}")
                                print("-" * 60)
                            except Exception as e:
                                print(f"    (无法生成 Diff，可能是编码问题: {e})")
                                print("-" * 60)

                    except subprocess.CalledProcessError:
                        print(f"无法处理文件: {file_path}")
                        sys.exit(1)

    if files_needing_format:
        print("\n" + "="*60)
        print(f"检查失败! 有 {len(files_needing_format)} 个文件不符合格式规范。")
        print(f"请运行命令进行自动修复: make fix")
        print("="*60)
        sys.exit(1)
    else:
        print("\n" + "="*60)
        print("检查通过! 所有文件格式正确。")
        print("="*60)
        sys.exit(0)

if __name__ == "__main__":
    check_format()

