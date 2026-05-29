import json
import sys
import io
import subprocess
from contextlib import redirect_stdout
from dotenv import load_dotenv

from paths import (
    ensure_data_dirs,
    ENV_FILE,
    INPUT_FILE,
    OUTPUT_FILE,
    CHAT_INPUT_FILE,
    CHAT_OUTPUT_FILE,
    STUDY_EXE,
    PROJECT_ROOT,
)
from assistant_model import chat

load_dotenv(dotenv_path=ENV_FILE)


def main():
    try:
        ensure_data_dirs()

        import os
        if not os.path.exists(CHAT_INPUT_FILE):
            write_error("chat input file not found")
            sys.exit(1)

        with open(CHAT_INPUT_FILE, "r", encoding="utf-8-sig") as f:
            data = json.load(f)

        message = data.get("message", "").strip()
        schedule = data.get("schedule")
        chat_history = data.get("chatHistory", [])

        if not message:
            write_error("message field is empty")
            sys.exit(1)

        with redirect_stdout(io.StringIO()):
            result = chat(message, schedule, chat_history, input_path=INPUT_FILE)

        input_text = None
        if isinstance(result, dict):
            reply = result.get("reply", "")
            plan_changed = bool(result.get("planChanged", False))
            input_text = result.get("inputText")
        else:
            reply = str(result)
            plan_changed = False

        new_schedule = None
        if plan_changed:
            try:
                completed = subprocess.run(
                    [STUDY_EXE],
                    cwd=PROJECT_ROOT,
                    timeout=120,
                    capture_output=True,
                    text=True,
                )
                if completed.returncode != 0:
                    reply += "\n\n⚠️ Schedule could not be regenerated automatically. Please click Generate Schedule manually."
                    plan_changed = False
                else:
                    with open(OUTPUT_FILE, "r", encoding="utf-8") as f:
                        new_schedule = json.load(f)
            except (subprocess.TimeoutExpired, FileNotFoundError, OSError):
                reply += "\n\n⚠️ Schedule could not be regenerated automatically. Please click Generate Schedule manually."
                plan_changed = False
                new_schedule = None

        output = {
            "reply": reply,
            "planChanged": plan_changed,
            "newSchedule": new_schedule if plan_changed else None,
            "inputText": input_text if plan_changed else None,
        }

        with open(CHAT_OUTPUT_FILE, "w", encoding="utf-8") as f:
            json.dump(output, f, ensure_ascii=False, indent=2)

        sys.exit(0)

    except Exception as e:
        write_error(str(e))
        sys.exit(1)


def write_error(error_message):
    try:
        ensure_data_dirs()
        with open(CHAT_OUTPUT_FILE, "w", encoding="utf-8") as f:
            json.dump({"error": error_message}, f, ensure_ascii=False, indent=2)
    except OSError:
        pass


if __name__ == "__main__":
    main()
