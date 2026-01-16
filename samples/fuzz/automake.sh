#!/bin/bash

# 简化版脚本 - 不备份，直接覆盖
# 功能：
# 1. 替换main.c文件（不备份）
# 2. 清理并重新编译
# 3. 直接替换生成的elf文件（不备份）

# 设置颜色输出
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

# 基础路径
HARNESS_BASE_DIR="/home/zwz/zephyr/samples/fuzz/harness/harness_gemini_3_flash_preview"
TARGET_MAIN_C="/home/zwz/zephyr/samples/fuzz/src/main.c"
FUZZ_DIR="/home/zwz/zephyr/samples/fuzz"
ELF_SOURCE="$FUZZ_DIR/build/zephyr/zephyr.elf"

echo -e "${GREEN}开始自动化处理流程（无备份版本）...${NC}"

# 检查基础目录
if [ ! -d "$HARNESS_BASE_DIR" ]; then
    echo -e "${RED}错误：harness基础目录不存在${NC}"
    exit 1
fi

# 查找harness子文件夹
echo -e "${YELLOW}查找harness子文件夹...${NC}"
subdirs=("$HARNESS_BASE_DIR"/*/)

if [ ${#subdirs[@]} -eq 0 ]; then
    echo -e "${RED}错误：在harness目录中未找到子文件夹${NC}"
    exit 1
fi

# 处理每个子文件夹
for dir in "${subdirs[@]}"; do
    dir=${dir%/}
    echo -e "${GREEN}处理文件夹: $(basename "$dir")${NC}"
    
    # 检查源main.c文件是否存在
    SOURCE_MAIN_C="$dir/main.c"
    if [ ! -f "$SOURCE_MAIN_C" ]; then
        echo -e "${YELLOW}警告：$SOURCE_MAIN_C 不存在，跳过此文件夹${NC}"
        continue
    fi
    
    # 步骤1: 直接替换main.c文件（不备份）
    echo -e "${YELLOW}步骤1: 替换main.c文件...${NC}"
    cp "$SOURCE_MAIN_C" "$TARGET_MAIN_C"
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}成功替换main.c文件${NC}"
    else
        echo -e "${RED}错误：替换main.c文件失败${NC}"
        exit 1
    fi
    
    # 步骤2: 进入fuzz目录并清理build
    echo -e "${YELLOW}步骤2: 清理并编译...${NC}"
    cd "$FUZZ_DIR" || {
        echo -e "${RED}错误：无法进入目录 $FUZZ_DIR${NC}"
        exit 1
    }
    
    # 删除build目录
    if [ -d "build" ]; then
        rm -rf build
    fi
    
    # 编译
    echo "运行: west build -b mps2\\an385"
    west build -b mps2/an385
    
    if [ $? -ne 0 ]; then
        echo -e "${RED}错误：编译失败${NC}"
        exit 1
    fi
    
    echo -e "${GREEN}编译成功！${NC}"
    
    # 步骤3: 检查并直接替换elf文件（不备份）
    echo -e "${YELLOW}步骤3: 替换elf文件...${NC}"
    if [ -f "$ELF_SOURCE" ]; then
        TARGET_ELF="$dir/zephyr.elf"
        
        # 直接复制，覆盖原文件（不备份）
        cp "$ELF_SOURCE" "$TARGET_ELF"
        if [ $? -eq 0 ]; then
            echo -e "${GREEN}成功替换elf文件${NC}"
            echo "新elf文件: $TARGET_ELF"
        else
            echo -e "${RED}错误：替换elf文件失败${NC}"
        fi
    else
        echo -e "${RED}错误：未找到生成的elf文件${NC}"
    fi
    
    echo -e "${GREEN}文件夹 $(basename "$dir") 处理完成！${NC}"
    echo "========================================"
done

echo -e "${GREEN}所有处理完成！${NC}"