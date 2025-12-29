#!/bin/bash

# ---------------------------------------------------------
# 增量 Clang-Format 检查/修复脚本
# 功能：对比当前 MR 与目标分支的差异
# 用法：
#   ./clang_format.sh          # 检查模式 (Check Mode)
#   ./clang_format.sh --fix    # 修复模式 (Fix Mode)
# ---------------------------------------------------------

# 0. 参数解析：检查是否开启修复模式
MODE="check"
if [[ "$1" == "--fix" ]]; then
    MODE="fix"
    echo "🛠️  Running in FIX mode. Files will be modified."
else
    echo "🔍 Running in CHECK mode. No files will be modified."
fi

# 检查是否安装了 git-clang-format
if ! command -v git-clang-format &> /dev/null; then
    echo "⚠️  git-clang-format not installed."
    echo "Please install manually (e.g., sudo apt install -y clang-format clang-tools)."
    exit 1
fi

# 1. 获取目标分支名称
TARGET_BRANCH="${CI_MERGE_REQUEST_TARGET_BRANCH_NAME:-main}"
if [ -z "$CI_MERGE_REQUEST_TARGET_BRANCH_NAME" ]; then
    # 本地测试时的回退逻辑
    if [ -z "$TARGET_BRANCH" ] || [ "$TARGET_BRANCH" == "main" ]; then
         TARGET_BRANCH="main" # 根据你的仓库实际主分支名称调整
    fi
    echo "⚠️  Not in a Merge Request pipeline. Target branch defaults to: $TARGET_BRANCH"
fi

TARGET_REF="origin/$TARGET_BRANCH"
echo "🎯 Target Reference: $TARGET_REF"

# 2. 解决 CI 浅克隆 (Shallow Clone) 问题
echo "⬇️  Fetching target branch info..."
git config --global --add safe.directory "*"
git fetch origin "$TARGET_BRANCH" --depth=1 > /dev/null 2>&1

# 定义需要检查的文件扩展名
EXTENSIONS="cpp,h,cc,hpp,cxx,c"

# 3. 根据模式执行逻辑
if [[ "$MODE" == "fix" ]]; then
    # ================= 修复模式 =================
    # 直接运行 git clang-format，不带 --diff 参数，它会直接修改文件
    echo "🚀 Applying formatting changes..."
    git clang-format "$TARGET_REF" --extensions "$EXTENSIONS"
    echo "✅ Formatting applied locally."
    echo "📝 Please check 'git status' and commit the changes if any files were modified."
    exit 0

else
    # ================= 检查模式 =================
    # --diff: 只输出差异内容，不修改文件
    DIFF_OUTPUT=$(git clang-format --diff "$TARGET_REF" --extensions "$EXTENSIONS")

    # 4. 结果判断
    # 如果输出包含 "no modified files..." 或 "clang-format did not modify..." 则通过
    if [[ "$DIFF_OUTPUT" == *"no modified files to format"* ]] || [[ "$DIFF_OUTPUT" == *"clang-format did not modify any files"* ]]; then
        echo "✅ Format check passed."
        exit 0
    else
        echo "❌ Format check failed! The following changes are required:"
        echo "--------------------------------------------------"
        echo "$DIFF_OUTPUT"
        echo "--------------------------------------------------"
        echo ""
        echo "💡 How to fix:"
        echo "   1. Run 'make format-fix' locally."
        echo "   2. Or run this script locally with --fix argument:"
        echo "      ./path/to/script.sh --fix"
        echo "   3. Or run the manual command:"
        echo '      git clang-format origin/'"$TARGET_BRANCH"
        exit 1
    fi
fi
