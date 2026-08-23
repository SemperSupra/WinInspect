from setuptools import setup, find_packages

with open("README.md") as f:
    long_description = f.read()

setup(
    name="wininspect",
    version="0.1.0",
    description="Python SDK for WinInspect remote desktop automation daemon",
    long_description=long_description,
    long_description_content_type="text/markdown",
    author="Mark E. DeYoung",
    author_email="mark.e.deyoung+wininspect-pypi@gmail.com",
    url="https://github.com/SemperSupra/WinInspect",
    download_url="https://github.com/SemperSupra/WinInspect/releases",
    project_urls={
        "Source": "https://github.com/SemperSupra/WinInspect",
        "Issues": "https://github.com/SemperSupra/WinInspect/issues",
    },
    packages=find_packages(),
    classifiers=[
        "Development Status :: 4 - Beta",
        "Intended Audience :: Developers",
        "License :: Other/Proprietary License",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "Topic :: System :: Monitoring",
    ],
    install_requires=[
        "websocket-client>=1.0",
    ],
    python_requires=">=3.8",
)
