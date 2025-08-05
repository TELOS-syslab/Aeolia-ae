# proj/data/setup.py
from setuptools import setup, find_packages

setup(
    name="proj-data",  # Different name from parent project
    version="0.1.0",
    packages=find_packages(),
    # Optional: Include other files
    # package_data={"": ["*.json", "*.csv"]},
)