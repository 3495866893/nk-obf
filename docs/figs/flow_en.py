# -*- coding: utf-8 -*-
"""英文版入口：与 flow_zh.py 共用同一套几何布局，只切换文字。

    python flow_en.py
"""

import os
import runpy
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
os.environ["FLOW_LANG"] = "en"
sys.argv = [os.path.join(HERE, "flow_zh.py"), "--lang", "en"]
runpy.run_path(os.path.join(HERE, "flow_zh.py"), run_name="__main__")
