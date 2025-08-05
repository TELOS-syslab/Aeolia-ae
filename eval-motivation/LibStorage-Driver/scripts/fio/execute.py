import importlib as il

def run_eval(eval_name):
    module = il.import_module(eval_name)
    module.main()

# evals = ["01_single_thread", "02_multi_threads"]
evals = ["02_multi_threads"]

for eval in evals:
    run_eval(eval)
