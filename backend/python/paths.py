"""Project path constants (all servers run with cwd = project root)."""
import os

BACKEND_PYTHON = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(os.path.dirname(BACKEND_PYTHON))
DATA_DIR = os.path.join(PROJECT_ROOT, "data")
CHAT_DIR = os.path.join(DATA_DIR, "chat")
BIN_DIR = os.path.join(PROJECT_ROOT, "bin")
FRONTEND_DIR = os.path.join(PROJECT_ROOT, "frontend")

INPUT_FILE = os.path.join(DATA_DIR, "input.txt")
OUTPUT_FILE = os.path.join(DATA_DIR, "output.json")
STATE_FILE = os.path.join(DATA_DIR, "state.json")
CHAT_INPUT_FILE = os.path.join(CHAT_DIR, "chat_input.json")
CHAT_OUTPUT_FILE = os.path.join(CHAT_DIR, "chat_output.json")
STUDY_EXE = os.path.join(BIN_DIR, "study.exe")
ENV_FILE = os.path.join(PROJECT_ROOT, ".env")


def ensure_data_dirs():
    os.makedirs(DATA_DIR, exist_ok=True)
    os.makedirs(CHAT_DIR, exist_ok=True)
