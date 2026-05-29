import json
import sys
import os
import io
import subprocess
from contextlib import redirect_stdout
from dotenv import load_dotenv
from assistant_model import chat, build_user_prompt

# Load environment variables from .env in the project root
dotenv_path = os.path.join(os.path.dirname(__file__), '.env')
load_dotenv(dotenv_path=dotenv_path)

def main():
    try:
        # Read chat_input.json from project root
        input_file = os.path.join(os.path.dirname(__file__), 'chat_input.json')
        if not os.path.exists(input_file):
            write_error("chat_input.json not found")
            sys.exit(1)

        with open(input_file, 'r', encoding='utf-8-sig') as f:
            data = json.load(f)

        # Extract fields
        message = data.get('message', '').strip()
        schedule = data.get('schedule')
        chat_history = data.get('chatHistory', [])

        if not message:
            write_error("message field is empty")
            sys.exit(1)

        # Call chat function with stdout suppressed (it prints during streaming)
        input_path = os.path.join(os.path.dirname(__file__), 'input.txt')

        with redirect_stdout(io.StringIO()):
            result = chat(message, schedule, chat_history, input_path=input_path)

        input_text = None
        if isinstance(result, dict):
            reply = result.get('reply', '')
            plan_changed = bool(result.get('planChanged', False))
            input_text = result.get('inputText')
        else:
            reply = str(result)
            plan_changed = False

        new_schedule = None
        if plan_changed:
            project_root = os.path.dirname(__file__)
            study_exe = os.path.join(project_root, 'bin', 'study.exe')
            if not os.path.isfile(study_exe):
                study_exe = os.path.join(project_root, 'study.exe')
            try:
                completed = subprocess.run(
                    [study_exe],
                    cwd=os.path.dirname(__file__),
                    timeout=15,
                    capture_output=True,
                    text=True
                )
                if completed.returncode != 0:
                    reply += "\n\n⚠️ Schedule could not be regenerated automatically. Please click Generate Schedule manually."
                    plan_changed = False
                else:
                    output_path = os.path.join(os.path.dirname(__file__), 'output.json')
                    try:
                        with open(output_path, 'r', encoding='utf-8') as f:
                            new_schedule = json.load(f)
                    except Exception:
                        reply += "\n\n⚠️ Schedule could not be regenerated automatically. Please click Generate Schedule manually."
                        plan_changed = False
                        new_schedule = None
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

        output_file = os.path.join(os.path.dirname(__file__), 'chat_output.json')
        with open(output_file, 'w', encoding='utf-8') as f:
            json.dump(output, f, ensure_ascii=False, indent=2)

        sys.exit(0)

    except Exception as e:
        write_error(str(e))
        sys.exit(1)

def write_error(error_message):
    """Write error to chat_output.json"""
    try:
        output_file = os.path.join(os.path.dirname(__file__), 'chat_output.json')
        with open(output_file, 'w', encoding='utf-8') as f:
            json.dump({"error": error_message}, f, ensure_ascii=False, indent=2)
    except:
        pass

if __name__ == "__main__":
    main()
