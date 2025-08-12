#! /usr/bin/bash

cd ${LOCAL_AE_DIR}/LibStorage-Driver
source scripts/env.sh
python scripts/fio/extract.py

python scripts/fig/fig6.py
python scripts/fig/fig10.py
python scripts/fig/fig11.py
python scripts/fig/fig12.py
python scripts/fig/fig13.py
