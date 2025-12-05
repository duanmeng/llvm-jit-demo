import os
import subprocess
import sys

# 配置
SOURCE_DIRS = ["src"]
EXTENSIONS = (".cpp", ".h", ".hpp", ".cc", ".cxx")
CLANG_FORMAT_CMD = "clang-format"

def check_format():
    files_needing_format = []
    
    # 检查 clang-format 是否安装
    try:
        subprocess.check_output([CLANG_FORMAT_CMD, "--version"])
    except FileNotFoundError:
        print(f"错误: 未找到 '{CLANG_FORMAT_CMD}'。请确保安装了 LLVM/Clang 工具链。")
        sys.exit(1)

    print(f"正在检查代码格式 (基于 .clang-format)...")
    
    for source_dir in SOURCE_DIRS:
        for root, _, files in os.walk(source_dir):
            for file in files:
                if file.endswith(EXTENSIONS):
                    file_path = os.path.join(root, file)
                    
                    try:
                        # 1. 读取原始文件内容
                        with open(file_path, 'rb') as f:
                            original_content = f.read()
                        
                        # 2. 获取 clang-format 格式化后的内容 (不修改文件，只输出到 stdout)
                        # -style=file 会自动查找项目根目录下的 .clang-format
                        formatted_content = subprocess.check_output(
                            [CLANG_FORMAT_CMD, "-style=file", file_path]
                        )
                        
                        # 3. 对比
                        if original_content != formatted_content:
                            files_needing_format.append(file_path)
                            print(f" [X] 格式错误: {file_path}")
                        
                    except subprocess.CalledProcessError as e:
                        print(f"处理文件出错: {file_path}")
                        print(e)
                        sys.exit(1)

    if files_needing_format:
        print("\n" + "="*40)
        print(f"检查失败! 有 {len(files_needing_format)} 个文件需要格式化。")
        print("请运行 'python3 apply_format.py' 进行修复。")
        print("="*40)
        sys.exit(1)
    else:
        print("\n" + "="*40)
        print("检查通过! 所有文件格式正确。")
        print("="*40)
        sys.exit(0)

if __name__ == "__main__":
    check_format()

