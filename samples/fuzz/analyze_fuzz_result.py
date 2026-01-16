import os
import json
import re
import datetime

# ================= 配置区域 =================
# 请确保这里没有多余的空格
ROOT_DIR = "/home/zwz/zephyr/samples/fuzz/harness/harness_fuzz"

REPORT_FILENAME = "fuzz_analysis_report.txt"
LOG_FILENAME = "rtos_fuzz_run.debug.log"
GEN_TIME_FILENAME = "generate_func_cli.log"
CRASH_DIR_NAME = "crashes"
# ===========================================

def log_output(message, file_handle):
    print(message)
    if file_handle:
        file_handle.write(message + "\n")

def is_valid_crash(metadata_path):
    try:
        with open(metadata_path, 'r', encoding='utf-8') as f:
            data = json.load(f)
        executions = data.get("executions")
        metadata_content = data.get("metadata", {})
        map_data = metadata_content.get("map", {})

        if (executions == 0 or executions is None) and not map_data:
            return False
        return True
    except Exception:
        return False

def get_last_edge_denominator(log_path):
    if not os.path.exists(log_path):
        # [调试] 如果找不到文件，打印一下路径提醒
        # print(f"[提示] 未找到日志文件: {log_path}")
        return 0
    edge_denominator = 0
    pattern = re.compile(r"edges:\s*\d+/(\d+)")
    try:
        with open(log_path, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()
            matches = pattern.findall(content)
            if matches:
                edge_denominator = int(matches[-1])
    except Exception:
        pass
    return edge_denominator

def get_generation_time(log_path):
    if not os.path.exists(log_path):
        return 0.0
    gen_time = 0.0
    pattern = re.compile(r"本次 generate-func 总耗时:\s*([\d\.]+)\s*秒")
    try:
        with open(log_path, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()
            match = pattern.search(content)
            if match:
                gen_time = float(match.group(1))
    except Exception:
        pass
    return gen_time

def main():
    if not os.path.exists(ROOT_DIR):
        print(f"错误: 根目录不存在 -> {ROOT_DIR}")
        return

    try:
        report_file = open(REPORT_FILENAME, 'w', encoding='utf-8')
    except Exception as e:
        print(f"无法创建报告文件: {e}")
        return

    current_time = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    # 获取所有子文件夹
    subdirs = [os.path.join(ROOT_DIR, d) for d in os.listdir(ROOT_DIR) if os.path.isdir(os.path.join(ROOT_DIR, d))]
    total_found = len(subdirs)

    log_output(f"=== Fuzzing 统计报告 ===", report_file)
    log_output(f"时间: {current_time}", report_file)
    log_output(f"根目录: {ROOT_DIR}", report_file)
    log_output(f"检测到子文件夹数量: {total_found}", report_file)
    log_output("-" * 60, report_file)

    valid_crashes = []
    edge_denominators = [] 
    generation_times = [] 
    harness_processed_count = 0

    print("正在处理中...")

    for api_dir in subdirs:
        api_name = os.path.basename(api_dir)
        harness_processed_count += 1
        
        # --- 1. 处理 Crashes ---
        # 路径拼接逻辑：ROOT/harness_xxx/crashes
        crashes_path = os.path.join(api_dir, CRASH_DIR_NAME)
        
        if os.path.exists(crashes_path):
            for f_name in os.listdir(crashes_path):
                if f_name.endswith(".metadata"):
                    metadata_full_path = os.path.join(crashes_path, f_name)
                    if is_valid_crash(metadata_full_path):
                        crash_file_name = f_name.replace(".metadata", "").replace("_1", "")
                        crash_full_path = os.path.join(crashes_path, crash_file_name)
                        valid_crashes.append({
                            "api": api_name,
                            "path": crash_full_path
                        })

        # --- 2. 处理 覆盖率 ---
        # 路径拼接逻辑：ROOT/harness_xxx/freertos_run.debug.log
        run_log_path = os.path.join(api_dir, LOG_FILENAME)
        denom = get_last_edge_denominator(run_log_path)
        if denom > 0:
            edge_denominators.append(denom)

        # --- 3. 处理 生成时间 ---
        # 路径拼接逻辑：ROOT/harness_xxx/generate_func_cli.log
        gen_log_path = os.path.join(api_dir, GEN_TIME_FILENAME)
        g_time = get_generation_time(gen_log_path)
        if g_time > 0:
            generation_times.append(g_time)

    # ================= 输出统计结果 =================

    log_output(f"\n1. 有效 Crash 统计: {len(valid_crashes)} 个", report_file)
    if valid_crashes:
        for idx, item in enumerate(valid_crashes):
            log_output(f"   [{idx+1}] API: {item['api']}", report_file)
            log_output(f"        Path: {item['path']}", report_file)
    else:
        log_output("   无有效 Crash。", report_file)

    log_output("\n" + "-" * 30, report_file)
    
    total_system_edges = sum(edge_denominators)
    avg_harness_edges = total_system_edges / harness_processed_count if harness_processed_count > 0 else 0
    
    log_output("2. 覆盖率统计 (Edges)", report_file)
    log_output(f"   - 系统覆盖边总和: {total_system_edges}", report_file)
    log_output(f"   - 平均 Harness 边覆盖率: {avg_harness_edges:.2f}", report_file)

    log_output("\n" + "-" * 30, report_file)

    total_gen_time = sum(generation_times)
    valid_time_count = len(generation_times)
    avg_gen_time = total_gen_time / valid_time_count if valid_time_count > 0 else 0
    
    log_output("3. 生成时间统计", report_file)
    log_output(f"   - 总生成耗时: {total_gen_time:.2f} 秒", report_file)
    log_output(f"   - 平均生成时间: {avg_gen_time:.2f} 秒 (统计文件数: {valid_time_count})", report_file)
    
    log_output("\n" + "=" * 60, report_file)
    report_file.close()
    
    print(f"\n[完成] 报告已生成: {os.path.abspath(REPORT_FILENAME)}")

if __name__ == "__main__":
    main()