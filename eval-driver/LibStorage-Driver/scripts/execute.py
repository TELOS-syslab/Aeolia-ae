import importlib as il
import os
import argparse
import subprocess
import sys

root_dir = os.getenv("INTUS_ROOT_DIR", "")
if root_dir == "":
    print("Please source env.sh first!")
    sys.exit(1)

eval_mapping = {
    1: ["01_single_thread"],
    2: ["02_multi_threads"],
    3: ["03_corun_io_comp_single_core", "04_corun_io_comp_4_cores"],
    4: ["05_corun_io_io_single_core", "06_corun_io_io_4_cores"],
}


def run_eval(eval_name, baseline):
    setup_script = os.path.join(root_dir, "scripts", "setup.sh")
    if os.path.exists(setup_script):
        print(f"Executing {setup_script}")
        try:
            subprocess.run(
                ["sudo", "bash", "-c", f"{setup_script}"],
                check=True,
            )
        except subprocess.CalledProcessError as e:
            print(f"Warning: Failed to execute {setup_script}: {e}")
    else:
        print(f"Warning: Setup script not found at {setup_script}")

    module = il.import_module(f"fio.{eval_name}")
    module.main(baseline)


def list_evals():
    message = """Available evaluations:
1: single thread           (Figure 10)
2: multiple threads        (Figure 11)
3: I/O & computation       (Figure 6.1, 12)
4: latency & throughput    (Figure 6.2, 13)"""

    print(message)


def main():
    parser = argparse.ArgumentParser(description="Run FIO evaluation scripts")
    parser.add_argument(
        "--list", "-l", action="store_true", help="List all available evaluations"
    )
    parser.add_argument(
        "--eval",
        "-e",
        type=int,
        nargs="+",
        metavar="EVAL",
        help="Run specific evaluations by number (e.g., -e 1 2 4)",
    )
    parser.add_argument(
        "--all",
        "-a",
        action="store_true",
        help="Run all evaluations (default behavior)",
    )
    parser.add_argument(
        "--baseline",
        "-b",
        action="store_true",
        help="Run baseline evaluations",
    )

    args = parser.parse_args()

    if args.baseline:
        baseline = True
    else:
        baseline = False

    # If no arguments provided, default to running all scripts
    if not any([args.list, args.eval is not None, args.all]):
        args.all = True

    if args.list:
        list_evals()
        return

    if args.eval:
        for eval_num in args.eval:
            if eval_num in eval_mapping:
                eval_names = eval_mapping[eval_num]
                print(f"Running evaluation {eval_num}: {eval_names}")
                for eval_name in eval_names:
                    print(f"  Executing: {eval_name}")
                    run_eval(eval_name, baseline)
            else:
                print(f"Warning: Evaluation number {eval_num} not found. Available: {list(eval_mapping.keys())}")
                exit(1)
        return

    if args.all:
        for eval in eval_mapping:
            for eval_name in eval_mapping[eval]:
                run_eval(eval_name, baseline)
        return


if __name__ == "__main__":
    main()
